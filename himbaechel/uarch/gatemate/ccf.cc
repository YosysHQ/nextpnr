/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <regex>

#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "gatemate.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

struct GateMateCCFReader
{
    Context *ctx;
    GateMateImpl *uarch;
    std::istream &in;
    int lineno;
    dict<IdString, Property> defaults;

    GateMateCCFReader(Context *ctx, GateMateImpl *uarch, std::istream &in) : ctx(ctx), uarch(uarch), in(in) {};

    std::string strip_quotes(const std::string &str)
    {
        if (str.at(0) == '"') {
            if (str.back() != '"') {
                log_error("expected '\"' at end of string '%s' (on line %d).\n", str.c_str(), lineno);
            }
            return str.substr(1, str.size() - 2);
        } else {
            return str;
        }
    };

    void parse_params(std::vector<std::string> &params, bool is_default, dict<IdString, Property> *props)
    {
        for (auto param : params) {
            std::vector<std::string> expr;
            boost::split(expr, param, boost::is_any_of("="));
            if (expr.size() != 2)
                log_error("each parameter must be in form NAME=VALUE (on line %d)\n", lineno);

            std::string name = expr.at(0);
            boost::algorithm::trim(name);
            boost::algorithm::to_upper(name);
            std::string value = strip_quotes(expr.at(1));
            boost::algorithm::trim(value);
            boost::algorithm::to_upper(value);

            if (name == "LOC") {
                if (is_default)
                    log_error("Value '%s' can not be defined for default GPIO in line %d.\n", name.c_str(), lineno);
                if (ctx->get_package_pin_bel(ctx->id(value)) == BelId())
                    log_error("Unknown location '%s' used in line %d.\n", value.c_str(), lineno);
                if (!uarch->available_pads.count(ctx->id(value)))
                    log_error("Pad '%s' used in line %d not available.\n", value.c_str(), lineno);
                props->emplace(id_LOC, Property(value));
                uarch->available_pads.erase(ctx->id(value));
            } else if (name == "SCHMITT_TRIGGER" || name == "PULLUP" || name == "PULLDOWN" || name == "KEEPER" ||
                       name == "FF_IBF" || name == "FF_OBF" || name == "LVDS_BOOST" || name == "LVDS_RTERM") {
                if (value == "TRUE") {
                    props->emplace(ctx->id(name.c_str()), Property(Property::State::S1));
                } else if (value == "FALSE") {
                    props->emplace(ctx->id(name.c_str()), Property(Property::State::S0));
                } else
                    log_error("Uknown value '%s' for parameter '%s' in line %d, must be TRUE or FALSE.\n",
                              value.c_str(), name.c_str(), lineno);
            } else if (name == "SLEW") {
                if (value == "FAST" || value == "SLOW") {
                    props->emplace(ctx->id(name.c_str()), Property(value));
                } else
                    log_error("Uknown value '%s' for parameter '%s' in line %d, must be SLOW or FAST.\n", value.c_str(),
                              name.c_str(), lineno);
            } else if (name == "DRIVE") {
                try {
                    int drive = boost::lexical_cast<int>(value.c_str());
                    if (drive == 3 || drive == 6 || drive == 9 || drive == 12) {
                        props->emplace(ctx->id(name.c_str()), Property(drive, 2));
                    } else
                        log_error("Parameter '%s' must have value 3,6,9 or 12 in line %d.\n", name.c_str(), lineno);
                } catch (boost::bad_lexical_cast const &) {
                    log_error("Parameter '%s' must be number in line %d.\n", name.c_str(), lineno);
                }
            } else if (name == "DELAY_IBF" || name == "DELAY_OBF") {
                try {
                    int delay = boost::lexical_cast<int>(value.c_str());
                    if (delay >= 0 && delay <= 15) {
                        props->emplace(ctx->id(name.c_str()), Property(delay, 4));
                    } else
                        log_error("Parameter '%s' must have value from 0 to 15 in line %d.\n", name.c_str(), lineno);
                } catch (boost::bad_lexical_cast const &) {
                    log_error("Parameter '%s' must be number in line %d.\n", name.c_str(), lineno);
                }
            } else {
                log_error("Uknown parameter name '%s' in line %d.\n", name.c_str(), lineno);
            }
        }
    }

    void run()
    {
        log_info("Parsing CCF file..\n");

        std::string line;
        std::string linebuf;
        defaults.clear();

        auto isempty = [](const std::string &str) {
            return std::all_of(str.begin(), str.end(), [](char c) { return isblank(c) || c == '\r' || c == '\n'; });
        };
        lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            // Both // and # are considered start of comment
            size_t com_start = line.find("//");
            if (com_start != std::string::npos)
                line = line.substr(0, com_start);
            com_start = line.find('#');
            if (com_start != std::string::npos)
                line = line.substr(0, com_start);
            if (isempty(line))
                continue;
            linebuf += line;

            size_t pos = linebuf.find(';');
            // Need to concatenate lines until there is closing ; sign
            while (pos != std::string::npos) {
                std::string content = linebuf.substr(0, pos);

                std::vector<std::string> params;
                boost::split(params, content, boost::is_any_of("|"));
                std::string command = params.at(0);

                std::stringstream ss(command);
                std::vector<std::string> words;
                std::string tmp;
                while (ss >> tmp)
                    words.push_back(tmp);
                std::string type = words.at(0);

                boost::algorithm::to_lower(type);
                if (type == "default_gpio") {
                    if (words.size() != 1)
                        log_error("line with default_GPIO should not contain only parameters (in line %d).\n", lineno);
                    params.erase(params.begin());
                    parse_params(params, true, &defaults);

                } else if (type == "net" || type == "pin_in" || type == "pin_out" || type == "pin_inout") {
                    if (words.size() < 3 || words.size() > 5)
                        log_error("pin definition line not properly formed (in line %d).\n", lineno);
                    std::string pin_name = strip_quotes(words.at(1));

                    // put back other words and use them as parameters
                    std::stringstream ss;
                    for (size_t i = 2; i < words.size(); i++)
                        ss << words.at(i);
                    params[0] = ss.str();

                    IdString cellname = ctx->id(pin_name);
                    if (ctx->cells.count(cellname)) {
                        CellInfo *cell = ctx->cells.at(cellname).get();
                        for (auto p : defaults)
                            cell->params[p.first] = p.second;
                        parse_params(params, false, &cell->params);
                    } else
                        log_warning("Pad with name '%s' not found in netlist.\n", pin_name.c_str());
                } else {
                    log_error("unknown type '%s' in line %d.\n", type.c_str(), lineno);
                }

                linebuf = linebuf.substr(pos + 1);
                pos = linebuf.find(';');
            }
        }
        if (!isempty(linebuf))
            log_error("unexpected end of CCF file\n");
    }
};

void GateMateImpl::parse_ccf(const std::string &filename)
{
    std::ifstream in(filename);
    if (!in)
        log_error("failed to open CCF file '%s'\n", filename.c_str());
    GateMateCCFReader reader(ctx, this, in);
    reader.run();
}

NEXTPNR_NAMESPACE_END

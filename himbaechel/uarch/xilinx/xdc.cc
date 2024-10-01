/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019-2023  gatecat <gatecat@ds0.me>
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

#include <boost/algorithm/string.hpp>
#include <fstream>
#include <regex>

#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "xilinx.h"

#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

void XilinxImpl::parse_xdc(const std::string &filename)
{
    std::ifstream in(filename);
    if (!in)
        log_error("failed to open XDC file '%s'\n", filename.c_str());
    log_info("Parsing XDC file...\n");
    std::string line;
    std::string linebuf;
    int lineno = 0;
    unsigned num_errors = 0;

    auto isempty = [](const std::string &str) {
        return std::all_of(str.begin(), str.end(), [](char c) { return std::isspace(c); });
    };
    auto strip_quotes = [](const std::string &str) {
        if (str.at(0) == '"') {
            NPNR_ASSERT(str.back() == '"');
            return str.substr(1, str.size() - 2);
        } else if (str.at(0) == '{') {
            NPNR_ASSERT(str.back() == '}');
            return str.substr(1, str.size() - 2);
        } else {
            return str;
        }
    };
    auto split_to_args = [](const std::string &str, bool group_brackets) {
        std::vector<std::string> split_args;
        std::string buffer;
        auto flush = [&]() {
            if (!buffer.empty())
                split_args.push_back(buffer);
            buffer.clear();
        };
        int brcount = 0;
        for (char c : str) {
            if ((c == '[' || c == '{') && group_brackets) {
                ++brcount;
            }
            if ((c == ']' || c == '}') && group_brackets) {
                --brcount;
                buffer += c;
                if (brcount == 0)
                    flush();
                continue;
            }
            if (std::isspace(c)) {
                if (brcount == 0) {
                    flush();
                    continue;
                }
            }
            buffer += c;
        }
        flush();
        return split_args;
    };

    auto get_cells = [&](std::string str) {
        std::vector<CellInfo *> tgt_cells;
        if (str.empty() || str.front() != '[')
            log_error("failed to parse target (on line %d)\n", lineno);
        str = str.substr(1, str.size() - 2);
        auto split = split_to_args(str, false);
        if (split.size() < 1)
            log_error("failed to parse target (on line %d)\n", lineno);
        if (split.front() != "get_ports")
            log_error("targets other than 'get_ports' are not supported (on line %d)\n", lineno);
        if (split.size() < 2)
            log_error("failed to parse target (on line %d)\n", lineno);
        IdString cellname = ctx->id(strip_quotes(split.at(1)));
        if (ctx->cells.count(cellname))
            tgt_cells.push_back(ctx->cells.at(cellname).get());
        return tgt_cells;
    };

    auto get_nets = [&](std::string str) {
        std::vector<NetInfo *> tgt_nets;
        if (str.empty())
            return tgt_nets;
        if (str.front() != '[' || str.back() != ']')
            log_error("failed to parse target '%s' (on line %d)\n", str.c_str(), lineno);
        str = str.substr(1, str.size() - 2);
        auto split = split_to_args(str, false);
        if (split.size() < 1)
            log_error("failed to parse target (on line %d)\n", lineno);
        if (split.front() != "get_ports" && split.front() != "get_nets")
            log_error("targets other than 'get_ports' or 'get_nets' are not supported (on line %d)\n", lineno);
        if (split.size() < 2)
            log_error("failed to parse target (on line %d)\n", lineno);
        str = strip_quotes(split.at(1));
        if (str.empty())
            return tgt_nets;
        IdString netname = ctx->id(str);
        NetInfo *maybe_net = ctx->getNetByAlias(netname);
        if (maybe_net != nullptr) {
            tgt_nets.push_back(maybe_net);
            return tgt_nets;
        }
        // Also test the lowercase variant, for better interoperability with synthesis tools
        boost::algorithm::to_lower(str);
        netname = ctx->id(str);
        maybe_net = ctx->getNetByAlias(netname);
        if (maybe_net != nullptr)
            tgt_nets.push_back(maybe_net);
        return tgt_nets;
    };

    while (std::getline(in, line)) {
        ++lineno;
        // Trim comments, from # until end of the line
        size_t cstart = line.find('#');
        if (cstart != std::string::npos)
            line = line.substr(0, cstart);
        if (isempty(line))
            continue;

        std::vector<std::string> arguments = split_to_args(line, true);
        if (arguments.empty())
            continue;
        std::string &cmd = arguments.front();

        if (cmd == "set_property") {
            std::vector<std::pair<std::string, std::string>> arg_pairs;
            if (arguments.size() < 4) {
                log_nonfatal_error("expected at least four arguments to 'set_property' (on line %d)\n", lineno);
                num_errors++;
                goto nextline;
            }
            else if (arguments.at(1) == "-dict") {
                std::vector<std::string> dict_args = split_to_args(strip_quotes(arguments.at(2)), false);
                if ((dict_args.size() % 2) != 0) {
                    log_nonfatal_error("expected an even number of argument for dictionary (on line %d)\n", lineno);
                    num_errors++;
                    goto nextline;
                }
                arg_pairs.reserve(dict_args.size() / 2);
                for (int cursor = 0; cursor + 1 < int(dict_args.size()); cursor += 2) {
                    arg_pairs.emplace_back(std::move(dict_args.at(cursor)), std::move(dict_args.at(cursor + 1)));
                }
            } else {
                arg_pairs.emplace_back(std::move(arguments.at(1)), std::move(arguments.at(2)));
            }
            // Warning : ug835 has lowercase example, so probably supporting lowercase too is needed
            if (arg_pairs.size() == 1 && arg_pairs.front().first == "INTERNAL_VREF") { // get_iobanks not supported
                log_warning("INTERNAL_VREF isn't supported, ignoring (on line %d)\n", lineno);
                continue;
            }
            if (arguments.at(3).size() > 2 && arguments.at(3) == "[current_design]") {
                log_warning("[current_design] isn't supported, ignoring (on line %d)\n", lineno);
                continue;
            }
            // All remaining arguments are supposed to designate cells
            std::vector<CellInfo *> dest;
            for (int cursor = 3; cursor < int(arguments.size()); cursor++) {
                std::vector<CellInfo *> dest_loc = get_cells(arguments.at(cursor));
                if (dest_loc.empty())
                    log_warning("found set_property with no cells matching '%s' (on line %d)\n", arguments.at(cursor).c_str(), lineno);
                dest.insert(dest.end(), dest_loc.begin(), dest_loc.end());
            }
            for (auto c : dest) {
                for (const auto &pair : arg_pairs) {
                    IdString id_prop = ctx->id(pair.first);
                    if (ctx->debug)
                        log_info("applying property '%s' = '%s' to cell '%s' (on line %d)\n", pair.first.c_str(), pair.second.c_str(), c->name.c_str(ctx), lineno);
                    if(c->attrs.find(id_prop) != c->attrs.end()) {
                        log_nonfatal_error("found multiple properties '%s' for cell '%s' (on line %d)\n", pair.first.c_str(), c->name.c_str(ctx), lineno);
                        num_errors++;
                    }
                    c->attrs[id_prop] = std::string(pair.second);
                }
            }
        } else if (cmd == "create_clock") {
            double period = 0;
            bool got_period = false;
            int cursor = 1;
            for (cursor = 1; cursor < int(arguments.size()); cursor++) {
                std::string opt = arguments.at(cursor);
                if (opt == "-add") {
                    log_warning("ignoring unsupported XDC option '%s' (on line %d)\n", opt.c_str(), lineno);
                }
                else if (opt == "-name" || opt == "-waveform") {
                    log_warning("ignoring unsupported XDC option '%s' (on line %d)\n", opt.c_str(), lineno);
                    cursor++;
                }
                else if (opt == "-period") {
                    cursor++;
                    period = std::stod(arguments.at(cursor));
                    got_period = true;
                }
                else
                    break;
            }
            if (!got_period) {
                log_nonfatal_error("found create_clock without period (on line %d)\n", lineno);
                num_errors++;
                goto nextline;
            }
            // All remaining arguments are supposed to designate ports/nets
            std::vector<NetInfo *> dest;
            if (cursor >= int(arguments.size()))
                log_warning("found create_clock without designated nets (on line %d)\n", lineno);
            for ( ; cursor < (int)arguments.size(); cursor++) {
                std::vector<NetInfo *> dest_loc = get_nets(arguments.at(cursor));
                if (dest_loc.empty())
                    log_warning("found create_clock with no nets matching '%s' (on line %d)\n", arguments.at(cursor).c_str(), lineno);
                dest.insert(dest.end(), dest_loc.begin(), dest_loc.end());
            }
            for (auto n : dest) {
                if (ctx->debug)
                    log_info("applying clock period constraint on net '%s' (on line %d)\n", n->name.c_str(ctx), lineno);
                if (n->clkconstr.get() != nullptr) {
                    log_nonfatal_error("found multiple clock constraints on net '%s' (on line %d)\n", n->name.c_str(ctx), lineno);
                    num_errors++;
                }
                n->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint);
                n->clkconstr->period = DelayPair(ctx->getDelayFromNS(period));
                n->clkconstr->high = DelayPair(ctx->getDelayFromNS(period / 2));
                n->clkconstr->low = DelayPair(ctx->getDelayFromNS(period / 2));
            }
        } else {
            log_warning("ignoring unsupported XDC command '%s' (on line %d)\n", cmd.c_str(), lineno);
        }

        nextline:
        ;  // Phony statement to have something legal after the label
    }
    if (!isempty(linebuf)) {
        log_nonfatal_error("unexpected end of XDC file\n");
        num_errors++;
    }
    if (num_errors > 0) {
        log_error("Stopping the program after %u errors found in XDC file\n", num_errors);
    }
}

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  Miodrag Milanovic <micko@yosyshq.com>
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
#include <fstream>
#include <regex>

#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "ng_ultra.h"

#define HIMBAECHEL_CONSTIDS "uarch/ng-ultra/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

void NgUltraImpl::parse_csv(const std::string &filename)
{
    std::ifstream in(filename);
    if (!in)
        log_error("failed to open CSV file '%s'\n", filename.c_str());
    log_info("Parsing CSV file..\n");
    std::string line;
    std::string linebuf;
    int lineno = 0;
    enum uint8_t
    {
        IO_PADS = 0,
        IO_BANKS,
        IO_GCKS,
        IO_ERROR
    } line_type;

    auto isempty = [](const std::string &str) {
        return std::all_of(str.begin(), str.end(), [](char c) { return std::isspace(c); });
    };
    auto is_number = [](const std::string &str) {
        return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
    };
    auto get_cells = [&](std::string str) {
        std::vector<CellInfo *> tgt_cells;
        IdString cellname = ctx->id(str);
        if (ctx->cells.count(cellname))
            tgt_cells.push_back(ctx->cells.at(cellname).get());
        return tgt_cells;
    };

    line_type = IO_PADS;
    pool<std::string> banks_used;
    while (std::getline(in, line)) {
        ++lineno;
        // Trim comments, from # until end of the line
        size_t cstart = line.find('#');
        if (cstart != std::string::npos)
            line = line.substr(0, cstart);
        if (isempty(line))
            continue;

        std::vector<std::string> arguments;
        boost::split(arguments, line, boost::is_any_of(","));
        if (arguments.empty())
            continue;
        switch(line_type) {
            case IO_PADS:
                {
                    if (arguments.size()==1 && arguments[0][0]=='!') {
                        line_type = IO_BANKS;
                        continue;
                    }
                    if (arguments.size()!=15)
                        log_error("number of parameters in line %d must be 15\n", lineno);

                    if (!(boost::starts_with(arguments.at(1), "IOB") && boost::contains(arguments.at(1),"_D"))) 
                        log_error("invalid location name '%s' must start with 'IOB' in line %d\n", arguments.at(1).c_str(), lineno);

                    const char* standard_values[] = { "LVDS", "LVCMOS", "SSTL", "HSTL" };
                    auto it = std::find(std::begin(standard_values),std::end(standard_values), arguments.at(2));
                    if (it == std::end(standard_values))
                        log_error("unknown standard value '%s' in line %d\n", arguments.at(2).c_str(), lineno);

                    const char* drive_values[] = { "2mA", "4mA", "8mA", "16mA", "I", "II" };
                    it = std::find(std::begin(drive_values),std::end(drive_values), arguments.at(3));
                    if (it == std::end(drive_values))
                        log_error("unknown drive value '%s' in line %d\n", arguments.at(3).c_str(), lineno);

                    const char* weak_values[] = { "None", "PullDown", "PullUp", "Keeper" };
                    it = std::find(std::begin(weak_values),std::end(weak_values), arguments.at(4));
                    if (it == std::end(weak_values))
                        log_error("unknown weak termination value '%s' in line %d\n", arguments.at(4).c_str(), lineno);

                    const char* slew_values[] = { "Slow", "Medium", "Fast" };
                    it = std::find(std::begin(slew_values),std::end(slew_values), arguments.at(5));
                    if (it == std::end(slew_values))
                        log_error("unknown weak termination value '%s' in line %d\n", arguments.at(5).c_str(), lineno);

                    if (!arguments.at(6).empty() && !is_number(arguments.at(6)))
                        log_error("termination must be string containing int, value '%s' in line %d\n", arguments.at(6).c_str(), lineno);

                    if (!arguments.at(7).empty() && !is_number(arguments.at(7)))
                        log_error("input delay must be number, value '%s' in line %d\n", arguments.at(7).c_str(), lineno);
                    if (!arguments.at(8).empty() && !is_number(arguments.at(8)))
                        log_error("output delay must be number, value '%s' in line %d\n", arguments.at(8).c_str(), lineno);

                    if (!arguments.at(9).empty() && arguments.at(9) != "True" && arguments.at(9) != "False")
                        log_error("differential must be boolean, value '%s' in line %d\n", arguments.at(9).c_str(), lineno);

                    const char* termref_values[] = { "Floating", "VT" };
                    it = std::find(std::begin(termref_values),std::end(termref_values), arguments.at(10));
                    if (it == std::end(termref_values))
                        log_error("unknown termination reference value '%s' in line %d\n", arguments.at(10).c_str(), lineno);

                    if (!arguments.at(11).empty() && arguments.at(11) != "True" && arguments.at(11) != "False")
                        log_error("turbo must be boolean, value '%s' in line %d\n", arguments.at(11).c_str(), lineno);

                    if (!arguments.at(12).empty() && !is_number(arguments.at(12)))
                        log_error("signal slope must be number, value '%s' in line %d\n", arguments.at(12).c_str(), lineno);
                    if (!arguments.at(13).empty() && !is_number(arguments.at(13)))
                        log_error("output capacity must be number, value '%s' in line %d\n", arguments.at(13).c_str(), lineno);

                    const char* registered_values[] = { "Auto", "I", "IC", "O", "OC", "IO", "IOC" };
                    it = std::find(std::begin(registered_values),std::end(registered_values), arguments.at(14));
                    if (it == std::end(registered_values))
                        log_error("unknown registered value '%s' in line %d\n", arguments.at(14).c_str(), lineno);

                    std::vector<CellInfo *> dest = get_cells(arguments.at(0));
                    for (auto c : dest) {
                        c->attrs[id_LOC] = arguments.at(1);
                        c->params[ctx->id("iobname")] = arguments.at(0);
                        c->params[ctx->id("standard")] = arguments.at(2);
                        c->params[ctx->id("drive")] = arguments.at(3);
                        c->params[ctx->id("weakTermination")] = arguments.at(4);
                        c->params[ctx->id("slewRate")] = arguments.at(5);
                        c->params[ctx->id("termination")] = arguments.at(6);
                        c->params[ctx->id("inputDelayLine")] = arguments.at(7);
                        c->params[ctx->id("outputDelayLine")] = arguments.at(8);
                        c->params[ctx->id("differential")] = arguments.at(9);
                        c->params[ctx->id("terminationReference")] = arguments.at(10);
                        c->params[ctx->id("turbo")] = arguments.at(11);
                        c->params[ctx->id("inputSignalSlope")] = arguments.at(12);
                        c->params[ctx->id("outputCapacity")] = arguments.at(13);
                        //c->params[ctx->id("IO_PATH")] = arguments.at(14);
                    }
                    if (dest.size()==0)
                        log_warning("Pad with name '%s' not found in netlist.\n", arguments.at(0).c_str());
                    
                    std::string bank_name = arguments.at(1).substr(0,arguments.at(1).find_first_of('_'));
                    banks_used.emplace(bank_name);
                }
                break;
            case IO_BANKS:
                {
                    if (arguments.size()==1 && arguments[0][0]=='!') {
                        line_type = IO_GCKS;
                        continue;
                    }
                    if (arguments.size()!=3)
                        log_error("number of parameters in line %d must be 3\n", lineno);

                    if (!boost::starts_with(arguments.at(0), "IOB")) 
                        log_error("wrong bank name '%s' in line %d\n", arguments.at(0).c_str(), lineno);
                    
                    const char* voltages[] = { "1.5V", "1.8V", "2.5V", "3.3V" };
                    auto it = std::find(std::begin(voltages),std::end(voltages), arguments.at(1));
                    if (it == std::end(voltages))
                        log_error("unknown voltage level '%s' in line %d\n", arguments.at(1).c_str(), lineno);

                    bank_voltage[arguments.at(0)] = arguments.at(1);
                }
                break;
            case IO_GCKS:
                {
                    if (arguments.size()==1 && arguments[0][0]=='!') {
                        line_type = IO_ERROR;
                        continue;
                    }
                    if (arguments.size()!=2)
                        log_error("number of parameters in line %d must be 2\n", lineno);
                }
                break;
            default:
                log_error("switching to unknown block of data in line %d\n", lineno);
        }
    }
    for(auto& bank_name : banks_used) {
        if (bank_voltage.count(bank_name) == 0) {
            log_error("IO for bank '%s' defined, but no bank configuration.\n", bank_name.c_str());
        }
    }
}

NEXTPNR_NAMESPACE_END

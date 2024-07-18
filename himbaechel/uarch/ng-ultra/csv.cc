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

                    std::string arg_iobname = arguments.at(0);
                    std::string arg_location = arguments.at(1);
                    std::string arg_standard = arguments.at(2);
                    std::string arg_drive = arguments.at(3);
                    std::string arg_slewRate = arguments.at(4);
                    std::string arg_inputDelayLine = arguments.at(5);
                    std::string arg_outputDelayLine = arguments.at(6);
                    std::string arg_differential = arguments.at(7);
                    std::string arg_weakTermination = arguments.at(8);
                    std::string arg_termination = arguments.at(9);
                    std::string arg_terminationReference = arguments.at(10);
                    std::string arg_turbo = arguments.at(11);
                    std::string arg_inputSignalSlope = arguments.at(12);
                    std::string arg_outputCapacity = arguments.at(13);
                    std::string arg_registered = arguments.at(14);

                    // TODO: Remove this block
                    const char* weak_values_check[] = { "None", "PullDown", "PullUp", "Keeper" };
                    auto it2 = std::find(std::begin(weak_values_check),std::end(weak_values_check), arguments.at(4));
                    if (it2 != std::end(weak_values_check)) {
                        log_warning("Old CSV format detected. Please update file.\n");
                        arg_weakTermination = arguments.at(4);
                        arg_slewRate = arguments.at(5);
                        arg_termination = arguments.at(6);
                        arg_inputDelayLine = arguments.at(7);
                        arg_outputDelayLine = arguments.at(8);
                        arg_differential = arguments.at(9);
                    }
                    // End of block

                    if (!(boost::starts_with(arg_location, "IOB") && boost::contains(arg_location,"_D"))) 
                        log_error("invalid location name '%s' must start with 'IOB' in line %d\n", arg_location.c_str(), lineno);

                    const char* standard_values[] = { "LVDS", "LVCMOS", "SSTL", "HSTL" }; // , "POD"
                    auto it = std::find(std::begin(standard_values),std::end(standard_values), arg_standard);
                    if (it == std::end(standard_values))
                        log_error("unknown standard value '%s' in line %d\n", arg_standard.c_str(), lineno);

                    const char* drive_values[] = { "2mA", "4mA", "8mA", "16mA", "CatI", "CatII", "Undefined" }; // "6mA", "12mA", 
                    it = std::find(std::begin(drive_values),std::end(drive_values), arg_drive);
                    if (it == std::end(drive_values))
                        log_error("unknown drive value '%s' in line %d\n", arg_drive.c_str(), lineno);

                    const char* slew_values[] = { "Slow", "Medium", "Fast" };
                    it = std::find(std::begin(slew_values),std::end(slew_values), arg_slewRate);
                    if (it == std::end(slew_values))
                        log_error("unknown weak termination value '%s' in line %d\n", arg_slewRate.c_str(), lineno);

                    if (!arg_inputDelayLine.empty() && !is_number(arg_inputDelayLine)) {
                        log_error("input delay must be number, value '%s' in line %d\n", arg_inputDelayLine.c_str(), lineno);
                        int delay = std::stoi(arg_inputDelayLine);
                        if (delay<0 || delay >63)
                            log_error("input delay value must be in range from 0 to 63 in line %d\n", lineno);
                    }
                    if (!arg_outputDelayLine.empty() && !is_number(arg_outputDelayLine)) {
                        log_error("output delay must be number, value '%s' in line %d\n", arg_outputDelayLine.c_str(), lineno);
                        int delay = std::stoi(arg_outputDelayLine);
                        if (delay<0 || delay >63)
                            log_error("output delay value must be in range from 0 to 63 in line %d\n", lineno);
                    }

                    if (!arg_differential.empty() && arg_differential != "True" && arg_differential != "False")
                        log_error("differential must be boolean, value '%s' in line %d\n", arg_differential.c_str(), lineno);

                    const char* weak_values[] = { "None", "PullDown", "PullUp", "Keeper" };
                    it = std::find(std::begin(weak_values),std::end(weak_values), arg_weakTermination);
                    if (it == std::end(weak_values))
                        log_error("unknown weak termination value '%s' in line %d\n", arg_weakTermination.c_str(), lineno);

                    if (!arg_termination.empty() && !is_number(arg_termination)) {
                        log_error("termination must be string containing int, value '%s' in line %d\n", arg_termination.c_str(), lineno);
                        int termination = std::stoi(arg_termination);
                        if (termination<30 || termination >80)
                            log_error("termination value must be in range from 30 to 80 in line %d\n", lineno);
                    }

                    const char* termref_values[] = { "Floating", "VT" };
                    it = std::find(std::begin(termref_values),std::end(termref_values), arg_terminationReference);
                    if (it == std::end(termref_values))
                        log_error("unknown termination reference value '%s' in line %d\n", arg_terminationReference.c_str(), lineno);

                    if (!arg_turbo.empty() && arg_turbo != "True" && arg_turbo != "False")
                        log_error("turbo must be boolean, value '%s' in line %d\n", arg_turbo.c_str(), lineno);

                    if (!arg_inputSignalSlope.empty() && !is_number(arg_inputSignalSlope))
                        log_error("signal slope must be number, value '%s' in line %d\n", arg_inputSignalSlope.c_str(), lineno);
                    if (!arg_outputCapacity.empty() && !is_number(arg_outputCapacity))
                        log_error("output capacity must be number, value '%s' in line %d\n", arg_outputCapacity.c_str(), lineno);

                    const char* registered_values[] = { "Auto", "I", "IC", "O", "OC", "IO", "IOC" };
                    it = std::find(std::begin(registered_values),std::end(registered_values), arg_registered);
                    if (it == std::end(registered_values))
                        log_error("unknown registered value '%s' in line %d\n", arg_registered.c_str(), lineno);

                    if (arg_standard=="LVDS" && arg_drive!="Undefined")
                        log_error("for port in line %d when standard is 'LVDS' drive must be 'Undefined'\n", lineno);
                    if (arg_standard=="LVCMOS" && !boost::ends_with(arg_drive,"mA"))
                        log_error("for port in line %d when standard is 'LVCMOS' drive current must be in mA\n", lineno);
                    if ((arg_standard=="SSTL" || arg_standard=="HSTL") && !boost::starts_with(arg_drive,"Cat"))
                        log_error("for port in line %d when standard is 'SSTL' or 'HSTL' drive current must be in 'CatI' or 'CatII'\n", lineno);


                    if (arg_terminationReference=="Floating") {
                        if (!(arg_differential == "True" && arg_weakTermination == "None")) {
                            log_error("for floating termination, differential myst be 'True' and weakTermination must be 'None' in line %d\n", lineno);
                        }
                    }
                    std::vector<CellInfo *> dest = get_cells(arg_iobname);
                    for (auto c : dest) {
                        c->params[ctx->id("iobname")] = arg_iobname;
                        c->params[ctx->id("location")] = arg_location;
                        c->params[ctx->id("standard")] = arg_standard;
                        c->params[ctx->id("drive")] = arg_drive;
                        c->params[ctx->id("slewRate")] = arg_slewRate;
                        c->params[ctx->id("inputDelayLine")] = arg_inputDelayLine;
                        c->params[ctx->id("outputDelayLine")] = arg_outputDelayLine;
                        c->params[ctx->id("inputDelayOn")] = std::string((std::stoi(arg_inputDelayLine)!=0) ? "True" : "False");
                        c->params[ctx->id("outputDelayOn")] = std::string((std::stoi(arg_outputDelayLine)!=0) ? "True" : "False");
                        c->params[ctx->id("differential")] = arg_differential;
                        c->params[ctx->id("weakTermination")] = arg_weakTermination;
                        if (!arg_termination.empty()) {
                            c->params[ctx->id("termination")] = arg_termination;
                            c->params[ctx->id("terminationReference")] = arg_terminationReference;
                        }
                        c->params[ctx->id("turbo")] = arg_turbo;
                        c->params[ctx->id("inputSignalSlope")] = arg_inputSignalSlope;
                        c->params[ctx->id("outputCapacity")] = arg_outputCapacity;
                        c->params[ctx->id("registered")] = arg_registered;
                    }
                    if (dest.size()==0)
                        log_warning("Pad with name '%s' not found in netlist.\n", arg_iobname.c_str());
                    
                    std::string bank_name = arg_location.substr(0,arg_location.find_first_of('_'));
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
                    
                    const char* voltages[] = { "1.2V", "1.5V", "1.8V", "2.5V", "3.3V" };
                    auto it = std::find(std::begin(voltages),std::end(voltages), arguments.at(1));
                    if (it == std::end(voltages))
                        log_error("unknown voltage level '%s' in line %d\n", arguments.at(1).c_str(), lineno);

                    const char * direct_io_voltages[] = { "1.8V", "2.5V", "3.3V" };
                    const char * complex_io_voltages[] = { "1.2V", "1.5V", "1.8V" };

                    int bank = std::stoi(arguments.at(0).substr(3));
                    switch(bank) {
                        // direct
                        case 0:
                        case 1:
                        case 6:
                        case 7:
                            {
                            auto it = std::find(std::begin(direct_io_voltages),std::end(direct_io_voltages), arguments.at(1));
                            if (it == std::end(direct_io_voltages))
                                log_error("unsupported voltage level '%s' for bank '%s'\n", arguments.at(1).c_str(), arguments.at(0).c_str());
                            }
                            break;
                        // complex
                        default:
                            auto it = std::find(std::begin(complex_io_voltages),std::end(complex_io_voltages), arguments.at(1));
                            if (it == std::end(complex_io_voltages))
                                log_error("unsupported voltage level '%s' for bank '%s'\n", arguments.at(1).c_str(), arguments.at(0).c_str());
                    }
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

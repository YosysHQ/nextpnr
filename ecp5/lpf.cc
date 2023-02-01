/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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
#include <sstream>

#include "arch.h"
#include "log.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

static const pool<std::string> sysconfig_keys = {
        "SLAVE_SPI_PORT",      "MASTER_SPI_PORT", "SLAVE_PARALLEL_PORT",
        "BACKGROUND_RECONFIG", "DONE_EX",         "DONE_OD",
        "DONE_PULL",           "MCCLK_FREQ",      "TRANSFR",
        "CONFIG_IOVOLTAGE",    "CONFIG_SECURE",   "WAKE_UP",
        "COMPRESS_CONFIG",     "CONFIG_MODE",     "INBUF",
};

static const pool<std::string> iobuf_keys = {
        "IO_TYPE", "BANK",      "BANK_VCC",     "VREF",      "PULLMODE",   "DRIVE",       "SLEWRATE",
        "CLAMP",   "OPENDRAIN", "DIFFRESISTOR", "DIFFDRIVE", "HYSTERESIS", "TERMINATION",
};

bool Arch::apply_lpf(std::string filename, std::istream &in)
{
    auto isempty = [](const std::string &str) {
        return std::all_of(str.begin(), str.end(), [](char c) { return isblank(c) || c == '\r' || c == '\n'; });
    };
    try {
        if (!in)
            log_error("failed to open LPF file\n");
        std::string line;
        std::string linebuf;
        int lineno = 0;
        auto strip_quotes = [&](const std::string &str) {
            if (str.at(0) == '"') {
                if (str.back() != '"') {
                    log_error("expected '\"' at end of string '%s' (on line %d)\n", str.c_str(), lineno);
                }
                return str.substr(1, str.size() - 2);
            } else {
                return str;
            }
        };
        while (std::getline(in, line)) {
            ++lineno;
            size_t cstart = line.find('#');
            if (cstart != std::string::npos)
                line = line.substr(0, cstart);
            cstart = line.find("//");
            if (cstart != std::string::npos)
                line = line.substr(0, cstart);
            if (isempty(line))
                continue;
            linebuf += line;
            // Look for a command up to a semicolon
            size_t scpos = linebuf.find(';');
            while (scpos != std::string::npos) {
                std::string command = linebuf.substr(0, scpos);
                // Split command into words
                std::stringstream ss(command);
                std::vector<std::string> words;
                std::string tmp;
                while (ss >> tmp)
                    words.push_back(tmp);
                if (words.size() > 0) {
                    std::string verb = words.at(0);
                    if (verb == "BLOCK") {
                        if (words.size() != 2 || (words.at(1) != "ASYNCPATHS" && words.at(1) != "RESETPATHS"))
                            log_warning("    ignoring unsupported LPF command '%s' (on line %d)\n", command.c_str(),
                                        lineno);
                    } else if (verb == "SYSCONFIG") {
                        for (size_t i = 1; i < words.size(); i++) {
                            std::string setting = words.at(i);
                            size_t eqpos = setting.find('=');
                            if (eqpos == std::string::npos)
                                log_error("expected syntax 'SYSCONFIG <attr>=<value>...' (on line %d)\n", lineno);
                            std::string key = setting.substr(0, eqpos), value = setting.substr(eqpos + 1);
                            if (!sysconfig_keys.count(key))
                                log_error("unexpected SYSCONFIG key '%s' (on line %d)\n", key.c_str(), lineno);
                            settings[id("arch.sysconfig." + key)] = value;
                        }
                    } else if (verb == "FREQUENCY") {
                        if (words.size() < 2)
                            log_error("expected object type after FREQUENCY (on line %d)\n", lineno);
                        std::string etype = words.at(1);
                        if (etype == "PORT" || etype == "NET") {
                            if (words.size() < 4)
                                log_error("expected frequency value and unit after 'FREQUENCY %s' (on line %d)\n",
                                          etype.c_str(), lineno);
                            std::string target = strip_quotes(words.at(2));
                            float freq = std::stof(words.at(3));
                            std::string unit = words.at(4);
                            boost::algorithm::to_upper(unit);
                            if (unit == "MHZ")
                                ;
                            else if (unit == "KHZ")
                                freq /= 1.0e3;
                            else if (unit == "HZ")
                                freq /= 1.0e6;
                            else
                                log_error("unsupported frequency unit '%s' (on line %d)\n", unit.c_str(), lineno);
                            addClock(id(target), freq);
                        } else {
                            log_warning("    ignoring unsupported LPF command '%s %s' (on line %d)\n", command.c_str(),
                                        etype.c_str(), lineno);
                        }
                    } else if (verb == "LOCATE") {
                        if (words.size() < 5)
                            log_error("expected syntax 'LOCATE COMP <port name> SITE <pin>' (on line %d)\n", lineno);
                        if (words.at(1) != "COMP")
                            log_error("expected 'COMP' after 'LOCATE' (on line %d)\n", lineno);
                        std::string cell = strip_quotes(words.at(2));
                        if (words.at(3) != "SITE")
                            log_error("expected 'SITE' after 'LOCATE COMP %s' (on line %d)\n", cell.c_str(), lineno);
                        if (words.size() > 5)
                            log_error("unexpected input following LOCATE clause (on line %d)\n", lineno);
                        auto fnd_cell = cells.find(id(cell));
                        // 1-bit wires are treated as scalar by nextpnr.
                        // In HDL they might have been a singleton vector.
                        if (fnd_cell == cells.end() && cell.size() >= 3 && cell.substr(cell.size() - 3) == "[0]") {
                            cell = cell.substr(0, cell.size() - 3);
                            fnd_cell = cells.find(id(cell));
                        }

                        if (fnd_cell != cells.end()) {
                            fnd_cell->second->attrs[id_LOC] = strip_quotes(words.at(4));
                        }
                    } else if (verb == "IOBUF") {
                        if (words.size() < 3)
                            log_error("expected syntax 'IOBUF PORT <port name> <attr>=<value>...' (on line %d)\n",
                                      lineno);
                        if (words.at(1) != "PORT")
                            log_error("expected 'PORT' after 'IOBUF' (on line %d)\n", lineno);
                        std::string cell = strip_quotes(words.at(2));
                        auto fnd_cell = cells.find(id(cell));
                        if (fnd_cell != cells.end()) {
                            for (size_t i = 3; i < words.size(); i++) {
                                std::string setting = words.at(i);
                                size_t eqpos = setting.find('=');
                                if (eqpos == std::string::npos)
                                    log_error(
                                            "expected syntax 'IOBUF PORT <port name> <attr>=<value>...' (on line %d)\n",
                                            lineno);
                                std::string key = setting.substr(0, eqpos), value = setting.substr(eqpos + 1);
                                if (!iobuf_keys.count(key))
                                    log_warning("IOBUF '%s' attribute '%s' is not recognised (on line %d)\n",
                                                cell.c_str(), key.c_str(), lineno);
                                fnd_cell->second->attrs[id(key)] = value;
                            }
                        }
                    }
                }

                linebuf = linebuf.substr(scpos + 1);
                scpos = linebuf.find(';');
            }
        }
        if (!isempty(linebuf))
            log_error("unexpected end of LPF file\n");
        settings[id("input/lpf")] = filename;
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#include <sstream>
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

bool Arch::applyLPF(std::string filename, std::istream &in)
{
    auto isempty = [](const std::string &str) {
        return std::all_of(str.begin(), str.end(), [](char c) { return isblank(c); });
    };
    auto strip_quotes = [](const std::string &str) {
        if (str.at(0) == '"') {
            NPNR_ASSERT(str.back() == '"');
            return str.substr(1, str.size() - 2);
        } else {
            return str;
        }
    };

    try {
        if (!in)
            log_error("failed to open LPF file\n");
        std::string line;
        std::string linebuf;
        while (std::getline(in, line)) {
            size_t cstart = line.find('#');
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
                if (words.size() >= 0) {
                    std::string verb = words.at(0);
                    if (verb == "BLOCK" || verb == "SYSCONFIG") {
                        log_warning("    ignoring unsupported LPF command '%s'\n", command.c_str());
                    } else if (verb == "FREQUENCY") {
                        std::string etype = words.at(1);
                        if (etype == "PORT" || etype == "NET") {
                            std::string target = words.at(2);
                            if (target.at(0) == '\"') {
                                NPNR_ASSERT(target.back() == '\"');
                                target = target.substr(1, target.length() - 2);
                            }
                            float freq = std::stof(words.at(3));
                            std::string unit = words.at(4);
                            if (unit == "MHz")
                                ;
                            else if (unit == "kHz")
                                freq /= 1.0e3;
                            else if (unit == "Hz")
                                freq /= 1.0e6;
                            else
                                log_error("unsupported frequency unit '%s'\n", unit.c_str());
                            addClock(id(target), freq);
                        } else {
                            log_warning("    ignoring unsupported LPF command '%s %s'\n", command.c_str(),
                                        etype.c_str());
                        }
                    } else if (verb == "LOCATE") {
                        NPNR_ASSERT(words.at(1) == "COMP");
                        std::string cell = strip_quotes(words.at(2));
                        NPNR_ASSERT(words.at(3) == "SITE");
                        auto fnd_cell = cells.find(id(cell));
                        if (fnd_cell == cells.end()) {
                            log_warning("unmatched LPF 'LOCATE COMP' '%s'\n", cell.c_str());
                        } else {
                            fnd_cell->second->attrs[id("LOC")] = strip_quotes(words.at(4));
                        }
                    } else if (verb == "IOBUF") {
                        NPNR_ASSERT(words.at(1) == "PORT");
                        std::string cell = strip_quotes(words.at(2));
                        auto fnd_cell = cells.find(id(cell));
                        if (fnd_cell == cells.end()) {
                            log_warning("unmatched LPF 'IOBUF PORT' '%s'\n", cell.c_str());
                        } else {
                            for (size_t i = 3; i < words.size(); i++) {
                                std::string setting = words.at(i);
                                size_t eqpos = setting.find('=');
                                NPNR_ASSERT(eqpos != std::string::npos);
                                std::string key = setting.substr(0, eqpos), value = setting.substr(eqpos + 1);
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
        settings.emplace(id("input/lpf"), filename);
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END

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

#include <boost/algorithm/string.hpp>
#include <sstream>
#include <cctype>
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

bool Arch::applyUCF(std::string filename, std::istream &in)
{
    auto isempty = [](const std::string &str) {
        return std::all_of(str.begin(), str.end(), [](char c) { return isblank(c) || c == '\r' || c == '\n'; });
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
            log_error("failed to open UCF file\n");
        std::string line;
        std::string linebuf;
        int lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
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
                    if (verb == "CONFIG") {
                            log_warning("    ignoring unsupported LPF command '%s' (on line %d)\n", command.c_str(),
                                        lineno);
                    } else if (verb == "NET") {
                        if (words.size() < 2)
                            log_error("expected name after NET (on line %d)\n", lineno);
                        std::string target = strip_quotes(words.at(1));
			int pos = 2;
			while (pos < words.size()) {
                            std::string attr = words.at(pos);
			    pos++;
			    if (attr == "LOC" || attr == "IOSTANDARD" || attr == "DRIVE" || attr == "SLEW") {
				if (pos + 2 > words.size() || words.at(pos) != "=")
                                    log_error("expected %s = value (on line %d)\n", attr.c_str(), lineno);
                                std::string value = strip_quotes(words.at(pos + 1));
			        pos += 2;
				auto fnd_cell = cells.find(id(target));
				if (fnd_cell != cells.end()) {
				    fnd_cell->second->attrs[id(attr)] = value;
				}
			    } else if (attr == "PULLUP" || attr == "PULLDOWN" || attr == "KEEPER") {
				auto fnd_cell = cells.find(id(target));
				if (fnd_cell != cells.end()) {
				    fnd_cell->second->attrs[id("PULLTYPE")] = attr;
				}
			    } else if (attr == "PERIOD") {
				if (pos + 2 > words.size() || words.at(pos) != "=")
                                    log_error("expected PERIOD = value (on line %d)\n", lineno);
                                std::string value = words.at(pos + 1);
			        pos += 2;
				int upos = 0;
				while (upos < value.size() && (std::isdigit(value[upos]) || value[upos] == '.'))
					upos++;
                                float freq = std::stof(value.substr(0, upos));
                                std::string unit = value.substr(upos);
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
                                log_warning("    ignoring unsupported NET attribute '%s' (on line %d)\n", attr.c_str(),
                                    lineno);
			    }
			    if (pos < words.size()) {
				std::string cur = words.at(pos);
				if (cur != "|")
                                    log_error("expected | before %s (on line %d)\n", cur.c_str(), lineno);
				pos++;
			    }
			}
                    } else {
                        log_warning("    ignoring unsupported UCF command '%s' (on line %d)\n", verb.c_str(),
                                    lineno);
                    }
                }

                linebuf = linebuf.substr(scpos + 1);
                scpos = linebuf.find(';');
            }
        }
        if (!isempty(linebuf))
            log_error("unexpected end of UCF file\n");
        settings.emplace(id("input/ucf"), filename);
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END


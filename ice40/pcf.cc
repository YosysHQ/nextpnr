/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#include "pcf.h"
#include <sstream>
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

// Read a w

// Apply PCF constraints to a pre-packing design
bool apply_pcf(Context *ctx, std::istream &in)
{
    try {
        if (!in)
            log_error("failed to open PCF file");
        std::string line;
        while (std::getline(in, line)) {
            size_t cstart = line.find("#");
            if (cstart != std::string::npos)
                line = line.substr(0, cstart);
            std::stringstream ss(line);
            std::vector<std::string> words;
            std::string tmp;
            while (ss >> tmp)
                words.push_back(tmp);
            if (words.size() == 0)
                continue;
            std::string cmd = words.at(0);
            if (cmd == "set_io") {
                size_t args_end = 1;
                while (args_end < words.size() && words.at(args_end).at(0) == '-')
                    args_end++;
                std::string cell = words.at(args_end);
                std::string pin = words.at(args_end + 1);
                auto fnd_cell = ctx->cells.find(ctx->id(cell));
                if (fnd_cell == ctx->cells.end()) {
                    log_warning("unmatched pcf constraint %s\n", cell.c_str());
                } else {
                    BelId pin_bel = ctx->getPackagePinBel(pin);
                    if (pin_bel == BelId())
                        log_error("package does not have a pin named %s\n", pin.c_str());
                    fnd_cell->second->attrs[ctx->id("BEL")] = ctx->getBelName(pin_bel).str(ctx);
                    log_info("constrained '%s' to bel '%s'\n", cell.c_str(),
                             fnd_cell->second->attrs[ctx->id("BEL")].c_str());
                }
            } else if (cmd == "set_loc") {
                size_t args_end = 1;
                while (args_end < words.size() && words.at(args_end).at(0) == '-')
                    args_end++;
                std::string cell = words.at(args_end);
                std::string x = words.at(args_end + 1);
                std::string y = words.at(args_end + 2);
                std::string z = words.at(args_end + 3);
                auto fnd_cell = ctx->cells.find(ctx->id(cell));
                if (fnd_cell == ctx->cells.end()) {
                    log_error("unmatched pcf constraint %s\n", cell.c_str());
                }

                Loc loc;
                loc.x = std::stoi(x);
                loc.y = std::stoi(y);
                loc.z = std::stoi(z);
                auto bel = ctx->getBelByLocation(loc);
                if (bel == BelId()) {
                    log_error("constrain '%s': unknown bel\n", line.c_str());
                }

                fnd_cell->second->attrs[ctx->id("BEL")] = ctx->getBelName(bel).str(ctx);
                log_info("constrained '%s' to bel '%s'\n", cell.c_str(), ctx->getBelName(bel).c_str(ctx));

            } else {
                log_error("unsupported pcf command '%s'\n", cmd.c_str());
            }
        }
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END

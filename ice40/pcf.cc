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
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// Read a w

// Apply PCF constraints to a pre-packing design
bool apply_pcf(Context *ctx, std::string filename, std::istream &in)
{
    try {
        if (!in)
            log_error("failed to open PCF file\n");
        std::string line;
        int lineno = 0;
        while (std::getline(in, line)) {
            lineno++;
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
            bool nowarn = false;
            if (cmd == "set_io") {
                size_t args_end = 1;
                std::vector<std::pair<IdString, std::string>> extra_attrs;
                while (args_end < words.size() && words.at(args_end).at(0) == '-') {
                    const auto &setting = words.at(args_end);
                    if (setting == "-pullup") {
                        const auto &value = words.at(++args_end);
                        if (value == "yes" || value == "1")
                            extra_attrs.emplace_back(std::make_pair(ctx->id("PULLUP"), "1"));
                        else if (value == "no" || value == "0")
                            extra_attrs.emplace_back(std::make_pair(ctx->id("PULLUP"), "0"));
                        else
                            log_error("Invalid value '%s' for -pullup (on line %d)\n", value.c_str(), lineno);
                    } else if (setting == "-pullup_resistor") {
                        const auto &value = words.at(++args_end);
                        if (ctx->args.type != ArchArgs::UP5K)
                            log_error("Pullup resistance can only be set on UP5K (on line %d)\n", lineno);
                        if (value != "3P3K" && value != "6P8K" && value != "10K" && value != "100K")
                            log_error("Invalid value '%s' for -pullup_resistor (on line %d)\n", value.c_str(), lineno);
                        extra_attrs.emplace_back(std::make_pair(ctx->id("PULLUP_RESISTOR"), value));
                    } else if (setting == "-nowarn") {
                        nowarn = true;
                    } else if (setting == "--warn-no-port") {
                    } else {
                        log_warning("Ignoring PCF setting '%s' (on line %d)\n", setting.c_str(), lineno);
                    }
                    args_end++;
                }
                if (args_end >= words.size() - 1)
                    log_error("expected PCF syntax 'set_io cell pin' (on line %d)\n", lineno);
                std::string cell = words.at(args_end);
                std::string pin = words.at(args_end + 1);
                auto fnd_cell = ctx->cells.find(ctx->id(cell));
                if (fnd_cell == ctx->cells.end()) {
                    if (!nowarn)
                        log_warning("unmatched constraint '%s' (on line %d)\n", cell.c_str(), lineno);
                } else {
                    BelId pin_bel = ctx->getPackagePinBel(pin);
                    if (pin_bel == BelId())
                        log_error("package does not have a pin named '%s' (on line %d)\n", pin.c_str(), lineno);
                    if (fnd_cell->second->attrs.count(ctx->id("BEL")))
                        log_error("duplicate pin constraint on '%s' (on line %d)\n", cell.c_str(), lineno);
                    fnd_cell->second->attrs[ctx->id("BEL")] = ctx->getBelName(pin_bel).str(ctx);
                    log_info("constrained '%s' to bel '%s'\n", cell.c_str(),
                             fnd_cell->second->attrs[ctx->id("BEL")].c_str());
                    for (const auto &attr : extra_attrs)
                        fnd_cell->second->attrs[attr.first] = attr.second;
                }
            } else {
                log_error("unsupported PCF command '%s' (on line %d)\n", cmd.c_str(), lineno);
            }
        }
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_obuf") ||
                ci->type == ctx->id("$nextpnr_iobuf")) {
                if (!ci->attrs.count(ctx->id("BEL"))) {
                    if (bool_or_default(ctx->settings, ctx->id("pcf_allow_unconstrained")))
                        log_warning("IO '%s' is unconstrained in PCF and will be automatically placed\n",
                                    cell.first.c_str(ctx));
                    else
                        log_error("IO '%s' is unconstrained in PCF (override this error with "
                                  "--pcf-allow-unconstrained)\n",
                                  cell.first.c_str(ctx));
                }
            }
        }
        ctx->settings.emplace(ctx->id("input/pcf"), filename);
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END

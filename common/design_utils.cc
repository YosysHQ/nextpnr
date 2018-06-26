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

#include "design_utils.h"
#include <map>
#include "log.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

void replace_port(CellInfo *old_cell, IdString old_name, CellInfo *rep_cell, IdString rep_name)
{
    PortInfo &old = old_cell->ports.at(old_name);
    PortInfo &rep = rep_cell->ports.at(rep_name);
    assert(old.type == rep.type);

    rep.net = old.net;
    old.net = nullptr;
    if (rep.type == PORT_OUT) {
        if (rep.net != nullptr) {
            rep.net->driver.cell = rep_cell;
            rep.net->driver.port = rep_name;
        }
    } else if (rep.type == PORT_IN) {
        if (rep.net != nullptr) {
            for (PortRef &load : rep.net->users) {
                if (load.cell == old_cell && load.port == old_name) {
                    load.cell = rep_cell;
                    load.port = rep_name;
                }
            }
        }
    } else {
        assert(false);
    }
}

// Print utilisation of a design
void print_utilisation(const Context *ctx)
{
    // Sort by Bel type
    std::map<BelType, int> used_types;
    for (auto &cell : ctx->cells) {
        used_types[ctx->belTypeFromId(cell.second.get()->type)]++;
    }
    std::map<BelType, int> available_types;
    for (auto bel : ctx->getBels()) {
        available_types[ctx->getBelType(bel)]++;
    }
    log_break();
    log_info("Device utilisation:\n");
    for (auto type : available_types) {
        IdString type_id = ctx->belTypeToId(type.first);
        int used_bels = get_or_default(used_types, type.first, 0);
        log_info("\t%20s: %5d/%5d %5d%%\n", type_id.c_str(ctx), used_bels, type.second, 100 * used_bels / type.second);
    }
    log_break();
}

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

NEXTPNR_NAMESPACE_BEGIN

void replace_port(CellInfo *old_cell, IdString old_name, CellInfo *rep_cell,
                  IdString rep_name)
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

NEXTPNR_NAMESPACE_END

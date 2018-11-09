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

#include "nextpnr.h"

#ifndef DESIGN_UTILS_H
#define DESIGN_UTILS_H

#include <algorithm>

NEXTPNR_NAMESPACE_BEGIN

/*
Utilities for design manipulation, intended for use inside packing algorithms
 */

// Disconnect a net (if connected) from old, and connect it to rep
void replace_port(CellInfo *old_cell, IdString old_name, CellInfo *rep_cell, IdString rep_name);

// If a net drives a given port of a cell matching a predicate (in many
// cases more than one cell type, e.g. SB_DFFxx so a predicate is used), return
// the first instance of that cell (otherwise nullptr). If exclusive is set to
// true, then this cell must be the only load. If ignore_cell is set, that cell
// is not considered
template <typename F1>
CellInfo *net_only_drives(const Context *ctx, NetInfo *net, F1 cell_pred, IdString port, bool exclusive = false,
                          CellInfo *exclude = nullptr)
{
    if (net == nullptr)
        return nullptr;
    if (exclusive) {
        if (exclude == nullptr) {
            if (net->users.size() != 1)
                return nullptr;
        } else {
            if (net->users.size() > 2) {
                return nullptr;
            } else if (net->users.size() == 2) {
                if (std::find_if(net->users.begin(), net->users.end(),
                                 [exclude](const PortRef &ref) { return ref.cell == exclude; }) == net->users.end())
                    return nullptr;
            }
        }
    }
    for (const auto &load : net->users) {
        if (load.cell != exclude && cell_pred(ctx, load.cell) && load.port == port) {
            return load.cell;
        }
    }
    return nullptr;
}

// If a net is driven by a given port of a cell matching a predicate, return
// that cell, otherwise nullptr
template <typename F1> CellInfo *net_driven_by(const Context *ctx, const NetInfo *net, F1 cell_pred, IdString port)
{
    if (net == nullptr)
        return nullptr;
    if (net->driver.cell == nullptr)
        return nullptr;
    if (cell_pred(ctx, net->driver.cell) && net->driver.port == port) {
        return net->driver.cell;
    } else {
        return nullptr;
    }
}

// Connect a net to a port
void connect_port(const Context *ctx, NetInfo *net, CellInfo *cell, IdString port_name);

// Disconnect a net from a port
void disconnect_port(const Context *ctx, CellInfo *cell, IdString port_name);

// Connect two ports together
void connect_ports(Context *ctx, CellInfo *cell1, IdString port1_name, CellInfo *cell2, IdString port2_name);

void print_utilisation(const Context *ctx);

NEXTPNR_NAMESPACE_END

#endif

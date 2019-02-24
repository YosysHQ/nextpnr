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
#include <algorithm>
#include <map>
#include "log.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

void replace_port(CellInfo *old_cell, IdString old_name, CellInfo *rep_cell, IdString rep_name)
{
    if (!old_cell->ports.count(old_name))
        return;
    PortInfo &old = old_cell->ports.at(old_name);
    PortInfo &rep = rep_cell->ports.at(rep_name);
    NPNR_ASSERT(old.type == rep.type);

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
        NPNR_ASSERT(false);
    }
}

// Print utilisation of a design
void print_utilisation(const Context *ctx)
{
    // Sort by Bel type
    std::map<IdString, int> used_types;
    for (auto &cell : ctx->cells) {
        used_types[cell.second.get()->type]++;
    }
    std::map<IdString, int> available_types;
    for (auto bel : ctx->getBels()) {
        available_types[ctx->getBelType(bel)]++;
    }
    log_break();
    log_info("Device utilisation:\n");
    for (auto type : available_types) {
        IdString type_id = type.first;
        int used_bels = get_or_default(used_types, type.first, 0);
        log_info("\t%20s: %5d/%5d %5d%%\n", type_id.c_str(ctx), used_bels, type.second, 100 * used_bels / type.second);
    }
    log_break();
}

// Connect a net to a port
void connect_port(const Context *ctx, NetInfo *net, CellInfo *cell, IdString port_name)
{
    if (net == nullptr)
        return;
    PortInfo &port = cell->ports.at(port_name);
    NPNR_ASSERT(port.net == nullptr);
    port.net = net;
    if (port.type == PORT_OUT) {
        NPNR_ASSERT(net->driver.cell == nullptr);
        net->driver.cell = cell;
        net->driver.port = port_name;
    } else if (port.type == PORT_IN) {
        PortRef user;
        user.cell = cell;
        user.port = port_name;
        net->users.push_back(user);
    } else {
        NPNR_ASSERT_FALSE("invalid port type for connect_port");
    }
}

void disconnect_port(const Context *ctx, CellInfo *cell, IdString port_name)
{
    if (!cell->ports.count(port_name))
        return;
    PortInfo &port = cell->ports.at(port_name);
    if (port.net != nullptr) {
        port.net->users.erase(std::remove_if(port.net->users.begin(), port.net->users.end(),
                                             [cell, port_name](const PortRef &user) {
                                                 return user.cell == cell && user.port == port_name;
                                             }),
                              port.net->users.end());
        if (port.net->driver.cell == cell && port.net->driver.port == port_name)
            port.net->driver.cell = nullptr;
    }
}

void connect_ports(Context *ctx, CellInfo *cell1, IdString port1_name, CellInfo *cell2, IdString port2_name)
{
    PortInfo &port1 = cell1->ports.at(port1_name);
    if (port1.net == nullptr) {
        // No net on port1; need to create one
        std::unique_ptr<NetInfo> p1net(new NetInfo());
        p1net->name = ctx->id(cell1->name.str(ctx) + "$conn$" + port1_name.str(ctx));
        connect_port(ctx, p1net.get(), cell1, port1_name);
        IdString p1name = p1net->name;
        NPNR_ASSERT(!ctx->cells.count(p1name));
        ctx->nets[p1name] = std::move(p1net);
    }
    connect_port(ctx, port1.net, cell2, port2_name);
}

NEXTPNR_NAMESPACE_END

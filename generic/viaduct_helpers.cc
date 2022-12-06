/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "viaduct_helpers.h"
#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

void ViaductHelpers::resize_ids(int x, int y, int z)
{
    NPNR_ASSERT(x >= 0 && y >= 0 && x <= 20000 && y <= 20000 && z <= 1000);
    while (int(x_ids.size()) <= x) {
        IdString next = ctx->idf("X%d", int(x_ids.size()));
        x_ids.push_back(next);
    }
    while (int(y_ids.size()) <= y) {
        IdString next = ctx->idf("Y%d", int(y_ids.size()));
        y_ids.push_back(next);
    }
    while (int(z_ids.size()) <= y) {
        IdString next = ctx->idf("Z%d", int(z_ids.size()));
        z_ids.push_back(next);
    }
}

IdStringList ViaductHelpers::xy_id(int x, int y, IdString base)
{
    resize_ids(x, y);
    std::array<IdString, 3> result{x_ids.at(x), y_ids.at(y), base};
    return IdStringList(result);
}

IdStringList ViaductHelpers::xy_id(int x, int y, IdStringList base)
{
    resize_ids(x, y);
    std::array<IdString, 2> prefix{x_ids.at(x), y_ids.at(y)};
    return IdStringList::concat(IdStringList(prefix), base);
}

IdStringList ViaductHelpers::xyz_id(int x, int y, int z, IdString base)
{
    resize_ids(x, y, z);
    std::array<IdString, 4> result{x_ids.at(x), y_ids.at(y), z_ids.at(z), base};
    return IdStringList(result);
}

IdStringList ViaductHelpers::xyz_id(int x, int y, int z, IdStringList base)
{
    resize_ids(x, y, z);
    std::array<IdString, 3> prefix{x_ids.at(x), y_ids.at(y), z_ids.at(z)};
    return IdStringList::concat(IdStringList(prefix), base);
}

void ViaductHelpers::remove_nextpnr_iobs(const pool<CellTypePort> &top_ports)
{
    std::vector<IdString> to_remove;
    for (auto &cell : ctx->cells) {
        auto &ci = *cell.second;
        if (!ci.type.in(ctx->id("$nextpnr_ibuf"), ctx->id("$nextpnr_obuf"), ctx->id("$nextpnr_iobuf")))
            continue;
        NetInfo *i = ci.getPort(ctx->id("I"));
        if (i && i->driver.cell) {
            if (!top_ports.count(CellTypePort(i->driver)))
                log_error("Top-level port '%s' driven by illegal port %s.%s\n", ctx->nameOf(&ci),
                          ctx->nameOf(i->driver.cell), ctx->nameOf(i->driver.port));
        }
        NetInfo *o = ci.getPort(ctx->id("O"));
        if (o) {
            for (auto &usr : o->users) {
                if (!top_ports.count(CellTypePort(usr)))
                    log_error("Top-level port '%s' driving illegal port %s.%s\n", ctx->nameOf(&ci),
                              ctx->nameOf(usr.cell), ctx->nameOf(usr.port));
            }
        }
        ci.disconnectPort(ctx->id("I"));
        ci.disconnectPort(ctx->id("O"));
        to_remove.push_back(ci.name);
    }
    for (IdString cell_name : to_remove)
        ctx->cells.erase(cell_name);
}

int ViaductHelpers::constrain_cell_pairs(const pool<CellTypePort> &src_ports, const pool<CellTypePort> &sink_ports,
                                         int delta_z, bool allow_fanout)
{
    int constrained = 0;
    for (auto &cell : ctx->cells) {
        auto &ci = *cell.second;
        if (ci.cluster != ClusterId())
            continue; // don't constrain already-constrained cells
        bool done = false;
        for (auto &port : ci.ports) {
            // look for starting source ports
            if (port.second.type != PORT_OUT || !port.second.net)
                continue;
            if (!src_ports.count(CellTypePort(ci.type, port.first)))
                continue;
            if (!allow_fanout && port.second.net->users.entries() > 1)
                continue;
            for (auto &usr : port.second.net->users) {
                if (!sink_ports.count(CellTypePort(usr)))
                    continue;
                if (usr.cell->cluster != ClusterId())
                    continue;
                // Add the constraint
                ci.cluster = ci.name;
                ci.constr_abs_z = false;
                ci.constr_children.push_back(usr.cell);
                usr.cell->cluster = ci.name;
                usr.cell->constr_x = 0;
                usr.cell->constr_y = 0;
                usr.cell->constr_z = delta_z;
                usr.cell->constr_abs_z = false;
                ++constrained;
                done = true;
                break;
            }
            if (done)
                break;
        }
    }
    return constrained;
}

void ViaductHelpers::replace_constants(CellTypePort vcc_driver, CellTypePort gnd_driver,
                                       const dict<IdString, Property> &vcc_params,
                                       const dict<IdString, Property> &gnd_params)
{
    CellInfo *vcc_drv = ctx->createCell(ctx->id("$PACKER_VCC_DRV"), vcc_driver.cell_type);
    vcc_drv->addOutput(vcc_driver.port);
    for (auto &p : vcc_params)
        vcc_drv->params[p.first] = p.second;

    CellInfo *gnd_drv = ctx->createCell(ctx->id("$PACKER_GND_DRV"), gnd_driver.cell_type);
    gnd_drv->addOutput(gnd_driver.port);
    for (auto &p : gnd_params)
        gnd_drv->params[p.first] = p.second;

    NetInfo *vcc_net = ctx->createNet(ctx->id("$PACKER_VCC"));
    NetInfo *gnd_net = ctx->createNet(ctx->id("$PACKER_GND"));

    vcc_drv->connectPort(vcc_driver.port, vcc_net);
    gnd_drv->connectPort(gnd_driver.port, gnd_net);

    std::vector<IdString> trim_cells;
    std::vector<IdString> trim_nets;
    for (auto &net : ctx->nets) {
        auto &ni = *net.second;
        if (!ni.driver.cell)
            continue;
        if (ni.driver.cell->type != ctx->id("GND") && ni.driver.cell->type != ctx->id("VCC"))
            continue;
        NetInfo *replace = (ni.driver.cell->type == ctx->id("VCC")) ? vcc_net : gnd_net;
        for (auto &usr : ni.users) {
            usr.cell->ports.at(usr.port).net = replace;
            usr.cell->ports.at(usr.port).user_idx = replace->users.add(usr);
        }
        trim_cells.push_back(ni.driver.cell->name);
        trim_nets.push_back(ni.name);
    }
    for (IdString cell_name : trim_cells)
        ctx->cells.erase(cell_name);
    for (IdString net_name : trim_nets)
        ctx->nets.erase(net_name);
}

NEXTPNR_NAMESPACE_END

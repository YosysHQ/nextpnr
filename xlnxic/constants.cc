/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020-21  gatecat <gatecat@ds0.me>
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
#include "log.h"
#include "nextpnr.h"

#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

struct ConstantPacker
{
    Context *ctx;
    IdString gnd_net_name, vcc_net_name;
    NetInfo *gnd_net, *vcc_net;

    ConstantPacker(Context *ctx) : ctx(ctx), gnd_net_name(id_GLOBAL_LOGIC0), vcc_net_name(id_GLOBAL_LOGIC1){};

    pool<IdString> dead_cells, dead_nets;
    std::vector<std::tuple<IdString, IdString, bool>> const_cell_ports;

    dict<IdString, const CellTypePOD *> cell2db;

    void find_constants()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            bool is_gnd = ci->type == id_GND, is_vcc = ci->type == id_VCC;
            if (!is_gnd && !is_vcc)
                continue;
            for (auto &port : ci->ports) {
                NPNR_ASSERT(port.second.type == PORT_OUT); // constant driver should not have inputs
                // Find what ports it's driving
                NetInfo *ni = port.second.net;
                if (ni == nullptr)
                    continue;
                for (auto &usr : ni->users) {
                    const_cell_ports.emplace_back(usr.cell->name, usr.port, is_vcc);
                    // Don't bother with the potentially slow disconnect_port that also updates users as we are about to
                    // destroy the net...
                    usr.cell->ports.at(usr.port).net = nullptr;
                    // Update any connected macros to use the new net name
                    if (usr.cell->macro_parent != IdString()) {
                        auto &macro_exp = ctx->expanded_macros.at(usr.cell->macro_parent);
                        for (auto &port : macro_exp.ports) {
                            if (port.second == ni->name)
                                port.second = is_vcc ? vcc_net_name : gnd_net_name;
                        }
                    }
                }
                // Add the net to the list of nets to remove
                dead_nets.insert(ni->name);
            }
            dead_cells.insert(ci->name);
        }
    }

    void do_trim()
    {
        // Remove now-redundant cells/nets from the design, as we are about to create new unified constant nets
        for (auto &net_name : dead_nets)
            ctx->nets.erase(net_name);
        for (auto &cell_name : dead_cells)
            ctx->cells.erase(cell_name);
    }

    IdString get_inversion_prop(const CellInfo *ci, IdString pin)
    {
        if (!cell2db.count(ci->type))
            return IdString();
        const auto &cell_data = *(cell2db.at(ci->type));
        if (ctx->getBelBucketForCellType(ci->type) == id_FF && pin == id_D)
            return IdString(); // Not actually usefully invertible...
        if (ci->type.in(id_RAMB18E2, id_RAMB36E2) && pin.in(id_CLKARDCLK, id_CLKBWRCLK))
            return IdString(); // Treated as non-invertible for constant routing purposes...
        for (auto inv_entry : cell_data.inversions) {
            if (IdString(inv_entry.pin_name) == pin)
                return IdString(inv_entry.parameter);
        }
        return IdString();
    }

    void tie_pin(IdString cell, IdString pin, bool value)
    {
        CellInfo *cell_data = ctx->cells.at(cell).get();
        IdString inv_prop = get_inversion_prop(cell_data, pin);
        if (inv_prop != IdString()) {
            bool curr_inv_value = bool_or_default(cell_data->params, inv_prop, false);
            if (curr_inv_value)
                value = !value;
            // Routing a 1 is cheaper than a 0, so always do this for invertible pins
            if (!value) {
                cell_data->params[inv_prop] = Property(1);
                value = true;
            }
        }
        ctx->connectPort(value ? vcc_net_name : gnd_net_name, cell, pin);
    }

    void create_constants()
    {
        CellInfo *gnd_driver = ctx->createCell(ctx->id("$GND_DRIVER"), id_GND);
        gnd_driver->addOutput(id_G);
        CellInfo *vcc_driver = ctx->createCell(ctx->id("$VCC_DRIVER"), id_VCC);
        vcc_driver->addOutput(id_P);
        gnd_net = ctx->createNet(gnd_net_name);
        vcc_net = ctx->createNet(vcc_net_name);

        ctx->connectPort(gnd_net_name, gnd_driver->name, id_G);
        ctx->connectPort(vcc_net_name, vcc_driver->name, id_P);
        // Connect up cell ports we disconnected previously
        for (auto &port : const_cell_ports) {
            tie_pin(std::get<0>(port), std::get<1>(port), std::get<2>(port));
        }
        // Place constant drivers at (0, 0)
        for (BelId bel : ctx->getBelsByTile(0, 0)) {
            if (gnd_driver->bel == BelId() && ctx->getBelType(bel) == id_GND)
                ctx->bindBel(bel, gnd_driver, STRENGTH_LOCKED);
            if (vcc_driver->bel == BelId() && ctx->getBelType(bel) == id_VCC)
                ctx->bindBel(bel, vcc_driver, STRENGTH_LOCKED);
        }
        NPNR_ASSERT(gnd_driver->bel != BelId());
        NPNR_ASSERT(vcc_driver->bel != BelId());
    }

    void create_defaults()
    {
        // Apply default cell connections
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (!cell2db.count(ci->type))
                continue;
            for (auto &def : cell2db.at(ci->type)->defaults) {
                IdString pin(def.pin_name);
                if (!ci->ports.count(pin))
                    ci->addInput(pin); // if it doesn't exist at all, create it
                // For 'floating' type defaults, nothing more to do
                if (def.value == CellPinDefaultPOD::DISCONN)
                    continue;
                NetInfo *port_net = ci->ports.at(pin).net;
                if (port_net != nullptr) {
                    if (port_net->driver.cell != nullptr)
                        continue; // it's connected and driven, nothing to do
                    // it's connected but undriven, disconnect it so we can tie it to a constant instead
                    ci->disconnectPort(pin);
                }
                tie_pin(ci->name, pin, (def.value == CellPinDefaultPOD::ONE));
            }
        }
    }

    void trim_undriven()
    {
        // Vivado dislikes these...
        pool<IdString> undriven_nets;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (ni->driver.cell)
                continue;
            bool has_io = false; // for pad nets
            for (auto &user : ni->users) {
                if (user.cell->ports.at(user.port).type == PORT_INOUT) {
                    has_io = true;
                    break;
                }
            }
            if (has_io)
                continue;
            undriven_nets.insert(net.first);
        }
        for (auto net : undriven_nets) {
            NetInfo *ni = ctx->nets.at(net).get();
            for (auto user : ni->users)
                user.cell->disconnectPort(user.port);
            ctx->nets.erase(net);
        }
    }

    void run()
    {
        // Build up a fast index of types
        for (auto &cell_type_data : ctx->chip_info->cell_types) {
            cell2db.emplace(IdString(cell_type_data.cell_type), &cell_type_data);
        }
        trim_undriven();
        find_constants();
        do_trim();
        create_constants();
        create_defaults();
    }
};

void Arch::pack_constants()
{
    ConstantPacker packer(getCtx());
    packer.run();
}

NEXTPNR_NAMESPACE_END

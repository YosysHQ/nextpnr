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

#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN
namespace {
struct MistralPacker
{
    MistralPacker(Context *ctx) : ctx(ctx){};
    Context *ctx;

    NetInfo *gnd_net, *vcc_net;

    void init_constant_nets()
    {
        CellInfo *gnd_drv = ctx->createCell(ctx->id("$PACKER_GND_DRV"), id_MISTRAL_CONST);
        gnd_drv->params[id_LUT] = 0;
        gnd_drv->addOutput(id_Q);
        CellInfo *vcc_drv = ctx->createCell(ctx->id("$PACKER_VCC_DRV"), id_MISTRAL_CONST);
        vcc_drv->params[id_LUT] = 1;
        vcc_drv->addOutput(id_Q);
        gnd_net = ctx->createNet(ctx->id("$PACKER_GND_NET"));
        vcc_net = ctx->createNet(ctx->id("$PACKER_VCC_NET"));
        connect_port(ctx, gnd_net, gnd_drv, id_Q);
        connect_port(ctx, vcc_net, vcc_drv, id_Q);
    }

    CellPinState get_pin_needed_muxval(CellInfo *cell, IdString port)
    {
        NetInfo *net = get_net_or_empty(cell, port);
        if (net == nullptr || net->driver.cell == nullptr) {
            // Pin is disconnected
            // If a mux value exists already, honour it
            CellPinState exist_mux = cell->get_pin_state(port);
            if (exist_mux != PIN_SIG)
                return exist_mux;
            // Otherwise, look up the default value and use that
            CellPinStyle pin_style = ctx->get_cell_pin_style(cell, port);
            if ((pin_style & PINDEF_MASK) == PINDEF_0)
                return PIN_0;
            else if ((pin_style & PINDEF_MASK) == PINDEF_1)
                return PIN_1;
            else
                return PIN_SIG;
        }
        // Look to see if the driver is an inverter or constant
        IdString drv_type = net->driver.cell->type;
        if (drv_type == id_MISTRAL_NOT)
            return PIN_INV;
        else if (drv_type == id_GND)
            return PIN_0;
        else if (drv_type == id_VCC)
            return PIN_1;
        else
            return PIN_SIG;
    }

    void uninvert_port(CellInfo *cell, IdString port)
    {
        // Rewire a port so it is driven by the input to an inverter
        NetInfo *net = get_net_or_empty(cell, port);
        NPNR_ASSERT(net != nullptr && net->driver.cell != nullptr && net->driver.cell->type == id_MISTRAL_NOT);
        CellInfo *inv = net->driver.cell;
        disconnect_port(ctx, cell, port);

        NetInfo *inv_a = get_net_or_empty(inv, id_A);
        if (inv_a != nullptr) {
            connect_port(ctx, inv_a, cell, port);
        }
    }

    void process_inv_constants(CellInfo *cell)
    {
        // TODO: we might need to create missing inputs here in some cases so we can tie them to the correct constant?
        // Fold inverters and constants into a cell
        for (auto &port : cell->ports) {
            // Iterate over all inputs
            if (port.second.type != PORT_IN)
                continue;
            IdString port_name = port.first;

            CellPinState req_mux = get_pin_needed_muxval(cell, port_name);
            if (req_mux == PIN_SIG) {
                // No special setting required, ignore
                continue;
            }

            CellPinStyle pin_style = ctx->get_cell_pin_style(cell, port_name);

            if (req_mux == PIN_INV) {
                // Pin is inverted. If there is a hard inverter; then use it
                if (pin_style & PINOPT_INV) {
                    uninvert_port(cell, port_name);
                    cell->pin_data[port_name].state = PIN_INV;
                }
            } else if (req_mux == PIN_0 || req_mux == PIN_1) {
                // Pin is tied to a constant
                // If there is a hard constant option; use it
                if ((pin_style & int(req_mux)) == req_mux) {
                    disconnect_port(ctx, cell, port_name);
                    cell->pin_data[port_name].state = req_mux;
                } else {
                    disconnect_port(ctx, cell, port_name);
                    // There is no hard constant, we need to connect it to the relevant soft-constant net
                    connect_port(ctx, (req_mux == PIN_1) ? vcc_net : gnd_net, cell, port_name);
                }
            }
        }
    }

    void trim_design()
    {
        // Remove unused inverters and high/low drivers
        std::vector<IdString> trim_cells;
        std::vector<IdString> trim_nets;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type != id_MISTRAL_NOT && ci->type != id_GND && ci->type != id_VCC)
                continue;
            IdString port = (ci->type == id_MISTRAL_NOT) ? id_Q : ci->type;
            NetInfo *out = get_net_or_empty(ci, port);
            if (out == nullptr) {
                trim_cells.push_back(ci->name);
                continue;
            }
            if (!out->users.empty())
                continue;

            disconnect_port(ctx, ci, id_A);

            trim_cells.push_back(ci->name);
            trim_nets.push_back(out->name);
        }

        for (IdString rem_net : trim_nets)
            ctx->nets.erase(rem_net);
        for (IdString rem_cell : trim_cells)
            ctx->cells.erase(rem_cell);
    }

    void pack_constants()
    {
        // Iterate through cells
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            // Skip certain cells at this point
            if (ci->type != id_MISTRAL_NOT && ci->type != id_GND && ci->type != id_VCC)
                process_inv_constants(cell.second);
        }
        // Remove superfluous inverters and constant drivers
        trim_design();
    }

    void run()
    {
        init_constant_nets();
        pack_constants();
    }
};
}; // namespace

bool Arch::pack()
{
    // TODO:
    //  - Constrain IO

    MistralPacker packer(getCtx());
    packer.run();

    return true;
}

NEXTPNR_NAMESPACE_END
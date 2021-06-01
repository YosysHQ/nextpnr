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
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_MISTRAL_NOT && ci->type != id_GND && ci->type != id_VCC)
                continue;
            IdString port = (ci->type == id_MISTRAL_NOT) ? id_Q : id_Y;
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
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            // Skip certain cells at this point
            if (ci->type != id_MISTRAL_NOT && ci->type != id_GND && ci->type != id_VCC)
                process_inv_constants(ci);
        }
        // Special case - SDATA can only be trimmed if SLOAD is low
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_MISTRAL_FF)
                continue;
            if (ci->get_pin_state(id_SLOAD) != PIN_0)
                continue;
            disconnect_port(ctx, ci, id_SDATA);
        }
        // Remove superfluous inverters and constant drivers
        trim_design();
    }

    void prepare_io()
    {
        // Find the actual IO buffer corresponding to a port; and copy attributes across to it
        // Note that this relies on Yosys to do IO buffer inference, to avoid tristate issues once we get to synthesised
        // JSON. In all cases the nextpnr-inserted IO buffers are removed as redundant.
        for (auto &port : ctx->ports) {
            if (!ctx->cells.count(port.first))
                log_error("Port '%s' doesn't seem to have a corresponding top level IO\n", ctx->nameOf(port.first));
            CellInfo *ci = ctx->cells.at(port.first).get();

            PortRef top_port;
            top_port.cell = nullptr;
            bool is_npnr_iob = false;

            if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
                // Might have an input buffer (IB etc) connected to it
                is_npnr_iob = true;
                NetInfo *o = get_net_or_empty(ci, id_O);
                if (o == nullptr)
                    ;
                else if (o->users.size() > 1)
                    log_error("Top level pin '%s' has multiple input buffers\n", ctx->nameOf(port.first));
                else if (o->users.size() == 1)
                    top_port = o->users.at(0);
            }
            if (ci->type == ctx->id("$nextpnr_obuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
                // Might have an output buffer (OB etc) connected to it
                is_npnr_iob = true;
                NetInfo *i = get_net_or_empty(ci, id_I);
                if (i != nullptr && i->driver.cell != nullptr) {
                    if (top_port.cell != nullptr)
                        log_error("Top level pin '%s' has multiple input/output buffers\n", ctx->nameOf(port.first));
                    top_port = i->driver;
                }
                // Edge case of a bidirectional buffer driving an output pin
                if (i->users.size() > 2) {
                    log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
                } else if (i->users.size() == 2) {
                    if (top_port.cell != nullptr)
                        log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
                    for (auto &usr : i->users) {
                        if (usr.cell->type == ctx->id("$nextpnr_obuf") || usr.cell->type == ctx->id("$nextpnr_iobuf"))
                            continue;
                        top_port = usr;
                        break;
                    }
                }
            }
            if (!is_npnr_iob)
                log_error("Port '%s' doesn't seem to have a corresponding top level IO (internal cell type mismatch)\n",
                          ctx->nameOf(port.first));

            if (top_port.cell == nullptr) {
                log_info("Trimming port '%s' as it is unused.\n", ctx->nameOf(port.first));
            } else {
                // Copy attributes to real IO buffer
                if (ctx->io_attr.count(port.first)) {
                    for (auto &kv : ctx->io_attr.at(port.first)) {
                        top_port.cell->attrs[kv.first] = kv.second;
                    }
                }
                // Make sure that top level net is set correctly
                port.second.net = top_port.cell->ports.at(top_port.port).net;
            }
            // Now remove the nextpnr-inserted buffer
            disconnect_port(ctx, ci, id_I);
            disconnect_port(ctx, ci, id_O);
            ctx->cells.erase(port.first);
        }
    }

    void pack_io()
    {
        // Step 0: deal with top level inserted IO buffers
        prepare_io();
        // Stage 1: apply constraints
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            // Iterate through all IO buffer primitives
            if (!ctx->is_io_cell(ci->type))
                continue;
            // We need all IO constrained at the moment, unconstrained IO are rare enough not to care
            if (!ci->attrs.count(id_LOC))
                log_error("Found unconstrained IO '%s', these are currently unsupported\n", ctx->nameOf(ci));
            // Convert package pin constraint to bel constraint
            std::string loc = ci->attrs.at(id_LOC).as_string();
            if (loc.compare(0, 4, "PIN_") != 0)
                log_error("Expecting PIN_-prefixed pin for IO '%s', got '%s'\n", ctx->nameOf(ci), loc.c_str());
            auto pin_info = ctx->cyclonev->pin_find_name(loc.substr(4));
            if (pin_info == nullptr)
                log_error("IO '%s' is constrained to invalid pin '%s'\n", ctx->nameOf(ci), loc.c_str());
            BelId bel = ctx->get_io_pin_bel(pin_info);

            if (bel == BelId()) {
                log_error("IO '%s' is constrained to pin %s which is not a supported IO pin.\n", ctx->nameOf(ci),
                          loc.c_str());
            } else {
                log_info("Constraining IO '%s' to pin %s (bel %s)\n", ctx->nameOf(ci), loc.c_str(),
                         ctx->nameOfBel(bel));
                ctx->bindBel(bel, ci, STRENGTH_LOCKED);
            }
        }
    }

    void constrain_carries()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_MISTRAL_ALUT_ARITH)
                continue;
            const NetInfo *cin = get_net_or_empty(ci, id_CI);
            if (cin != nullptr && cin->driver.cell != nullptr)
                continue; // not the start of a chain
            std::vector<CellInfo *> chain;
            CellInfo *cursor = ci;
            while (true) {
                chain.push_back(cursor);
                const NetInfo *co = get_net_or_empty(cursor, id_CO);
                if (co == nullptr || co->users.empty())
                    break;
                if (co->users.size() > 1)
                    log_error("Carry net %s has more than one sink!\n", ctx->nameOf(co));
                auto &usr = co->users.at(0);
                if (usr.port != id_CI)
                    log_error("Carry net %s drives port %s, expected CI\n", ctx->nameOf(co), ctx->nameOf(usr.port));
                cursor = usr.cell;
            }

            chain.at(0)->constr_abs_z = true;
            chain.at(0)->constr_z = 0;
            chain.at(0)->cluster = chain.at(0)->name;

            for (int i = 1; i < int(chain.size()); i++) {
                chain.at(i)->constr_x = 0;
                chain.at(i)->constr_y = -(i / 20);
                // 2 COMB, 4 FF per ALM
                chain.at(i)->constr_z = ((i / 2) % 10) * 6 + (i % 2);
                chain.at(i)->constr_abs_z = true;
                chain.at(i)->cluster = chain.at(0)->name;
                chain.at(0)->constr_children.push_back(chain.at(i));
            }

            if (ctx->debug) {
                log_info("Chain: \n");
                for (int i = 0; i < int(chain.size()); i++) {
                    auto &c = chain.at(i);
                    log_info("    i=%d cell=%s dy=%d z=%d ci=%s co=%s\n", i, ctx->nameOf(c), c->constr_y, c->constr_z,
                             ctx->nameOf(get_net_or_empty(c, id_CI)), ctx->nameOf(get_net_or_empty(c, id_CO)));
                }
            }
        }
        // Check we reached all the cells in the above pass
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_MISTRAL_ALUT_ARITH)
                continue;
            if (ci->cluster == ClusterId())
                log_error("Failed to include arith cell '%s' in any chain (CI=%s)\n", ctx->nameOf(ci),
                          ctx->nameOf(get_net_or_empty(ci, id_CI)));
        }
    }

    void run()
    {
        init_constant_nets();
        pack_constants();
        pack_io();
        constrain_carries();
    }
};
}; // namespace

bool Arch::pack()
{
    MistralPacker packer(getCtx());
    packer.run();

    assignArchInfo();

    return true;
}

NEXTPNR_NAMESPACE_END

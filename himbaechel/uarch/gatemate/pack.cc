/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include <boost/algorithm/string.hpp>

#include "design_utils.h"
#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

// Return true if a cell is a flipflop
inline bool is_dff(const BaseCtx *ctx, const CellInfo *cell) { return cell->type.in(id_CC_DFF, id_CC_DLT); }

void GateMatePacker::flush_cells()
{
    for (auto pcell : packed_cells) {
        for (auto &port : ctx->cells[pcell]->ports) {
            ctx->cells[pcell]->disconnectPort(port.first);
        }
        ctx->cells.erase(pcell);
    }
    packed_cells.clear();
}

void GateMatePacker::disconnect_if_gnd(CellInfo *cell, IdString input)
{
    NetInfo *net = cell->getPort(input);
    if (!net)
        return;
    if (net->name.in(ctx->id("$PACKER_GND"))) {
        cell->disconnectPort(input);
    }
}

BelId GateMatePacker::get_bank_cpe(int bank)
{
    switch (bank) {
    case 0:
        return ctx->getBelByLocation(Loc(97 + 2, 128 + 2, 1)); // N1, RAM_O1
    case 1:
        return ctx->getBelByLocation(Loc(97 + 2, 128 + 2, 0)); // N2, RAM_O2
    case 2:
        return ctx->getBelByLocation(Loc(160 + 2, 65 + 2, 1)); // E1, RAM_O1
    case 3:
        return ctx->getBelByLocation(Loc(160 + 2, 65 + 2, 0)); // E2, RAM_O2
    case 4:
        return ctx->getBelByLocation(Loc(1 + 2, 65 + 2, 1)); // W1, RAM_O1
    case 5:
        return ctx->getBelByLocation(Loc(1 + 2, 65 + 2, 0)); // W2, RAM_O2
    case 6:
        return ctx->getBelByLocation(Loc(97 + 2, 1 + 2, 1)); // S1, RAM_O1
    case 7:
        return ctx->getBelByLocation(Loc(97 + 2, 1 + 2, 0)); // S2, RAM_O2
    case 8:
        return ctx->getBelByLocation(Loc(49 + 2, 1 + 2, 1)); // S3, RAM_O1
    default:
        log_error("Unkown bank\n");
    }
}

void GateMatePacker::pack_io()
{
    // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
    for (auto &port : ctx->ports) {
        if (!ctx->cells.count(port.first))
            log_error("Port '%s' doesn't seem to have a corresponding top level IO\n", ctx->nameOf(port.first));
        CellInfo *ci = ctx->cells.at(port.first).get();

        PortRef top_port;
        top_port.cell = nullptr;
        bool is_npnr_iob = false;

        if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
            // Might have an input buffer connected to it
            is_npnr_iob = true;
            NetInfo *o = ci->getPort(id_O);
            if (o == nullptr)
                ;
            else if (o->users.entries() > 1)
                log_error("Top level pin '%s' has multiple input buffers\n", ctx->nameOf(port.first));
            else if (o->users.entries() == 1)
                top_port = *o->users.begin();
        }
        if (ci->type == ctx->id("$nextpnr_obuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
            // Might have an output buffer connected to it
            is_npnr_iob = true;
            NetInfo *i = ci->getPort(id_I);
            if (i != nullptr && i->driver.cell != nullptr) {
                if (top_port.cell != nullptr)
                    log_error("Top level pin '%s' has multiple input/output buffers\n", ctx->nameOf(port.first));
                top_port = i->driver;
            }
            // Edge case of a bidirectional buffer driving an output pin
            if (i->users.entries() > 2) {
                log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
            } else if (i->users.entries() == 2) {
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
            for (auto &attrs : ci->attrs)
                top_port.cell->attrs[attrs.first] = attrs.second;
            for (auto &params : ci->params) {
                IdString key = params.first;
                if (top_port.cell->type.in(id_CC_LVDS_IBUF, id_CC_LVDS_OBUF, id_CC_LVDS_TOBUF, id_CC_LVDS_IOBUF)) {
                    if (top_port.port.in(id_I_P, id_O_P, id_IO_P))
                        key = id_PIN_NAME_P;
                    if (top_port.port.in(id_I_N, id_O_N, id_IO_N))
                        key = id_PIN_NAME_N;
                }
                if (top_port.cell->params.count(key)) {
                    if (top_port.cell->params[key] != params.second) {
                        std::string val = params.second.is_string ? params.second.as_string()
                                                                  : std::to_string(params.second.as_int64());
                        log_warning("Overriding parameter '%s' with value '%s' for cell '%s'.\n", key.c_str(ctx),
                                    val.c_str(), ctx->nameOf(top_port.cell));
                    }
                }
                top_port.cell->params[key] = params.second;
            }

            // Make sure that top level net is set correctly
            port.second.net = top_port.cell->ports.at(top_port.port).net;
        }
        // Now remove the nextpnr-inserted buffer
        ci->disconnectPort(id_I);
        ci->disconnectPort(id_O);
        ctx->cells.erase(port.first);
    }

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_IBUF, id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF, id_CC_LVDS_IBUF, id_CC_LVDS_OBUF,
                        id_CC_LVDS_TOBUF, id_CC_LVDS_IOBUF))
            continue;

        bool is_lvds = ci.type.in(id_CC_LVDS_IBUF, id_CC_LVDS_OBUF, id_CC_LVDS_TOBUF, id_CC_LVDS_IOBUF);

        std::string loc = str_or_default(ci.params, is_lvds ? id_PIN_NAME_P : id_PIN_NAME, "UNPLACED");
        if (ci.params.count(id_LOC)) {
            std::string new_loc = str_or_default(ci.params, id_LOC, "UNPLACED");
            if (loc != "UNPLACED" && loc != new_loc)
                log_warning("Overriding location of cell '%s' from '%s' with '%s'\n", ctx->nameOf(&ci), loc.c_str(),
                            new_loc.c_str());
            loc = new_loc;
        }

        if (loc == "UNPLACED")
            log_warning("IO signal name '%s' is not defined in CCF file and will be auto-placed.\n", ctx->nameOf(&ci));

        disconnect_if_gnd(&ci, id_T);
        if (ci.type == id_CC_TOBUF && !ci.getPort(id_T))
            ci.type = id_CC_OBUF;
        if (ci.type == id_CC_LVDS_TOBUF && !ci.getPort(id_T))
            ci.type = id_CC_LVDS_OBUF;

        std::vector<IdString> keys;
        for (auto &p : ci.params) {

            if (p.first.in(id_PIN_NAME, id_PIN_NAME_P, id_PIN_NAME_N)) {
                if (ctx->get_package_pin_bel(ctx->id(p.second.as_string())) == BelId())
                    log_error("Unknown %s '%s' for cell '%s'.\n", p.first.c_str(ctx), p.second.as_string().c_str(),
                              ci.name.c_str(ctx));
                keys.push_back(p.first);
                continue;
            }
            if (p.first.in(id_V_IO, id_LOC)) {
                keys.push_back(p.first);
                continue;
            }
            if (ci.type.in(id_CC_IBUF, id_CC_IOBUF) &&
                p.first.in(id_PULLUP, id_PULLDOWN, id_KEEPER, id_SCHMITT_TRIGGER, id_DELAY_IBF, id_FF_IBF))
                continue;
            if (ci.type.in(id_CC_TOBUF) && p.first.in(id_PULLUP, id_PULLDOWN, id_KEEPER))
                continue;
            if (ci.type.in(id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF) &&
                p.first.in(id_DRIVE, id_SLEW, id_DELAY_OBF, id_FF_OBF))
                continue;
            if (ci.type.in(id_CC_LVDS_IBUF, id_CC_LVDS_IOBUF) && p.first.in(id_LVDS_RTERM, id_DELAY_IBF, id_FF_IBF))
                continue;
            if (ci.type.in(id_CC_LVDS_OBUF, id_CC_LVDS_TOBUF, id_CC_LVDS_IOBUF) &&
                p.first.in(id_LVDS_BOOST, id_DELAY_OBF, id_FF_OBF))
                continue;
            log_warning("Removing unsupported parameter '%s' for type '%s'.\n", p.first.c_str(ctx), ci.type.c_str(ctx));
            keys.push_back(p.first);
        }
        if (ci.params.count(id_SLEW)) {
            std::string val = str_or_default(ci.params, id_SLEW, "UNDEFINED");
            if (val == "UNDEFINED")
                keys.push_back(id_SLEW);
            else if (val == "FAST")
                ci.params[id_SLEW] = Property(Property::State::S1);
            else if (val == "SLOW")
                ci.params[id_SLEW] = Property(Property::State::S0);
            else
                log_error("Unknown value '%s' for SLEW parameter of '%s' cell.\n", val.c_str(), ci.name.c_str(ctx));
        }
        if (is_lvds) {
            std::string p_pin = str_or_default(ci.params, id_PIN_NAME_P, "UNPLACED");
            std::string n_pin = str_or_default(ci.params, id_PIN_NAME_N, "UNPLACED");
            if (p_pin == "UNPLACED" || n_pin == "UNPLACED")
                log_error("Both LVDS pins must be set to a valid locations.\n");
            if (p_pin.substr(0, 6) != n_pin.substr(0, 6) || p_pin[7] != n_pin[7])
                log_error("Both LVDS pads '%s' and '%s' do not match.\n", p_pin.c_str(), n_pin.c_str());
            if (p_pin[6] != 'A')
                log_error("Both LVDS positive pad must be from type A.\n");
            if (n_pin[6] != 'B')
                log_error("Both LVDS negative pad must be from type B.\n");
        }
        for (auto key : keys)
            ci.params.erase(key);

        // For output pins set SLEW to FAST if not defined
        if (!ci.params.count(id_SLEW) && ci.type.in(id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF))
            ci.params[id_SLEW] = Property(Property::State::S1);

        if ((ci.params.count(id_KEEPER) + ci.params.count(id_PULLUP) + ci.params.count(id_PULLDOWN)) > 1)
            log_error("PULLUP, PULLDOWN and KEEPER are mutually exclusive parameters.\n");

        if (is_lvds)
            ci.params[id_LVDS_EN] = Property(Property::State::S1);

        // DELAY_IBF and DELAY_OBF must be set depending of type
        // Also we need to enable input/output
        if (ci.type.in(id_CC_IBUF, id_CC_IOBUF, id_CC_LVDS_IBUF, id_CC_LVDS_IOBUF)) {
            ci.params[id_DELAY_IBF] = Property(1 << int_or_default(ci.params, id_DELAY_IBF, 0), 16);
            if (is_lvds)
                ci.params[id_LVDS_IE] = Property(Property::State::S1);
            else
                ci.params[id_INPUT_ENABLE] = Property(Property::State::S1);
        }
        if (ci.type.in(id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF, id_CC_LVDS_OBUF, id_CC_LVDS_TOBUF, id_CC_LVDS_IOBUF)) {
            ci.params[id_DELAY_OBF] = Property(1 << int_or_default(ci.params, id_DELAY_OBF, 0), 16);
            ci.params[id_OE_ENABLE] = Property(Property::State::S1);
        }
        if (ci.params.count(id_DRIVE)) {
            int val = int_or_default(ci.params, id_DRIVE, 0);
            if (val != 3 && val != 6 && val != 9 && val != 12)
                log_error("Unsupported value '%d' for DRIVE parameter of '%s' cell.\n", val, ci.name.c_str(ctx));
            ci.params[id_DRIVE] = Property((val - 3) / 3, 2);
        }
        for (auto &p : ci.params) {
            if (p.first.in(id_PULLUP, id_PULLDOWN, id_KEEPER, id_SCHMITT_TRIGGER, id_FF_OBF, id_FF_IBF, id_LVDS_RTERM,
                           id_LVDS_BOOST)) {
                int val = int_or_default(ci.params, p.first, 0);
                if (val != 0 && val != 1)
                    log_error("Unsupported value '%d' for %s parameter of '%s' cell.\n", val, p.first.c_str(ctx),
                              ci.name.c_str(ctx));
                ci.params[p.first] = Property(val, 1);
            }
        }

        // Disconnect PADs
        ci.disconnectPort(id_IO);
        ci.disconnectPort(id_I);
        ci.disconnectPort(id_O);
        ci.disconnectPort(id_IO_P);
        ci.disconnectPort(id_IO_N);
        ci.disconnectPort(id_I_P);
        ci.disconnectPort(id_I_N);
        ci.disconnectPort(id_O_P);
        ci.disconnectPort(id_O_N);

        if (loc.empty() || loc=="UNPLACED") {
            if (uarch->available_pads.empty())
                log_error("No more pads available.\n");
            IdString id = *(uarch->available_pads.begin());
            uarch->available_pads.erase(id);
            loc = id.c_str(ctx);
        }
        ci.params[id_LOC] = Property(loc);

        BelId bel = ctx->get_package_pin_bel(ctx->id(loc));
        if (bel == BelId())
            log_error("Unable to constrain IO '%s', device does not have a pin named '%s'\n", ci.name.c_str(ctx),
                        loc.c_str());
        log_info("    Constraining '%s' to pad '%s'\n", ci.name.c_str(ctx), loc.c_str());
        if (!ctx->checkBelAvail(bel)) {
            log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci),
                        ctx->nameOfBel(bel), ctx->nameOf(ctx->getBoundBelCell(bel)));
        }
        ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_FIXED);
    }
}

void GateMatePacker::pack_io_sel()
{
    std::vector<CellInfo *> cells;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_IBUF, id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF, id_CC_LVDS_IBUF, id_CC_LVDS_OBUF,
                        id_CC_LVDS_TOBUF, id_CC_LVDS_IOBUF))
            continue;

        cells.push_back(&ci);
    }

    CellInfo *ddr[9] = {nullptr}; // for each bank

    auto set_out_clk = [&](CellInfo *cell, CellInfo *target) -> bool {
        NetInfo *clk_net = cell->getPort(id_CLK);
        if (clk_net) {
            if (clk_net->name == ctx->id("$PACKER_GND")) {
                cell->disconnectPort(id_CLK);
            } else if (clk_net->name == ctx->id("$PACKER_VCC")) {
                cell->disconnectPort(id_CLK);
            } else {
                if (!global_signals.count(clk_net)) {
                    cell->movePortTo(id_CLK, target, id_OUT4);
                    target->params[id_SEL_OUT_CLOCK] = Property(Property::State::S1);
                    return true;
                } else {
                    int index = global_signals[clk_net];
                    target->params[id_OUT_CLOCK] = Property(index, 2);
                }
            }
        }
        return false;
    };
    auto set_in_clk = [&](CellInfo *cell, CellInfo *target) {
        NetInfo *clk_net = cell->getPort(id_CLK);
        if (clk_net) {
            if (clk_net->name == ctx->id("$PACKER_GND")) {
                cell->disconnectPort(id_CLK);
            } else if (clk_net->name == ctx->id("$PACKER_VCC")) {
                cell->disconnectPort(id_CLK);
            } else {
                if (!global_signals.count(clk_net)) {
                    cell->movePortTo(id_CLK, target, id_OUT4);
                    target->params[id_SEL_IN_CLOCK] = Property(Property::State::S1);
                } else {
                    int index = global_signals[clk_net];
                    target->params[id_IN_CLOCK] = Property(index, 2);
                }
            }
        }
    };

    auto merge_ibf = [&](NetInfo *di_net, CellInfo &ci, bool use_custom_clock) -> bool {
        CellInfo *dff = (*di_net->users.begin()).cell;
        if (is_gpio_valid_dff(dff)) {
            if (!global_signals.count(ci.getPort(id_CLK)) && use_custom_clock) {
                log_warning("Found DFF %s cell, but not enough CLK signals.\n", dff->name.c_str(ctx));
                return false;
            }   
            // We configure both GPIO IN and let router decide
            ci.params[id_IN1_FF] = Property(Property::State::S1);
            ci.params[id_IN2_FF] = Property(Property::State::S1);
            packed_cells.emplace(dff->name);
            ci.disconnectPort(id_Y);
            dff->movePortTo(id_Q, &ci, id_DI);
            set_in_clk(dff, &ci);
            bool invert = int_or_default(dff->params, id_CLK_INV, 0) == 1;
            if (invert) {
                ci.params[id_INV_IN1_CLOCK] = Property(Property::State::S1);
                ci.params[id_INV_IN2_CLOCK] = Property(Property::State::S1);
            }
            return true;
        } else {
            log_warning("Found DFF %s cell, but it is not valid for merge.\n", dff->name.c_str(ctx));
        }
        return false;
    };

    auto merge_iddr = [&](NetInfo *di_net, CellInfo &ci, bool use_custom_clock) -> bool {
        CellInfo *iddr = (*di_net->users.begin()).cell;
        if (!global_signals.count(ci.getPort(id_CLK)) && use_custom_clock) {
            log_warning("Found IDDR %s cell, but not enough CLK signals.\n", iddr->name.c_str(ctx));
            return false;
        }

        ci.params[id_IN1_FF] = Property(Property::State::S1);
        ci.params[id_IN2_FF] = Property(Property::State::S1);
        packed_cells.emplace(iddr->name);
        ci.disconnectPort(id_Y);

        iddr->movePortTo(id_Q0, &ci, id_IN1);
        iddr->movePortTo(id_Q1, &ci, id_IN2);

        set_in_clk(iddr, &ci);
        bool invert = int_or_default(iddr->params, id_CLK_INV, 0) == 1;
        if (invert) {
            ci.params[id_INV_IN1_CLOCK] = Property(Property::State::S1);
        } else {
            ci.params[id_INV_IN2_CLOCK] = Property(Property::State::S1);
        }
        return false;
    };

    for (auto &cell : cells) {
        CellInfo &ci = *cell;
        bool ff_obf = int_or_default(ci.params, id_FF_OBF, 0) == 1;
        bool ff_ibf = int_or_default(ci.params, id_FF_IBF, 0) == 1;
        ci.unsetParam(id_FF_OBF);
        ci.unsetParam(id_FF_IBF);

        if (ci.getPort(id_T)) {
            ci.params[id_OE_SIGNAL] = Property(0b10, 2);
            ci.renamePort(id_T, id_OUT3);
        }

        ci.cluster = ci.name;
        std::string loc = str_or_default(ci.params, id_LOC, "UNPLACED");
        ci.unsetParam(id_LOC);

        NetInfo *do_net = ci.getPort(id_A);
        bool use_custom_clock = false;
        if (do_net) {
            if (do_net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
                ci.params[id_OUT23_14_SEL] =
                        Property(do_net->name == ctx->id("$PACKER_VCC") ? Property::State::S1 : Property::State::S0);
                ci.disconnectPort(id_A);
            } else {
                ci.params[id_OUT_SIGNAL] = Property(Property::State::S1);
                // ci.params[id_OUT1_4] = Property(Property::State::S1);
                // ci.params[id_OUT2_3] = Property(Property::State::S1);
                // ci.params[id_OUT23_14_SEL] = Property(Property::State::S1);
                bool ff_obf_merged = false;
                if (ff_obf && do_net->driver.cell->type == id_CC_DFF && do_net->users.entries() == 1) {
                    CellInfo *dff = do_net->driver.cell;
                    if (is_gpio_valid_dff(dff)) {
                        ci.params[id_OUT1_FF] = Property(Property::State::S1);
                        packed_cells.emplace(dff->name);
                        ci.disconnectPort(id_A);
                        dff->movePortTo(id_D, &ci, id_OUT1);
                        use_custom_clock = set_out_clk(dff, &ci);
                        bool invert = int_or_default(dff->params, id_CLK_INV, 0) == 1;
                        if (invert) {
                            ci.params[id_INV_OUT1_CLOCK] = Property(Property::State::S1);
                            ci.params[id_INV_OUT2_CLOCK] = Property(Property::State::S1);
                        }
                        ff_obf_merged = true;
                    } else {
                        log_warning("Found DFF %s cell, but it is not valid for merge.\n", dff->name.c_str(ctx));
                    }
                }
                bool oddr_merged = false;
                if (do_net->driver.cell->type == id_CC_ODDR && do_net->users.entries() == 1) {
                    CellInfo *oddr = do_net->driver.cell;
                    ci.params[id_OUT1_FF] = Property(Property::State::S1);
                    ci.params[id_OUT2_FF] = Property(Property::State::S1);
                    ci.params[id_USE_DDR] = Property(Property::State::S1);
                    packed_cells.emplace(oddr->name);
                    ci.disconnectPort(id_A);
                    // TODO: check mapping
                    oddr->movePortTo(id_D0, &ci, id_OUT2);
                    oddr->movePortTo(id_D1, &ci, id_OUT1);
                    const auto &pad = ctx->get_package_pin(ctx->id(loc));
                    CellInfo *cpe_half = ddr[pad->pad_bank];
                    if (cpe_half) {
                        if (cpe_half->getPort(id_IN1) != oddr->getPort(id_DDR))
                            log_error("DDR port use signal different than already occupied DDR source.\n");
                        ci.ports[id_DDR].name = id_DDR;
                        ci.ports[id_DDR].type = PORT_IN;
                        ci.connectPort(id_DDR, cpe_half->getPort(id_RAM_O));
                    } else {
                        oddr->movePortTo(id_DDR, &ci, id_DDR);
                        cpe_half = move_ram_o(&ci, id_DDR, NO_PLACE);
                        ctx->bindBel(get_bank_cpe(pad->pad_bank), cpe_half, PlaceStrength::STRENGTH_FIXED);
                        ddr[pad->pad_bank] = cpe_half;
                    }
                    use_custom_clock = set_out_clk(oddr, &ci);
                    bool invert = int_or_default(oddr->params, id_CLK_INV, 0) == 1;
                    if (invert) {
                        ci.params[id_INV_OUT1_CLOCK] = Property(Property::State::S1);
                    } else {
                        ci.params[id_INV_OUT2_CLOCK] = Property(Property::State::S1);
                    }
                    oddr_merged = true;
                }
                if (!ff_obf_merged && !oddr_merged)
                    ci.renamePort(id_A, id_OUT1);
            }
        }
        NetInfo *di_net = ci.getPort(id_Y);
        if (di_net) {
            bool ff_ibf_merged = false;
            if (ff_ibf && di_net->users.entries() == 1 && (*di_net->users.begin()).cell->type == id_CC_DFF) {
                ff_ibf_merged = merge_ibf(di_net, ci, use_custom_clock);
            }
            bool iddr_merged = false;
            if (di_net->users.entries() == 1 && (*di_net->users.begin()).cell->type == id_CC_IDDR) {
                iddr_merged = merge_iddr(di_net, ci, use_custom_clock);
            }

            if (!ff_ibf_merged && !iddr_merged)
                ci.renamePort(id_Y, id_DI);
        }

        CellInfo *cpe_out[4];
        for (int i = 0; i < 4; i++)
            cpe_out[i] = move_ram_o(&ci, ctx->idf("OUT%d", i + 1), PLACE_GPIO_CPE_OUT1 + i);

        for (int i = 0; i < 4; i++) {
            if (cpe_out[i]) {
                BelId b = ctx->getBelByLocation(uarch->getGPIOOutCPE(ci.bel, i));
                ctx->bindBel(b, cpe_out[i], PlaceStrength::STRENGTH_FIXED);
            }
        }
    }
    flush_cells();
}

bool GateMatePacker::is_gpio_valid_dff(CellInfo *dff)
{
    NetInfo *en_net = dff->getPort(id_EN);
    bool invert = int_or_default(dff->params, id_EN_INV, 0) == 1;
    if (en_net) {
        if (en_net->name == ctx->id("$PACKER_GND")) {
            if (!invert)
                return false;
            dff->disconnectPort(id_EN);
        } else if (en_net->name == ctx->id("$PACKER_VCC")) {
            if (invert)
                return false;
            dff->disconnectPort(id_EN);
        } else {
            return false;
        }
    }
    dff->unsetParam(id_EN_INV);

    NetInfo *sr_net = dff->getPort(id_SR);
    invert = int_or_default(dff->params, id_SR_INV, 0) == 1;
    if (sr_net) {
        if (sr_net->name == ctx->id("$PACKER_GND")) {
            if (invert)
                log_error("Invalid DFF configuration\n.");
            dff->disconnectPort(id_SR);
        } else if (sr_net->name == ctx->id("$PACKER_VCC")) {
            if (!invert)
                log_error("Invalid DFF configuration\n.");
            dff->disconnectPort(id_SR);
        } else {
            return false;
        }
    }
    dff->unsetParam(id_SR_VAL);
    dff->unsetParam(id_SR_INV);

    // Sanity check for CLK signal, that it must exist
    NetInfo *clk_net = dff->getPort(id_CLK);
    if (clk_net) {
        if (clk_net->name == ctx->id("$PACKER_GND")) {
            return false;
        } else if (clk_net->name == ctx->id("$PACKER_VCC")) {
            return false;
        }
    } else {
        return false;
    }

    return true;
}

void GateMatePacker::dff_to_cpe(CellInfo *dff, CellInfo *cpe)
{
    bool invert;
    bool is_latch = dff->type == id_CC_DLT;
    if (is_latch) {
        NetInfo *g_net = cpe->getPort(id_G);
        invert = int_or_default(dff->params, id_G_INV, 0) == 1;
        if (g_net) {
            if (g_net->name == ctx->id("$PACKER_GND")) {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
                cpe->disconnectPort(id_G);
            } else if (g_net->name == ctx->id("$PACKER_VCC")) {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b00 : 0b11, 2);
                cpe->disconnectPort(id_G);
            } else {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b01 : 0b10, 2);
            }
        } else {
            cpe->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
        }
        dff->unsetParam(id_G_INV);
        cpe->renamePort(id_G, id_CLK);

        cpe->params[id_C_CPE_EN] = Property(0b11, 2);
        cpe->params[id_C_L_D] = Property(0b1, 1);
    } else {
        NetInfo *en_net = cpe->getPort(id_EN);
        bool invert = int_or_default(dff->params, id_EN_INV, 0) == 1;
        if (en_net) {
            if (en_net->name == ctx->id("$PACKER_GND")) {
                cpe->params[id_C_CPE_EN] = Property(invert ? 0b11 : 0b00, 2);
                cpe->disconnectPort(id_EN);
            } else if (en_net->name == ctx->id("$PACKER_VCC")) {
                cpe->params[id_C_CPE_EN] = Property(invert ? 0b00 : 0b11, 2);
                cpe->disconnectPort(id_EN);
            } else {
                cpe->params[id_C_CPE_EN] = Property(invert ? 0b01 : 0b10, 2);
            }
        } else {
            cpe->params[id_C_CPE_EN] = Property(invert ? 0b11 : 0b00, 2);
        }
        dff->unsetParam(id_EN_INV);

        NetInfo *clk_net = cpe->getPort(id_CLK);
        invert = int_or_default(dff->params, id_CLK_INV, 0) == 1;
        if (clk_net) {
            if (clk_net->name == ctx->id("$PACKER_GND")) {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
                cpe->disconnectPort(id_CLK);
            } else if (clk_net->name == ctx->id("$PACKER_VCC")) {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b00 : 0b11, 2);
                cpe->disconnectPort(id_CLK);
            } else {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b01 : 0b10, 2);
            }
        } else {
            cpe->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
        }
        dff->unsetParam(id_CLK_INV);
    }

    NetInfo *sr_net = cpe->getPort(id_SR);
    invert = int_or_default(dff->params, id_SR_INV, 0) == 1;
    int sr_val = int_or_default(dff->params, id_SR_VAL, 0) == 1;
    if (sr_net) {
        if (sr_net->name == ctx->id("$PACKER_GND")) {
            if (invert)
                log_error("Invalid DFF configuration\n.");
            cpe->params[id_C_CPE_RES] = Property(0b11, 2);
            cpe->params[id_C_CPE_SET] = Property(0b11, 2);
            cpe->disconnectPort(id_SR);
        } else if (sr_net->name == ctx->id("$PACKER_VCC")) {
            if (!invert)
                log_error("Invalid DFF configuration\n.");
            cpe->params[id_C_CPE_RES] = Property(0b11, 2);
            cpe->params[id_C_CPE_SET] = Property(0b11, 2);
            cpe->disconnectPort(id_SR);
        } else {
            if (sr_val) {
                cpe->params[id_C_CPE_RES] = Property(0b11, 2);
                cpe->params[id_C_CPE_SET] = Property(invert ? 0b01 : 0b10, 2);
                cpe->params[id_C_EN_SR] = Property(0b1, 1);
            } else {
                cpe->params[id_C_CPE_RES] = Property(invert ? 0b01 : 0b10, 2);
                cpe->params[id_C_CPE_SET] = Property(0b11, 2);
            }
        }
    } else {
        if (invert)
            log_error("Invalid DFF configuration\n.");
        cpe->params[id_C_CPE_RES] = Property(0b11, 2);
        cpe->params[id_C_CPE_SET] = Property(0b11, 2);
    }
    dff->unsetParam(id_SR_VAL);
    dff->unsetParam(id_SR_INV);

    if (dff->params.count(id_INIT) && dff->params[id_INIT].is_fully_def()) {
        bool init = int_or_default(dff->params, id_INIT, 0) == 1;
        if (init)
            cpe->params[id_FF_INIT] = Property(0b11, 2);
        else
            cpe->params[id_FF_INIT] = Property(0b10, 2);
        dff->unsetParam(id_INIT);
    } else {
        dff->unsetParam(id_INIT);
    }
    cpe->timing_index = ctx->get_cell_timing_idx(id_CPE_DFF);
    cpe->params[id_C_O] = Property(0b00, 2);
}

void GateMatePacker::pack_cpe()
{
    log_info("Packing CPEs..\n");
    std::vector<CellInfo *> l2t5_list;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_L2T4, id_CC_L2T5, id_CC_LUT2, id_CC_LUT1, id_CC_MX2))
            continue;
        if (ci.type == id_CC_L2T5) {
            l2t5_list.push_back(&ci);
            ci.renamePort(id_I0, id_IN1);
            ci.renamePort(id_I1, id_IN2);
            ci.renamePort(id_I2, id_IN3);
            ci.renamePort(id_I3, id_IN4);

            ci.renamePort(id_O, id_OUT);

            ci.params[id_C_O] = Property(0b11, 2);
            ci.type = id_CPE_HALF_L;
        } else if (ci.type == id_CC_MX2) {
            ci.params[id_C_O] = Property(0b11, 2);
            ci.renamePort(id_D1, id_IN1);
            NetInfo *sel = ci.getPort(id_S0);
            ci.renamePort(id_S0, id_IN2);
            ci.ports[id_IN3].name = id_IN3;
            ci.ports[id_IN3].type = PORT_IN;
            ci.connectPort(id_IN3, sel);
            ci.renamePort(id_D0, id_IN4);
            ci.disconnectPort(id_D1);
            ci.params[id_INIT_L00] = Property(0b1000, 4); // AND
            ci.params[id_INIT_L01] = Property(0b0100, 4); // AND inv D0
            ci.params[id_INIT_L10] = Property(0b1110, 4); // OR
            ci.renamePort(id_Y, id_OUT);
            ci.type = id_CPE_HALF;
        } else {
            ci.renamePort(id_I0, id_IN1);
            ci.renamePort(id_I1, id_IN2);
            ci.renamePort(id_I2, id_IN3);
            ci.renamePort(id_I3, id_IN4);
            ci.renamePort(id_O, id_OUT);
            ci.params[id_C_O] = Property(0b11, 2);
            if (ci.type.in(id_CC_LUT1, id_CC_LUT2)) {
                uint8_t val = int_or_default(ci.params, id_INIT, 0);
                if (ci.type == id_CC_LUT1)
                    val = val << 2 | val;
                ci.params[id_INIT_L00] = Property(val, 4);
                ci.unsetParam(id_INIT);
                ci.params[id_INIT_L10] = Property(0b1010, 4);
            }
            ci.type = id_CPE_HALF;
        }
        NetInfo *o = ci.getPort(id_OUT);
        if (o) {
            CellInfo *dff = net_only_drives(ctx, o, is_dff, id_D, true);
            if (dff) {
                if (dff->type == id_CC_DLT) {
                    dff->movePortTo(id_G, &ci, id_G);
                } else {
                    dff->movePortTo(id_EN, &ci, id_EN);
                    dff->movePortTo(id_CLK, &ci, id_CLK);
                }
                dff->movePortTo(id_SR, &ci, id_SR);
                dff->disconnectPort(id_D);
                ci.disconnectPort(id_OUT);
                dff->movePortTo(id_Q, &ci, id_OUT);
                dff_to_cpe(dff, &ci);
                packed_cells.insert(dff->name);
            }
        }
    }

    for (auto ci : l2t5_list) {
        CellInfo *upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$upper", ci->name.c_str(ctx)));
        upper->cluster = ci->name;
        upper->constr_abs_z = false;
        upper->constr_z = -1;
        ci->cluster = ci->name;
        ci->movePortTo(id_I4, upper, id_IN1);
        upper->params[id_INIT_L00] = Property(0b1010, 4);
        upper->params[id_INIT_L10] = Property(0b1010, 4);
        ci->constr_children.push_back(upper);
    }
    l2t5_list.clear();

    flush_cells();

    std::vector<CellInfo *> mux_list;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_MX4))
            continue;
        mux_list.push_back(&ci);
    }
    for (auto &cell : mux_list) {
        CellInfo &ci = *cell;
        ci.cluster = ci.name;
        ci.renamePort(id_Y, id_OUT);

        ci.renamePort(id_S0, id_IN2); // IN6
        ci.renamePort(id_S1, id_IN4); // IN8

        uint8_t select = 0;
        uint8_t invert = 0;
        for (int i = 0; i < 4; i++) {
            NetInfo *net = ci.getPort(ctx->idf("D%d", i));
            if (net) {
                if (net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
                    if (net->name == ctx->id("$PACKER_VCC"))
                        invert |= 1 << i;
                    ci.disconnectPort(ctx->idf("D%d", i));
                } else {
                    select |= 1 << i;
                }
            }
        }
        ci.params[id_C_FUNCTION] = Property(C_MX4, 3);
        ci.params[id_INIT_L02] = Property(0b1100, 4); // IN6
        ci.params[id_INIT_L03] = Property(0b1100, 4); // IN8
        ci.params[id_INIT_L11] = Property(invert, 4); // Inversion bits
        // ci.params[id_INIT_L20] = Property(0b1100, 4); // Always D1
        ci.params[id_C_O] = Property(0b11, 2);
        ci.type = id_CPE_HALF_L;

        CellInfo *upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$upper", ci.name.c_str(ctx)));
        upper->cluster = ci.name;
        upper->constr_abs_z = false;
        upper->constr_z = -1;
        upper->params[id_INIT_L10] = Property(select, 4); // Selection bits
        upper->params[id_C_FUNCTION] = Property(C_MX4, 3);

        ci.movePortTo(id_D0, upper, id_IN1);
        ci.movePortTo(id_D1, upper, id_IN2);
        ci.movePortTo(id_D2, upper, id_IN3);
        ci.movePortTo(id_D3, upper, id_IN4);
        ci.constr_children.push_back(upper);
    }
    mux_list.clear();

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_DFF, id_CC_DLT))
            continue;
        ci.renamePort(id_Q, id_OUT);
        NetInfo *d_net = ci.getPort(id_D);
        if (d_net->name == ctx->id("$PACKER_GND")) {
            ci.params[id_INIT_L00] = Property(0b0000, 4);
            ci.disconnectPort(id_D);
        } else if (d_net->name == ctx->id("$PACKER_VCC")) {
            ci.params[id_INIT_L00] = Property(0b1111, 4);
            ci.disconnectPort(id_D);
        } else {
            ci.params[id_INIT_L00] = Property(0b1010, 4);
        }
        ci.params[id_INIT_L10] = Property(0b1010, 4);
        ci.renamePort(id_D, id_IN1);
        dff_to_cpe(&ci, &ci);
        ci.type = id_CPE_HALF;
    }
}

template <typename T>
std::vector<std::vector<T>> splitNestedVector(const std::vector<std::vector<T>> &input, size_t maxSize = 8)
{
    std::vector<std::vector<T>> result;

    for (const auto &inner : input) {
        size_t i = 0;
        while (i < inner.size()) {
            size_t end = std::min(i + maxSize, inner.size());
            result.emplace_back(inner.begin() + i, inner.begin() + end);
            i = end;
        }
    }

    return result;
}

void GateMatePacker::pack_addf()
{
    log_info("Packing ADDFs..\n");

    std::vector<CellInfo *> root_cys;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type != id_CC_ADDF)
            continue;
        NetInfo *ci_net = ci->getPort(id_CI);

        if (!ci_net || !ci_net->driver.cell || ci_net->driver.cell->type != id_CC_ADDF) {
            root_cys.push_back(ci);
        }
    }
    std::vector<std::vector<CellInfo *>> groups;
    for (auto root : root_cys) {
        std::vector<CellInfo *> group;
        CellInfo *cy = root;
        group.push_back(cy);
        while (true) {
            NetInfo *co_net = cy->getPort(id_CO);
            if (co_net && co_net->users.entries() == 1) {
                cy = (*co_net->users.begin()).cell;
                if (cy->type == id_CC_ADDF)
                    group.push_back(cy);
                else
                    break;
            } else
                break;
        }
        groups.push_back(group);
    }

    for (auto &grp : splitNestedVector(groups)) {
        CellInfo *root = grp.front();
        root->cluster = root->name;

        CellInfo *ci_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$ci_upper", root->name.c_str(ctx)));
        root->constr_children.push_back(ci_upper);
        ci_upper->cluster = root->name;
        ci_upper->constr_abs_z = false;
        ci_upper->constr_z = -1;
        ci_upper->constr_y = -1;

        CellInfo *ci_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$ci_lower", root->name.c_str(ctx)));
        root->constr_children.push_back(ci_lower);
        ci_lower->cluster = root->name;
        ci_lower->constr_abs_z = false;
        ci_lower->constr_y = -1;
        ci_lower->params[id_C_O] = Property(0b11, 2);
        ci_lower->params[id_C_SELY1] = Property(1, 1);
        ci_lower->params[id_C_CY1_I] = Property(1, 1);
        ci_lower->params[id_INIT_L10] = Property(0b1010, 4); // D0

        NetInfo *ci_net = root->getPort(id_CI);
        if (ci_net->name == ctx->id("$PACKER_GND")) {
            ci_lower->params[id_INIT_L00] = Property(0b0000, 4);
            root->disconnectPort(id_CI);
        } else if (ci_net->name == ctx->id("$PACKER_VCC")) {
            ci_lower->params[id_INIT_L00] = Property(0b1111, 4);
            root->disconnectPort(id_CI);
        } else {
            root->movePortTo(id_CI, ci_lower, id_IN1);
            ci_lower->params[id_INIT_L00] = Property(0b1010, 4); // IN5
        }

        NetInfo *ci_conn = ctx->createNet(ctx->idf("%s$ci", root->name.c_str(ctx)));
        ci_lower->connectPort(id_COUTY1, ci_conn);

        root->ports[id_CINY1].name = id_CINY1;
        root->ports[id_CINY1].type = PORT_IN;
        root->connectPort(id_CINY1, ci_conn);

        for (size_t i = 0; i < grp.size(); i++) {
            CellInfo *cy = grp.at(i);
            if (i != 0) {
                cy->cluster = root->name;
                root->constr_children.push_back(cy);
                cy->constr_abs_z = false;
                cy->constr_y = +i;
                cy->renamePort(id_CI, id_CINY1);
            }

            cy->params[id_C_FUNCTION] = Property(C_ADDF, 3);
            cy->params[id_INIT_L02] = Property(0b0000, 4); // 0
            cy->params[id_INIT_L03] = Property(0b0000, 4); // 0
            cy->params[id_INIT_L11] = Property(0b0110, 4); // XOR
            cy->params[id_INIT_L20] = Property(0b0110, 4); // XOR
            cy->params[id_C_O] = Property(0b11, 2);
            cy->type = id_CPE_HALF_L;
            cy->renamePort(id_S, id_OUT);

            CellInfo *upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$upper", cy->name.c_str(ctx)));
            upper->cluster = root->name;
            root->constr_children.push_back(upper);
            upper->constr_abs_z = false;
            upper->constr_y = +i;
            upper->constr_z = -1;

            NetInfo *a_net = cy->getPort(id_A);
            if (a_net->name == ctx->id("$PACKER_GND")) {
                upper->params[id_INIT_L00] = Property(0b0000, 4);
                cy->disconnectPort(id_A);
            } else if (a_net->name == ctx->id("$PACKER_VCC")) {
                upper->params[id_INIT_L00] = Property(0b1111, 4);
                cy->disconnectPort(id_A);
            } else {
                cy->movePortTo(id_A, upper, id_IN1);
                upper->params[id_INIT_L00] = Property(0b1010, 4); // IN1
            }
            NetInfo *b_net = cy->getPort(id_B);
            if (b_net->name == ctx->id("$PACKER_GND")) {
                upper->params[id_INIT_L01] = Property(0b0000, 4);
                cy->disconnectPort(id_B);
            } else if (b_net->name == ctx->id("$PACKER_VCC")) {
                upper->params[id_INIT_L01] = Property(0b1111, 4);
                cy->disconnectPort(id_B);
            } else {
                cy->movePortTo(id_B, upper, id_IN3);
                upper->params[id_INIT_L01] = Property(0b1010, 4); // IN3
            }

            upper->params[id_INIT_L10] = Property(0b0110, 4); // XOR
            upper->params[id_C_FUNCTION] = Property(C_ADDF, 3);

            if (i == grp.size() - 1) {
                CellInfo *co_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$co_upper", cy->name.c_str(ctx)));
                co_upper->cluster = root->name;
                root->constr_children.push_back(co_upper);
                co_upper->constr_abs_z = false;
                co_upper->constr_z = -1;
                co_upper->constr_y = +i + 1;
                CellInfo *co_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$co_lower", cy->name.c_str(ctx)));
                co_lower->cluster = root->name;
                root->constr_children.push_back(co_lower);
                co_lower->constr_abs_z = false;
                co_lower->constr_y = +i + 1;
                co_lower->params[id_C_O] = Property(0b11, 2);
                co_lower->params[id_C_FUNCTION] = Property(C_EN_CIN, 3);
                co_lower->params[id_INIT_L11] = Property(0b1100, 4);
                co_lower->params[id_INIT_L20] = Property(0b1100, 4);

                NetInfo *co_conn = ctx->createNet(ctx->idf("%s$co", cy->name.c_str(ctx)));

                co_lower->ports[id_CINY1].name = id_CINY1;
                co_lower->ports[id_CINY1].type = PORT_IN;
                co_lower->connectPort(id_CINY1, co_conn);
                cy->ports[id_COUTY1].name = id_COUTY1;
                cy->ports[id_COUTY1].type = PORT_OUT;
                cy->connectPort(id_COUTY1, co_conn);

                cy->movePortTo(id_CO, co_lower, id_OUT);
            } else {
                cy->renamePort(id_CO, id_COUTY1);
            }
        }
    }
}

void GateMatePacker::sort_bufg()
{
    struct ItemBufG
    {
        CellInfo *cell;
        int32_t fan_out;
        ItemBufG(CellInfo *cell, int32_t fan_out) : cell(cell), fan_out(fan_out) {}
    };

    std::vector<ItemBufG> bufg;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_BUFG))
            continue;

        NetInfo *i_net = ci.getPort(id_I);
        if (!i_net) {
            log_warning("Removing BUFG cell %s since there is no input used.\n", ci.name.c_str(ctx));
            packed_cells.emplace(ci.name); // Remove if no input
            continue;
        }
        NetInfo *o_net = ci.getPort(id_O);
        if (!o_net) {
            log_warning("Removing BUFG cell %s since there is no output used.\n", ci.name.c_str(ctx));
            packed_cells.emplace(ci.name); // Remove if no output
            continue;
        }
        bufg.push_back(ItemBufG(&ci, o_net->users.entries()));
    }

    if (bufg.size() > 4) {
        log_warning("More than 4 BUFG used. Those with highest fan-out will be used.\n");
        std::sort(bufg.begin(), bufg.end(), [](const ItemBufG &a, const ItemBufG &b) { return a.fan_out > b.fan_out; });
        for (size_t i = 4; i < bufg.size(); i++) {
            log_warning("Removing BUFG cell %s.\n", bufg.at(i).cell->name.c_str(ctx));
            CellInfo *cell = bufg.at(i).cell;
            NetInfo *i_net = cell->getPort(id_I);
            NetInfo *o_net = cell->getPort(id_O);
            for(auto s : o_net->users) {
                s.cell->disconnectPort(s.port);
                s.cell->connectPort(s.port, i_net);
            }
            packed_cells.emplace(bufg.at(i).cell->name);
        }
    }
    flush_cells();
}

void GateMatePacker::pack_bufg()
{
    log_info("Packing BUFGs..\n");
    CellInfo *bufg[4] = { nullptr };
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_BUFG))
            continue;

        NetInfo *in_net = ci.getPort(id_I);
        if (in_net) {
            bool is_cpe_source = true;
            if (ctx->getBelBucketForCellType(in_net->driver.cell->type) == id_GPIO) {
                auto pad_info = uarch->bel_to_pad[in_net->driver.cell->bel];
                if (pad_info->flags)
                    is_cpe_source = false;
            }
            if (ctx->getBelBucketForCellType(in_net->driver.cell->type) == id_PLL) {
                is_cpe_source = false;
                int pll_index = in_net->driver.cell->constr_z - 4;
                if (bufg[pll_index]== nullptr) {
                    bufg[pll_index] = &ci;
                } else {
                    IdString origPort = in_net->driver.port;
                    int index = 0;
                    if (origPort==id_CLK90) index = 1;
                    else if (origPort==id_CLK180) index = 2;
                    else if (origPort==id_CLK270) index = 3;
                    if (bufg[index]== nullptr) {
                        bufg[pll_index] = &ci;
                    } else {
                        log_error("Unable to place BUFG for PLL.\n");
                    }
                }
            }
            if (is_cpe_source) {
                ci.cluster = ci.name;
                move_ram_o(&ci, id_I, PLACE_USR_GLB);
            }
        }
        ci.type = id_BUFG;
    }

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_BUFG))
            continue;
        NetInfo *in_net = ci.getPort(id_I);
        if (in_net && ctx->getBelBucketForCellType(in_net->driver.cell->type) != id_PLL) {
            for(int i=0;i<4; i++) {
                if (bufg[i]==nullptr) {
                    bufg[i] = &ci;
                    break;
                }
            }
        }
    }

    for(int i=0;i<4; i++) {
        if (bufg[i]) {
            CellInfo &ci = *bufg[i];
            global_signals.emplace(ci.getPort(id_O), i);
            ci.cluster = ci.name;
            ci.constr_abs_z = true;
            ci.constr_z = i;
        }
    }
}

template <typename KeyType>
double double_or_default(const dict<KeyType, Property> &ct, const KeyType &key, double def = 0)
{
    auto found = ct.find(key);
    if (found == ct.end())
        return def;
    else {
        if (found->second.is_string) {
            try {
                return std::stod(found->second.as_string());
            } catch (std::invalid_argument &e) {
                log_error("Expecting numeric value but got '%s'.\n", found->second.as_string().c_str());
            }
        } else
            return double(found->second.as_int64());
    }
};

CellInfo *GateMatePacker::move_ram_i(CellInfo *cell, IdString origPort, int placement)
{
    CellInfo *cpe_half = nullptr;
    NetInfo *net = cell->getPort(origPort);
    if (net) {
        cpe_half = create_cell_ptr(id_CPE_HALF, ctx->idf("%s$%s_cpe_half", cell->name.c_str(ctx), origPort.c_str(ctx)));
        if (placement != NO_PLACE) {
            cell->constr_children.push_back(cpe_half);
            cpe_half->cluster = cell->name;
            cpe_half->constr_abs_z = false;
            cpe_half->constr_z = placement;
        }
        cpe_half->params[id_C_RAM_I] = Property(1, 1);

        NetInfo *ram_i = ctx->createNet(ctx->idf("%s$ram_i", net->name.c_str(ctx)));
        cell->movePortTo(origPort, cpe_half, id_OUT);
        cell->connectPort(origPort, ram_i);
        cpe_half->connectPort(id_RAM_I, ram_i);
    }
    return cpe_half;
}

CellInfo *GateMatePacker::move_ram_o(CellInfo *cell, IdString origPort, int placement)
{
    CellInfo *cpe_half = nullptr;
    NetInfo *net = cell->getPort(origPort);
    if (net) {
        cpe_half = create_cell_ptr(id_CPE_HALF, ctx->idf("%s$%s_cpe_half", cell->name.c_str(ctx), origPort.c_str(ctx)));
        if (placement != NO_PLACE) {
            cell->constr_children.push_back(cpe_half);
            cpe_half->cluster = cell->name;
            cpe_half->constr_abs_z = false;
            cpe_half->constr_z = placement;
        }
        cpe_half->params[id_INIT_L00] = Property(0b1010, 4);
        cpe_half->params[id_INIT_L10] = Property(0b1010, 4);
        cpe_half->params[id_C_O] = Property(0b11, 2);
        cpe_half->params[id_C_RAM_O] = Property(1, 1);

        NetInfo *ram_o = ctx->createNet(ctx->idf("%s$ram_o", cpe_half->name.c_str(ctx)));
        cell->movePortTo(origPort, cpe_half, id_IN1);
        cell->connectPort(origPort, ram_o);
        cpe_half->connectPort(id_RAM_O, ram_o);
    }
    return cpe_half;
}

void GateMatePacker::pll_out(CellInfo *cell, IdString origPort, int placement)
{
    NetInfo *net = cell->getPort(origPort);
    if (!net)
        return;
    CellInfo *bufg = nullptr;
    for (auto &usr : net->users) {
        if (usr.cell->type == id_CC_BUFG)
            bufg = usr.cell;
    }
    if (bufg) {
        if (net->users.entries() != 1) {
            log_error("not handled BUFG\n");
        }
    } else {
        move_ram_i(cell, origPort, placement);
    }
}

template <typename KeyType>
int extract_bits(const dict<KeyType, Property> &ct, const KeyType &key, int start, int bits, int def = 0)
{
    Property extr = get_or_default(ct, key, Property()).extract(start, bits);

    if (extr.is_string) {
        try {
            return std::stoi(extr.as_string());
        } catch (std::invalid_argument &e) {
            log_error("Expecting numeric value but got '%s'.\n", extr.as_string().c_str());
        }
    } else
        return extr.as_int64();
}


void GateMatePacker::insert_bufg(CellInfo *cell, IdString port)
{
    NetInfo *clk = cell->getPort(port);
    if (clk) {
        if (!(clk->users.entries()==1 && (*clk->users.begin()).cell->type == id_CC_BUFG)) {
            CellInfo *bufg = create_cell_ptr(id_CC_BUFG, ctx->idf("%s$BUFG_%s", cell->name.c_str(ctx), port.c_str(ctx)));
            cell->movePortTo(port, bufg, id_O);
            cell->ports[port].name = port;
            cell->ports[port].type = PORT_OUT;
            NetInfo *net = ctx->createNet(ctx->idf("%s", bufg->name.c_str(ctx)));
            cell->connectPort(port, net);
            bufg->connectPort(id_I, net);
            log_info("Added BUFG for cell '%s' signal %s\n", cell->name.c_str(ctx), port.c_str(ctx));
        }
    }
}

void GateMatePacker::insert_pll_bufg()
{
    std::vector<CellInfo *> cells;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_PLL, id_CC_PLL_ADV))
            continue;
        cells.push_back(&ci);
    }
    for (auto &cell : cells) {
        insert_bufg(cell, id_CLK0);
        insert_bufg(cell, id_CLK90);
        insert_bufg(cell, id_CLK180);
        insert_bufg(cell, id_CLK270);
    }
}

void GateMatePacker::pack_pll()
{
    int pll_index = 0;
    log_info("Packing PLLss..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_PLL, id_CC_PLL_ADV))
            continue;

        disconnect_if_gnd(&ci, id_CLK_REF);
        disconnect_if_gnd(&ci, id_USR_CLK_REF);
        disconnect_if_gnd(&ci, id_CLK_FEEDBACK);
        disconnect_if_gnd(&ci, id_USR_LOCKED_STDY_RST);

        ci.cluster = ci.name;
        ci.constr_abs_z = true;
        ci.constr_z = 4 + pll_index; // Position to a proper Z location

        if (pll_index > 4)
            log_error("Used more than available PLLs.\n");

        if (ci.getPort(id_CLK_REF) == nullptr && ci.getPort(id_USR_CLK_REF) == nullptr)
            log_error("At least one reference clock (CLK_REF or USR_CLK_REF) must be set.\n");

        if (ci.getPort(id_CLK_REF) != nullptr && ci.getPort(id_USR_CLK_REF) != nullptr)
            log_error("CLK_REF and USR_CLK_REF are not allowed to be set in same time.\n");

        NetInfo *clk = ci.getPort(id_CLK_REF);
        if (clk) {
            if (ctx->getBelBucketForCellType(clk->driver.cell->type) != id_GPIO)
                log_error("CLK_REF must be driven with GPIO pin.\n");
            auto pad_info = uarch->bel_to_pad[clk->driver.cell->bel];
            if (!(pad_info->flags & 1))
                log_error("CLK_REF must be driven with CLK dedicated pin.\n");
        }

        clk = ci.getPort(id_USR_CLK_REF);
        if (clk) {
            move_ram_o(&ci, id_USR_CLK_REF, PLACE_USR_CLK_REF);
        }
        // TODO: handle CLK_FEEDBACK
        // TODO: handle CLK_REF_OUT
        pll_out(&ci, id_CLK0, PLACE_CPE_CLK0_OUT);
        pll_out(&ci, id_CLK90, PLACE_CPE_CLK90_OUT);
        pll_out(&ci, id_CLK180, PLACE_CPE_CLK180_OUT);
        pll_out(&ci, id_CLK270, PLACE_CPE_CLK270_OUT);

        move_ram_i(&ci, id_USR_PLL_LOCKED, PLACE_USR_PLL_LOCKED);
        move_ram_i(&ci, id_USR_PLL_LOCKED_STDY, PLACE_USR_PLL_LOCKED_STDY);

        if (ci.type == id_CC_PLL) {
            int low_jitter = int_or_default(ci.params, id_LOW_JITTER, 0);
            int ci_const = int_or_default(ci.params, id_CI_FILTER_CONST, 0);
            int cp_const = int_or_default(ci.params, id_CP_FILTER_CONST, 0);
            int clk270_doub = int_or_default(ci.params, id_CLK270_DOUB, 0);
            int clk180_doub = int_or_default(ci.params, id_CLK180_DOUB, 0);
            int lock_req = int_or_default(ci.params, id_LOCK_REQ, 0);

            if (!ci.getPort(id_CLK_FEEDBACK))
                ci.params[id_LOCK_REQ] = Property(lock_req, 1);
            ci.params[id_CLK180_DOUB] = Property(clk180_doub, 1);
            ci.params[id_CLK270_DOUB] = Property(clk270_doub, 1);
            std::string mode = str_or_default(ci.params, id_PERF_MD, "SPEED");
            boost::algorithm::to_upper(mode);
            int perf_md;
            double max_freq = 0.0;
            if (mode == "LOWPOWER") {
                perf_md = 1;
                max_freq = 250.00;
            } else if (mode == "ECONOMY") {
                perf_md = 2;
                max_freq = 312.50;
            } else if (mode == "SPEED") {
                perf_md = 3;
                max_freq = 416.75;
            } else {
                log_error("Unknown PERF_MD parameter value '%s' for cell %s.\n", mode.c_str(), ci.name.c_str(ctx));
            }

            double ref_clk = double_or_default(ci.params, id_REF_CLK, 0.0);
            if (ref_clk <= 0 || ref_clk > 125)
                log_error("REF_CLK parameter is out of range (0,125.00].\n");

            double out_clk = double_or_default(ci.params, id_OUT_CLK, 0.0);
            if (out_clk <= 0 || out_clk > max_freq)
                log_error("OUT_CLK parameter is out of range (0,%.2lf].\n", max_freq);

            if ((ci_const < 1) || (ci_const > 31)) {
                log_warning("CI const out of range. Set to default CI = 2\n");
                ci_const = 2;
            }
            if ((cp_const < 1) || (cp_const > 31)) {
                log_warning("CP const out of range. Set to default CP = 4\n");
                cp_const = 4;
            }
            // PLL_cfg_val_800_1400  PLL values from 11.08.2021
            // ci.params[ctx->id("CFG_A.CI_FILTER_CONST")] = Property(0b00010,5);
            // ci.params[ctx->id("CFG_A.CP_FILTER_CONST")] = Property(0b00100,5);
            // ci.params[ctx->id("CFG_A.N1")] = Property(0b001000,6);
            // ci.params[ctx->id("CFG_A.N2")] = Property(0b0001100100,10);
            // ci.params[ctx->id("CFG_A.M1")] = Property(0b001000,6);
            // ci.params[ctx->id("CFG_A.M2")] = Property(0b0001100100,10);
            // ci.params[ctx->id("CFG_A.K")] = Property(0b000000000001,12);
            // ci.params[ctx->id("CFG_A.PDIV1_SEL")] = Property(0b1,1);
            bool feedback = false;
            if (ci.getPort(id_CLK_FEEDBACK)) {
                ci.params[ctx->id("CFG_A.FB_PATH")] = Property(0b1, 1);
                feedback = true;
            }
            ci.params[ctx->id("CFG_A.FINE_TUNE")] = Property(0b00011001000, 11);
            ci.params[ctx->id("CFG_A.COARSE_TUNE")] = Property(0b100, 3);
            ci.params[ctx->id("CFG_A.AO_SW")] = Property(0b01000, 5);
            // ci.params[ctx->id("CFG_A.OPEN_LOOP")] = Property(0b0,1);
            // ci.params[ctx->id("CFG_A.ENFORCE_LOCK")] = Property(0b0,1);
            // ci.params[ctx->id("CFG_A.PFD_SEL")] = Property(0b0,1);
            // ci.params[ctx->id("CFG_A.LOCK_DETECT_WIN")] = Property(0b0,1);
            // ci.params[ctx->id("CFG_A.SYNC_BYPASS")] = Property(0b0,1);
            ci.params[ctx->id("CFG_A.FILTER_SHIFT")] = Property(0b10, 2);
            ci.params[ctx->id("CFG_A.FAST_LOCK")] = Property(0b1, 1);
            ci.params[ctx->id("CFG_A.SAR_LIMIT")] = Property(0b010, 3);
            // ci.params[ctx->id("CFG_A.OP_LOCK")] = Property(0b0,1);
            ci.params[ctx->id("CFG_A.PDIV0_MUX")] = Property(0b1, 1);
            ci.params[ctx->id("CFG_A.EN_COARSE_TUNE")] = Property(0b1, 1);
            // ci.params[ctx->id("CFG_A.EN_USR_CFG")] = Property(0b0,1);
            // ci.params[ctx->id("CFG_A.PLL_EN_SEL")] = Property(0b0,1);

            ci.params[ctx->id("CFG_A.CI_FILTER_CONST")] = Property(ci_const, 5);
            ci.params[ctx->id("CFG_A.CP_FILTER_CONST")] = Property(cp_const, 5);
            /*
                clock path selection
                0-0 PDIV0_MUX = 0, FB_PATH = 0 // DCO clock with intern feedback
                1-0 PDIV0_MUX = 1, FB_PATH = 0 // divided clock: PDIV1->M1->M2 with intern feedback  DEFAULT
                0-1 not possible  f_core = f_ref will set PDIV0_MUX = 1
                1-1 PDIV0_MUX = 1, FB_PATH = 1 // divided clock: PDIV1->M1->M2  with extern feedback
            PDIV1->M1->M2->PDIV0->N1->N2 }
            */
            bool pdiv0_mux = true;
            PllCfgRecord val = get_pll_settings(ref_clk, out_clk, perf_md, low_jitter, pdiv0_mux, feedback);
            if (val.f_core > 0) { // cfg exists
                ci.params[ctx->id("CFG_A.K")] = Property(val.K, 12);
                ci.params[ctx->id("CFG_A.N1")] = Property(val.N1, 6);
                ci.params[ctx->id("CFG_A.N2")] = Property(val.N2, 10);
                ci.params[ctx->id("CFG_A.M1")] = Property(val.M1, 6);
                ci.params[ctx->id("CFG_A.M2")] = Property(val.M2, 10);
                ci.params[ctx->id("CFG_A.PDIV1_SEL")] = Property(val.PDIV1 == 2 ? 1 : 0, 1);
            } else {
                log_error("Unable to configure PLL %s\n", ci.name.c_str(ctx));
            }
            // Remove all not propagated parameters
            ci.unsetParam(id_PERF_MD);
            ci.unsetParam(id_REF_CLK);
            ci.unsetParam(id_OUT_CLK);
            ci.unsetParam(id_LOW_JITTER);
            ci.unsetParam(id_CI_FILTER_CONST);
            ci.unsetParam(id_CP_FILTER_CONST);
        } else {
            // Handling CC_PLL_ADV
            for (int i = 0; i < 2; i++) {
                char cfg = 'A' + i;
                IdString id = i == 0 ? id_PLL_CFG_A : id_PLL_CFG_B;
                ci.params[ctx->idf("CFG_%c.CI_FILTER_CONST", cfg)] = Property(extract_bits(ci.params, id, 0, 5), 5);
                ci.params[ctx->idf("CFG_%c.CP_FILTER_CONST", cfg)] = Property(extract_bits(ci.params, id, 5, 5), 5);
                ci.params[ctx->idf("CFG_%c.N1", cfg)] = Property(extract_bits(ci.params, id, 10, 6), 6);
                ci.params[ctx->idf("CFG_%c.N2", cfg)] = Property(extract_bits(ci.params, id, 16, 10), 10);
                ci.params[ctx->idf("CFG_%c.M1", cfg)] = Property(extract_bits(ci.params, id, 26, 6), 6);
                ci.params[ctx->idf("CFG_%c.M2", cfg)] = Property(extract_bits(ci.params, id, 32, 10), 10);
                ci.params[ctx->idf("CFG_%c.K", cfg)] = Property(extract_bits(ci.params, id, 42, 12), 12);
                ci.params[ctx->idf("CFG_%c.FB_PATH", cfg)] = Property(extract_bits(ci.params, id, 54, 1), 1);
                ci.params[ctx->idf("CFG_%c.FINE_TUNE", cfg)] = Property(extract_bits(ci.params, id, 55, 11), 11);
                ci.params[ctx->idf("CFG_%c.COARSE_TUNE", cfg)] = Property(extract_bits(ci.params, id, 66, 3), 3);
                ci.params[ctx->idf("CFG_%c.AO_SW", cfg)] = Property(extract_bits(ci.params, id, 69, 5), 5);
                ci.params[ctx->idf("CFG_%c.OPEN_LOOP", cfg)] = Property(extract_bits(ci.params, id, 74, 1), 1);
                ci.params[ctx->idf("CFG_%c.ENFORCE_LOCK", cfg)] = Property(extract_bits(ci.params, id, 75, 1), 1);
                ci.params[ctx->idf("CFG_%c.PFD_SEL", cfg)] = Property(extract_bits(ci.params, id, 76, 1), 1);
                ci.params[ctx->idf("CFG_%c.LOCK_DETECT_WIN", cfg)] = Property(extract_bits(ci.params, id, 77, 1), 1);
                ci.params[ctx->idf("CFG_%c.SYNC_BYPASS", cfg)] = Property(extract_bits(ci.params, id, 78, 1), 1);
                ci.params[ctx->idf("CFG_%c.FILTER_SHIFT", cfg)] = Property(extract_bits(ci.params, id, 79, 2), 2);
                ci.params[ctx->idf("CFG_%c.FAST_LOCK", cfg)] = Property(extract_bits(ci.params, id, 81, 1), 1);
                ci.params[ctx->idf("CFG_%c.SAR_LIMIT", cfg)] = Property(extract_bits(ci.params, id, 82, 3), 3);
                ci.params[ctx->idf("CFG_%c.OP_LOCK", cfg)] = Property(extract_bits(ci.params, id, 85, 1), 1);
                ci.params[ctx->idf("CFG_%c.PDIV1_SEL", cfg)] = Property(extract_bits(ci.params, id, 86, 1), 1);
                ci.params[ctx->idf("CFG_%c.PDIV0_MUX", cfg)] = Property(extract_bits(ci.params, id, 87, 1), 1);
                ci.params[ctx->idf("CFG_%c.EN_COARSE_TUNE", cfg)] = Property(extract_bits(ci.params, id, 88, 1), 1);
                ci.params[ctx->idf("CFG_%c.EN_USR_CFG", cfg)] = Property(extract_bits(ci.params, id, 89, 1), 1);
                ci.params[ctx->idf("CFG_%c.PLL_EN_SEL", cfg)] = Property(extract_bits(ci.params, id, 90, 1), 1);
            }
            NetInfo *select_net = ci.getPort(id_USR_SEL_A_B);
            if (select_net == nullptr || select_net->name == ctx->id("$PACKER_GND")) {
                ci.params[ctx->id("SET_SEL")] = Property(0b0, 1);
                ci.params[ctx->id("USR_SET")] = Property(0b0, 1);
                ci.disconnectPort(id_USR_SEL_A_B);
            } else if (select_net->name == ctx->id("$PACKER_VCC")) {
                ci.params[ctx->id("SET_SEL")] = Property(0b1, 1);
                ci.params[ctx->id("USR_SET")] = Property(0b0, 1);
                ci.disconnectPort(id_USR_SEL_A_B);
            } else {
                ci.params[ctx->id("USR_SET")] = Property(0b1, 1);
                move_ram_o(&ci, id_USR_SEL_A_B, PLACE_USR_SEL_A_B);
            }
            ci.params[ctx->id("LOCK_REQ")] = Property(0b1, 1);
            ci.unsetParam(id_PLL_CFG_A);
            ci.unsetParam(id_PLL_CFG_B);
            if (!ci.getPort(id_CLK_FEEDBACK))
                ci.params[ctx->id("LOCK_REQ")] = Property(0b1, 1);
        }

        // PLL control register A
        ci.params[ctx->id("PLL_RST")] = Property(0b1, 1);
        ci.params[ctx->id("PLL_EN")] = Property(0b1, 1);
        // PLL_AUTN - for Autonomous Mode - not set
        // SET_SEL - handled in CC_PLL_ADV
        // USR_SET - handled in CC_PLL_ADV
        // TODO: USR_CLK_REF - based on signals used
        ci.params[ctx->id("CLK_OUT_EN")] = Property(0b1, 1);
        // LOCK_REQ - set by CC_PLL parameter

        // PLL control register B
        // AUTN_CT_I - for Autonomous Mode - not set
        // CLK180_DOUB - set by CC_PLL parameter
        // CLK270_DOUB - set by CC_PLL parameter
        // bits 6 and 7 are unused
        // USR_CLK_OUT - part of routing, mux from chipdb

        ci.type = id_PLL;

        pll_index++;
    }
}

void GateMatePacker::pack_misc()
{
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_USR_RSTN))
            continue;
        ci.type = id_USR_RSTN;
        ci.cluster = ci.name;
        move_ram_i(&ci, id_USR_RSTN, PLACE_USR_RSTN);
    }
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_ODDR, id_CC_IDDR))
            continue;
        log_error("Cell '%s' of type %s is not connected to GPIO pin.\n", ci.name.c_str(ctx), ci.type.c_str(ctx));
    }
}

void GateMatePacker::pack_constants()
{
    log_info("Packing constants..\n");
    // Replace constants with LUTs
    const dict<IdString, Property> vcc_params = {{id_INIT_L10, Property(0b1111, 4)}, {id_C_O, Property(0b11, 2)}};
    const dict<IdString, Property> gnd_params = {{id_INIT_L10, Property(0b0000, 4)}, {id_C_O, Property(0b11, 2)}};

    h.replace_constants(CellTypePort(id_CPE_HALF, id_OUT), CellTypePort(id_CPE_HALF, id_OUT), vcc_params, gnd_params);
}

void GateMatePacker::remove_constants()
{
    log_info("Removing constants..\n");
    auto fnd_cell = ctx->cells.find(ctx->id("$PACKER_VCC_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_VCC"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries() == 0) {
            BelId bel = (*fnd_cell).second.get()->bel;
            if (bel != BelId())
                ctx->unbindBel(bel);
            ctx->cells.erase(fnd_cell);
            ctx->nets.erase(fnd_net);
            log_info("    Removed unused VCC cell\n");
        }
    }
    fnd_cell = ctx->cells.find(ctx->id("$PACKER_GND_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_GND"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries() == 0) {
            BelId bel = (*fnd_cell).second.get()->bel;
            if (bel != BelId())
                ctx->unbindBel(bel);
            ctx->cells.erase(fnd_cell);
            ctx->nets.erase(fnd_net);
            log_info("    Removed unused GND cell\n");
        }
    }
}

void GateMatePacker::remove_not_used()
{
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        for (auto &p : ci.ports) {
            if (p.second.type == PortType::PORT_OUT) {
                NetInfo *net = ci.getPort(p.first);
                if (net && net->users.entries() == 0) {
                    ci.disconnectPort(p.first);
                }
            }
        }
    }
}
void GateMateImpl::pack()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("ccf")) {
        parse_ccf(args.options.at("ccf"));
    }

    GateMatePacker packer(ctx, this);
    packer.pack_constants();
    packer.remove_not_used();
    packer.pack_io();
    packer.insert_pll_bufg();
    packer.sort_bufg();
    packer.pack_pll();
    packer.pack_bufg();
    packer.pack_io_sel(); // merge in FF and DDR
    packer.pack_misc();
    packer.pack_addf();
    packer.pack_cpe();
    packer.remove_constants();
}

NEXTPNR_NAMESPACE_END

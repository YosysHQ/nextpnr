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

#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

std::string get_die_name(int total_dies, int die)
{
    if (total_dies == 1)
        return "";
    return stringf("on die '%d%c'", int(die / total_dies) + 1, 'A' + int(die % total_dies));
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
                if (key == id_LOC &&
                    top_port.cell->type.in(id_CC_LVDS_IBUF, id_CC_LVDS_OBUF, id_CC_LVDS_TOBUF, id_CC_LVDS_IOBUF)) {
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

        if (loc == "SER_CLK" || loc == "SER_CLK_N") {
            if (ci.type.in(id_CC_IBUF)) {
                log_info("    Constraining '%s' to pad '%s'\n", ci.name.c_str(ctx), loc.c_str());
                NetInfo *ser_clk = ci.getPort(id_I);
                for (auto s : ci.getPort(id_Y)->users) {
                    s.cell->disconnectPort(s.port);
                    s.cell->connectPort(s.port, ser_clk);
                }
                ci.disconnectPort(id_I);
                packed_cells.emplace(ci.name);
                continue;
            } else {
                log_error("SER_CLK and SER_CLK_N pins can only be used on input port.\n");
            }
        }
        if (loc == "UNPLACED") {
            const ArchArgs &args = ctx->args;
            if (args.options.count("allow-unconstrained"))
                log_warning("IO '%s' is unconstrained in CCF and will be automatically placed.\n", ctx->nameOf(&ci));
            else
                log_error("IO '%s' is unconstrained in CCF (override this error with "
                          "--vopt allow-unconstrained).\n",
                          ctx->nameOf(&ci));
        }

        disconnect_if_gnd(&ci, id_T);
        if (ci.type == id_CC_TOBUF && !ci.getPort(id_T))
            ci.type = id_CC_OBUF;
        if (ci.type == id_CC_LVDS_TOBUF && !ci.getPort(id_T))
            ci.type = id_CC_LVDS_OBUF;

        if (ci.type.in(id_CC_IBUF, id_CC_IOBUF))
            copy_constraint(ci.getPort(id_I), ci.getPort(id_Y));
        if (ci.type.in(id_CC_LVDS_IBUF, id_CC_LVDS_IOBUF)) {
            copy_constraint(ci.getPort(id_I_P), ci.getPort(id_Y));
            copy_constraint(ci.getPort(id_I_N), ci.getPort(id_Y));
        }

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
                ci.params[id_SLEW] = Property(Property::State::S0);
            else if (val == "SLOW")
                ci.params[id_SLEW] = Property(Property::State::S1);
            else
                log_error("Unknown value '%s' for SLEW parameter of '%s' cell.\n", val.c_str(), ci.name.c_str(ctx));
        }
        if (is_lvds) {
            std::string p_pin = str_or_default(ci.params, id_PIN_NAME_P, "UNPLACED");
            std::string n_pin = str_or_default(ci.params, id_PIN_NAME_N, "UNPLACED");
            if (p_pin == "UNPLACED" || n_pin == "UNPLACED")
                log_error("Both LVDS pins must be set to a valid locations.\n");
            if (p_pin.substr(0, 6) != n_pin.substr(0, 6) || p_pin[7] != n_pin[7])
                log_error("LVDS pads '%s' and '%s' do not match.\n", p_pin.c_str(), n_pin.c_str());
            if (p_pin[6] != 'A')
                log_error("LVDS positive pad must be from type A.\n");
            if (n_pin[6] != 'B')
                log_error("LVDS negative pad must be from type B.\n");
        }
        for (auto key : keys)
            ci.params.erase(key);

        // For output pins set SLEW to SLOW if not defined
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

        static dict<IdString, IdString> map_types = {
                {id_CC_IBUF, id_CPE_IBUF},           {id_CC_OBUF, id_CPE_OBUF},
                {id_CC_TOBUF, id_CPE_TOBUF},         {id_CC_IOBUF, id_CPE_IOBUF},
                {id_CC_LVDS_IBUF, id_CPE_LVDS_IBUF}, {id_CC_LVDS_TOBUF, id_CPE_LVDS_TOBUF},
                {id_CC_LVDS_OBUF, id_CPE_LVDS_OBUF}, {id_CC_LVDS_IOBUF, id_CPE_LVDS_IOBUF},
        };
        ci.type = map_types[ci.type];

        if (loc.empty() || loc == "UNPLACED") {
            // Skip SER_CLK and SER_CLK_N if found in available_pads
            auto it = uarch->available_pads.begin();
            while (it != uarch->available_pads.end()) {
                std::string pad_name = it->str(ctx);
                if (pad_name == "SER_CLK" || pad_name == "SER_CLK_N") {
                    ++it;
                    continue;
                }
                break;
            }

            if (it == uarch->available_pads.end())
                log_error("No more pads available.\n");

            IdString id = *it;
            uarch->available_pads.erase(id);
            loc = id.c_str(ctx);
        }
        ci.params[id_LOC] = Property(loc);

        BelId bel;
        if (uarch->locations.count(std::make_pair(ctx->id(loc), uarch->preferred_die)))
            bel = ctx->getBelByLocation(uarch->locations[std::make_pair(ctx->id(loc), uarch->preferred_die)]);
        else
            bel = ctx->get_package_pin_bel(ctx->id(loc));
        if (bel == BelId())
            log_error("Unable to constrain IO '%s', device does not have a pin named '%s'\n", ci.name.c_str(ctx),
                      loc.c_str());
        log_info("    Constraining '%s' to pad '%s'%s.\n", ci.name.c_str(ctx), loc.c_str(),
                 get_die_name(uarch->dies, uarch->tile_extra_data(bel.tile)->die).c_str());
        if (!ctx->checkBelAvail(bel)) {
            log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci), ctx->nameOfBel(bel),
                      ctx->nameOf(ctx->getBoundBelCell(bel)));
        }
        ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_FIXED);
    }
    flush_cells();
}

void GateMatePacker::pack_io_sel()
{
    std::vector<CellInfo *> cells;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!uarch->getBelBucketForCellType(ci.type).in(id_GPIO))
            continue;

        cells.push_back(&ci);
    }

    std::vector<std::array<CellInfo *, 9>> ddr(uarch->dies); // for each bank

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
                    cell->movePortTo(id_CLK, target, ctx->idf("CLOCK%d", index + 1));
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
                    cell->movePortTo(id_CLK, target, ctx->idf("CLOCK%d", index + 1));
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
            dff->movePortTo(id_Q, &ci, id_IN1);
            set_in_clk(dff, &ci);
            bool invert = bool_or_default(dff->params, id_CLK_INV, 0);
            if (invert) {
                ci.params[id_INV_IN1_CLOCK] = Property(Property::State::S1);
                ci.params[id_INV_IN2_CLOCK] = Property(Property::State::S1);
            }
            return true;
        } else {
            log_warning("DFF '%s' cell for IO '%s', but unable to merge.\n", dff->name.c_str(ctx), ci.name.c_str(ctx));
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
        bool invert = bool_or_default(iddr->params, id_CLK_INV, 0);
        if (invert) {
            ci.params[id_INV_IN1_CLOCK] = Property(Property::State::S1);
        } else {
            ci.params[id_INV_IN2_CLOCK] = Property(Property::State::S1);
        }
        return true;
    };

    for (auto &cell : cells) {
        CellInfo &ci = *cell;
        bool ff_obf = bool_or_default(ci.params, id_FF_OBF, 0);
        bool ff_ibf = bool_or_default(ci.params, id_FF_IBF, 0);
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
                bool ff_obf_merged = false;
                if (ff_obf && do_net->driver.cell->type == id_CC_DFF && do_net->users.entries() == 1) {
                    CellInfo *dff = do_net->driver.cell;
                    if (is_gpio_valid_dff(dff)) {
                        ci.params[id_OUT1_FF] = Property(Property::State::S1);
                        packed_cells.emplace(dff->name);
                        ci.disconnectPort(id_A);
                        dff->movePortTo(id_D, &ci, id_OUT1);
                        use_custom_clock = set_out_clk(dff, &ci);
                        bool invert = bool_or_default(dff->params, id_CLK_INV, 0);
                        if (invert) {
                            ci.params[id_INV_OUT1_CLOCK] = Property(Property::State::S1);
                            ci.params[id_INV_OUT2_CLOCK] = Property(Property::State::S1);
                        }
                        ff_obf_merged = true;
                    } else {
                        log_warning("DFF '%s' cell for IO '%s', but unable to merge.\n", dff->name.c_str(ctx),
                                    ci.name.c_str(ctx));
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
                    oddr->movePortTo(id_D0, &ci, id_OUT2);
                    oddr->movePortTo(id_D1, &ci, id_OUT1);
                    const auto &pad = ctx->get_package_pin(ctx->id(loc));
                    int die = uarch->tile_extra_data(ci.bel.tile)->die;
                    CellInfo *cpe_half = ddr[die][pad->pad_bank];
                    if (cpe_half) {
                        if (cpe_half->getPort(id_IN1) != oddr->getPort(id_DDR))
                            log_error("DDR port use signal different than already occupied DDR source.\n");
                        ci.ports[id_DDR].name = id_DDR;
                        ci.ports[id_DDR].type = PORT_IN;
                        ci.connectPort(id_DDR, cpe_half->getPort(id_RAM_O));
                    } else {
                        oddr->movePortTo(id_DDR, &ci, id_DDR);
                        cpe_half = move_ram_o(&ci, id_DDR, false);
                        uarch->ddr_nets.insert(cpe_half->getPort(id_IN1)->name);
                        auto l = reinterpret_cast<const GateMatePadExtraDataPOD *>(pad->extra_data.get());
                        ctx->bindBel(ctx->getBelByLocation(Loc(l->x, l->y, l->z)), cpe_half,
                                     PlaceStrength::STRENGTH_FIXED);
                        ddr[die][pad->pad_bank] = cpe_half;
                    }
                    use_custom_clock = set_out_clk(oddr, &ci);
                    bool invert = bool_or_default(oddr->params, id_CLK_INV, 0);
                    if (!invert) {
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
                ci.renamePort(id_Y, id_IN1);
        }

        Loc root_loc = ctx->getBelLocation(ci.bel);
        for (int i = 0; i < 4; i++) {
            CellInfo *cpe = move_ram_o_fixed(&ci, ctx->idf("OUT%d", i + 1), root_loc);
            if (cpe && i == 2)
                cpe->params[id_INIT_L10] = Property(0b0101, 4); // Invert CPE out for output enable (OUT3)
        }
    }
    flush_cells();
}

bool GateMatePacker::is_gpio_valid_dff(CellInfo *dff)
{
    NetInfo *en_net = dff->getPort(id_EN);
    bool invert = bool_or_default(dff->params, id_EN_INV, 0);
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
    invert = bool_or_default(dff->params, id_SR_INV, 0);
    if (sr_net) {
        if (sr_net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
            bool sr_signal = sr_net->name == ctx->id("$PACKER_VCC");
            if (sr_signal ^ invert)
                log_error("Currently unsupported DFF configuration for '%s'\n.", dff->name.c_str(ctx));
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

NEXTPNR_NAMESPACE_END

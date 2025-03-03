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

#include "pack.h"
#include "design_utils.h"

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

        // Remap ports to GPIO bel
        ci.renamePort(id_A, id_DO);
        ci.renamePort(id_Y, id_DI);
        ci.renamePort(id_T, id_OE);

        NetInfo *do_net = ci.getPort(id_DO);
        if (do_net) {
            if (do_net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
                ci.params[id_OUT23_14_SEL] =
                        Property(do_net->name == ctx->id("$PACKER_VCC") ? Property::State::S1 : Property::State::S0);
                ci.disconnectPort(id_DO);
            } else {
                ci.params[id_OUT_SIGNAL] = Property(Property::State::S1);
            }
        }
        if (!loc.empty()) {
            BelId bel = ctx->get_package_pin_bel(ctx->id(loc));
            if (bel == BelId())
                log_error("Unable to constrain IO '%s', device does not have a pin named '%s'\n", ci.name.c_str(ctx),
                          loc.c_str());
            log_info("    Constraining '%s' to pad '%s'\n", ci.name.c_str(ctx), loc.c_str());
            if (!ctx->checkBelAvail(bel)) {
                log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci),
                          ctx->nameOfBel(bel), ctx->nameOf(ctx->getBoundBelCell(bel)));
            }
            const auto extra = uarch->bel_extra_data(bel);
            Loc l = ctx->getBelLocation(bel);
            switch(extra->flags) {
                case BEL_EXTRA_GPIO_L: {
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x+3,l.y,0)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x+3,l.y,1)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x+3,l.y+1,0)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x+3,l.y+1,1)));
                    break;
                }
                case BEL_EXTRA_GPIO_R: {
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x-3,l.y,0)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x-3,l.y,1)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x-3,l.y+1,0)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x-3,l.y+1,1)));
                    break;
                }
                case BEL_EXTRA_GPIO_T: {
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x,l.y-3,0)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x,l.y-3,1)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x+1,l.y-3,0)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x+1,l.y-3,1)));
                    break;
                }
                case BEL_EXTRA_GPIO_B: {
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x,l.y+3,0)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x,l.y+3,1)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x+1,l.y+3,0)));
                    uarch->blocked_bels.emplace(ctx->getBelByLocation(Loc(l.x+1,l.y+3,1)));
                    break;
                }                    
            }
            ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_FIXED);
        }
    }
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
    int  sr_val = int_or_default(dff->params, id_SR_VAL, 0) == 1;
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
        for(int i=0;i<4;i++) {
            NetInfo *net = ci.getPort(ctx->idf("D%d",i));
            if (net) {
                if (net->name.in(ctx->id("$PACKER_GND"),ctx->id("$PACKER_VCC"))) {
                    if (net->name == ctx->id("$PACKER_VCC"))
                        invert |= 1<< i;
                    ci.disconnectPort(ctx->idf("D%d",i));
                } else {
                    select |= 1 << i;
                }
            }
        }
        ci.params[id_C_FUNCTION] = Property(C_MX4, 3);
        ci.params[id_INIT_L02] = Property(0b1100, 4); // IN6
        ci.params[id_INIT_L03] = Property(0b1100, 4); // IN8
        ci.params[id_INIT_L11] = Property(invert, 4); // Inversion bits
        //ci.params[id_INIT_L20] = Property(0b1100, 4); // Always D1
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
std::vector<std::vector<T>> splitNestedVector(const std::vector<std::vector<T>>& input, size_t maxSize = 8) {
    std::vector<std::vector<T>> result;

    for (const auto& inner : input) {
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
        ci_upper->cluster = root->name;;
        ci_upper->constr_abs_z = false;
        ci_upper->constr_z = -1;
        ci_upper->constr_y = -1;

        CellInfo *ci_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$ci_lower", root->name.c_str(ctx)));
        root->constr_children.push_back(ci_lower);
        ci_lower->cluster = root->name;;
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
            ci_lower->params[id_INIT_L00] = Property(0b1010, 4); //IN5
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
               
            if (i == grp.size()-1) {
                CellInfo *co_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$co_upper", cy->name.c_str(ctx)));
                co_upper->cluster = root->name;
                root->constr_children.push_back(co_upper);
                co_upper->constr_abs_z = false;
                co_upper->constr_z = -1;
                co_upper->constr_y = +i+1;
                CellInfo *co_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$co_lower", cy->name.c_str(ctx)));
                co_lower->cluster = root->name;
                root->constr_children.push_back(co_lower);
                co_lower->constr_abs_z = false;
                co_lower->constr_y = +i+1;
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

void GateMatePacker::pack_bufg()
{
    log_info("Packing BUFGs..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_BUFG))
            continue;
        ci.type = id_BUFG;
    }
}

template <typename KeyType> double double_or_default(const dict<KeyType, Property> &ct, const KeyType &key, double def = 0)
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

void GateMatePacker::pack_pll()
{
    log_info("Packing PLLss..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_PLL))
            continue;

        disconnect_if_gnd(&ci, id_CLK_REF);
        disconnect_if_gnd(&ci, id_USR_CLK_REF);
        disconnect_if_gnd(&ci, id_CLK_FEEDBACK);
        disconnect_if_gnd(&ci, id_USR_LOCKED_STDY_RST);

        ci.params[id_LOCK_REQ] = Property(int_or_default(ci.params, id_LOCK_REQ, 1),1);
        ci.params[id_CLK180_DOUB] = Property(int_or_default(ci.params, id_CLK180_DOUB, 0),1);
        ci.params[id_CLK270_DOUB] = Property(int_or_default(ci.params, id_CLK270_DOUB, 0),1);
        std::string mode = str_or_default(ci.params, id_PERF_MD, "SPEED");
        boost::algorithm::to_upper(mode);
        int perf_md;
        if (mode == "LOWPOWER") perf_md = 1;
        else if (mode == "ECONOMY") perf_md = 2;
        else if (mode == "SPEED") perf_md = 3;
        else log_error("Unknown PERF_MD parameter value '%s' for cell %s.\n", mode.c_str(), ci.name.c_str(ctx));


        double ref_clk = double_or_default(ci.params, id_REF_CLK, 0.0);
        if (ref_clk <= 0 || ref_clk > 125)
            log_error("REF_CLK parameter is out of range (0,125.00].\n");

        double max_freq = 0.0;
        switch(perf_md) {
            case 1: max_freq = 250.00; break;
            case 2: max_freq = 312.50; break;
            case 3: max_freq = 416.75; break;
        }

        double out_clk = double_or_default(ci.params, id_OUT_CLK, 0.0);
        if (out_clk <= 0 || out_clk > max_freq)
            log_error("OUT_CLK parameter is out of range (0,%.2lf].\n", max_freq);

        // For 10 MHz - > 25 MHz
        ci.params[ctx->id("CFG_A.AO_SW")] = Property(0b01000,5);
        ci.params[ctx->id("CFG_A.CI_FILTER_CONST")] = Property(0b00010,5);
        ci.params[ctx->id("CFG_A.COARSE_TUNE")] = Property(0b100,3);
        ci.params[ctx->id("CFG_A.CP_FILTER_CONST")] = Property(0b00100,5);
        ci.params[ctx->id("CFG_A.EN_COARSE_TUNE")] = Property(0b1,1);
        ci.params[ctx->id("CFG_A.FAST_LOCK")] = Property(0b1,1);
        ci.params[ctx->id("CFG_A.FILTER_SHIFT")] = Property(0b10,2);
        ci.params[ctx->id("CFG_A.FINE_TUNE")] = Property(0b00011001000,11);
        ci.params[ctx->id("CFG_A.K")] = Property(0b000000000001,12);
        ci.params[ctx->id("CFG_A.M1")] = Property(0b000010,6);
        ci.params[ctx->id("CFG_A.M2")] = Property(0b0000001001,10);
        ci.params[ctx->id("CFG_A.N1")] = Property(0b001001,6);
        ci.params[ctx->id("CFG_A.N2")] = Property(0b0000001010,10);
        ci.params[ctx->id("CFG_A.PDIV0_MUX")] = Property(0b1,1);
        ci.params[ctx->id("CFG_A.PDIV1_SEL")] = Property(0b1,1);
        ci.params[ctx->id("CFG_A.SAR_LIMIT")] = Property(0b010,3);

        // Remove all not propagated parameters
        ci.unsetParam(id_PERF_MD);
        ci.unsetParam(id_REF_CLK);
        ci.unsetParam(id_OUT_CLK);        
        ci.unsetParam(id_LOW_JITTER);
        ci.unsetParam(id_CI_FILTER_CONST);
        ci.unsetParam(id_CP_FILTER_CONST);
        //ci.unsetParam(id_PLL_CFG_A);
        //ci.unsetParam(id_PLL_CFG_B);

        ci.type = id_PLL;
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

        CellInfo *ci_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$ci_upper", ci.name.c_str(ctx)));
        ci.constr_children.push_back(ci_upper);
        ci_upper->cluster = ci.name;
        ci_upper->constr_abs_z = false;
        ci_upper->constr_z = -2;
        ci_upper->params[id_C_RAM_I2] = Property(1, 1);

        NetInfo *ram_i = ctx->createNet(ctx->idf("%s$ram_i", ci.name.c_str(ctx)));
        ci.movePortTo(id_USR_RSTN, ci_upper, id_OUT);
        ci.connectPort(id_USR_RSTN, ram_i);
        ci_upper->connectPort(id_RAM_I, ram_i);
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

void GateMateImpl::pack()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("ccf")) {
        parse_ccf(args.options.at("ccf"));
    }

    GateMatePacker packer(ctx, this);
    packer.pack_constants();
    packer.pack_io();
    packer.pack_pll();
    packer.pack_bufg();
    packer.pack_misc();
    packer.pack_addf();
    packer.pack_cpe();
    packer.remove_constants();
}

NEXTPNR_NAMESPACE_END

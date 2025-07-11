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
#include "design_utils.h"
#include "gatemate_util.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

void GateMatePacker::flush_cells(bool unbind)
{
    for (auto pcell : packed_cells) {
        for (auto &port : ctx->cells[pcell]->ports) {
            ctx->cells[pcell]->disconnectPort(port.first);
        }
        if (unbind)
            ctx->unbindBel(ctx->cells[pcell]->bel);
        ctx->cells.erase(pcell);
    }
    packed_cells.clear();
}

void GateMatePacker::disconnect_if_gnd(CellInfo *cell, IdString input)
{
    if (cell->getPort(input) == net_PACKER_GND)
        cell->disconnectPort(input);
}

void GateMatePacker::pack_misc()
{
    log_info("Packing misc..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_USR_RSTN))
            continue;
        ci.type = id_USR_RSTN;
        ci.cluster = ci.name;
        Loc fixed_loc = uarch->locations[std::make_pair(id_USR_RSTN, uarch->preferred_die)];
        ctx->bindBel(ctx->getBelByLocation(fixed_loc), &ci, PlaceStrength::STRENGTH_FIXED);

        move_ram_i_fixed(&ci, id_USR_RSTN, fixed_loc);
    }
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_CFG_CTRL))
            continue;
        ci.type = id_CFG_CTRL;
        ci.cluster = ci.name;
        Loc fixed_loc = uarch->locations[std::make_pair(id_CFG_CTRL, uarch->preferred_die)];
        ctx->bindBel(ctx->getBelByLocation(fixed_loc), &ci, PlaceStrength::STRENGTH_FIXED);

        move_ram_o_fixed(&ci, id_CLK, fixed_loc);
        move_ram_o_fixed(&ci, id_EN, fixed_loc);
        move_ram_o_fixed(&ci, id_VALID, fixed_loc);
        move_ram_o_fixed(&ci, id_RECFG, fixed_loc);
        for (int i = 0; i < 8; i++)
            move_ram_o_fixed(&ci, ctx->idf("DATA[%d]", i), fixed_loc);
    }
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_ODDR, id_CC_IDDR))
            continue;
        log_error("Cell '%s' of type %s is not connected to GPIO pin.\n", ci.name.c_str(ctx), ci.type.c_str(ctx));
    }
}

void GateMatePacker::disconnect_not_used()
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

void GateMatePacker::copy_constraint(NetInfo *in_net, NetInfo *out_net)
{
    if (!in_net || !out_net)
        return;
    if (ctx->debug)
        log_info("copy clock period constraint on net '%s' from net '%s'\n", out_net->name.c_str(ctx),
                 in_net->name.c_str(ctx));
    if (out_net->clkconstr.get() != nullptr)
        log_warning("found multiple clock constraints on net '%s'\n", out_net->name.c_str(ctx));
    if (in_net->clkconstr) {
        out_net->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
        out_net->clkconstr->low = in_net->clkconstr->low;
        out_net->clkconstr->high = in_net->clkconstr->high;
        out_net->clkconstr->period = in_net->clkconstr->period;
    }
}

void GateMatePacker::move_connections(NetInfo *from_net, NetInfo *to_net)
{
    for (const auto &usr : from_net->users) {
        IdString port = usr.port;
        usr.cell->disconnectPort(port);
        usr.cell->connectPort(port, to_net);
    }
}

void GateMatePacker::count_cell(CellInfo &ci)
{
    packed_cells.insert(ci.name);
    if (!count_per_type.count(ci.type))
        count_per_type[ci.type] = 1;
    else
        count_per_type[ci.type]++;
    count++;
}

void GateMatePacker::optimize_lut()
{
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_LUT1, id_CC_LUT2))
            continue;
        NetInfo *o_net = ci.getPort(id_O);
        if (!o_net) {
            count_cell(ci);
            continue;
        }

        uint8_t val = int_or_default(ci.params, id_INIT, 0);
        if (ci.type == id_CC_LUT1)
            val = val << 2 | val;
        switch (val) {
        case LUT_ZERO: // constant 0
            move_connections(o_net, net_PACKER_GND);
            count_cell(ci);
            break;
        case LUT_D0: // propagate
            move_connections(o_net, ci.getPort(id_I0));
            count_cell(ci);
            break;
        case LUT_D1: // propagate
            move_connections(o_net, ci.getPort(id_I1));
            count_cell(ci);
            break;
        case LUT_ONE: // constant 1
            move_connections(o_net, net_PACKER_VCC);
            count_cell(ci);
            break;
        default:
            break;
        }
    }
    flush_cells();
}

void GateMatePacker::optimize_mx()
{
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_MX2, id_CC_MX4))
            continue;
        NetInfo *y_net = ci.getPort(id_Y);
        if (!y_net) {
            count_cell(ci);
            continue;
        }
        if (ci.type == id_CC_MX2) {
            if (ci.getPort(id_S0) == net_PACKER_GND) {
                move_connections(y_net, ci.getPort(id_D0));
                count_cell(ci);
                continue;
            } else if (ci.getPort(id_S0) == net_PACKER_VCC) {
                move_connections(y_net, ci.getPort(id_D1));
                count_cell(ci);
                continue;
            }
        } else {
            if ((ci.getPort(id_S1) == net_PACKER_GND) && (ci.getPort(id_S0) == net_PACKER_GND)) {
                move_connections(y_net, ci.getPort(id_D0));
                count_cell(ci);
                continue;
            } else if ((ci.getPort(id_S1) == net_PACKER_GND) && (ci.getPort(id_S0) == net_PACKER_VCC)) {
                move_connections(y_net, ci.getPort(id_D1));
                count_cell(ci);
                continue;
            } else if ((ci.getPort(id_S1) == net_PACKER_VCC) && (ci.getPort(id_S0) == net_PACKER_GND)) {
                move_connections(y_net, ci.getPort(id_D2));
                count_cell(ci);
                continue;
            } else if ((ci.getPort(id_S1) == net_PACKER_VCC) && (ci.getPort(id_S0) == net_PACKER_VCC)) {
                move_connections(y_net, ci.getPort(id_D3));
                count_cell(ci);
                continue;
            }
        }
    }
    flush_cells();
}

void GateMatePacker::optimize_ff()
{
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_DFF, id_CC_DLT))
            continue;

        NetInfo *q_net = ci.getPort(id_Q);
        if (!q_net) {
            count_cell(ci);
            continue;
        }

        int cpe_clk = int_or_default(ci.params, id_C_CPE_CLK, 0);
        int cpe_en = int_or_default(ci.params, id_C_CPE_EN, 0);
        int cpe_res = int_or_default(ci.params, id_C_CPE_RES, 0);
        int cpe_set = int_or_default(ci.params, id_C_CPE_SET, 0);
        int ff_init = int_or_default(ci.params, id_FF_INIT, 0);
        bool ff_has_init = (ff_init >> 1) & 1;
        bool ff_init_value = ff_init & 1;

        if (cpe_res == 0) { // RES is always ON
            move_connections(q_net, net_PACKER_GND);
            count_cell(ci);
            continue;
        }
        if (cpe_set == 0) { // SET is always ON
            move_connections(q_net, net_PACKER_VCC);
            count_cell(ci);
            continue;
        }

        if (ci.type == id_CC_DFF) {
            if ((cpe_en == 0 || cpe_clk == 0) && ci.getPort(id_SR) == nullptr) {
                // Only when there is no SR signal
                // EN always OFF (never loads) or CLK never triggers
                move_connections(q_net,
                                 ff_has_init ? (ff_init_value ? net_PACKER_VCC : net_PACKER_GND) : net_PACKER_GND);
                count_cell(ci);
                continue;
            }
        } else {
            if (cpe_clk == 3 && ci.getPort(id_SR) == nullptr && cpe_res == 3 && cpe_set == 3) {
                // Clamp G if there is no set or reset
                move_connections(q_net, ci.getPort(id_D));
                count_cell(ci);
                continue;
            }
        }
    }
    flush_cells();
}

void GateMatePacker::cleanup()
{
    log_info("Running cleanups..\n");
    dff_update_params();
    int i = 1;
    do {
        count = 0;
        disconnect_not_used();
        optimize_lut();
        optimize_mx();
        optimize_ff();
        for (auto c : count_per_type)
            log_info("    %6d %s cells removed (iteration %d)\n", c.second, c.first.c_str(ctx), i);
        count_per_type.clear();
        i++;
    } while (count != 0);
}

void GateMatePacker::rename_param(CellInfo *cell, IdString name, IdString new_name, int width)
{
    if (cell->params.count(name)) {
        cell->params[new_name] = Property(int_or_default(cell->params, name, 0), width);
        cell->unsetParam(name);
    }
}

void GateMatePacker::repack()
{
    log_info("Repacking CPEs..\n");
    for (auto &cell : ctx->cells) {
        if (cell.second->type.in(id_CPE_L2T4)) {
            Loc l = ctx->getBelLocation(cell.second->bel);
            if (l.z == CPE_LT_L_Z) {
                if (!cell.second->params.count(id_INIT_L20))
                    cell.second->params[id_INIT_L20] = Property(LUT_D1, 4);
            }
            cell.second->params[id_L2T4_UPPER] = Property((l.z == CPE_LT_U_Z) ? 1 : 0, 1);
        } else if (cell.second->type.in(id_CPE_LT_L)) {
            BelId bel = cell.second->bel;
            PlaceStrength strength = cell.second->belStrength;
            uint8_t func = int_or_default(cell.second->params, id_C_FUNCTION, 0);
            Loc loc = ctx->getBelLocation(bel);
            loc.z = CPE_LT_FULL_Z;
            ctx->unbindBel(bel);
            ctx->bindBel(ctx->getBelByLocation(loc), cell.second.get(), strength);
            cell.second->renamePort(id_IN1, id_IN5);
            cell.second->renamePort(id_IN2, id_IN6);
            cell.second->renamePort(id_IN3, id_IN7);
            cell.second->renamePort(id_IN4, id_IN8);
            cell.second->renamePort(id_OUT, id_OUT1);
            cell.second->renamePort(id_CPOUT, id_CPOUT1);
            if (!cell.second->params.count(id_INIT_L20))
                cell.second->params[id_INIT_L20] = Property(LUT_D1, 4);
            rename_param(cell.second.get(), id_INIT_L00, id_INIT_L02, 4);
            rename_param(cell.second.get(), id_INIT_L01, id_INIT_L03, 4);
            rename_param(cell.second.get(), id_INIT_L10, id_INIT_L11, 4);

            switch (func) {
            case C_ADDF:
                cell.second->type = id_CPE_ADDF;
                break;
            case C_ADDF2:
                cell.second->type = id_CPE_ADDF2;
                break;
            case C_MULT:
                cell.second->type = id_CPE_MULT;
                break;
            case C_MX4:
                cell.second->type = id_CPE_MX4;
                break;
            case C_EN_CIN:
                log_error("EN_CIN should be using L2T4.\n");
                break;
            case C_CONCAT:
                cell.second->type = id_CPE_CONCAT;
                break;
            case C_ADDCIN:
                log_error("ADDCIN should be using L2T4.\n");
                break;
            default:
                break;
            }

            loc.z = CPE_LT_U_Z;
            CellInfo *upper = ctx->getBoundBelCell(ctx->getBelByLocation(loc));
            if (upper->params.count(id_INIT_L00))
                cell.second->params[id_INIT_L00] = Property(int_or_default(upper->params, id_INIT_L00, 0), 4);
            if (upper->params.count(id_INIT_L01))
                cell.second->params[id_INIT_L01] = Property(int_or_default(upper->params, id_INIT_L01, 0), 4);
            if (upper->params.count(id_INIT_L10))
                cell.second->params[id_INIT_L10] = Property(int_or_default(upper->params, id_INIT_L10, 0), 4);
            if (upper->params.count(id_C_I1))
                cell.second->params[id_C_I1] = Property(int_or_default(upper->params, id_C_I1, 0), 1);
            if (upper->params.count(id_C_I2))
                cell.second->params[id_C_I2] = Property(int_or_default(upper->params, id_C_I2, 0), 1);
            upper->movePortTo(id_IN1, cell.second.get(), id_IN1);
            upper->movePortTo(id_IN2, cell.second.get(), id_IN2);
            upper->movePortTo(id_IN3, cell.second.get(), id_IN3);
            upper->movePortTo(id_IN4, cell.second.get(), id_IN4);
            upper->movePortTo(id_OUT, cell.second.get(), id_OUT2);
            upper->movePortTo(id_CPOUT, cell.second.get(), id_CPOUT2);

        }
        // Mark for deletion
        else if (cell.second->type.in(id_CPE_LT_U, id_CPE_DUMMY)) {
            packed_cells.insert(cell.second->name);
        }
    }
    flush_cells(true);
}

void GateMateImpl::pack()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("ccf")) {
        parse_ccf(args.options.at("ccf"));
    }

    GateMatePacker packer(ctx, this);
    packer.pack_constants();
    packer.cleanup();
    packer.pack_io();
    packer.insert_pll_bufg();
    packer.sort_bufg();
    packer.pack_pll();
    packer.pack_bufg();
    packer.pack_io_sel(); // merge in FF and DDR
    packer.pack_misc();
    packer.pack_ram();
    packer.pack_serdes();
    // packer.pack_mult();
    packer.pack_addf();
    packer.pack_cpe();
    packer.remove_constants();
    packer.remove_clocking();
}

void GateMateImpl::repack()
{
    GateMatePacker packer(ctx, this);
    packer.repack();
}

NEXTPNR_NAMESPACE_END

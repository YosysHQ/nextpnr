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

#include "gatemate.h"
#include "design_utils.h"
#include "log.h"
#include "placer_heap.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"

#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

GateMateImpl::~GateMateImpl() {};

void GateMateImpl::init_database(Arch *arch)
{
    const ArchArgs &args = arch->args;
    init_uarch_constids(arch);
    arch->load_chipdb(stringf("gatemate/chipdb-%s.bin", args.device.c_str()));
    arch->set_package("FBGA324");
    fpga_mode = 3;
    timing_mode = 3;
    if (args.options.count("fpga_mode"))
        fpga_mode = std::stoi(args.options.at("fpga_mode"));
    if (args.options.count("om"))
        fpga_mode = std::stoi(args.options.at("om"));
    if (args.options.count("time_mode"))
        timing_mode = std::stoi(args.options.at("time_mode"));
    if (args.options.count("tm"))
        timing_mode = std::stoi(args.options.at("tm"));

    std::string speed_grade = "";
    switch (fpga_mode) {
    case 1:
        speed_grade = "best_";
        break;
    case 2:
        speed_grade = "typ_";
        break;
    case 3:
        speed_grade = "worst_";
        break;
    default:
        log_error("timing mode valid values are {1:best, 2:typical, 3:worst}\n");
    }
    log_info("Using timing mode '%s'\n", fpga_mode == 1   ? "BEST"
                                         : fpga_mode == 2 ? "TYPICAL"
                                         : fpga_mode == 3 ? "WORST"
                                                          : "");

    switch (timing_mode) {
    case 1:
        speed_grade += "lpr";
        break;
    case 2:
        speed_grade += "eco";
        break;
    case 3:
        speed_grade += "spd";
        break;
    default:
        log_error("operation mode valid values are {1:lowpower, 2:economy, 3:speed}\n");
    }
    log_info("Using operation mode '%s'\n", timing_mode == 1   ? "LOWPOWER"
                                            : timing_mode == 2 ? "ECONOMY"
                                            : timing_mode == 3 ? "SPEED"
                                                               : "");
    arch->set_speed_grade(speed_grade);
}

void GateMateImpl::init(Context *ctx)
{
    HimbaechelAPI::init(ctx);
    for (const auto &pad : ctx->package_info->pads) {
        available_pads.emplace(IdString(pad.package_pin));
        BelId bel = ctx->getBelByName(IdStringList::concat(IdString(pad.tile), IdString(pad.bel)));
        bel_to_pad.emplace(bel, &pad);
    }
    for (auto bel : ctx->getBels()) {
        auto *ptr = bel_extra_data(bel);
        std::map<IdString, const GateMateBelPinConstraintPOD *> pins;
        for (const auto &p : ptr->constraints)
            pins.emplace(IdString(p.name), &p);
        pin_to_constr.emplace(bel, pins);
    }
}

delay_t GateMateImpl::estimateDelay(WireId src, WireId dst) const
{
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);

    return 100 + 100 * (std::abs(dx - sx) + std::abs(dy - sy));
}

bool GateMateImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    CellInfo *cell = ctx->getBoundBelCell(bel);
    if (cell == nullptr) {
        return true;
    }
    if (ctx->getBelType(bel).in(id_CPE_HALF, id_CPE_HALF_L, id_CPE_HALF_U)) {
        Loc loc = ctx->getBelLocation(bel);
        const CellInfo *adj_half = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, loc.z == 1 ? 0 : 1)));
        if (adj_half) {
            const auto &half_data = fast_cell_info.at(cell->flat_index);
            if (half_data.dff_used) {
                const auto &adj_data = fast_cell_info.at(adj_half->flat_index);
                if (adj_data.dff_used) {
                    if (adj_data.ff_config != half_data.ff_config)
                        return false;
                    if (adj_data.ff_en != half_data.ff_en)
                        return false;
                    if (adj_data.ff_clk != half_data.ff_clk)
                        return false;
                    if (adj_data.ff_sr != half_data.ff_sr)
                        return false;
                }
            }
        }
        return true;
    }
    return true;
}

Loc GateMateImpl::getRelativeConstraint(Loc &root_loc, IdString id) const
{
    Loc child_loc;
    BelId root_bel = ctx->getBelByLocation(root_loc);
    if (pin_to_constr.count(root_bel)) {
        auto &constr = pin_to_constr.at(root_bel);
        if (constr.count(id)) {
            auto &p = constr.at(id);
            child_loc.x = root_loc.x + p->constr_x;
            child_loc.y = root_loc.y + p->constr_y;
            child_loc.z = p->constr_z;
        } else {
            log_error("Constrain info not available for pin.\n");
        }
    } else {
        log_error("Bel info not available for constraints.\n");
    }
    return child_loc;
}

bool GateMateImpl::getChildPlacement(const BaseClusterInfo *cluster, Loc root_loc,
                                     std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    for (auto child : cluster->constr_children) {
        Loc child_loc;
        if (child->constr_z >= PLACE_DB_CONSTR) {
            child_loc = getRelativeConstraint(root_loc, IdString(child->constr_z - PLACE_DB_CONSTR));
        } else {
            child_loc.x = root_loc.x + child->constr_x;
            child_loc.y = root_loc.y + child->constr_y;
            child_loc.z = child->constr_abs_z ? child->constr_z : (root_loc.z + child->constr_z);
        }
        if (child_loc.x < 0 || child_loc.x >= ctx->getGridDimX())
            return false;
        if (child_loc.y < 0 || child_loc.y >= ctx->getGridDimY())
            return false;
        BelId child_bel = ctx->getBelByLocation(child_loc);
        if (child_bel == BelId() || !this->isValidBelForCellType(child->type, child_bel))
            return false;
        placement.emplace_back(child, child_bel);
        if (!getChildPlacement(child, child_loc, placement))
            return false;
    }
    return true;
}

bool GateMateImpl::getClusterPlacement(ClusterId cluster, BelId root_bel,
                                       std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    CellInfo *root_cell = get_cluster_root(ctx, cluster);
    placement.clear();
    NPNR_ASSERT(root_bel != BelId());
    Loc root_loc = ctx->getBelLocation(root_bel);
    if (root_cell->constr_abs_z) {
        // Coerce root to absolute z constraint
        root_loc.z = root_cell->constr_z;
        root_bel = ctx->getBelByLocation(root_loc);
        if (root_bel == BelId() || !this->isValidBelForCellType(root_cell->type, root_bel))
            return false;
    }
    placement.emplace_back(root_cell, root_bel);
    return getChildPlacement(root_cell, root_loc, placement);
}

bool GateMateImpl::need_inversion(CellInfo *cell, IdString port)
{
    PortRef sink;
    sink.cell = cell;
    sink.port = port;

    NetInfo *net_info = cell->getPort(port);
    if (!net_info)
        return false;

    WireId src_wire = ctx->getNetinfoSourceWire(net_info);
    WireId dst_wire = ctx->getNetinfoSinkWire(net_info, sink, 0);

    if (src_wire == WireId())
        return false;

    WireId cursor = dst_wire;
    bool invert = false;
    while (cursor != WireId() && cursor != src_wire) {
        auto it = net_info->wires.find(cursor);

        if (it == net_info->wires.end())
            break;

        PipId pip = it->second.pip;
        if (pip == PipId())
            break;

        invert ^= ctx->isPipInverting(pip);
        cursor = ctx->getPipSrcWire(pip);
    }

    return invert;
}

void GateMateImpl::update_cpe_lt(CellInfo *cell, IdString port, IdString init)
{
    unsigned init_val = int_or_default(cell->params, init);
    bool invert = need_inversion(cell, port);
    if (invert) {
        if (port.in(id_IN1, id_IN3))
            init_val = (init_val & 0b1010) >> 1 | (init_val & 0b0101) << 1;
        else
            init_val = (init_val & 0b0011) << 2 | (init_val & 0b1100) >> 2;
        cell->params[init] = Property(init_val, 4);
    }
}

void GateMateImpl::update_cpe_inv(CellInfo *cell, IdString port, IdString param)
{
    unsigned init_val = int_or_default(cell->params, param);
    bool invert = need_inversion(cell, port);
    if (invert) {
        cell->params[param] = Property(3 - init_val, 2);
    }
}

void GateMateImpl::update_cpe_mux(CellInfo *cell, IdString port, IdString param, int bit)
{
    // Mux inversion data is contained in other CPE half
    Loc l = ctx->getBelLocation(cell->bel);
    CellInfo *cell_l = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(l.x, l.y, 1)));
    unsigned init_val = int_or_default(cell_l->params, param);
    bool invert = need_inversion(cell, port);
    if (invert) {
        int old = (init_val >> bit) & 1;
        int val = (init_val & (~(1 << bit) & 0xf)) | ((!old) << bit);
        cell_l->params[param] = Property(val, 4);
    }
}

void GateMateImpl::rename_param(CellInfo *cell, IdString name, IdString new_name, int width)
{
    if (cell->params.count(name)) {
        cell->params[new_name] = Property(int_or_default(cell->params, name, 0), width);
        cell->unsetParam(name);
    }
}

void GateMateImpl::prePlace() { assign_cell_info(); }

void GateMateImpl::postPlace()
{
    ctx->assignArchInfo();
    for (auto &cell : ctx->cells) {
        if (cell.second->type.in(id_CPE_HALF, id_CPE_HALF_U, id_CPE_HALF_L)) {
            Loc l = ctx->getBelLocation(cell.second->bel);
            if (l.z == 0) { // CPE_HALF_U
                if (cell.second->params.count(id_C_O) && int_or_default(cell.second->params, id_C_O, 0) == 0)
                    cell.second->params[id_C_2D_IN] = Property(1, 1);
                rename_param(cell.second.get(), id_C_O, id_C_O2, 2);
                rename_param(cell.second.get(), id_C_RAM_I, id_C_RAM_I2, 1);
                rename_param(cell.second.get(), id_C_RAM_O, id_C_RAM_O2, 1);
                cell.second->type = id_CPE_HALF_U;
            } else { // CPE_HALF_L
                if (!cell.second->params.count(id_INIT_L20))
                    cell.second->params[id_INIT_L20] = Property(0b1100, 4);
                rename_param(cell.second.get(), id_C_O, id_C_O1, 2);
                rename_param(cell.second.get(), id_INIT_L00, id_INIT_L02, 4);
                rename_param(cell.second.get(), id_INIT_L01, id_INIT_L03, 4);
                rename_param(cell.second.get(), id_INIT_L10, id_INIT_L11, 4);
                rename_param(cell.second.get(), id_C_RAM_I, id_C_RAM_I1, 1);
                rename_param(cell.second.get(), id_C_RAM_O, id_C_RAM_O1, 1);
                cell.second->type = id_CPE_HALF_L;
            }
        }
    }
}

void GateMateImpl::preRoute() { route_clock(); }

void GateMateImpl::postRoute()
{
    ctx->assignArchInfo();
    // Update configuration bits based on signal inversion
    for (auto &cell : ctx->cells) {
        if (cell.second->type.in(id_CPE_HALF_U)) {
            uint8_t func = int_or_default(cell.second->params, id_C_FUNCTION, 0);
            if (func != C_MX4) {
                update_cpe_lt(cell.second.get(), id_IN1, id_INIT_L00);
                update_cpe_lt(cell.second.get(), id_IN2, id_INIT_L00);
                update_cpe_lt(cell.second.get(), id_IN3, id_INIT_L01);
                update_cpe_lt(cell.second.get(), id_IN4, id_INIT_L01);
            } else {
                update_cpe_mux(cell.second.get(), id_IN1, id_INIT_L11, 0);
                update_cpe_mux(cell.second.get(), id_IN2, id_INIT_L11, 1);
                update_cpe_mux(cell.second.get(), id_IN3, id_INIT_L11, 2);
                update_cpe_mux(cell.second.get(), id_IN4, id_INIT_L11, 3);
            }
        }
        if (cell.second->type.in(id_CPE_HALF_L)) {
            update_cpe_lt(cell.second.get(), id_IN1, id_INIT_L02);
            update_cpe_lt(cell.second.get(), id_IN2, id_INIT_L02);
            update_cpe_lt(cell.second.get(), id_IN3, id_INIT_L03);
            update_cpe_lt(cell.second.get(), id_IN4, id_INIT_L03);
        }
        if (cell.second->type.in(id_CPE_HALF_U, id_CPE_HALF_L)) {
            update_cpe_inv(cell.second.get(), id_CLK, id_C_CPE_CLK);
            update_cpe_inv(cell.second.get(), id_EN, id_C_CPE_EN);
            bool set = int_or_default(cell.second->params, id_C_EN_SR, 0) == 1;
            if (set)
                update_cpe_inv(cell.second.get(), id_SR, id_C_CPE_SET);
            else
                update_cpe_inv(cell.second.get(), id_SR, id_C_CPE_RES);
        }
    }
    // Sanity check
    for (auto &c : ctx->cells) {
        CellInfo *cell = c.second.get();
        if (!cell->type.in(id_CPE_HALF_U, id_CPE_HALF_L)) {
            for (auto port : cell->ports) {
                if (need_inversion(cell, port.first)) {
                    log_error("Unhandled cell '%s' of type '%s' port '%s'\n", cell->name.c_str(ctx),
                              cell->type.c_str(ctx), port.first.c_str(ctx));
                }
            }
        }
    }
    print_utilisation(ctx);

    const ArchArgs &args = ctx->args;
    if (args.options.count("out")) {
        write_bitstream(args.device, args.options.at("out"));
    }
}

void GateMateImpl::configurePlacerHeap(PlacerHeapCfg &cfg)
{
    cfg.beta = 0.5;
    cfg.placeAllAtOnce = true;
}

void GateMateImpl::assign_cell_info()
{
    fast_cell_info.resize(ctx->cells.size());
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        auto &fc = fast_cell_info.at(ci->flat_index);
        if (ci->type.in(id_CPE_HALF, id_CPE_HALF_U, id_CPE_HALF_L)) {
            fc.signal_used = int_or_default(ci->params, id_C_O, -1);
            fc.ff_en = ci->getPort(id_EN);
            fc.ff_clk = ci->getPort(id_CLK);
            fc.ff_sr = ci->getPort(id_SR);
            fc.ff_config = 0;
            if (fc.signal_used == 0) {
                fc.ff_config |= int_or_default(ci->params, id_C_CPE_EN, 0);
                fc.ff_config <<= 2;
                fc.ff_config |= int_or_default(ci->params, id_C_CPE_CLK, 0);
                fc.ff_config <<= 2;
                fc.ff_config |= int_or_default(ci->params, id_C_CPE_RES, 0);
                fc.ff_config <<= 2;
                fc.ff_config |= int_or_default(ci->params, id_C_CPE_SET, 0);
                fc.ff_config <<= 2;
                fc.ff_config |= int_or_default(ci->params, id_C_EN_SR, 0);
                fc.ff_config <<= 1;
                fc.ff_config |= int_or_default(ci->params, id_C_L_D, 0);
                fc.ff_config <<= 1;
                fc.ff_config |= int_or_default(ci->params, id_FF_INIT, 0);
                fc.dff_used = true;
            }
        }
    }
}

// Bel bucket functions
IdString GateMateImpl::getBelBucketForCellType(IdString cell_type) const
{
    if (cell_type.in(id_CC_IBUF, id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF, id_CC_LVDS_IBUF, id_CC_LVDS_TOBUF,
                     id_CC_LVDS_OBUF, id_CC_LVDS_IOBUF))
        return id_GPIO;
    else if (cell_type.in(id_CPE_HALF_U, id_CPE_HALF_L, id_CPE_HALF))
        return id_CPE_HALF;
    else
        return cell_type;
}

BelBucketId GateMateImpl::getBelBucketForBel(BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type.in(id_CPE_HALF_U, id_CPE_HALF_L))
        return id_CPE_HALF;
    return bel_type;
}

bool GateMateImpl::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type == id_GPIO)
        return cell_type.in(id_CC_IBUF, id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF, id_CC_LVDS_IBUF, id_CC_LVDS_TOBUF,
                            id_CC_LVDS_OBUF, id_CC_LVDS_IOBUF);
    else if (bel_type == id_CPE_HALF_U)
        return cell_type.in(id_CPE_HALF_U, id_CPE_HALF);
    else if (bel_type == id_CPE_HALF_L)
        return cell_type.in(id_CPE_HALF_L, id_CPE_HALF);
    else
        return (bel_type == cell_type);
}

bool GateMateImpl::isPipInverting(PipId pip) const
{
    const auto &extra_data =
            *reinterpret_cast<const GateMatePipExtraDataPOD *>(chip_pip_info(ctx->chip_info, pip).extra_data.get());
    return extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_INVERT);
}

const GateMateBelExtraDataPOD *GateMateImpl::bel_extra_data(BelId bel) const
{
    return reinterpret_cast<const GateMateBelExtraDataPOD *>(chip_bel_info(ctx->chip_info, bel).extra_data.get());
}

struct GateMateArch : HimbaechelArch
{
    GateMateArch() : HimbaechelArch("gatemate") {};
    bool match_device(const std::string &device) override
    {
        return device.size() > 6 && device.substr(0, 6) == "CCGM1A";
    }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device, const dict<std::string, std::string> &args)
    {
        return std::make_unique<GateMateImpl>();
    }
} gateMateArch;

NEXTPNR_NAMESPACE_END

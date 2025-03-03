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
    arch->set_speed_grade("DEFAULT");
}

void GateMateImpl::init(Context *ctx) { HimbaechelAPI::init(ctx); }

delay_t GateMateImpl::estimateDelay(WireId src, WireId dst) const
{
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);

    return 100 * (std::abs(dx - sx) / 4 + std::abs(dy - sy) / 4 + 2);
}

bool GateMateImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    CellInfo *cell = ctx->getBoundBelCell(bel);
    if (cell == nullptr) {
        return true;
    }
    if (ctx->getBelType(bel).in(id_CPE_HALF, id_CPE_HALF_L, id_CPE_HALF_U)) {
        if (blocked_bels.count(bel))
            return false;
        Loc loc = ctx->getBelLocation(bel);
        const CellInfo *adj_half = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, loc.z==1 ? 0 : 1)));
        if (adj_half) {
            const auto &half_data = fast_cell_info.at(cell->flat_index);
            if(half_data.dff_used) {
                const auto &adj_data = fast_cell_info.at(adj_half->flat_index);
                if(adj_data.dff_used) {
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


bool GateMateImpl::getChildPlacement(const BaseClusterInfo *cluster, Loc root_loc,
                                    std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    //Loc prev = root_loc;
    for (auto child : cluster->constr_children) {
        Loc child_loc;
        switch (child->constr_z) {
/*        case PLACE_CY_CHAIN:
            child_loc = getNextLocInCYChain(prev);
            prev = child_loc;
            break;
*/
        default:
            child_loc.x = root_loc.x + child->constr_x;
            child_loc.y = root_loc.y + child->constr_y;
            child_loc.z = child->constr_abs_z ? child->constr_z : (root_loc.z + child->constr_z);
        }
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

void updateLUT(Context *ctx, CellInfo *cell, IdString port, IdString init)
{
    if (cell->params.count(init) == 0) return;
    unsigned init_val = int_or_default(cell->params, init);
    WireId pin_wire = ctx->getBelPinWire(cell->bel, port);
    for (PipId pip : ctx->getPipsUphill(pin_wire)) {
        if (!ctx->getBoundPipNet(pip))
            continue;
        const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(
                chip_pip_info(ctx->chip_info, pip).extra_data.get());
        if (!extra_data.name)
            continue;
        if (extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_CPE_INV)) {
            if (port.in(id_IN1,id_IN3))
                init_val = (init_val & 0b1010) >> 1 | (init_val & 0b0101) << 1;
            else
                init_val = (init_val & 0b0011) << 2 | (init_val & 0b1100) >> 2;
            cell->params[init] = Property(init_val, 4);
        }
    }
}

void updateINV(Context *ctx, CellInfo *cell, IdString port, IdString param)
{
    if (cell->params.count(param) == 0) return;
    unsigned init_val = int_or_default(cell->params, param);
    WireId pin_wire = ctx->getBelPinWire(cell->bel, port);
    for (PipId pip : ctx->getPipsUphill(pin_wire)) {
        if (!ctx->getBoundPipNet(pip))
            continue;
        const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(
                chip_pip_info(ctx->chip_info, pip).extra_data.get());
        if (!extra_data.name)
            continue;
        if (extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_CPE_INV)) {
            cell->params[param] = Property(3 - init_val, 2);
        }
    }
}

void updateMUX_INV(Context *ctx, CellInfo *cell, IdString port, IdString param, int bit)
{
    // Mux inversion data is contained in other CPE half
    Loc l = ctx->getBelLocation(cell->bel);
    CellInfo *cell_l = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(l.x,l.y,1)));
    unsigned init_val = int_or_default(cell_l->params, param);
    WireId pin_wire = ctx->getBelPinWire(cell->bel, port);
    for (PipId pip : ctx->getPipsUphill(pin_wire)) {
        if (!ctx->getBoundPipNet(pip))
            continue;
        const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(
                chip_pip_info(ctx->chip_info, pip).extra_data.get());
        if (!extra_data.name)
            continue;
        if (extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_CPE_INV)) {
            int old = (init_val >> bit) & 1;
            int val = (init_val & (~(1 << bit) & 0xf)) | ((!old) << bit);
            cell_l->params[param] = Property(val, 4);
        }
    }
}

void GateMateImpl::postRoute()
{
    ctx->assignArchInfo();
    for (auto &cell : ctx->cells) {
        if (cell.second->type.in(id_CPE_HALF, id_CPE_HALF_U, id_CPE_HALF_L)) {
            Loc l = ctx->getBelLocation(cell.second->bel);
            if (l.z==0) { // CPE_HALF_U
                if(cell.second->params.count(id_C_O)) {
                    int mode = int_or_default(cell.second->params, id_C_O, 0);
                    cell.second->params[id_C_O2] = Property(mode, 2);
                    cell.second->unsetParam(id_C_O);
                    if (mode==0)
                        cell.second->params[id_C_2D_IN] = Property(1, 1);
                }
                cell.second->type = id_CPE_HALF_U;
            } else {// CPE_HALF_L
                if(!cell.second->params.count(id_INIT_L20))
                    cell.second->params[id_INIT_L20] = Property(0b1100, 4);
                if(cell.second->params.count(id_C_O)) {
                    cell.second->params[id_C_O1] = Property(int_or_default(cell.second->params, id_C_O, 0), 2);
                    cell.second->unsetParam(id_C_O);
                }
                if(cell.second->params.count(id_INIT_L00)) {
                    cell.second->params[id_INIT_L02] = Property(int_or_default(cell.second->params, id_INIT_L00, 0), 4);
                    cell.second->unsetParam(id_INIT_L00);
                }
                if(cell.second->params.count(id_INIT_L01)) {
                    cell.second->params[id_INIT_L03] = Property(int_or_default(cell.second->params, id_INIT_L01, 0), 4);
                    cell.second->unsetParam(id_INIT_L01);
                }
                if(cell.second->params.count(id_INIT_L10)) {
                    cell.second->params[id_INIT_L11] = Property(int_or_default(cell.second->params, id_INIT_L10, 0), 4);
                    cell.second->unsetParam(id_INIT_L10);
                }
                cell.second->type = id_CPE_HALF_L;
            }
        }
    }
    log_break();
    log_info("Resources spent on routing:\n");
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        for (auto &w : ni->wires) {
            if (w.second.pip != PipId()) {
                const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(
                        chip_pip_info(ctx->chip_info, w.second.pip).extra_data.get());
                if (!extra_data.name)
                    continue;
                if (extra_data.type == PipExtra::PIP_EXTRA_CPE) {
                    Loc l = ctx->getPipLocation(w.second.pip);
                    BelId bel_u = ctx->getBelByLocation(Loc(l.x,l.y,0));
                    BelId bel_l = ctx->getBelByLocation(Loc(l.x,l.y,1));
                    if (IdString(extra_data.name) == id_RAM_O2) {
                        if (ctx->getBoundBelCell(bel_u))
                            log_error("Issue adding pass trough signal.\n");
                        CellInfo *cell = ctx->createCell(ctx->id(ctx->nameOfBel(bel_u)), id_CPE_HALF_U);
                        ctx->bindBel(bel_u, cell, PlaceStrength::STRENGTH_FIXED);
                        // Propagate IN1 to O2 and RAM_O2
                        cell->params[id_INIT_L00] = Property(0b1010, 4);
                        cell->params[id_INIT_L10] = Property(0b1010, 4);
                        cell->params[id_C_O2] = Property(0b11, 2);
                        cell->params[id_C_RAM_O2] = Property(1, 1);
                    } else if (IdString(extra_data.name) == id_RAM_O1) {
                        if (ctx->getBoundBelCell(bel_l))
                            log_error("Issue adding pass trough signal.\n");
                        CellInfo *cell = ctx->createCell(ctx->id(ctx->nameOfBel(bel_l)), id_CPE_HALF_L);
                        ctx->bindBel(bel_l, cell, PlaceStrength::STRENGTH_FIXED);
                        // Propagate IN1 to O1 and RAM_O1
                        cell->params[id_INIT_L02] = Property(0b1010, 4);
                        cell->params[id_INIT_L11] = Property(0b1010, 4);
                        cell->params[id_INIT_L20] = Property(0b1100, 4);
                        cell->params[id_C_O1] = Property(0b11, 2);
                        cell->params[id_C_RAM_O1] = Property(1, 1);
                    } else if (IdString(extra_data.name) == id_RAM_I1) {
                        if (ctx->getBoundBelCell(bel_l))
                            log_error("Issue adding pass trough signal.\n");
                        CellInfo *cell = ctx->createCell(ctx->id(ctx->nameOfBel(bel_l)), id_CPE_HALF_L);
                        ctx->bindBel(bel_l, cell, PlaceStrength::STRENGTH_FIXED);
                        cell->params[id_C_RAM_I1] = Property(1, 1);
                    } else if (IdString(extra_data.name) == id_RAM_I2) {
                        if (ctx->getBoundBelCell(bel_u))
                            log_error("Issue adding pass trough signal.\n");
                        CellInfo *cell = ctx->createCell(ctx->id(ctx->nameOfBel(bel_u)), id_CPE_HALF_U);
                        ctx->bindBel(bel_u, cell, PlaceStrength::STRENGTH_FIXED);
                        cell->params[id_C_RAM_I2] = Property(1, 1);
                    } else {
                        log_error("Issue adding pass trough signal for %s.\n",IdString(extra_data.name).c_str(ctx));
                    }
                }
            }
        }
    }
    for (auto &cell : ctx->cells) {
        if (cell.second->type.in(id_CPE_HALF_U)) {
            uint8_t func = int_or_default(cell.second->params, id_C_FUNCTION, 0);
            cell.second->unsetParam(id_C_FUNCTION);
            if (func != C_MX4) {
                updateLUT(ctx, cell.second.get(), id_IN1, id_INIT_L00);
                updateLUT(ctx, cell.second.get(), id_IN2, id_INIT_L00);
                updateLUT(ctx, cell.second.get(), id_IN3, id_INIT_L01);
                updateLUT(ctx, cell.second.get(), id_IN4, id_INIT_L01);
            } else {
                updateMUX_INV(ctx, cell.second.get(), id_IN1, id_INIT_L11, 0);
                updateMUX_INV(ctx, cell.second.get(), id_IN2, id_INIT_L11, 1);
                updateMUX_INV(ctx, cell.second.get(), id_IN3, id_INIT_L11, 2);
                updateMUX_INV(ctx, cell.second.get(), id_IN4, id_INIT_L11, 3);
            }
        }
        if (cell.second->type.in(id_CPE_HALF_L)) {
            updateLUT(ctx, cell.second.get(), id_IN1, id_INIT_L02);
            updateLUT(ctx, cell.second.get(), id_IN2, id_INIT_L02);
            updateLUT(ctx, cell.second.get(), id_IN3, id_INIT_L03);
            updateLUT(ctx, cell.second.get(), id_IN4, id_INIT_L03);
        }
        if (cell.second->type.in(id_CPE_HALF_U, id_CPE_HALF_L)) {
            updateINV(ctx, cell.second.get(), id_CLK, id_C_CPE_CLK);
            updateINV(ctx, cell.second.get(), id_EN,  id_C_CPE_EN);
            bool set = int_or_default(cell.second->params, id_C_EN_SR, 0) == 1;
            if (set)
                updateINV(ctx, cell.second.get(), id_SR, id_C_CPE_SET);
            else
                updateINV(ctx, cell.second.get(), id_SR, id_C_CPE_RES);
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
    //cfg.beta = 0.5;
    cfg.placeAllAtOnce = true;
}

void GateMateImpl::prePlace()
{
    assign_cell_info();
}

void GateMateImpl::postPlace()
{
    ctx->assignArchInfo();
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
            if (fc.signal_used==0) {
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

void GateMateImpl::setupArchContext()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("read")) {
        if (!read_bitstream(args.device, args.options.at("read")))
            log_error("Loading bitstream failed.\n");
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

const GateMateTileExtraDataPOD *GateMateImpl::tile_extra_data(int tile) const
{
    return reinterpret_cast<const GateMateTileExtraDataPOD *>(ctx->chip_info->tile_insts[tile].extra_data.get());
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

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
    arch->set_speed_grade("DEFAULT");
    dies = std::stoi(args.device.substr(6));
}

const GateMateTileExtraDataPOD *GateMateImpl::tile_extra_data(int tile) const
{
    return reinterpret_cast<const GateMateTileExtraDataPOD *>(ctx->chip_info->tile_insts[tile].extra_data.get());
}

void GateMateImpl::init(Context *ctx)
{
    HimbaechelAPI::init(ctx);
    for (const auto &pad : ctx->package_info->pads) {
        available_pads.emplace(IdString(pad.package_pin));
        BelId bel = ctx->getBelByName(IdStringList::concat(IdString(pad.tile), IdString(pad.bel)));
        bel_to_pad.emplace(bel, &pad);
        locations.emplace(std::make_pair(IdString(pad.package_pin), tile_extra_data(bel.tile)->die),
                          ctx->getBelLocation(bel));
    }
    available_pads.emplace(ctx->id("SER_CLK"));
    available_pads.emplace(ctx->id("SER_CLK_N"));
    for (auto bel : ctx->getBels()) {
        auto *ptr = bel_extra_data(bel);
        std::map<IdString, const GateMateBelPinConstraintPOD *> pins;
        for (const auto &p : ptr->constraints)
            pins.emplace(IdString(p.name), &p);
        pin_to_constr.emplace(bel, pins);
        if (ctx->getBelType(bel).in(id_CLKIN, id_GLBOUT, id_PLL, id_USR_RSTN, id_CFG_CTRL, id_SERDES)) {
            locations.emplace(std::make_pair(ctx->getBelName(bel)[1], tile_extra_data(bel.tile)->die),
                              ctx->getBelLocation(bel));
        }
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

    // TODO: remove when placemente per die is better handled
    if (cell->belStrength != PlaceStrength::STRENGTH_FIXED && tile_extra_data(bel.tile)->die != preferred_die)
        return false;

    if (getBelBucketForCellType(ctx->getBelType(bel)) == id_CPE_FF) {
        Loc loc = ctx->getBelLocation(bel);
        const CellInfo *adj_half = ctx->getBoundBelCell(
                ctx->getBelByLocation(Loc(loc.x, loc.y, loc.z == CPE_FF_L_Z ? CPE_FF_U_Z : CPE_FF_L_Z)));
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
            log_error("Constrain info not available for pin '%s'.\n", id.c_str(ctx));
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

void GateMateImpl::prePlace() { assign_cell_info(); }

void GateMateImpl::postPlace() { repack(); }

void GateMateImpl::preRoute()
{
    ctx->assignArchInfo();
    route_clock();
}

void GateMateImpl::postRoute()
{
    ctx->assignArchInfo();

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

int GateMateImpl::get_dff_config(CellInfo *dff) const
{
    int val = 0;
    val |= int_or_default(dff->params, id_C_CPE_EN, 0);
    val <<= 2;
    val |= int_or_default(dff->params, id_C_CPE_CLK, 0);
    val <<= 2;
    val |= int_or_default(dff->params, id_C_CPE_RES, 0);
    val <<= 2;
    val |= int_or_default(dff->params, id_C_CPE_SET, 0);
    val <<= 2;
    val |= int_or_default(dff->params, id_C_EN_SR, 0);
    val <<= 1;
    val |= int_or_default(dff->params, id_C_L_D, 0);
    val <<= 1;
    val |= int_or_default(dff->params, id_FF_INIT, 0);
    return val;
}

void GateMateImpl::assign_cell_info()
{
    fast_cell_info.resize(ctx->cells.size());
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        auto &fc = fast_cell_info.at(ci->flat_index);
        if (getBelBucketForCellType(ci->type) == id_CPE_FF) {
            fc.ff_en = ci->getPort(id_EN);
            fc.ff_clk = ci->getPort(id_CLK);
            fc.ff_sr = ci->getPort(id_SR);
            fc.ff_config = get_dff_config(ci);
            fc.dff_used = true;
        }
    }
}

// Bel bucket functions
IdString GateMateImpl::getBelBucketForCellType(IdString cell_type) const
{
    if (cell_type.in(id_CPE_IBUF, id_CPE_OBUF, id_CPE_TOBUF, id_CPE_IOBUF, id_CPE_LVDS_IBUF, id_CPE_LVDS_TOBUF,
                     id_CPE_LVDS_OBUF, id_CPE_LVDS_IOBUF))
        return id_GPIO;
    else if (cell_type.in(id_CPE_LT_U, id_CPE_LT_L, id_CPE_LT, id_CPE_L2T4))
        return id_CPE_LT;
    else if (cell_type.in(id_CPE_FF_U, id_CPE_FF_L, id_CPE_FF, id_CPE_LATCH))
        return id_CPE_FF;
    else if (cell_type.in(id_CPE_RAMIO, id_CPE_RAMI, id_CPE_RAMO))
        return id_CPE_RAMIO;
    else
        return cell_type;
}

BelBucketId GateMateImpl::getBelBucketForBel(BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type.in(id_CPE_LT_U, id_CPE_LT_L))
        return id_CPE_LT;
    else if (bel_type.in(id_CPE_FF_U, id_CPE_FF_L))
        return id_CPE_FF;
    else if (bel_type.in(id_CPE_RAMIO_U, id_CPE_RAMIO_L))
        return id_CPE_RAMIO;
    return bel_type;
}

bool GateMateImpl::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type == id_GPIO)
        return cell_type.in(id_CPE_IBUF, id_CPE_OBUF, id_CPE_TOBUF, id_CPE_IOBUF, id_CPE_LVDS_IBUF, id_CPE_LVDS_TOBUF,
                            id_CPE_LVDS_OBUF, id_CPE_LVDS_IOBUF);
    else if (bel_type == id_CPE_LT_U)
        return cell_type.in(id_CPE_LT_U, id_CPE_LT, id_CPE_L2T4, id_CPE_DUMMY);
    else if (bel_type == id_CPE_LT_L)
        return cell_type.in(id_CPE_LT_L, id_CPE_LT, id_CPE_L2T4, id_CPE_DUMMY);
    else if (bel_type == id_CPE_FF_U)
        return cell_type.in(id_CPE_FF_U, id_CPE_FF, id_CPE_LATCH);
    else if (bel_type == id_CPE_FF_L)
        return cell_type.in(id_CPE_FF_L, id_CPE_FF, id_CPE_LATCH);
    else if (bel_type.in(id_CPE_RAMIO_U, id_CPE_RAMIO_L))
        return cell_type.in(id_CPE_RAMIO, id_CPE_RAMI, id_CPE_RAMO);
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
    std::unique_ptr<HimbaechelAPI> create(const std::string &device,
                                          const dict<std::string, std::string> &args) override
    {
        return std::make_unique<GateMateImpl>();
    }
} gateMateArch;

NEXTPNR_NAMESPACE_END

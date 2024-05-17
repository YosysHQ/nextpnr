/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  Lofty <lofty@yosyshq.com>
 *  Copyright (C) 2023  Miodrag Milanovic <micko@yosyshq.com>
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
#include <fstream>

#include "himbaechel_api.h"
#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "extra_data.h"
#include "placer_heap.h"

#include "himbaechel_helpers.h"

#include "ng_ultra.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/ng-ultra/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

NgUltraImpl::~NgUltraImpl(){};

void NgUltraImpl::init_database(Arch *arch)
{
    init_uarch_constids(arch);
    arch->load_chipdb("ng-ultra/ng-ultra.bin");
    arch->set_package("FF-1760");
    arch->set_speed_grade("DEFAULT");
}

void NgUltraImpl::init(Context *ctx)
{
    HimbaechelAPI::init(ctx);
    for (auto bel : ctx->getBels()) {
        if (ctx->getBelType(bel) == id_IOM) {
            std::deque<BelId> wfgs;
            IdString bank = tile_name_id(bel.tile);
            iom_bels.emplace(bank,bel);
        } else if (ctx->getBelType(bel) == id_IOTP) {
            if (ctx->getBelName(bel)[1] == ctx->id("D08P_CLK.IOTP")) {
                global_capable_bels.emplace(bel,id_P17RI);
            } else if (ctx->getBelName(bel)[1] == ctx->id("D09P_CLK.IOTP")) {
                global_capable_bels.emplace(bel,id_P19RI);
            }
        }
    }
}

const NGUltraTileInstExtraDataPOD *NgUltraImpl::tile_extra_data(int tile) const
{
    return reinterpret_cast<const NGUltraTileInstExtraDataPOD *>(ctx->chip_info->tile_insts[tile].extra_data.get());
}

IdString NgUltraImpl::tile_name_id(int tile) const
{
    const auto &data = *tile_extra_data(tile);
    return IdString(data.name);
}

std::string NgUltraImpl::tile_name(int tile) const
{
    return stringf("%s", tile_name_id(tile).c_str(ctx));
}

int NgUltraImpl::tile_lobe(int tile) const
{
    const auto &data = *tile_extra_data(tile);
    return data.lobe;
}

void NgUltraImpl::preRoute()
{
    route_clocks();
}

bool NgUltraImpl::get_mux_data(BelId bel, IdString port, uint8_t *value)
{
    return get_mux_data(ctx->getBelPinWire(bel, port), value);
}

bool NgUltraImpl::get_mux_data(WireId wire, uint8_t *value)
{
    for (PipId pip : ctx->getPipsUphill(wire)) {
        if (!ctx->getBoundPipNet(pip))
            continue;
        const auto &pip_data = chip_pip_info(ctx->chip_info, pip);
        const auto &extra_data = *reinterpret_cast<const NGUltraPipExtraDataPOD *>(pip_data.extra_data.get());
        if (!extra_data.name) continue;
        if (extra_data.type == PipExtra::PIP_EXTRA_MUX) {
            *value = extra_data.input;
            return true;
        }
    }
    return false;
}

void NgUltraImpl::postRoute()
{
    ctx->assignArchInfo();
    log_break();
    log_info("Resources spent on routing:\n");
    int dff_bypass = 0, lut_bypass = 0, fe_new = 0, wfg_bypass = 0, gck_bypass = 0;
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        for (auto &w : ni->wires) {
            if (w.second.pip != PipId()) {
                const auto &pip_data = chip_pip_info(ctx->chip_info, w.second.pip);
                const auto &extra_data = *reinterpret_cast<const NGUltraPipExtraDataPOD *>(pip_data.extra_data.get());
                if (!extra_data.name) continue;
                if (extra_data.type == PipExtra::PIP_EXTRA_BYPASS) {
                    IdStringList id = ctx->getPipName(w.second.pip);
                    BelId bel = ctx->getBelByName(IdStringList::concat(id[0], IdString(extra_data.name)));
                    IdString type = ctx->getBelType(bel);
                    if (!ctx->getBoundBelCell(bel)) {
                        CellInfo *cell = ctx->createCell(ctx->id(ctx->nameOfBel(bel)), type);
                        ctx->bindBel(bel,cell,PlaceStrength::STRENGTH_FIXED);
                        if (type==id_BEYOND_FE) fe_new++;
                    }
                    CellInfo *cell = ctx->getBoundBelCell(bel);
                    switch(type.index) {
                        case id_BEYOND_FE.index : 
                                           if (extra_data.input==0) {
                                                dff_bypass++;
                                                // set bypass mode for DFF
                                                cell->setParam(ctx->id("type"), Property("BFF"));
                                                cell->params[id_dff_used] = Property(1,1);
                                            } else {
                                                lut_bypass++;
                                                cell->params[id_lut_used] = Property(1,1);
                                                cell->params[id_lut_table] = Property(0xaaaa, 16);
                                            }
                                            break;
                        case id_WFG.index : wfg_bypass++;
                                            cell->setParam(ctx->id("type"), Property("WFB"));
                                            break;
                        case id_GCK.index : gck_bypass++; 
                                            cell->setParam(ctx->id("std_mode"), extra_data.input == 0 ? Property("BYPASS") : Property("CSC"));
                                            break;
                        default:
                            log_error("Unmaped bel type '%s' for routing\n",type.c_str(ctx));
                    }
                }
            }
        }
    }
    log_info("    %6d DFFs used in bypass mode (BFF)\n", dff_bypass);
    log_info("    %6d LUTs used in bypass mode\n", lut_bypass);
    log_info("    %6d newly allocated FEs\n", fe_new);
    log_info("    %6d WFGs used as WFB\n", wfg_bypass);
    log_info("    %6d GCK\n", gck_bypass);

    // Handle LUT permutation
    for (auto &cell : ctx->cells) {
        if (cell.second->type == id_BEYOND_FE) {
            // if LUT part used
            if (cell.second->params.count(id_lut_table) != 0) {
                std::array<std::vector<unsigned>, 4> phys_to_log;
                unsigned orig_init = int_or_default(cell.second->params, id_lut_table);
                const std::array<IdString, 4> ports{id_I1, id_I2, id_I3, id_I4};
                for (unsigned i = 0; i < 4; i++) {
                    WireId pin_wire = ctx->getBelPinWire(cell.second->bel, ports[i]);
                    for (PipId pip : ctx->getPipsUphill(pin_wire)) {
                        if (!ctx->getBoundPipNet(pip))
                            continue;
                        const auto &pip_data = chip_pip_info(ctx->chip_info, pip);
                        const auto &extra_data = *reinterpret_cast<const NGUltraPipExtraDataPOD *>(pip_data.extra_data.get());
                        if (!extra_data.name) continue;
                        if (extra_data.type == PipExtra::PIP_EXTRA_LUT_PERMUTATION) {
                            NPNR_ASSERT(extra_data.output == i);
                            phys_to_log[extra_data.input].push_back(i);
                        }
                    }
                }
                unsigned permuted_init = 0;
                for (unsigned i = 0; i < 16; i++) {
                    unsigned log_idx = 0;
                    for (unsigned j = 0; j < 4; j++) {
                        if ((i >> j) & 0x1) {
                            for (auto log_pin : phys_to_log[j])
                                log_idx |= (1 << log_pin);
                        }
                    }
                    if ((orig_init >> log_idx) & 0x1)
                        permuted_init |= (1 << i);
                }
                cell.second->params[id_lut_table] = Property(permuted_init, 16);
            }
        }
    }

    print_utilisation(ctx);
    const ArchArgs &args = ctx->args;
    if (args.options.count("bit")) {
        write_bitstream_json(args.options.at("bit"));
    }
}

void NgUltraImpl::configurePlacerHeap(PlacerHeapCfg &cfg)
{
    cfg.hpwl_scale_x = 2;
    cfg.hpwl_scale_y = 1;
    cfg.beta = 0.5;
    cfg.placeAllAtOnce = true;
}

namespace {

template <size_t N> bool check_assign_sig(std::array<const NetInfo*, N> &sig_set, const NetInfo *sig)
{
    if (sig == nullptr)
        return true;
    for (size_t i = 0; i < N; i++)
        if (sig_set[i] == sig) {
            return true;
        } else if (sig_set[i] == nullptr) {
            sig_set[i] = sig;
            return true;
        }
    return false;
};

struct SectionFEWorker
{
    std::array<const NetInfo *, 2> clk{};
    std::array<const NetInfo *, 4> reset_load{};
    bool run(const NgUltraImpl *impl,const Context *ctx, BelId bel)
    {
        CellInfo *cell = ctx->getBoundBelCell(bel);
        if (cell == nullptr) {
            return true;
        }
        Loc loc = ctx->getBelLocation(bel);
        for (uint8_t id = 0; id <= BEL_LUT_MAX_Z; id++) {
            const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,id)));
            if (ff == nullptr)
                continue;
            if (!check_assign_sig(reset_load, ff->getPort(id_R)))
                return false;
            if (!check_assign_sig(reset_load, ff->getPort(id_L)))
                return false;
            if (!check_assign_sig(clk, ff->getPort(id_CK)))
                return false;
        }
        return true;
    }
};

}; // namespace

bool NgUltraImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    if (ctx->getBelType(bel) == id_BEYOND_FE) {
        SectionFEWorker worker;
        return worker.run(this, ctx, bel);
    }
    else if (ctx->getBelType(bel).in(id_RF, id_XRF)) {
        CellInfo *cell = ctx->getBoundBelCell(bel);
        if (cell == nullptr) {
            return true;
        }
        Loc loc = ctx->getBelLocation(bel);
        if (loc.z == BEL_XRF_Z) {
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,BEL_RF_Z)))) return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,BEL_RF_Z+1)))) return false;
        } else {
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,BEL_XRF_Z)))) return false;
        }
    }
    else if (ctx->getBelType(bel).in(id_FIFO, id_XFIFO)) {
        CellInfo *cell = ctx->getBoundBelCell(bel);
        if (cell == nullptr) {
            return true;
        }
        Loc loc = ctx->getBelLocation(bel);
        if (loc.z == BEL_XFIFO_Z) {
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,BEL_FIFO_Z)))) return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,BEL_FIFO_Z+1)))) return false;
        } else {
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,BEL_XFIFO_Z)))) return false;
        }
    }
    else if (ctx->getBelType(bel).in(id_CDC, id_XCDC)) {
        CellInfo *cell = ctx->getBoundBelCell(bel);
        if (cell == nullptr) {
            return true;
        }
        Loc loc = ctx->getBelLocation(bel);
        if (loc.z == BEL_XCDC_Z) {
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,BEL_CDC_Z)))) return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,BEL_CDC_Z+1)))) return false;
        } else {
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x,loc.y,BEL_XCDC_Z)))) return false;
        }
    }
    return true;
}

// Bel bucket functions
IdString NgUltraImpl::getBelBucketForCellType(IdString cell_type) const
{
    return cell_type;
}

bool NgUltraImpl::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type == id_IOTP)
        return cell_type.in(id_IOP,id_IOTP);
    else if (bel_type == id_DDFR)
        return cell_type.in(id_DFR,id_DDFR);
    else
        return (bel_type == cell_type);
}

Loc getNextLocInCYChain(Loc loc)
{
    static const std::vector<Loc> map = 
    {
        Loc(0, 1,  0), // S1 0 -> S2 0  CY24->CY1
        Loc(0, 0, -1), // S1 1 -> S1 0  CY23->CY24
        Loc(0, 0, -1), // S1 2 -> S1 1  CY22->CY23
        Loc(0, 0, -1), // S1 3 -> S1 2  CY21->CY22

        Loc(-1, 0,+3), // S5 0 -> S1 1  CY20->CY21
        Loc(0, 0, -1), // S5 1 -> S5 0  CY19->CY20
        Loc(0, 0, -1), // S5 2 -> S5 1  CY18->CY19
        Loc(0, 0, -1), // S5 3 -> S5 2  CY17->CY18

        Loc(-1, 0,+3), // S9 0 -> S5 1  CY16->CY17
        Loc(0, 0, -1), // S9 1 -> S9 0  CY15->CY16
        Loc(0, 0, -1), // S9 2 -> S9 1  CY14->CY15
        Loc(0, 0, -1), // S9 3 -> S9 2  CY13->CY14

        Loc(0, 0, +1), // S2 0 -> S2 1  CY1->CY2
        Loc(0, 0, +1), // S2 1 -> S2 2  CY2->CY3
        Loc(0, 0, +1), // S2 2 -> S2 3  CY3->CY4
        Loc(1, 0, -3), // S2 3 -> S6 0  CY4->CY5

        Loc(0, 0, +1), // S6 0 -> S6 1  CY5->CY6
        Loc(0, 0, +1), // S6 1 -> S6 2  CY6->CY7
        Loc(0, 0, +1), // S6 2 -> S6 3  CY7->CY8
        Loc(1, 0, -3), // S6 3 -> S10 0 CY8->CY9

        Loc(0, 0, +1), // S10 0 -> S10 1 CY9->CY10
        Loc(0, 0, +1), // S10 1 -> S10 2 CY10->CY11
        Loc(0, 0, +1), // S10 2 -> S10 3 CY11->CY12
        Loc(0,-1,  0), // S10 3 -> S9  3 CY12->CY13
    };
    int section = (loc.x % 4 - 1 + 3 * (loc.y % 4)) * 4 + loc.z - BEL_CY_Z;
    Loc result = map.at(section);
    result.x += loc.x;
    result.y += loc.y;
    result.z += loc.z;
    return result;
}

Loc getCYFE(Loc root, int pos)
{
    int p[] = { 2-1, 25-1, 10-1, 17-1 };
    int cy = root.z - BEL_CY_Z;
    Loc result;
    result.x = root.x;
    result.y = root.y;
    result.z = p[pos] + cy * 2;
    return result;
}

Loc getXRFFE(Loc root, int pos)
{
   static const std::vector<Loc> map = 
    {
        Loc(-1, 0, 1),// I/O1
        Loc(-1, 0, 2),// I/O2
        Loc(-1, 0, 5),// I/O3
        Loc(-1, 0, 6),// I/O4
        Loc(-1, 0, 7),// I/O5
        Loc(-1, 0, 9),// I/O6
        Loc(-1, 0, 10),// I/O7
        Loc(-1, 0, 13),// I/O8
        Loc(-1, 0, 14),// I/O9
        Loc(-1, 0, 15),// I/O10
        Loc(-1, 0, 16),// I/O11
        Loc(-1, 0, 17),// I/O12
        Loc(-1, 0, 18),// I/O13
        Loc(-1, 0, 21),// I/O14
        Loc(-1, 0, 24),// I/O15
        Loc(-1, 0, 25),// I/O16
        Loc(-1, 0, 26),// I/O17
        Loc(-1, 0, 29),// I/O18

        Loc(+1, 0, 1),// I/O19
        Loc(+1, 0, 2),// I/O20
        Loc(+1, 0, 5),// I/O21
        Loc(+1, 0, 6),// I/O22
        Loc(+1, 0, 7),// I/O23
        Loc(+1, 0, 9),// I/O24
        Loc(+1, 0, 10),// I/O25
        Loc(+1, 0, 13),// I/O26
        Loc(+1, 0, 14),// I/O27
        Loc(+1, 0, 15),// I/O28
        Loc(+1, 0, 16),// I/O29
        Loc(+1, 0, 17),// I/O30
        Loc(+1, 0, 18),// I/O31
        Loc(+1, 0, 21),// I/O32
        Loc(+1, 0, 24),// I/O33
        Loc(+1, 0, 25),// I/O34
        Loc(+1, 0, 26),// I/O35
        Loc(+1, 0, 29),// I/O36

        Loc(-1, 0, 4),// RA1
        Loc(-1, 0, 12),// RA2
        Loc(-1, 0, 20),// RA3
        Loc(-1, 0, 27),// RA4
        Loc(-1, 0, 31),// RA5

        Loc(+1, 0, 4),// RA6
        Loc(+1, 0, 12),// RA7
        Loc(+1, 0, 20),// RA8
        Loc(+1, 0, 27),// RA9
        Loc(+1, 0, 31),// RA10

        Loc(-1, 0, 3),// WA1
        Loc(-1, 0, 11),// WA2
        Loc(-1, 0, 19),// WA3
        Loc(-1, 0, 23),// WA4
        Loc(-1, 0, 28),// WA5
        
        Loc(+1, 0, 3),// WA6

        Loc(-1, 0, 0),// WE
        Loc(-1, 0, 8),// WEA

    };
 
    Loc result = map.at(pos);
    if (root.z == BEL_XRF_Z) {
        // XRF1
        result.x += root.x;
    } else  {
        // RF1 or RF2
        result.x = root.x + ((root.z == BEL_RF_Z) ? -1 : +1);
    }
    result.y = root.y;
    return result;
}

bool NgUltraImpl::getChildPlacement(const BaseClusterInfo *cluster, Loc root_loc,
                                    std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    Loc prev = root_loc;
    for (auto child : cluster->constr_children) {
        Loc child_loc = if_using_basecluster<Loc>(child, [&](const BaseClusterInfo *child) {
            switch(child->constr_z) {
                case PLACE_CY_CHAIN : { Loc l = getNextLocInCYChain(prev); prev = l; return l; }
                case PLACE_CY_FE1: return getCYFE(root_loc,0);
                case PLACE_CY_FE2: return getCYFE(root_loc,1);
                case PLACE_CY_FE3: return getCYFE(root_loc,2);
                case PLACE_CY_FE4: return getCYFE(root_loc,3);
                case PLACE_XRF_I1:
                case PLACE_XRF_I2:
                case PLACE_XRF_I3:
                case PLACE_XRF_I4:
                case PLACE_XRF_I5:
                case PLACE_XRF_I6:
                case PLACE_XRF_I7:
                case PLACE_XRF_I8:
                case PLACE_XRF_I9:
                case PLACE_XRF_I10:
                case PLACE_XRF_I11:
                case PLACE_XRF_I12:
                case PLACE_XRF_I13:
                case PLACE_XRF_I14:
                case PLACE_XRF_I15:
                case PLACE_XRF_I16:
                case PLACE_XRF_I17:
                case PLACE_XRF_I18:
                case PLACE_XRF_I19:
                case PLACE_XRF_I20:
                case PLACE_XRF_I21:
                case PLACE_XRF_I22:
                case PLACE_XRF_I23:
                case PLACE_XRF_I24:
                case PLACE_XRF_I25:
                case PLACE_XRF_I26:
                case PLACE_XRF_I27:
                case PLACE_XRF_I28:
                case PLACE_XRF_I29:
                case PLACE_XRF_I30:
                case PLACE_XRF_I31:
                case PLACE_XRF_I32:
                case PLACE_XRF_I33:
                case PLACE_XRF_I34:
                case PLACE_XRF_I35:
                case PLACE_XRF_I36:
                case PLACE_XRF_RA1:
                case PLACE_XRF_RA2:
                case PLACE_XRF_RA3:
                case PLACE_XRF_RA4:
                case PLACE_XRF_RA5:
                case PLACE_XRF_RA6:
                case PLACE_XRF_RA7:
                case PLACE_XRF_RA8:
                case PLACE_XRF_RA9:
                case PLACE_XRF_RA10:
                case PLACE_XRF_WA1:
                case PLACE_XRF_WA2:
                case PLACE_XRF_WA3:
                case PLACE_XRF_WA4:
                case PLACE_XRF_WA5:
                case PLACE_XRF_WA6:
                case PLACE_XRF_WE:
                case PLACE_XRF_WEA:
                                    return getXRFFE(root_loc, child->constr_z - PLACE_XRF_I1 );
                default:
                    Loc result;
                    result.x = root_loc.x + child->constr_x;
                    result.y = root_loc.y + child->constr_y;
                    result.z = child->constr_abs_z ? child->constr_z : (root_loc.z + child->constr_z);
                    return result;
            }
        });
        BelId child_bel = ctx->getBelByLocation(child_loc);
        if (child_bel == BelId() || !this->isValidBelForCellType(child->type, child_bel))
            return false;
        placement.emplace_back(child, child_bel);
        bool val = if_using_basecluster<bool>(child, [&](const BaseClusterInfo *child_cluster) -> bool {
            return getChildPlacement(child_cluster, child_loc, placement);
        });
        if (!val) return false;
    }
    return true;
}

bool NgUltraImpl::getClusterPlacement(ClusterId cluster, BelId root_bel,
                                    std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    CellInfo *root_cell = get_cluster_root(ctx, cluster);
    return if_using_basecluster<bool>(root_cell, [&](const BaseClusterInfo *cluster) -> bool {
        placement.clear();
        NPNR_ASSERT(root_bel != BelId());
        Loc root_loc = ctx->getBelLocation(root_bel);
        if (cluster->constr_abs_z) {
            // Coerce root to absolute z constraint
            root_loc.z = cluster->constr_z;
            root_bel = ctx->getBelByLocation(root_loc);
            if (root_bel == BelId() || !this->isValidBelForCellType(root_cell->type, root_bel))
                return false;
        }
        placement.emplace_back(root_cell, root_bel);
        return getChildPlacement(cluster, root_loc, placement);
    });
}

BoundingBox NgUltraImpl::getRouteBoundingBox(WireId src, WireId dst) const
{
    int x0, y0, x1, y1;
    auto expand = [&](int x, int y) {
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    };
    tile_xy(ctx->chip_info, src.tile, x0, y0);
    x1 = x0;
    y1 = y0;
    int dx, dy;
    tile_xy(ctx->chip_info, dst.tile, dx, dy);
    expand(dx, dy);
    // Two TILEs left and up, and one tile right and down
    return {(x0 & 0xfffc) - 8, 
            (y0 & 0xfffc) - 8,
            (x1 & 0xfffc) + 8, 
            (y1 & 0xfffc) + 8};
}

delay_t NgUltraImpl::estimateDelay(WireId src, WireId dst) const
{
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);
    if (sx==dx && sy==dy) {
        // Same sub tile
        return 50;
    } else if (((sx & 0xfffc) == (dx & 0xfffc)) && ((sy & 0xfffc) == (dy & 0xfffc))) {
        // Same "TILE"
        return 200;
    }
    return 500 + 100 * (std::abs(dy - sy)/4 + std::abs(dx - sx)/4);
}


delay_t NgUltraImpl::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    Loc src_loc = ctx->getBelLocation(src_bel), dst_loc = ctx->getBelLocation(dst_bel);
    if (src_loc.x==dst_loc.x && src_loc.y==dst_loc.y) {
        // Same sub tile
        return 50;
    } else if (((src_loc.x & 0xfffc) == (dst_loc.x & 0xfffc)) && ((src_loc.y & 0xfffc) == (dst_loc.y & 0xfffc))) {
        // Same "TILE"
        return 200;
    }
    return 500 + 100 * (std::abs(dst_loc.y - src_loc.y)/4 + std::abs(dst_loc.x - src_loc.x)/4);
}

struct NgUltraArch : HimbaechelArch
{
    NgUltraArch() : HimbaechelArch("ng-ultra"){};
    bool match_device(const std::string &device) override { return device == "NG-ULTRA"; }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device, const dict<std::string, std::string> &args)
    {
        return std::make_unique<NgUltraImpl>();
    }
} ngUltraArch;

NEXTPNR_NAMESPACE_END

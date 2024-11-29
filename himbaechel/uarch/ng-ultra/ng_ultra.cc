/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Beyond Authors.
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
#include <queue>

#include "design_utils.h"
#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "placer_heap.h"
#include "util.h"

#include "himbaechel_helpers.h"

#include "location_map.h"
#include "ng_ultra.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/ng-ultra/constids.inc"
#include "himbaechel_constids.h"
using namespace NEXTPNR_NAMESPACE_PREFIX ng_ultra;

NEXTPNR_NAMESPACE_BEGIN

NgUltraImpl::~NgUltraImpl() {};

void NgUltraImpl::init_database(Arch *arch)
{
    init_uarch_constids(arch);
    arch->load_chipdb("ng-ultra/chipdb-ng-ultra.bin");
    arch->set_package("FF-1760");
    arch->set_speed_grade("DEFAULT");
}

void NgUltraImpl::init(Context *ctx)
{
    HimbaechelAPI::init(ctx);
    for (int i = 1; i <= 8; i++)
        for (int j = 0; j < 20; j++)
            gck_per_lobe[i].push_back(GckConfig(BelId()));
    for (auto bel : ctx->getBels()) {
        if (ctx->getBelType(bel) == id_IOM) {
            std::set<IdString> ckg;
            IdString bank = tile_name_id(bel.tile);
            iom_bels.emplace(bank, bel);
            WireId belpin = ctx->getBelPinWire(bel, id_CKO1);
            for (auto dh : ctx->getPipsDownhill(belpin)) {
                WireId pip_dst = ctx->getPipDstWire(dh);
                for (const auto &item : ctx->getWireBelPins(pip_dst)) {
                    if (boost::contains(ctx->nameOfBel(item.bel), "WFG_C")) {
                        unused_wfg[item.bel] = tile_name_id(item.bel.tile);
                    } else if (boost::contains(ctx->nameOfBel(item.bel), "PLL")) {
                        ckg.emplace(tile_name_id(item.bel.tile));
                        unused_pll[item.bel] = tile_name_id(item.bel.tile);
                    }
                }
            }
            std::pair<IdString, IdString> p;
            p.first = *ckg.begin();
            if (ckg.size() == 2)
                p.second = *(ckg.begin()++);
            bank_to_ckg[bank] = p;
        } else if (ctx->getBelType(bel) == id_IOTP) {
            if (ctx->getBelName(bel)[1] == ctx->id("D08P_CLK.IOTP")) {
                global_capable_bels.emplace(bel, id_P17RI);
            } else if (ctx->getBelName(bel)[1] == ctx->id("D09P_CLK.IOTP")) {
                global_capable_bels.emplace(bel, id_P19RI);
            }
        } else if (ctx->getBelType(bel) == id_GCK) {
            std::string name = ctx->getBelName(bel)[1].c_str(ctx);
            int lobe = std::stoi(name.substr(1, 1));
            int num = std::stoi(name.substr(4, 2).c_str());
            gck_per_lobe[lobe][num - 1] = GckConfig(bel);
        }
        locations.emplace(stringf("%s:%s", tile_name(bel.tile).c_str(), ctx->getBelName(bel)[1].c_str(ctx)), bel);
        Loc loc = ctx->getBelLocation(bel);
        tile_locations.emplace(tile_name(bel.tile).c_str(), Loc(loc.x & 0xfffe, loc.y & 0xfffe, 0));
    }
    for (auto bel : ctx->getBels()) {
        if (ctx->getBelType(bel) == id_DSP) {
            WireId cco = ctx->getBelPinWire(bel, id_CCO);
            WireId cci;
            for (auto dh : ctx->getPipsDownhill(cco)) {
                cci = ctx->getPipDstWire(dh);
            }
            if (cci != WireId()) {
                std::string loc = stringf("%s:%s", tile_name(cci.tile).c_str(), ctx->getWireName(cci)[1].c_str(ctx));
                loc.erase(loc.find(".CCI"));
                BelId dsp_bel = locations[loc];
                dsp_cascade.emplace(dsp_bel, bel);
            }
        }
    }
}

namespace {
// Note: These are per Cell type not Bel type
// Sinks
const dict<IdString, pool<IdString>> fabric_lowskew_sinks = {
        // TILE - DFF
        {id_BEYOND_FE, {id_CK, id_L, id_R}},
        // { id_DFF, { id_CK }},  // This is part of BEYOND_FE
        // TILE - Register file
        {id_RF, {id_WCK}},
        {id_RFSP, {id_WCK}},
        {id_XHRF, {id_WCK1, id_WCK2}},
        {id_XWRF, {id_WCK1, id_WCK2}},
        {id_XPRF, {id_WCK1, id_WCK2}},
        // TILE - CDC
        {id_CDC, {id_CK1, id_CK2}},
        {id_DDE, {id_CK1, id_CK2}},
        {id_TDE, {id_CK1, id_CK2}},
        {id_XCDC, {id_CK1, id_CK2}},
        // TILE - FIFO
        {id_FIFO, {id_RCK, id_WCK}},
        {id_XHFIFO, {id_RCK1, id_RCK2, id_WCK1, id_WCK2}},
        {id_XWFIFO, {id_RCK1, id_RCK2, id_WCK1, id_WCK2}},
        // CGB - RAM
        {id_RAM, {id_ACK, id_BCK}},
        // CGB - DSP
        {id_DSP, {id_CK}},
};

const dict<IdString, pool<IdString>> ring_clock_sinks = {
        // CKG
        {id_PLL, {id_CLK_CAL, id_FBK, id_REF}},
        {id_WFB, {id_ZI}},
        {id_WFG, {id_ZI}}};

const dict<IdString, pool<IdString>> ring_over_tile_clock_sinks = {
        // IOB
        {id_DFR, {id_CK}},
        {id_DDFR, {id_CK, id_CKF}},
};
// IOB
// { id_IOM, { id_ALCK1, id_ALCK2, id_ALCK3, id_CCK, id_FCK1, id_FCK2, id_FDCK,
//             id_LDSCK1, id_LDSCK2, id_LDSCK3, id_SWRX1CK, id_SWRX2CK }},

// HSSL
// { id_CRX, { id_LINK }},
// { id_CTX, { id_LINK }},
// { id_PMA, { id_hssl_clock_i1, id_hssl_clock_i2, id_hssl_clock_i3, id_hssl_clock_i4 },

const dict<IdString, pool<IdString>> tube_clock_sinks = {
        // TUBE
        {id_GCK, {id_SI1, id_SI2}},
};
// Sources
// CKG
const dict<IdString, pool<IdString>> ring_clock_source = {
        {id_IOM, {id_CKO1, id_CKO2}},
        {id_WFB, {id_ZO}},
        {id_WFG, {id_ZO}},
        {id_PLL,
         {id_OSC, id_VCO, id_REFO, id_LDFO, id_CLK_DIV1, id_CLK_DIV2, id_CLK_DIV3, id_CLK_DIV4, id_CLK_DIVD1,
          id_CLK_DIVD2, id_CLK_DIVD3, id_CLK_DIVD4, id_CLK_DIVD5, id_CLK_CAL_DIV}}};
// TUBE
const dict<IdString, pool<IdString>> tube_clock_source = {
        {id_GCK, {id_SO}},
};

}; // namespace

const dict<IdString, pool<IdString>> &NgUltraImpl::get_fabric_lowskew_sinks() { return fabric_lowskew_sinks; }

bool NgUltraImpl::is_fabric_lowskew_sink(const PortRef &ref)
{
    return fabric_lowskew_sinks.count(ref.cell->type) && fabric_lowskew_sinks.at(ref.cell->type).count(ref.port);
}

bool NgUltraImpl::is_ring_clock_sink(const PortRef &ref)
{
    return ring_clock_sinks.count(ref.cell->type) && ring_clock_sinks.at(ref.cell->type).count(ref.port);
}

bool NgUltraImpl::is_ring_over_tile_clock_sink(const PortRef &ref)
{
    return ring_over_tile_clock_sinks.count(ref.cell->type) &&
           ring_over_tile_clock_sinks.at(ref.cell->type).count(ref.port);
}

bool NgUltraImpl::is_tube_clock_sink(const PortRef &ref)
{
    return tube_clock_sinks.count(ref.cell->type) && tube_clock_sinks.at(ref.cell->type).count(ref.port);
}

bool NgUltraImpl::is_ring_clock_source(const PortRef &ref)
{
    return ring_clock_source.count(ref.cell->type) && ring_clock_source.at(ref.cell->type).count(ref.port);
}

bool NgUltraImpl::is_tube_clock_source(const PortRef &ref)
{
    return tube_clock_source.count(ref.cell->type) && tube_clock_source.at(ref.cell->type).count(ref.port);
}

const NGUltraTileInstExtraDataPOD *NgUltraImpl::tile_extra_data(int tile) const
{
    return reinterpret_cast<const NGUltraTileInstExtraDataPOD *>(ctx->chip_info->tile_insts[tile].extra_data.get());
}

const NGUltraPipExtraDataPOD *NgUltraImpl::pip_extra_data(PipId pip) const
{
    return reinterpret_cast<const NGUltraPipExtraDataPOD *>(chip_pip_info(ctx->chip_info, pip).extra_data.get());
}

const NGUltraBelExtraDataPOD *NgUltraImpl::bel_extra_data(BelId bel) const
{
    return reinterpret_cast<const NGUltraBelExtraDataPOD *>(chip_bel_info(ctx->chip_info, bel).extra_data.get());
}

IdString NgUltraImpl::tile_name_id(int tile) const
{
    const auto &data = *tile_extra_data(tile);
    return IdString(data.name);
}

std::string NgUltraImpl::tile_name(int tile) const { return stringf("%s", tile_name_id(tile).c_str(ctx)); }

int NgUltraImpl::tile_lobe(int tile) const
{
    const auto &data = *tile_extra_data(tile);
    return data.lobe;
}

TileTypeExtra NgUltraImpl::tile_type(int tile) const
{
    const auto &data = *tile_extra_data(tile);
    return (TileTypeExtra)data.tile_type;
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
        const auto &extra_data = *pip_extra_data(pip);
        if (!extra_data.name)
            continue;
        if (extra_data.type == PipExtra::PIP_EXTRA_MUX) {
            *value = extra_data.input;
            return true;
        }
    }
    return false;
}

bool NgUltraImpl::update_bff_to_csc(CellInfo *cell, BelId bel, PipId dst_pip)
{
    const auto &extra_data = *bel_extra_data(bel);
    // Check if CSC mode only if FE is capable
    if (extra_data.flags & BEL_EXTRA_FE_CSC) {
        WireId dwire = ctx->getPipDstWire(dst_pip);
        for (PipId pip : ctx->getPipsDownhill(dwire)) {
            if (!ctx->getBoundPipNet(pip))
                continue;
            for (PipId pip2 : ctx->getPipsDownhill(ctx->getPipDstWire(pip))) {
                if (!ctx->getBoundPipNet(pip2))
                    continue;
                IdString dst = ctx->getWireName(ctx->getPipDstWire(pip2))[1];
                if (boost::ends_with(dst.c_str(ctx), ".DS")) {
                    cell->setParam(id_type, Property("CSC"));
                    return true;
                }
            }
        }
    }
    return false;
}

bool NgUltraImpl::update_bff_to_scc(CellInfo *cell, BelId bel, PipId dst_pip)
{
    const auto &extra_data = *bel_extra_data(bel);
    // Check if SCC mode only if FE is capable
    if (extra_data.flags & BEL_EXTRA_FE_SCC) {
        WireId dwire = ctx->getPipDstWire(dst_pip);
        for (PipId pip : ctx->getPipsUphill(dwire)) {
            if (!ctx->getBoundPipNet(pip))
                continue;
            for (PipId pip2 : ctx->getPipsUphill(ctx->getPipSrcWire(pip))) {
                if (!ctx->getBoundPipNet(pip2))
                    continue;
                IdString dst = ctx->getWireName(ctx->getPipSrcWire(pip2))[1];
                if (boost::starts_with(dst.c_str(ctx), "SYSTEM.ST1")) {
                    cell->setParam(id_type, Property("SCC"));
                    return true;
                }
            }
        }
    }
    return false;
}

void NgUltraImpl::postRoute()
{
    ctx->assignArchInfo();
    log_break();
    log_info("Resources spent on routing:\n");
    int bff_count = 0, csc_count = 0, scc_count = 0, lut_bypass = 0, fe_new = 0, wfg_bypass = 0, gck_bypass = 0;
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        for (auto &w : ni->wires) {
            if (w.second.pip != PipId()) {
                const auto &extra_data = *pip_extra_data(w.second.pip);
                if (!extra_data.name)
                    continue;
                if (extra_data.type == PipExtra::PIP_EXTRA_BYPASS) {
                    IdStringList id = ctx->getPipName(w.second.pip);
                    BelId bel = ctx->getBelByName(IdStringList::concat(id[0], IdString(extra_data.name)));
                    IdString type = ctx->getBelType(bel);
                    if (!ctx->getBoundBelCell(bel)) {
                        CellInfo *cell = ctx->createCell(ctx->id(ctx->nameOfBel(bel)), type);
                        ctx->bindBel(bel, cell, PlaceStrength::STRENGTH_FIXED);
                        if (type == id_BEYOND_FE)
                            fe_new++;
                    }
                    CellInfo *cell = ctx->getBoundBelCell(bel);
                    switch (type.index) {
                    case id_BEYOND_FE.index:
                        if (extra_data.input == 0) {
                            // set bypass mode for DFF
                            cell->setParam(id_type, Property("BFF"));
                            cell->params[id_dff_used] = Property(1, 1);
                            // Note: no conflict, CSC and SCC modes are never available on same position
                            if (update_bff_to_csc(cell, bel, w.second.pip))
                                csc_count++;
                            else if (update_bff_to_scc(cell, bel, w.second.pip))
                                scc_count++;
                            else
                                bff_count++;
                        } else {
                            lut_bypass++;
                            cell->params[id_lut_used] = Property(1, 1);
                            cell->params[id_lut_table] = Property(0xaaaa, 16);
                        }
                        break;
                    case id_WFG.index:
                        wfg_bypass++;
                        cell->type = id_WFB;
                        break;
                    case id_GCK.index:
                        gck_bypass++;
                        cell->setParam(id_std_mode, extra_data.input == 0 ? Property("BYPASS") : Property("CSC"));
                        break;
                    default:
                        log_error("Unmaped bel type '%s' for routing\n", type.c_str(ctx));
                    }
                }
            }
        }
    }
    if (bff_count)
        log_info("    %6d DFFs used as BFF\n", bff_count);
    if (csc_count)
        log_info("    %6d DFFs used as CSC\n", csc_count);
    if (scc_count)
        log_info("    %6d DFFs used as SCC\n", scc_count);
    if (lut_bypass)
        log_info("    %6d LUTs used in bypass mode\n", lut_bypass);
    if (fe_new)
        log_info("    %6d newly allocated FEs\n", fe_new);
    if (wfg_bypass)
        log_info("    %6d WFGs used as WFB\n", wfg_bypass);
    if (gck_bypass)
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
                        const auto &extra_data = *pip_extra_data(pip);
                        if (!extra_data.name)
                            continue;
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

    fixup_crossbars();

    print_utilisation(ctx);
    const ArchArgs &args = ctx->args;
    if (args.options.count("bit")) {
        write_bitstream_json(args.options.at("bit"));
    }
}

void NgUltraImpl::configurePlacerHeap(PlacerHeapCfg &cfg)
{
    cfg.beta = 0.5;
    cfg.placeAllAtOnce = true;
}

namespace {

template <size_t N> bool check_assign_sig(std::array<const NetInfo *, N> &sig_set, const NetInfo *sig)
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
    std::array<const NetInfo *, 2> clk{};    // from local system matrix
    std::array<const NetInfo *, 2> reset{};  // from local system matrix
    std::array<const NetInfo *, 2> load{};   // from local system matrix
    std::array<const NetInfo *, 1> shared{}; // 1 from local system matrix
    // Additional R and L can be used from RI network
    bool run(const NgUltraImpl *impl, const Context *ctx, BelId bel, CellInfo *cell)
    {
        Loc loc = ctx->getBelLocation(bel);
        for (uint8_t id = 0; id <= BEL_LUT_MAX_Z; id++) {
            const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, id)));
            if (ff == nullptr)
                continue;
            if (!check_assign_sig(reset, ff->getPort(id_R))) {
                if (!check_assign_sig(shared, ff->getPort(id_R)))
                    return false;
            }
            if (!check_assign_sig(load, ff->getPort(id_L))) {
                if (!check_assign_sig(shared, ff->getPort(id_L)))
                    return false;
            }
            if (!check_assign_sig(clk, ff->getPort(id_CK)))
                return false;
        }
        const auto &extra_data = *impl->bel_extra_data(bel);
        if (cell->params.count(id_type)) {
            const std::string &type = cell->params[id_type].as_string();
            if (type == "CSC" && (extra_data.flags & BEL_EXTRA_FE_CSC) == 0)
                return false; // No CSC capability on FE
            if (type == "SCC" && (extra_data.flags & BEL_EXTRA_FE_SCC) == 0)
                return false; // No SCC capability on FE
        }
        if (extra_data.flags & BEL_EXTRA_FE_CSC)
            return false;
        return true;
    }
};

}; // namespace

bool NgUltraImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    CellInfo *cell = ctx->getBoundBelCell(bel);
    if (cell == nullptr) {
        return true;
    }
    if (ctx->getBelType(bel) == id_BEYOND_FE) {
        SectionFEWorker worker;
        return worker.run(this, ctx, bel, cell);
    } else if (ctx->getBelType(bel).in(id_RF, id_XRF)) {
        Loc loc = ctx->getBelLocation(bel);
        if (loc.z == BEL_XRF_Z) {
            // If we used any of RFs we can not used XRF
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_RF_Z))))
                return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_RF_Z + 1))))
                return false;
            // If we used any FIFO we can not use XRF
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_FIFO_Z))))
                return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_FIFO_Z + 1))))
                return false;
            // If we used XFIFO we can not use XRF
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_XFIFO_Z))))
                return false;
        } else {
            // If we used XRF we can not use individual RF
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_XRF_Z))))
                return false;
            // If we used XFIFO we can not use RF
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_XFIFO_Z))))
                return false;
            int index = loc.z - BEL_RF_Z;
            // If we used coresponding FIFO we can not use RF
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_FIFO_Z + index))))
                return false;
        }
    } else if (ctx->getBelType(bel).in(id_FIFO, id_XFIFO)) {
        Loc loc = ctx->getBelLocation(bel);
        if (loc.z == BEL_XFIFO_Z) {
            // If we used any of RFs we can not used XFIFO
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_RF_Z))))
                return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_RF_Z + 1))))
                return false;
            // If we used any FIFO we can not use XFIFO
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_FIFO_Z))))
                return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_FIFO_Z + 1))))
                return false;
            // If we used XFIFO we can not use XFIFO
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_XFIFO_Z))))
                return false;
            // If we used any CDC we can not use XFIFO
            // NOTE: CDC1 is in S4 and CDC2 is S12
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x - 1, loc.y, BEL_CDC_Z))))
                return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x + 1, loc.y, BEL_CDC_Z + 1))))
                return false;
            // If we used XCDC we can not use XFIFO
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_XCDC_Z))))
                return false;
        } else {
            // If we used XFIFO we can not use individual FIFO
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_XFIFO_Z))))
                return false;
            // If we used XRF we can not use FIFO
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_XRF_Z))))
                return false;
            // If we used XCDC we can not use FIFO
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_XCDC_Z))))
                return false;
            int index = loc.z - BEL_FIFO_Z;
            // If we used coresponding RF we can not use FIFO
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_RF_Z + index))))
                return false;
            // If we used coresponding CDC we can not use FIFO
            // NOTE: CDC1 is in S4 and CDC2 is S12
            int rel = (index == 0) ? -1 : +1;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x + rel, loc.y, BEL_CDC_Z + index))))
                return false;
        }
    } else if (ctx->getBelType(bel).in(id_CDC, id_XCDC)) {
        Loc loc = ctx->getBelLocation(bel);
        if (loc.z == BEL_XCDC_Z) {
            // If we used any of CDCs we can not used XCDC
            // NOTE: CDC1 is in S4 and CDC2 is S12
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x - 1, loc.y, BEL_CDC_Z))))
                return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x + 1, loc.y, BEL_CDC_Z + 1))))
                return false;
            // If we used any FIFO we can not use XCDC
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_FIFO_Z))))
                return false;
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_FIFO_Z + 1))))
                return false;
            // If we used XFIFO we can not use XCDC
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BEL_XFIFO_Z))))
                return false;
        } else {
            // NOTE: CDC1 is in S4 and CDC2 is S12 so we move calculation relative to S8
            int index = loc.z - BEL_CDC_Z;
            int fix = (index == 0) ? +1 : -1;
            // If we used XCDC we can not use individual CDC
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x + fix, loc.y, BEL_XCDC_Z))))
                return false;
            // If we used XFIFO we can not use CDC
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x + fix, loc.y, BEL_XFIFO_Z))))
                return false;
            // If we used coresponding FIFO we can not use CDC
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x + fix, loc.y, BEL_FIFO_Z + index))))
                return false;
        }
    }
    return true;
}

// Bel bucket functions
IdString NgUltraImpl::getBelBucketForCellType(IdString cell_type) const
{
    if (cell_type.in(id_IOP, id_IP, id_OP, id_IOTP, id_ITP, id_OTP))
        return ctx->idf("IOP/IOTP");
    else if (cell_type.in(id_BFR, id_DFR, id_DDFR))
        return ctx->idf("DFR/DDFR");
    else if (cell_type.in(id_RF, id_RFSP))
        return id_RF;
    else if (cell_type.in(id_XHRF, id_XWRF, id_XPRF))
        return id_XRF;
    else if (cell_type.in(id_DDE, id_TDE, id_CDC, id_BGC, id_GBC))
        return id_CDC;
    else if (cell_type.in(id_XCDC))
        return id_XCDC;
    else if (cell_type.in(id_FIFO))
        return id_FIFO;
    else if (cell_type.in(id_XHFIFO, id_XWFIFO))
        return id_XFIFO;
    else if (cell_type.in(id_WFB, id_WFG))
        return id_WFG;
    else
        return cell_type;
}

BelBucketId NgUltraImpl::getBelBucketForBel(BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type.in(id_IOP, id_IOTP))
        return ctx->idf("IOP/IOTP");
    else if (bel_type.in(id_DFR, id_DDFR))
        return ctx->idf("DFR/DDFR");
    return bel_type;
}

bool NgUltraImpl::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type == id_IOTP)
        return cell_type.in(id_IOP, id_IP, id_OP, id_IOTP, id_ITP, id_OTP);
    else if (bel_type == id_IOP)
        return cell_type.in(id_IOP, id_IP, id_OP);
    else if (bel_type == id_DDFR)
        return cell_type.in(id_BFR, id_DFR, id_DDFR);
    else if (bel_type == id_DFR)
        return cell_type.in(id_BFR, id_DFR);
    else if (bel_type == id_RF)
        return cell_type.in(id_RF, id_RFSP);
    else if (bel_type == id_XRF)
        return cell_type.in(id_XHRF, id_XWRF, id_XPRF);
    else if (bel_type == id_CDC)
        return cell_type.in(id_DDE, id_TDE, id_CDC, id_BGC, id_GBC);
    else if (bel_type == id_XCDC)
        return cell_type.in(id_XCDC);
    else if (bel_type == id_FIFO)
        return cell_type.in(id_FIFO);
    else if (bel_type == id_XFIFO)
        return cell_type.in(id_XHFIFO, id_XWFIFO);
    else if (bel_type == id_WFG)
        return cell_type.in(id_WFB, id_WFG);
    else
        return (bel_type == cell_type);
}

bool NgUltraImpl::getChildPlacement(const BaseClusterInfo *cluster, Loc root_loc,
                                    std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    Loc prev = root_loc;
    for (auto child : cluster->constr_children) {
        Loc child_loc;
        switch (child->constr_z) {
        case PLACE_CY_CHAIN:
            child_loc = getNextLocInCYChain(prev);
            prev = child_loc;
            break;
        case PLACE_LUT_CHAIN:
            child_loc = getNextLocInLUTChain(prev);
            prev = child_loc;
            break;
        case PLACE_DFF_CHAIN:
            child_loc = getNextLocInDFFChain(prev);
            prev = child_loc;
            break;
        case PLACE_CY_FE1 ... PLACE_CY_FE4:
            child_loc = getCYFE(root_loc, child->constr_z - PLACE_CY_FE1);
            break;
        case PLACE_XLUT_FE1 ... PLACE_XLUT_FE4:
            child_loc = getXLUTFE(root_loc, child->constr_z - PLACE_XLUT_FE1);
            break;
        case PLACE_XRF_I1 ... PLACE_XRF_WEA:
            child_loc = getXRFFE(root_loc, child->constr_z - PLACE_XRF_I1);
            break;
        case PLACE_CDC_AI1 ... PLACE_CDC_DDRSTI:
            child_loc = getCDCFE(root_loc, child->constr_z - PLACE_CDC_AI1);
            break;
        case PLACE_FIFO_I1 ... PLACE_FIFO_REQ2:
            child_loc = getFIFOFE(root_loc, child->constr_z - PLACE_FIFO_I1);
            break;
        case PLACE_DSP_CHAIN:
            child_loc = getNextLocInDSPChain(this, prev);
            prev = child_loc;
            break;
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

bool NgUltraImpl::getClusterPlacement(ClusterId cluster, BelId root_bel,
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

BoundingBox NgUltraImpl::getRouteBoundingBox(WireId src, WireId dst) const
{
    if ((tile_type(src.tile) != TILE_EXTRA_FABRIC || tile_type(dst.tile) != TILE_EXTRA_FABRIC)) {
        return {0, 0, ctx->getGridDimX(), ctx->getGridDimY()};
    }
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
    // Reach 7 MESH above and below (3 left and 3 right of TILE/CGB)
    return {(x0 & 0xfffc) - 3 * 4, // 3 MESH on left
            (y0 & 0xfffc) - 4,     // row above
            (x1 & 0xfffc) + 4 * 4, // MESH bellow and 3 right
            (y1 & 0xfffc) + 8};    // current and row bellow
}

void NgUltraImpl::expandBoundingBox(BoundingBox &bb) const
{
    // Updated numbers for NG-ULTRA only
    // x0 and y0 substract 1 TILE
    // x1 and y1 adds 1 TILE (and of one added)
    bb.x0 = std::max((bb.x0 & 0xfffc) - 4, 0);
    bb.y0 = std::max((bb.y0 & 0xfffc) - 4, 0);
    bb.x1 = std::min((bb.x1 & 0xfffc) + 8, ctx->getGridDimX());
    bb.y1 = std::min((bb.y1 & 0xfffc) + 8, ctx->getGridDimY());
}

delay_t NgUltraImpl::estimateDelay(WireId src, WireId dst) const
{
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);
    if (sx == dx && sy == dy) {
        // Same sub tile
        return 50;
    } else if (((sx & 0xfffc) == (dx & 0xfffc)) && ((sy & 0xfffc) == (dy & 0xfffc))) {
        // Same "TILE"
        return 200;
    }
    return 500 + 100 * (std::abs(dy - sy) / 4 + std::abs(dx - sx) / 4);
}

delay_t NgUltraImpl::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    Loc src_loc = ctx->getBelLocation(src_bel), dst_loc = ctx->getBelLocation(dst_bel);
    if (src_loc.x == dst_loc.x && src_loc.y == dst_loc.y) {
        // Same sub tile
        return 50;
    } else if (((src_loc.x & 0xfffc) == (dst_loc.x & 0xfffc)) && ((src_loc.y & 0xfffc) == (dst_loc.y & 0xfffc))) {
        // Same "TILE"
        return 200;
    }
    return 500 + 100 * (std::abs(dst_loc.y - src_loc.y) / 4 + std::abs(dst_loc.x - src_loc.x) / 4);
}

void NgUltraImpl::fixup_crossbars()
{

    auto is_crossbar_pip = [&](PipId pip) {
        const auto &extra_data = *pip_extra_data(pip);
        return (extra_data.name && extra_data.type == PipExtra::PIP_EXTRA_CROSSBAR);
    };

    auto crossbar_key = [&](PipId pip) {
        const auto &extra_data = *pip_extra_data(pip);
        return std::make_pair(pip.tile, IdString(extra_data.name));
    };

    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        dict<WireId, std::vector<PipId>> downstream;
        // build a map of wires to sinks
        for (auto &w : ni->wires) {
            if (w.second.pip != PipId()) {
                downstream[ctx->getPipSrcWire(w.second.pip)].push_back(w.second.pip);
            }
        }
        // the original drivers of crossbars
        dict<std::pair<int, IdString>, WireId> crossbar_entries;
        // traverse from the start forwards so we always reach closer wires first in the route tree
        WireId src = ctx->getNetinfoSourceWire(ni);
        if (src == WireId() || !downstream.count(src)) {
            continue;
        }
        std::queue<WireId> visit;
        visit.push(src);
        while (!visit.empty()) {
            WireId cursor = visit.front();
            visit.pop();
            if (!downstream.count(cursor))
                continue;
            for (auto pip : downstream.at(cursor)) {
                WireId dst = ctx->getPipDstWire(pip);
                if (is_crossbar_pip(pip)) {
                    auto key = crossbar_key(pip);
                    if (!crossbar_entries.count(key)) {
                        crossbar_entries[key] = ctx->getPipSrcWire(pip);
                    } else {
                        WireId xbar_src = crossbar_entries.at(key);
                        if (ctx->getPipSrcWire(pip) != xbar_src) {
                            // rewrite to be driven by original entry
                            ctx->unbindPip(pip);
                            PipId found_pip = PipId();
                            for (auto search_pip : ctx->getPipsUphill(dst)) {
                                if (ctx->getPipSrcWire(search_pip) == xbar_src) {
                                    found_pip = search_pip;
                                    break;
                                }
                            }
                            NPNR_ASSERT(found_pip != PipId());
                            // rebind
                            // log_info(" replacing crossbar pip %s with %s on %s\n", ctx->nameOfPip(pip),
                            // ctx->nameOfPip(found_pip), ctx->nameOf(ni));
                            ctx->bindPip(found_pip, ni, STRENGTH_STRONG);
                        }
                    }
                }
                visit.push(dst);
            }
            downstream.erase(cursor);
        }
        // check everything was visited by our BFS tree traversal
        NPNR_ASSERT(downstream.empty());
    }
}

void NgUltraImpl::drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc)
{
    GraphicElement el;
    el.type = GraphicElement::TYPE_BOX;
    el.style = style;
    switch (bel_type.index) {
    case id_BEYOND_FE.index:
        el.x1 = loc.x + 0.15 + (loc.z % 8) * 0.1;
        el.x2 = el.x1 + 0.05;
        el.y1 = loc.y + 0.9 - (loc.z / 8) * 0.15;
        el.y2 = el.y1 - 0.05;
        g.push_back(el);
        break;
    case id_XLUT.index:
        el.x1 = loc.x + 0.15 + ((loc.z - BEL_XLUT_Z) % 8) * 0.1;
        el.x2 = el.x1 + 0.05;
        el.y1 = loc.y + 0.9 - 4 * 0.15;
        el.y2 = el.y1 - 0.1;
        g.push_back(el);
        break;
    case id_CY.index:
        el.x1 = loc.x + 0.15 + ((loc.z - BEL_CY_Z) % 4) * 0.2;
        el.x2 = el.x1 + 0.15;
        el.y1 = loc.y + 0.9 - 4 * 0.15;
        el.y2 = el.y1 - 0.1;
        g.push_back(el);
        break;
    case id_RF.index:
        el.x1 = loc.x + 0.15 + ((loc.z - BEL_RF_Z) % 2) * 0.6;
        el.x2 = el.x1 + 0.15;
        el.y1 = loc.y + 0.9 - 4 * 0.15;
        el.y2 = el.y1 - 0.05;
        g.push_back(el);
        break;
    case id_XRF.index:
        el.x1 = loc.x + 0.15 + 0.2;
        el.x2 = el.x1 + 0.35;
        el.y1 = loc.y + 0.9 - 4 * 0.15;
        el.y2 = el.y1 - 0.05;
        g.push_back(el);
        break;
    case id_CDC.index:
        el.x1 = loc.x + 0.15 + ((loc.z - BEL_CDC_Z) % 2) * 0.6;
        el.x2 = el.x1 + 0.15;
        el.y1 = loc.y + 0.9 - 4 * 0.15 - 0.1;
        el.y2 = el.y1 - 0.05;
        g.push_back(el);
        break;
    case id_XCDC.index:
        el.x1 = loc.x + 0.15 + 0.2;
        el.x2 = el.x1 + 0.35;
        el.y1 = loc.y + 0.9 - 4 * 0.15 - 0.1;
        el.y2 = el.y1 - 0.05;
        g.push_back(el);
        break;
    case id_FIFO.index:
        el.x1 = loc.x + 0.15 + ((loc.z - BEL_FIFO_Z) % 2) * 0.6;
        el.x2 = el.x1 + 0.15;
        el.y1 = loc.y + 0.9 - 4 * 0.15 - 0.2;
        el.y2 = el.y1 - 0.05;
        g.push_back(el);
        break;
    case id_XFIFO.index:
        el.x1 = loc.x + 0.15 + 0.2;
        el.x2 = el.x1 + 0.35;
        el.y1 = loc.y + 0.9 - 4 * 0.15 - 0.2;
        el.y2 = el.y1 - 0.05;
        g.push_back(el);
        break;
    case id_IOTP.index:
        el.x1 = loc.x + 0.15 + loc.z / 4 * 0.11;
        el.x2 = el.x1 + 0.06;
        if (loc.y == 3) { // bottom
            el.y1 = 0.1;
            el.y2 = el.y1 + 0.2;
        } else { // top
            el.y1 = loc.y + 0.9;
            el.y2 = el.y1 - 0.2;
        }
        g.push_back(el);
        break;
    case id_IOM.index:
        el.x1 = loc.x + 0.15;
        el.x2 = el.x1 + 33 * 0.11 + 0.06;
        if (loc.y == 3) { // bottom
            el.y1 = 0.4;
            el.y2 = el.y1 + 0.2;
        } else { // top
            el.y1 = loc.y + 0.6;
            el.y2 = el.y1 - 0.2;
        }
        g.push_back(el);
        break;
    case id_DDFR.index:
        el.x1 = loc.x + 0.15 + loc.z / 4 * 0.11 + (loc.z % 4 - 1) * 0.02;
        el.x2 = el.x1 + 0.015;
        if (loc.y == 3) { // bottom
            el.y1 = 0.7;
            el.y2 = el.y1 + 0.1;
        } else { // top
            el.y1 = loc.y + 0.3;
            el.y2 = el.y1 - 0.1;
        }
        g.push_back(el);
        break;
    case id_IOP.index:
        if (loc.x == ctx->getGridDimX() - 4) { // right
            el.x1 = ctx->getGridDimX() - 0.1;
            el.x2 = el.x1 - 0.2;
        } else { // left
            el.x1 = loc.x + 0.1;
            el.x2 = el.x1 + 0.2;
        }
        el.y1 = loc.y + 0.85 - loc.z / 4 * 0.11;
        el.y2 = el.y1 - 0.06;
        g.push_back(el);
        break;
    case id_DFR.index:
        if (loc.x == ctx->getGridDimX() - 4) { // right
            el.x1 = ctx->getGridDimX() - 0.4;
            el.x2 = el.x1 - 0.1;
        } else { // left
            el.x1 = loc.x + 0.4;
            el.x2 = el.x1 + 0.1;
        }
        el.y1 = loc.y + 0.85 - loc.z / 4 * 0.11 - (loc.z % 4 - 1) * 0.02 - 0.02;
        el.y2 = el.y1 + 0.015;
        g.push_back(el);
        break;
    case id_PLL.index:
        el.x1 = loc.x + 0.1;
        el.x2 = el.x1 + 0.8;
        el.y1 = loc.y + 0.9;
        el.y2 = el.y1 - 0.8;
        g.push_back(el);
        break;
    case id_WFG.index:
        el.x1 = loc.x + 1.1;
        el.x2 = el.x1 + 0.8;
        el.y1 = loc.y + 0.95 - (loc.z - 1) * 0.25 + 3;
        el.y2 = el.y1 - 0.2;
        g.push_back(el);
        break;
    case id_RAM.index:
        el.x1 = loc.x + 0.2;
        el.x2 = el.x1 + 3.6;
        el.y1 = loc.y + 0.8;
        el.y2 = el.y1 - 1.6;
        g.push_back(el);
        break;
    case id_DSP.index:
        el.x1 = (loc.x - 1) + 0.2;
        el.x2 = el.x1 + 1.6;
        el.y1 = loc.y + 0.8;
        el.y2 = el.y1 - 1.6;
        g.push_back(el);
        break;
    case id_GCK.index: {
        int lobe = loc.z / 20;
        el.x1 = (47 + (lobe % 2) * 3) * 4 + 0.1;
        el.x2 = el.x1 + 0.8;
        el.y1 = (ctx->getGridDimY() - 1 - (7 * 4 + 12 * 4 * (lobe >> 1))) + 0.95 - (loc.z % 20) * 0.25;
        el.y2 = el.y1 - 0.2;
        g.push_back(el);
    } break;
    default:
        break;
    }
}

struct NgUltraArch : HimbaechelArch
{
    NgUltraArch() : HimbaechelArch("ng-ultra") {};
    bool match_device(const std::string &device) override { return device == "NG-ULTRA"; }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device, const dict<std::string, std::string> &args)
    {
        return std::make_unique<NgUltraImpl>();
    }
} ngUltraArch;

NEXTPNR_NAMESPACE_END

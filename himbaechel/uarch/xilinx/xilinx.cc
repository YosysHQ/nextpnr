/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  gatecat <gatecat@ds0.me>
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
#include <queue>
#include <regex>

#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "placer_heap.h"
#include "xilinx.h"

#include "himbaechel_helpers.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

XilinxImpl::~XilinxImpl(){};

void XilinxImpl::init_database(Arch *arch)
{
    const ArchArgs &args = arch->args;
    init_uarch_constids(arch);
    std::regex devicere = std::regex("(xc7[azkv]\\d+t)([a-z0-9]+)-(\\dL?)");
    std::smatch match;
    if (!std::regex_match(args.device, match, devicere)) {
        log_error("Invalid device %s\n", args.device.c_str());
    }
    std::string die = match[1].str();
    if (die == "xc7a35t")
        die = "xc7a50t";
    arch->load_chipdb(stringf("xilinx/chipdb-%s.bin", die.c_str()));
    std::string package = match[2].str();
    arch->set_package(package);
    arch->set_speed_grade("DEFAULT");
}

void XilinxImpl::init(Context *ctx)
{
    h.init(ctx);
    HimbaechelAPI::init(ctx);

    tile_status.resize(ctx->chip_info->tile_insts.size());
    for (int i = 0; i < ctx->chip_info->tile_insts.ssize(); i++) {
        auto extra_data = tile_extra_data(i);
        tile_status.at(i).site_variant.resize(extra_data->sites.ssize());
    }
}

SiteIndex XilinxImpl::get_bel_site(BelId bel) const
{
    auto &bel_data = chip_bel_info(ctx->chip_info, bel);
    auto site_key = BelSiteKey::unpack(bel_data.site);
    return SiteIndex(bel.tile, site_key.site);
}

IdString XilinxImpl::get_site_name(SiteIndex site) const
{
    const auto &site_data = tile_extra_data(site.tile)->sites[site.site];
    return ctx->idf("%s_X%dY%d", IdString(site_data.name_prefix).c_str(ctx), site_data.site_x, site_data.site_y);
}

BelId XilinxImpl::get_site_bel(SiteIndex site, IdString bel_name) const
{
    const auto &tile_data = chip_tile_info(ctx->chip_info, site.tile);
    for (int32_t i = 0; i < tile_data.bels.ssize(); i++) {
        const auto &bel_data = tile_data.bels[i];
        if (BelSiteKey::unpack(bel_data.site).site != site.site)
            continue;
        if (reinterpret_cast<const XlnxBelExtraDataPOD *>(bel_data.extra_data.get())->name_in_site != bel_name.index)
            continue;
        return BelId(site.tile, i);
    }
    return BelId();
}

IdString XilinxImpl::bel_name_in_site(BelId bel) const
{
    const auto &bel_data = chip_bel_info(ctx->chip_info, bel);
    return IdString(reinterpret_cast<const XlnxBelExtraDataPOD *>(bel_data.extra_data.get())->name_in_site);
}

IdStringList XilinxImpl::get_site_bel_name(BelId bel) const
{
    return IdStringList::concat(get_site_name(get_bel_site(bel)), bel_name_in_site(bel));
}

void XilinxImpl::notifyBelChange(BelId bel, CellInfo *cell)
{
    auto &ts = tile_status.at(bel.tile);
    auto &bel_data = chip_bel_info(ctx->chip_info, bel);
    auto site_key = BelSiteKey::unpack(bel_data.site);
    // Update bound site variant for pip validity use later on
    if (cell && cell->type != id_PAD && site_key.site >= 0 && site_key.site < int(ts.site_variant.size())) {
        ts.site_variant.at(site_key.site) = site_key.site_variant;
    }
    if (is_logic_tile(bel))
        update_logic_bel(bel, cell);
    if (is_bram_tile(bel))
        update_bram_bel(bel, cell);
}

void XilinxImpl::update_logic_bel(BelId bel, CellInfo *cell)
{
    int z = ctx->getBelLocation(bel).z;
    NPNR_ASSERT(z < 128);
    auto &tts = tile_status.at(bel.tile);
    if (!tts.lts)
        tts.lts = std::make_unique<LogicTileStatus>();
    auto &ts = *(tts.lts);
    auto tags = get_tags(cell), last_tags = get_tags(ts.cells[z]);
    if ((z == ((3 << 4) | BEL_6LUT)) || (z == ((3 << 4) | BEL_5LUT))) {
        if ((tags && tags->lut.is_memory) || (last_tags && last_tags->lut.is_memory)) {
            // Special case - memory write port invalidates everything
            for (int i = 0; i < 8; i++)
                ts.eights[i].dirty = true;
            // if (xc7)
            ts.halfs[0].dirty = true; // WCLK and CLK0 shared
        }
    }
    if ((((z & 0xF) == BEL_6LUT) || ((z & 0xF) == BEL_5LUT)) &&
        ((tags && tags->lut.is_srl) || (last_tags && last_tags->lut.is_srl))) {
        // SRLs invalidate everything due to write clock
        for (int i = 0; i < 8; i++)
            ts.eights[i].dirty = true;
        // if (xc7)
        ts.halfs[0].dirty = true; // WCLK and CLK0 shared
    }

    ts.cells[z] = cell;

    // determine which sections to mark as dirty
    switch (z & 0xF) {
    case BEL_FF:
    case BEL_FF2:
        ts.halfs[(z >> 4) / 4].dirty = true;
        if ((((z >> 4) / 4) == 0) /*&& xc7*/)
            ts.eights[3].dirty = true;
    /* fall-through */
    case BEL_6LUT:
    case BEL_5LUT:
        ts.eights[z >> 4].dirty = true;
        break;
    case BEL_F7MUX:
        ts.eights[z >> 4].dirty = true;
        ts.eights[(z >> 4) + 1].dirty = true;
        break;
    case BEL_F8MUX:
        ts.eights[(z >> 4) + 1].dirty = true;
        ts.eights[(z >> 4) + 2].dirty = true;
        break;
    case BEL_CARRY4:
        for (int i = ((z >> 4) / 4) * 4; i < (((z >> 4) / 4) + 1) * 4; i++)
            ts.eights[i].dirty = true;
        break;
    }
}

void XilinxImpl::update_bram_bel(BelId bel, CellInfo *cell) {}

bool XilinxImpl::is_pip_unavail(PipId pip) const
{
    const auto &pip_data = chip_pip_info(ctx->chip_info, pip);
    const auto &extra_data = *reinterpret_cast<const XlnxPipExtraDataPOD *>(pip_data.extra_data.get());
    unsigned pip_type = pip_data.flags;
    if (pip_type == PIP_SITE_ENTRY) {
        WireId dst = ctx->getPipDstWire(pip);
        if (ctx->getWireType(dst) == id_INTENT_SITE_GND) {
            const auto &lts = tile_status[dst.tile].lts;
            if (lts && (lts->cells[BEL_5LUT] != nullptr || lts->cells[BEL_6LUT] != nullptr))
                return true; // Ground driver only available if lowest 5LUT and 6LUT not used
        }
    } else if (pip_type == PIP_CONST_DRIVER) {
        WireId dst = ctx->getPipDstWire(pip);
        const auto &lts = tile_status[dst.tile].lts;
        if (lts && (lts->cells[BEL_5LUT] != nullptr || lts->cells[BEL_6LUT] != nullptr))
            return true; // Ground driver only available if lowest 5LUT and 6LUT not used
    } else if (pip_type == PIP_SITE_INTERNAL) {
        if (extra_data.bel_name == ID_TRIBUF)
            return true;
        auto site = BelSiteKey::unpack(extra_data.site_key);
        // Check site variant of PIP matches configured site variant of tile
        if (site.site >= 0 && site.site < int(tile_status[pip.tile].site_variant.size())) {
            if (site.site_variant > 0 && site.site_variant != tile_status[pip.tile].site_variant.at(site.site))
                return true;
        }
    } else if (pip_type == PIP_LUT_PERMUTATION) {
        const auto &lts = tile_status[pip.tile].lts;
        if (!lts)
            return false;
        int eight = (extra_data.pip_config >> 8) & 0xF;

        if (((extra_data.pip_config >> 4) & 0xF) == (extra_data.pip_config & 0xF))
            return false; // from==to, always valid

        auto lut6 = get_tags(lts->cells[(eight << 4) | BEL_6LUT]);
        if (lut6 && (lut6->lut.is_memory || lut6->lut.is_srl))
            return true;
        auto lut5 = get_tags(lts->cells[(eight << 4) | BEL_5LUT]);
        if (lut5 && (lut5->lut.is_memory || lut5->lut.is_srl))
            return true;
    } else if (pip_type == PIP_LUT_ROUTETHRU) {
        int eight = (extra_data.pip_config >> 8) & 0xF;
        int dest = (extra_data.pip_config & 0x1);
        if (eight == 0)
            return true; // FIXME: conflict with ground
        if (dest & 0x1)
            return true; // FIXME: routethru to MUX
        const auto &lts = tile_status[pip.tile].lts;
        if (!lts)
            return false;
        const CellInfo *lut6 = lts->cells[(eight << 4) | BEL_6LUT];
        if (lut6)
            return true;
        const CellInfo *lut5 = lts->cells[(eight << 4) | BEL_5LUT];
        if (lut5)
            return true;
    }

    return false;
}

void XilinxImpl::prePlace() { assign_cell_tags(); }

void XilinxImpl::postPlace()
{
    fixup_placement();
    ctx->assignArchInfo();
}

void XilinxImpl::configurePlacerHeap(PlacerHeapCfg &cfg)
{
    cfg.hpwl_scale_x = 2;
    cfg.hpwl_scale_y = 1;
    cfg.beta = 0.5;
    cfg.placeAllAtOnce = true;
}

void XilinxImpl::preRoute()
{
    find_source_sink_locs();
    route_clocks();
}

void XilinxImpl::postRoute()
{
    fixup_routing();
    ctx->assignArchInfo();
    const ArchArgs &args = ctx->args;
    if (args.options.count("fasm")) {
        write_fasm(args.options.at("fasm"));
    }
}

IdString XilinxImpl::bel_tile_type(BelId bel) const
{
    return IdString(chip_tile_info(ctx->chip_info, bel.tile).type_name);
}

bool XilinxImpl::is_logic_tile(BelId bel) const
{
    return bel_tile_type(bel).in(id_CLEL_L, id_CLEL_R, id_CLEM, id_CLEM_R, id_CLBLL_L, id_CLBLL_R, id_CLBLM_L,
                                 id_CLBLM_R);
}
bool XilinxImpl::is_bram_tile(BelId bel) const { return bel_tile_type(bel).in(id_BRAM, id_BRAM_L, id_BRAM_R); }

const XlnxTileInstExtraDataPOD *XilinxImpl::tile_extra_data(int tile) const
{
    return reinterpret_cast<const XlnxTileInstExtraDataPOD *>(ctx->chip_info->tile_insts[tile].extra_data.get());
}

std::string XilinxImpl::tile_name(int tile) const
{
    const auto &data = *tile_extra_data(tile);
    return stringf("%s_X%dY%d", IdString(data.name_prefix).c_str(ctx), data.tile_x, data.tile_y);
}

Loc XilinxImpl::rel_site_loc(SiteIndex site) const
{
    const auto &site_data = tile_extra_data(site.tile)->sites[site.site];
    return Loc(site_data.rel_x, site_data.rel_y, 0);
}

int XilinxImpl::hclk_for_iob(BelId pad) const
{
    std::string tile_type = bel_tile_type(pad).str(ctx);
    int ioi = pad.tile;
    if (boost::starts_with(tile_type, "LIOB"))
        ioi += 1;
    else if (boost::starts_with(tile_type, "RIOB"))
        ioi -= 1;
    else
        NPNR_ASSERT_FALSE("unknown IOB side");
    return hclk_for_ioi(ioi);
}

int XilinxImpl::hclk_for_ioi(int tile) const
{
    WireId ioclk0;
    auto &td = chip_tile_info(ctx->chip_info, tile);
    for (int i = 0; i < td.wires.ssize(); i++) {
        std::string name = IdString(td.wires[i].name).str(ctx);
        if (name == "IOI_IOCLK0" || name == "IOI_SING_IOCLK0") {
            ioclk0 = ctx->normalise_wire(tile, i);
            break;
        }
    }
    NPNR_ASSERT(ioclk0 != WireId());
    for (auto uh : ctx->getPipsUphill(ioclk0))
        return uh.tile;
    NPNR_ASSERT_FALSE("failed to find HCLK pips");
}

void XilinxImpl::assign_cell_tags()
{
    cell_tags.resize(ctx->cells.size());
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        auto &ct = cell_tags.at(ci->flat_index);
        if (ci->type == id_SLICE_LUTX) {
            ct.lut.input_count = 0;
            for (IdString a : {id_A1, id_A2, id_A3, id_A4, id_A5, id_A6}) {
                NetInfo *pn = ci->getPort(a);
                if (pn != nullptr)
                    ct.lut.input_sigs[ct.lut.input_count++] = pn;
            }
            ct.lut.output_count = 0;
            for (IdString o : {id_O6, id_O5}) {
                NetInfo *pn = ci->getPort(o);
                if (pn != nullptr)
                    ct.lut.output_sigs[ct.lut.output_count++] = pn;
            }
            for (int i = ct.lut.output_count; i < 2; i++)
                ct.lut.output_sigs[i] = nullptr;
            ct.lut.di1_net = ci->getPort(id_DI1);
            ct.lut.di2_net = ci->getPort(id_DI2);
            ct.lut.wclk = ci->getPort(id_CLK);
            ct.lut.memory_group = 0; // fixme
            ct.lut.is_srl = ci->attrs.count(id_X_LUT_AS_SRL);
            ct.lut.is_memory = ci->attrs.count(id_X_LUT_AS_DRAM);
            ct.lut.only_drives_carry = false;
            if (ci->cluster != ClusterId() && ct.lut.output_count > 0 && ct.lut.output_sigs[0] != nullptr &&
                ct.lut.output_sigs[0]->users.entries() == 1 &&
                (*ct.lut.output_sigs[0]->users.begin()).cell->type == id_CARRY4)
                ct.lut.only_drives_carry = true;

            const IdString addr_msb_sigs[] = {id_WA7, id_WA8, id_WA9};
            for (int i = 0; i < 3; i++)
                ct.lut.address_msb[i] = ci->getPort(addr_msb_sigs[i]);

        } else if (ci->type == id_SLICE_FFX) {
            ct.ff.d = ci->getPort(id_D);
            ct.ff.clk = ci->getPort(id_CK);
            ct.ff.ce = ci->getPort(id_CE);
            ct.ff.sr = ci->getPort(id_SR);
            ct.ff.is_clkinv = bool_or_default(ci->params, id_IS_CLK_INVERTED, false);
            ct.ff.is_srinv = bool_or_default(ci->params, id_IS_R_INVERTED, false) ||
                             bool_or_default(ci->params, id_IS_S_INVERTED, false) ||
                             bool_or_default(ci->params, id_IS_CLR_INVERTED, false) ||
                             bool_or_default(ci->params, id_IS_PRE_INVERTED, false);
            ct.ff.is_latch = ci->attrs.count(id_X_FF_AS_LATCH);
            ct.ff.ffsync = ci->attrs.count(id_X_FFSYNC);
        } else if (ci->type.in(id_F7MUX, id_F8MUX, id_F9MUX, id_SELMUX2_1)) {
            ct.mux.sel = ci->getPort(id_S0);
            ct.mux.out = ci->getPort(id_OUT);
        } else if (ci->type == id_CARRY4) {
            for (int i = 0; i < 4; i++) {
                ct.carry.out_sigs[i] = ci->getPort(ctx->idf("O%d", i));
                ct.carry.cout_sigs[i] = ci->getPort(ctx->idf("CO%d", i));
                ct.carry.x_sigs[i] = nullptr;
            }
            ct.carry.x_sigs[0] = ci->getPort(id_CYINIT);
        }
    }
}

bool XilinxImpl::is_general_routing(WireId wire) const
{
    IdString intent = ctx->getWireType(wire);
    return !intent.in(id_INTENT_DEFAULT, id_NODE_DEDICATED, id_NODE_OPTDELAY, id_NODE_OUTPUT, id_NODE_INT_INTERFACE,
                      id_PINFEED, id_INPUT, id_PADOUTPUT, id_PADINPUT, id_IOBINPUT, id_IOBOUTPUT, id_GENERIC,
                      id_IOBIN2OUT, id_INTENT_SITE_WIRE, id_INTENT_SITE_GND);
}

void XilinxImpl::find_source_sink_locs()
{
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        for (auto &usr : ni->users) {
            BelId bel = usr.cell->bel;
            if (bel == BelId() || is_logic_tile(bel))
                continue; // don't need to do this for logic bels, which are always next to their INT
            WireId sink = ctx->getNetinfoSinkWire(ni, usr, 0);
            if (sink == WireId() || sink_locs.count(sink))
                continue;
            std::queue<WireId> visit;
            dict<WireId, WireId> backtrace;
            int iter = 0;
            // as this is a best-effort optimisation to slightly improve routing,
            // don't spend too long with a nice low iteration limit
            const int iter_max = 500;
            visit.push(sink);
            while (!visit.empty() && iter < iter_max) {
                ++iter;
                WireId cursor = visit.front();
                visit.pop();
                if (is_general_routing(cursor)) {
                    Loc loc(0, 0, 0);
                    tile_xy(ctx->chip_info, cursor.tile, loc.x, loc.y);
                    sink_locs[sink] = loc;

                    while (backtrace.count(cursor)) {
                        cursor = backtrace.at(cursor);
                        if (!sink_locs.count(cursor)) {
                            sink_locs[cursor] = loc;
                        }
                    }

                    break;
                }
                for (auto pip : ctx->getPipsUphill(cursor)) {
                    WireId src = ctx->getPipSrcWire(pip);
                    if (!backtrace.count(src)) {
                        backtrace[src] = cursor;
                        visit.push(src);
                    }
                }
            }
        }
        auto &drv = ni->driver;
        if (drv.cell != nullptr) {
            BelId bel = drv.cell->bel;
            if (bel == BelId() || is_logic_tile(bel))
                continue; // don't need to do this for logic bels, which are always next to their INT
            WireId source = ctx->getNetinfoSourceWire(ni);
            if (source == WireId() || source_locs.count(source))
                continue;
            std::queue<WireId> visit;
            dict<WireId, WireId> backtrace;
            int iter = 0;
            // as this is a best-effort optimisation to slightly improve routing,
            // don't spend too long with a nice low iteration limit
            const int iter_max = 500;
            visit.push(source);
            while (!visit.empty() && iter < iter_max) {
                ++iter;
                WireId cursor = visit.front();
                visit.pop();
                if (is_general_routing(cursor)) {
                    Loc loc(0, 0, 0);
                    tile_xy(ctx->chip_info, cursor.tile, loc.x, loc.y);
                    source_locs[source] = loc;

                    while (backtrace.count(cursor)) {
                        cursor = backtrace.at(cursor);
                        if (!source_locs.count(cursor)) {
                            source_locs[cursor] = loc;
                        }
                    }

                    break;
                }
                for (auto pip : ctx->getPipsDownhill(cursor)) {
                    WireId dst = ctx->getPipDstWire(pip);
                    if (!backtrace.count(dst)) {
                        backtrace[dst] = cursor;
                        visit.push(dst);
                    }
                }
            }
        }
    }
}

delay_t XilinxImpl::estimateDelay(WireId src, WireId dst) const
{
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);
    auto fnd_src = source_locs.find(src);
    if (fnd_src != source_locs.end()) {
        sx = fnd_src->second.x;
        sy = fnd_src->second.y;
    }
    auto fnd_snk = sink_locs.find(dst);
    if (fnd_snk != sink_locs.end()) {
        dx = fnd_snk->second.x;
        dy = fnd_snk->second.y;
    }
    // TODO: improve sophistication here based on old nextpnr-xilinx code
    return 800 + 50 * (std::abs(dy - sy) + std::abs(dx - sx));
}

BoundingBox XilinxImpl::getRouteBoundingBox(WireId src, WireId dst) const
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

    auto fnd_src = source_locs.find(src);
    if (fnd_src != source_locs.end()) {
        expand(fnd_src->second.x, fnd_src->second.y);
    }
    auto fnd_snk = sink_locs.find(dst);
    if (fnd_snk != sink_locs.end()) {
        expand(fnd_snk->second.x, fnd_snk->second.y);
    }
    return {x0 - 2, y0 - 2, x1 + 2, y1 + 2};
}

namespace {
struct XilinxArch : HimbaechelArch
{
    XilinxArch() : HimbaechelArch("xilinx"){};
    bool match_device(const std::string &device) override { return device.size() > 3 && device.substr(0, 3) == "xc7"; }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device, const dict<std::string, std::string> &args)
    {
        return std::make_unique<XilinxImpl>();
    }
} xilinxArch;
} // namespace

NEXTPNR_NAMESPACE_END

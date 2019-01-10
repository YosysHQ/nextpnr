/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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

#include <algorithm>
#include <cmath>
#include <regex>
#include "cells.h"
#include "gfx.h"
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "router1.h"
#include "util.h"

#include "torc/common/DirectoryTree.hpp"

NEXTPNR_NAMESPACE_BEGIN

std::unique_ptr<const TorcInfo> torc_info;
TorcInfo::TorcInfo(BaseCtx *ctx, const std::string &inDeviceName, const std::string &inPackageName)
        : TorcInfo(inDeviceName, inPackageName)
{
    static const std::regex re_loc(".+_X(\\d+)Y(\\d+)");
    std::cmatch what;
    tile_to_xy.resize(tiles.getTileCount());
    for (TileIndex tileIndex(0); tileIndex < tiles.getTileCount(); tileIndex++) {
        const auto &tileInfo = tiles.getTileInfo(tileIndex);
        if (!std::regex_match(tileInfo.getName(), what, re_loc))
            throw;
        const auto x = boost::lexical_cast<int>(what.str(1));
        const auto y = boost::lexical_cast<int>(what.str(2));
        tile_to_xy[tileIndex] = std::make_pair(x, y);
    }

    bel_to_site_index.reserve(sites.getSiteCount() * 4);
    bel_to_loc.reserve(sites.getSiteCount() * 4);
    site_index_to_bel.resize(sites.getSiteCount());
    site_index_to_type.resize(sites.getSiteCount());
    BelId b;
    b.index = 0;
    for (SiteIndex i(0); i < sites.getSiteCount(); ++i) {
        const auto &site = sites.getSite(i);
        const auto &pd = site.getPrimitiveDefPtr();
        const auto &type = pd->getName();
        int x, y;
        std::tie(x, y) = tile_to_xy[site.getTileIndex()];

        if (type == "SLICEL" || type == "SLICEM") {
            bel_to_site_index.push_back(i);
            bel_to_site_index.push_back(i);
            bel_to_site_index.push_back(i);
            bel_to_site_index.push_back(i);
            site_index_to_type[i] = id_SLICE_LUT6;
            const auto site_name = site.getName();
            if (!std::regex_match(site_name.c_str(), what, re_loc))
                throw;
            const auto sx = boost::lexical_cast<int>(what.str(1));
            if ((sx & 1) == 0) {
                bel_to_loc.emplace_back(x, y, 0);
                bel_to_loc.emplace_back(x, y, 1);
                bel_to_loc.emplace_back(x, y, 2);
                bel_to_loc.emplace_back(x, y, 3);
            } else {
                bel_to_loc.emplace_back(x, y, 4);
                bel_to_loc.emplace_back(x, y, 5);
                bel_to_loc.emplace_back(x, y, 6);
                bel_to_loc.emplace_back(x, y, 7);
            }
            site_index_to_bel[i] = b;
            b.index += 4;
        } else if (type == "IOB33S" || type == "IOB33M") {
            bel_to_site_index.push_back(i);
            site_index_to_type[i] = id_IOB33;
            // TODO: Fix z when two IOBs on same tile
            bel_to_loc.emplace_back(x, y, 0);
            site_index_to_bel[i] = b;
            ++b.index;
        } else if (type == "IOB18S" || type == "IOB18M") {
            bel_to_site_index.push_back(i);
            site_index_to_type[i] = id_IOB18;
            // TODO: Fix z when two IOBs on same tile
            bel_to_loc.emplace_back(x, y, 0);
            site_index_to_bel[i] = b;
            ++b.index;
        } else {
            bel_to_site_index.push_back(i);
            site_index_to_type[i] = ctx->id(type);
            bel_to_loc.emplace_back(x, y, 0);
            site_index_to_bel[i] = b;
            ++b.index;
        }
    }
    num_bels = bel_to_site_index.size();
    bel_to_site_index.shrink_to_fit();
    bel_to_loc.shrink_to_fit();

    const std::regex re_124("(.+_)?[NESW][NESWLR](\\d)((BEG(_[NS])?)|(END(_[NS])?)|[A-E])?\\d(_\\d)?");
    const std::regex re_L("(.+_)?L(H|V|VB)(_L)?\\d+(_\\d)?");
    const std::regex re_BYP("BYP(_ALT)?\\d");
    const std::regex re_BYP_B("BYP_[BL]\\d");
    const std::regex re_BOUNCE_NS("(BYP|FAN)_BOUNCE_[NS]3_\\d");
    const std::regex re_FAN("FAN(_ALT)?\\d");
    const std::regex re_CLB_I1_6("CLBL[LM]_(L|LL|M)_[A-D]([1-6])");
    const std::regex bufg_i("CLK_BUFG_BUFGCTRL\\d+_I0");
    const std::regex bufg_o("CLK_BUFG_BUFGCTRL\\d+_O");
    const std::regex hrow("CLK_HROW_CLK[01]_[34]");
    std::unordered_map</*TileTypeIndex*/ unsigned, std::vector<delay_t>> delay_lookup;
    std::unordered_map<Segments::SegmentReference, TileIndex> segment_to_anchor;
    Tilewire currentTilewire;
    WireId w;
    w.index = 0;
    for (TileIndex tileIndex(0); tileIndex < tiles.getTileCount(); tileIndex++) {
        // iterate over every wire in the tile
        const auto &tileInfo = tiles.getTileInfo(tileIndex);
        auto tileTypeIndex = tileInfo.getTypeIndex();
        auto wireCount = tiles.getWireCount(tileTypeIndex);
        currentTilewire.setTileIndex(tileIndex);
        for (WireIndex wireIndex(0); wireIndex < wireCount; wireIndex++) {
            currentTilewire.setWireIndex(wireIndex);
            const auto &currentSegment = segments.getTilewireSegment(currentTilewire);

            if (!currentSegment.isTrivial()) {
                auto r = segment_to_anchor.emplace(currentSegment, currentSegment.getAnchorTileIndex());
                if (r.second) {
                    TilewireVector segment;
                    const_cast<DDB &>(*ddb).expandSegment(currentTilewire, segment, DDB::eExpandDirectionNone);
                    // expand all of the arcs
                    TilewireVector::const_iterator sep = segment.begin();
                    TilewireVector::const_iterator see = segment.end();
                    while (sep < see) {
                        // expand the tilewire sinks
                        const Tilewire &tilewire = *sep++;

                        const auto &tileInfo = tiles.getTileInfo(tilewire.getTileIndex());
                        const auto &tileTypeName = tiles.getTileTypeName(tileInfo.getTypeIndex());
                        if (boost::starts_with(tileTypeName, "INT") || boost::starts_with(tileTypeName, "CLB")) {
                            r.first->second = tilewire.getTileIndex();
                            break;
                        }
                    }
                }
                if (r.first->second != tileIndex)
                    continue;

                segment_to_wire.emplace(currentSegment, w);
            } else
                trivial_to_wire.emplace(currentTilewire, w);

            wire_to_tilewire.push_back(currentTilewire);

            auto it = delay_lookup.find(tileTypeIndex);
            if (it == delay_lookup.end()) {
                auto wireCount = tiles.getWireCount(tileTypeIndex);
                std::vector<delay_t> tile_delays(wireCount);
                for (WireIndex wireIndex(0); wireIndex < wireCount; wireIndex++) {
                    const WireInfo &wireInfo = tiles.getWireInfo(tileTypeIndex, wireIndex);
                    auto wire_name = wireInfo.getName();
                    if (std::regex_match(wire_name, what, re_124)) {
                        switch (what.str(2)[0]) {
                        case '1':
                            tile_delays[wireIndex] = 150;
                            break;
                        case '2':
                            tile_delays[wireIndex] = 170;
                            break;
                        case '4':
                            tile_delays[wireIndex] = 210;
                            break;
                        case '6':
                            tile_delays[wireIndex] = 210;
                            break;
                        default:
                            throw;
                        }
                    } else if (std::regex_match(wire_name, what, re_L)) {
                        std::string l(what[2]);
                        if (l == "H")
                            tile_delays[wireIndex] = 360;
                        else if (l == "VB")
                            tile_delays[wireIndex] = 300;
                        else if (l == "V")
                            tile_delays[wireIndex] = 350;
                        else
                            throw;
                    } else if (std::regex_match(wire_name, what, re_BYP)) {
                        tile_delays[wireIndex] = 190;
                    } else if (std::regex_match(wire_name, what, re_BYP_B)) {
                    } else if (std::regex_match(wire_name, what, re_FAN)) {
                        tile_delays[wireIndex] = 190;
                    } else if (std::regex_match(wire_name, what, re_CLB_I1_6)) {
                        switch (what.str(2)[0]) {
                        case '1':
                            tile_delays[wireIndex] = 280;
                            break;
                        case '2':
                            tile_delays[wireIndex] = 280;
                            break;
                        case '3':
                            tile_delays[wireIndex] = 180;
                            break;
                        case '4':
                            tile_delays[wireIndex] = 180;
                            break;
                        case '5':
                            tile_delays[wireIndex] = 80;
                            break;
                        case '6':
                            tile_delays[wireIndex] = 40;
                            break;
                        default:
                            throw;
                        }
                    }
                }
                it = delay_lookup.emplace(tileTypeIndex, std::move(tile_delays)).first;
            }
            assert(it != delay_lookup.end());

            DelayInfo d;
            d.delay = it->second[currentTilewire.getWireIndex()];
            wire_to_delay.emplace_back(std::move(d));

            ++w.index;
        }
    }
    segment_to_anchor.clear();
    wire_to_tilewire.shrink_to_fit();
    wire_to_delay.shrink_to_fit();
    num_wires = wire_to_tilewire.size();
    wire_is_global.resize(num_wires);

    wire_to_pips_downhill.resize(num_wires);
    // std::unordered_map<Arc, int> arc_to_pip;
    ArcVector arcs;
    ExtendedWireInfo ewi(*ddb);
    PipId p;
    p.index = 0;
    for (w.index = 0; w.index < num_wires; ++w.index) {
        const auto &currentTilewire = wire_to_tilewire[w.index];
        if (currentTilewire.isUndefined())
            continue;

        const auto &tileInfo = tiles.getTileInfo(currentTilewire.getTileIndex());
        const auto tileTypeName = tiles.getTileTypeName(tileInfo.getTypeIndex());
        const bool clb = boost::starts_with(
                tileTypeName, "CLB"); // Disable all CLB route-throughs (i.e. LUT in->out, LUT A->AMUX, for now)

        auto &pips = wire_to_pips_downhill[w.index];
        const bool clk_tile = boost::starts_with(tileTypeName, "CLK");

        bool global_tile = false;

        arcs.clear();
        // const_cast<DDB &>(*ddb).expandSegmentSinks(currentTilewire, arcs, DDB::eExpandDirectionNone,
        //                                           false /* inUseTied */, true /*inUseRegular */,
        //                                           true /* inUseIrregular */, !clb /* inUseRoutethrough */);
        {
            // expand the segment
            TilewireVector segment;
            const_cast<DDB &>(*ddb).expandSegment(currentTilewire, segment, DDB::eExpandDirectionNone);
            // expand all of the arcs
            TilewireVector::const_iterator sep = segment.begin();
            TilewireVector::const_iterator see = segment.end();
            while (sep < see) {
                // expand the tilewire sinks
                const Tilewire &tilewire = *sep++;

                const auto &tileInfo = tiles.getTileInfo(tilewire.getTileIndex());
                const auto &tileTypeName = tiles.getTileTypeName(tileInfo.getTypeIndex());
                global_tile = global_tile || boost::starts_with(tileTypeName, "CLK") ||
                              boost::starts_with(tileTypeName, "HCLK") || boost::starts_with(tileTypeName, "CFG");

                TilewireVector sinks;
                const_cast<DDB &>(*ddb).expandTilewireSinks(tilewire, sinks, false /*inUseTied*/, true /*inUseRegular*/,
                                                            true /*inUseIrregular*/, !clb /* inUseRoutethrough */);
                // rewrite the sinks as arcs
                TilewireVector::const_iterator sip = sinks.begin();
                TilewireVector::const_iterator sie = sinks.end();
                while (sip < sie) {
                    Arc a(tilewire, *sip++);

                    // Disable BUFG I0 -> O routethrough
                    if (clk_tile) {
                        ewi.set(a.getSourceTilewire());
                        if (std::regex_match(ewi.mWireName, bufg_i)) {
                            ewi.set(a.getSinkTilewire());
                            if (std::regex_match(ewi.mWireName, bufg_o))
                                continue;
                        }
                    }

                    // Disable entering HROW from INT_[LR].CLK[01]
                    if (boost::starts_with(tileTypeName, "CLK_HROW")) {
                        ewi.set(a.getSourceTilewire());
                        if (std::regex_match(ewi.mWireName, hrow))
                            continue;
                    }

                    pips.emplace_back(p);
                    pip_to_arc.emplace_back(a);
                    // arc_to_pip.emplace(a, p.index);
                    ++p.index;
                }
            }
        }
        pips.shrink_to_fit();

        if (global_tile)
            wire_is_global[w.index] = true;
    }
    pip_to_arc.shrink_to_fit();
    num_pips = pip_to_arc.size();

    height = (int)tiles.getRowCount();
    width = (int)tiles.getColCount();
}
TorcInfo::TorcInfo(const std::string &inDeviceName, const std::string &inPackageName)
        : ddb(new DDB(inDeviceName, inPackageName)), sites(ddb->getSites()), tiles(ddb->getTiles()),
          segments(ddb->getSegments())
{
}

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);
#include "constids.inc"
#undef X
}

// -----------------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
    std::stringstream ss;
    ss << TORC_ROOT << "/src/torc";
    torc::common::DirectoryTree directoryTree(ss.str().c_str());
    if (args.type == ArchArgs::Z020) {
        torc_info = std::unique_ptr<TorcInfo>(new TorcInfo(this, "xc7z020", args.package));
    } else if (args.type == ArchArgs::VX980) {
        torc_info = std::unique_ptr<TorcInfo>(new TorcInfo(this, "xc7vx980t", args.package));
    } else {
        log_error("Unsupported XC7 chip type.\n");
    }

    // TODO: FIXME
    width = torc_info->width;
    height = torc_info->height;

    /*if (getCtx()->verbose)*/ {
        log_info("Number of bels:  %d\n", torc_info->num_bels);
        log_info("Number of wires: %d\n", torc_info->num_wires);
        log_info("Number of pips:  %d\n", torc_info->num_pips);
    }

    bel_to_cell.resize(torc_info->num_bels);
    wire_to_net.resize(torc_info->num_wires);
    pip_to_net.resize(torc_info->num_pips);
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const
{
    if (args.type == ArchArgs::Z020) {
        return "z020";
    } else if (args.type == ArchArgs::VX980) {
        return "vx980";
    } else {
        log_error("Unsupported XC7 chip type.\n");
    }
}

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::Z020)
        return id("z020");
    if (args.type == ArchArgs::VX980)
        return id("vx980");
    return IdString();
}

// -----------------------------------------------------------------------

static bool endsWith(const std::string &str, const std::string &suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

BelId Arch::getBelByName(IdString name) const
{
    std::string n = name.str(this);
    int ndx = 0;
    if (endsWith(n, "_A") || endsWith(n, "_B") || endsWith(n, "_C") || endsWith(n, "_D")) {
        ndx = (int)(n.back() - 'A');
        n = n.substr(0, n.size() - 2);
    }
    auto it = torc_info->sites.findSiteIndex(n);
    if (it != SiteIndex(-1)) {
        BelId id = torc_info->site_index_to_bel.at(it);
        id.index += ndx;
        return id;
    }
    return BelId();
}

BelId Arch::getBelByLocation(Loc loc) const
{
    BelId bel;

    if (bel_by_loc.empty()) {
        for (int i = 0; i < torc_info->num_bels; i++) {
            BelId b;
            b.index = i;
            bel_by_loc[getBelLocation(b)] = b;
        }
    }

    auto it = bel_by_loc.find(loc);
    if (it != bel_by_loc.end())
        bel = it->second;

    return bel;
}

BelRange Arch::getBelsByTile(int x, int y) const
{
    BelRange br;
    br.b.cursor = 0;
    br.e.cursor = 0;
    NPNR_ASSERT("TODO");
    return br;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());
    NPNR_ASSERT("TODO");
    return PORT_INOUT;
}

std::vector<std::pair<IdString, std::string>> Arch::getBelAttrs(BelId bel) const
{
    std::vector<std::pair<IdString, std::string>> ret;
    return ret;
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    auto pin_name = pin.str(this);
    auto bel_type = getBelType(bel);
    if (bel_type == id_SLICE_LUT6) {
        // For all LUT based inputs and outputs (I1-I6,O,OQ,OMUX) then change the I/O into the LUT
        if (pin_name[0] == 'I' || pin_name[0] == 'O') {
            switch (torc_info->bel_to_loc[bel.index].z) {
            case 0:
            case 4:
                pin_name[0] = 'A';
                break;
            case 1:
            case 5:
                pin_name[0] = 'B';
                break;
            case 2:
            case 6:
                pin_name[0] = 'C';
                break;
            case 3:
            case 7:
                pin_name[0] = 'D';
                break;
            default:
                throw;
            }
        }
    } else if (bel_type == id_PS7 || bel_type == id_MMCME2_ADV) {
        // e.g. Convert DDRARB[0] -> DDRARB0
        pin_name.erase(std::remove_if(pin_name.begin(), pin_name.end(), boost::is_any_of("[]")), pin_name.end());
    }

    auto site_index = torc_info->bel_to_site_index[bel.index];
    const auto &site = torc_info->sites.getSite(site_index);
    auto &tw = site.getPinTilewire(pin_name);

    if (tw.isUndefined())
        log_error("no wire found for site '%s' pin '%s' \n", torc_info->bel_to_name(bel.index).c_str(),
                  pin_name.c_str());

    return torc_info->tilewire_to_wire(tw);
}

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    NPNR_ASSERT("TODO");
    return ret;
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdString name) const
{
    WireId ret;
    if (wire_by_name.empty()) {
        for (int i = 0; i < torc_info->num_wires; i++)
            wire_by_name[id(torc_info->wire_to_name(i))] = i;
    }

    auto it = wire_by_name.find(name);
    if (it != wire_by_name.end())
        ret.index = it->second;

    return ret;
}

IdString Arch::getWireType(WireId wire) const
{
    NPNR_ASSERT(wire != WireId());
    NPNR_ASSERT("TODO");
    return IdString();
}

// -----------------------------------------------------------------------
std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const
{
    std::vector<std::pair<IdString, std::string>> ret;
    NPNR_ASSERT("TODO");
    return ret;
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    PipId ret;

    if (pip_by_name.empty()) {
        for (int i = 0; i < torc_info->num_pips; i++) {
            PipId pip;
            pip.index = i;
            pip_by_name[getPipName(pip)] = i;
        }
    }

    auto it = pip_by_name.find(name);
    if (it != pip_by_name.end())
        ret.index = it->second;

    return ret;
}

IdString Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());

    ExtendedWireInfo ewi_src(*torc_info->ddb, torc_info->pip_to_arc[pip.index].getSourceTilewire());
    ExtendedWireInfo ewi_dst(*torc_info->ddb, torc_info->pip_to_arc[pip.index].getSinkTilewire());
    std::stringstream pip_name;
    pip_name << ewi_src.mTileName << "." << ewi_src.mWireName << ".->." << ewi_dst.mWireName;
    return id(pip_name.str());
}

std::vector<std::pair<IdString, std::string>> Arch::getPipAttrs(PipId pip) const
{
    std::vector<std::pair<IdString, std::string>> ret;
    NPNR_ASSERT("TODO");
    return ret;
}

// -----------------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const { return getBelByName(id(pin)); }

std::string Arch::getBelPackagePin(BelId bel) const
{
    NPNR_ASSERT("TODO");
    return "";
}

// -----------------------------------------------------------------------

GroupId Arch::getGroupByName(IdString name) const
{
    for (auto g : getGroups())
        if (getGroupName(g) == name)
            return g;
    return GroupId();
}

IdString Arch::getGroupName(GroupId group) const
{
    std::string suffix;

    switch (group.type) {
        NPNR_ASSERT("TODO");
    default:
        return IdString();
    }

    return id("X" + std::to_string(group.x) + "/Y" + std::to_string(group.y) + "/" + suffix);
}

std::vector<GroupId> Arch::getGroups() const
{
    std::vector<GroupId> ret;
    NPNR_ASSERT("TODO");
    return ret;
}

std::vector<BelId> Arch::getGroupBels(GroupId group) const
{
    std::vector<BelId> ret;
    return ret;
}

std::vector<WireId> Arch::getGroupWires(GroupId group) const
{
    std::vector<WireId> ret;
    return ret;
}

std::vector<PipId> Arch::getGroupPips(GroupId group) const
{
    std::vector<PipId> ret;
    NPNR_ASSERT("TODO");
    return ret;
}

std::vector<GroupId> Arch::getGroupGroups(GroupId group) const
{
    std::vector<GroupId> ret;
    NPNR_ASSERT("TODO");
    return ret;
}

// -----------------------------------------------------------------------

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

// -----------------------------------------------------------------------

bool Arch::place() { return placer1(getCtx(), Placer1Cfg(getCtx())); }

bool Arch::route() { return router1(getCtx(), Router1Cfg(getCtx())); }

// -----------------------------------------------------------------------

DecalXY Arch::getBelDecal(BelId bel) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_BEL;
    decalxy.decal.index = bel.index;
    decalxy.decal.active = bel_to_cell.at(bel.index) != nullptr;
    return decalxy;
}

DecalXY Arch::getWireDecal(WireId wire) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_WIRE;
    decalxy.decal.index = wire.index;
    decalxy.decal.active = wire_to_net.at(wire.index) != nullptr;
    return decalxy;
}

DecalXY Arch::getPipDecal(PipId pip) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_PIP;
    decalxy.decal.index = pip.index;
    decalxy.decal.active = pip_to_net.at(pip.index) != nullptr;
    return decalxy;
};

DecalXY Arch::getGroupDecal(GroupId group) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_GROUP;
    decalxy.decal.index = (group.type << 16) | (group.x << 8) | (group.y);
    decalxy.decal.active = true;
    return decalxy;
};

std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const
{
    std::vector<GraphicElement> ret;

    if (decal.type == DecalId::TYPE_BEL) {
        BelId bel;
        bel.index = decal.index;
        auto bel_type = getBelType(bel);
        int x = torc_info->bel_to_loc[bel.index].x;
        int y = torc_info->bel_to_loc[bel.index].y;
        int z = torc_info->bel_to_loc[bel.index].z;
        if (bel_type == id_SLICE_LUT6) {
            GraphicElement el;
            /*if (z>3) {
                z = z - 4;
                x -= logic_cell_x2- logic_cell_x1;
            }*/
            el.type = GraphicElement::TYPE_BOX;
            el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
            el.x1 = x + logic_cell_x1;
            el.x2 = x + logic_cell_x2;
            el.y1 = y + logic_cell_y1 + (z)*logic_cell_pitch;
            el.y2 = y + logic_cell_y2 + (z)*logic_cell_pitch;
            ret.push_back(el);
        }
    }

    return ret;
}

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    if (cell->type == id_SLICE_LUT6) {
        if (fromPort.index >= id_I1.index && fromPort.index <= id_I6.index) {
            if (toPort == id_O) {
                delay.delay = 124; // Tilo
                return true;
            }
            if (toPort == id_OQ) {
                delay.delay = 95; // Tas
                return true;
            }
        }
        if (fromPort == id_CLK) {
            if (toPort == id_OQ) {
                delay.delay = 456; // Tcko
                return true;
            }
        }
    } else if (cell->type == id_BUFGCTRL) {
        return true;
    }
    return false;
}

// Get the port class, also setting clockPort to associated clock if applicable
TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    if (cell->type == id_SLICE_LUT6) {
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        if (port == id_CIN)
            return TMG_COMB_INPUT;
        if (port == id_COUT)
            return TMG_COMB_OUTPUT;
        if (port == id_O) {
            // LCs with no inputs are constant drivers
            if (cell->lcInfo.inputCount == 0)
                return TMG_IGNORE;
            return TMG_COMB_OUTPUT;
        }
        if (cell->lcInfo.dffEnable) {
            clockInfoCount = 1;
            if (port == id_OQ)
                return TMG_REGISTER_OUTPUT;
            return TMG_REGISTER_INPUT;
        } else {
            return TMG_COMB_INPUT;
        }
        // TODO
        // if (port == id_OMUX)
    } else if (cell->type == id_IOB33 || cell->type == id_IOB18) {
        if (port == id_I)
            return TMG_STARTPOINT;
        else if (port == id_O)
            return TMG_ENDPOINT;
    } else if (cell->type == id_BUFGCTRL) {
        if (port == id_O)
            return TMG_COMB_OUTPUT;
        return TMG_COMB_INPUT;
    } else if (cell->type == id_PS7) {
        // TODO
        return TMG_IGNORE;
    } else if (cell->type == id_MMCME2_ADV) {
        return TMG_IGNORE;
    }
    log_error("no timing info for port '%s' of cell type '%s'\n", port.c_str(this), cell->type.c_str(this));
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo info;
    if (cell->type == id_SLICE_LUT6) {
        info.clock_port = id_CLK;
        info.edge = cell->lcInfo.negClk ? FALLING_EDGE : RISING_EDGE;
        if (port == id_OQ) {
            bool has_clktoq = getCellDelay(cell, id_CLK, id_OQ, info.clockToQ);
            NPNR_ASSERT(has_clktoq);
        } else {
            info.setup.delay = 124; // Tilo
            info.hold.delay = 0;
        }
    } else {
        NPNR_ASSERT_FALSE("unhandled cell type in getPortClockingInfo");
    }
    return info;
}

bool Arch::isGlobalNet(const NetInfo *net) const
{
    if (net == nullptr)
        return false;
    return net->driver.cell != nullptr && net->driver.cell->type == id_BUFGCTRL && net->driver.port == id_O;
}

// Assign arch arg info
void Arch::assignArchInfo()
{
    for (auto &net : getCtx()->nets) {
        NetInfo *ni = net.second.get();
        if (isGlobalNet(ni))
            ni->is_global = true;
        ni->is_enable = false;
        ni->is_reset = false;
        for (auto usr : ni->users) {
            if (is_enable_port(this, usr))
                ni->is_enable = true;
            if (is_reset_port(this, usr))
                ni->is_reset = true;
        }
    }
    for (auto &cell : getCtx()->cells) {
        CellInfo *ci = cell.second.get();
        assignCellInfo(ci);
    }
}

void Arch::assignCellInfo(CellInfo *cell)
{
    cell->belType = cell->type;
    if (cell->type == id_SLICE_LUT6) {
        cell->lcInfo.dffEnable = bool_or_default(cell->params, id_DFF_ENABLE);
        cell->lcInfo.carryEnable = bool_or_default(cell->params, id_CARRY_ENABLE);
        cell->lcInfo.negClk = bool_or_default(cell->params, id_NEG_CLK);
        cell->lcInfo.clk = get_net_or_empty(cell, id_CLK);
        cell->lcInfo.cen = get_net_or_empty(cell, id_CEN);
        cell->lcInfo.sr = get_net_or_empty(cell, id_SR);
        cell->lcInfo.inputCount = 0;
        if (get_net_or_empty(cell, id_I1))
            cell->lcInfo.inputCount++;
        if (get_net_or_empty(cell, id_I2))
            cell->lcInfo.inputCount++;
        if (get_net_or_empty(cell, id_I3))
            cell->lcInfo.inputCount++;
        if (get_net_or_empty(cell, id_I4))
            cell->lcInfo.inputCount++;
        if (get_net_or_empty(cell, id_I5))
            cell->lcInfo.inputCount++;
        if (get_net_or_empty(cell, id_I6))
            cell->lcInfo.inputCount++;
    }
}

NEXTPNR_NAMESPACE_END

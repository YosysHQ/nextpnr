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
#include "cells.h"
#include "gfx.h"
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "router1.h"
#include "util.h"

#include <boost/serialization/unique_ptr.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
struct nextpnr_binary_iarchive : public boost::archive::binary_iarchive {
    nextpnr_binary_iarchive(boost::iostreams::filtering_istreambuf &ifs, NEXTPNR_NAMESPACE::BaseCtx* ctx, const std::string& inDeviceName, const std::string& inPackageName) : boost::archive::binary_iarchive(ifs), ctx(ctx), inDeviceName(inDeviceName), inPackageName(inPackageName) {}
    NEXTPNR_NAMESPACE::BaseCtx *ctx;
    std::string inDeviceName, inPackageName;
};
struct nextpnr_binary_oarchive : public boost::archive::binary_oarchive {
    nextpnr_binary_oarchive(boost::iostreams::filtering_ostreambuf &ofs, NEXTPNR_NAMESPACE::BaseCtx* ctx) : boost::archive::binary_oarchive(ofs), ctx(ctx) {}
    NEXTPNR_NAMESPACE::BaseCtx *ctx;
};

#include "torc/common/DirectoryTree.hpp"

//#define TORC_INFO_DB "torc_info.ar"

NEXTPNR_NAMESPACE_BEGIN

std::unique_ptr<const TorcInfo> torc_info;
TorcInfo::TorcInfo(BaseCtx *ctx, const std::string &inDeviceName, const std::string &inPackageName)
        : TorcInfo(inDeviceName, inPackageName)
{
    static const boost::regex re_loc(".+_X(\\d+)Y(\\d+)");
    boost::cmatch what;
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
        const auto &tileInfo = tiles.getTileInfo(site.getTileIndex());
        if (!boost::regex_match(tileInfo.getName(), what, re_loc))
            throw;
        const auto x = boost::lexical_cast<int>(what.str(1));
        const auto y = boost::lexical_cast<int>(what.str(2));

        if (type == "SLICEL" || type == "SLICEM") {
            bel_to_site_index.push_back(i);
            bel_to_site_index.push_back(i);
            bel_to_site_index.push_back(i);
            bel_to_site_index.push_back(i);
            site_index_to_type[i] = id_SLICE_LUT6;
            const auto site_name = site.getName();            
            const auto site_name_back = site_name.back();
            if (site_name_back == '0' || site_name_back == '2' || site_name_back == '4' || site_name_back == '6' ||
                site_name_back == '8') {
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

    const boost::regex re_124("(.+_)?[NESW][NESWLR](\\d)((BEG(_[NS])?)|(END(_[NS])?)|[A-E])?\\d(_\\d)?");
    const boost::regex re_L("(.+_)?L(H|V|VB)(_L)?\\d+(_\\d)?");
    const boost::regex re_BYP("BYP(_ALT)?\\d");
    const boost::regex re_BYP_B("BYP_[BL]\\d");
    const boost::regex re_BOUNCE_NS("(BYP|FAN)_BOUNCE_[NS]3_\\d");
    const boost::regex re_FAN("FAN(_ALT)?\\d");
    const boost::regex re_CLB_I1_6("CLBL[LM]_(L|LL|M)_[A-D]([1-6])");
    const boost::regex bufg_i("(CMT|CLK)_BUFG_BUFGCTRL\\d+_I0");
    const boost::regex bufg_o("(CMT|CLK)_BUFG_BUFGCTRL\\d+_O");
    const boost::regex int_clk("CLK(_L)?[01]");
    const boost::regex gclk("GCLK_(L_)?B\\d+(_EAST|_WEST)?");
    std::unordered_map</*TileTypeIndex*/ unsigned, std::vector<delay_t>> delay_lookup;
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
                if (currentSegment.getAnchorTileIndex() != tileIndex)
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
                    if (boost::regex_match(wire_name, what, re_124)) {
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
                    } else if (boost::regex_match(wire_name, what, re_L)) {
                        std::string l(what[2]);
                        if (l == "H")
                            tile_delays[wireIndex] = 360;
                        else if (l == "VB")
                            tile_delays[wireIndex] = 300;
                        else if (l == "V")
                            tile_delays[wireIndex] = 350;
                        else
                            throw;
                    } else if (boost::regex_match(wire_name, what, re_BYP)) {
                        tile_delays[wireIndex] = 190;
                    } else if (boost::regex_match(wire_name, what, re_BYP_B)) {
                    } else if (boost::regex_match(wire_name, what, re_FAN)) {
                        tile_delays[wireIndex] = 190;
                    } else if (boost::regex_match(wire_name, what, re_CLB_I1_6)) {
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
    wire_to_tilewire.shrink_to_fit();
    wire_to_delay.shrink_to_fit();
    num_wires = wire_to_tilewire.size();

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
        arcs.clear();

        const auto &tileInfo = tiles.getTileInfo(currentTilewire.getTileIndex());
        const auto tileTypeName = tiles.getTileTypeName(tileInfo.getTypeIndex());
        const bool clb = boost::starts_with(
                tileTypeName, "CLB"); // Disable all CLB route-throughs (i.e. LUT in->out, LUT A->AMUX, for now)

        arcs.clear();
        const_cast<DDB &>(*ddb).expandSegmentSinks(currentTilewire, arcs, DDB::eExpandDirectionNone,
                                                   false /* inUseTied */, true /*inUseRegular */,
                                                   true /* inUseIrregular */, !clb /* inUseRoutethrough */);

        auto &pips = wire_to_pips_downhill[w.index];
        pips.reserve(arcs.size());
        const bool clk_tile = boost::starts_with(tileTypeName, "CMT") || boost::starts_with(tileTypeName, "CLK");
        const bool int_tile = boost::starts_with(tileTypeName, "INT");

        for (const auto &a : arcs) {
            // Disable BUFG I0 -> O routethrough
            if (clk_tile) {
                ewi.set(a.getSourceTilewire());
                if (boost::regex_match(ewi.mWireName, bufg_i)) {
                    ewi.set(a.getSinkTilewire());
                    if (boost::regex_match(ewi.mWireName, bufg_o))
                        continue;
                }
            }
            // Disable CLK inputs from being driven from the fabric (must be from global clock network)
            else if (int_tile) {
                ewi.set(a.getSinkTilewire());
                if (boost::regex_match(ewi.mWireName, int_clk)) {
                    ewi.set(a.getSourceTilewire());
                    if (!boost::regex_match(ewi.mWireName, gclk))
                        continue;
                }
            }
            pips.emplace_back(p);
            pip_to_arc.emplace_back(a);
            // arc_to_pip.emplace(a, p.index);
            const auto &tw = a.getSinkTilewire();
            pip_to_dst_wire.emplace_back(tilewire_to_wire(tw));
            ++p.index;
        }
        pips.shrink_to_fit();
    }
    pip_to_arc.shrink_to_fit();
    num_pips = pip_to_arc.size();

    pip_to_dst_wire.reserve(num_pips);
    for (const auto &arc : pip_to_arc) {
        const auto &tw = arc.getSinkTilewire();
        pip_to_dst_wire.emplace_back(tilewire_to_wire(tw));
    }

    height = (int)tiles.getRowCount();
    width = (int)tiles.getColCount();
}
TorcInfo::TorcInfo(const std::string& inDeviceName, const std::string &inPackageName)
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
    torc::common::DirectoryTree directoryTree("/opt/torc/src/torc");
    if (args.type == ArchArgs::Z020) {
#ifdef TORC_INFO_DB
        std::ifstream ifs(TORC_INFO_DB, std::ios::binary);
        if (ifs) {
            boost::iostreams::filtering_istreambuf fifs;
            fifs.push(boost::iostreams::zlib_decompressor());
            fifs.push(ifs);
            nextpnr_binary_iarchive ia(fifs, this, "xc7z020", args.package);
            ia >> torc_info;
        } else
#endif
        {
            torc_info = std::unique_ptr<TorcInfo>(new TorcInfo(this, "xc7z020", args.package));
#ifdef TORC_INFO_DB
            std::ofstream ofs(TORC_INFO_DB, std::ios::binary);
            if (ofs) {
                boost::iostreams::filtering_ostreambuf fofs;
                fofs.push(boost::iostreams::zlib_compressor());
                fofs.push(ofs);
                nextpnr_binary_oarchive oa(fofs, this);
                oa << torc_info;
            }
#endif
        }
    } else {
        log_error("Unsupported XC7 chip type.\n");
    }

        width = torc_info->width;
        height = torc_info->height;
    /*if (getCtx()->verbose)*/ {
        log_info("Number of bels:  %d\n", torc_info->num_bels);
        log_info("Number of wires: %d\n", torc_info->num_wires);
        log_info("Number of pips:  %d\n", torc_info->num_pips);
    }

    //    package_info = nullptr;
    //    for (int i = 0; i < chip_info->num_packages; i++) {
    //        if (chip_info->packages_data[i].name.get() == args.package) {
    //            package_info = &(chip_info->packages_data[i]);
    //            break;
    //        }
    //    }
    //    if (package_info == nullptr)
    //        log_error("Unsupported package '%s'.\n", args.package.c_str());

    // bel_carry.resize(chip_info->num_bels);
    bel_to_cell.resize(torc_info->num_bels);
    wire_to_net.resize(torc_info->num_wires);
    pip_to_net.resize(torc_info->num_pips);
    // switches_locked.resize(chip_info->num_switches);
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const
{
    if (args.type == ArchArgs::Z020) {
        return "z020";
    } else {
        log_error("Unsupported XC7 chip type.\n");
    }
}

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::Z020)
        return id("z020");
    return IdString();
}

// -----------------------------------------------------------------------

static bool endsWith(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
}

BelId Arch::getBelByName(IdString name) const
{
    std::string n = name.str(this);
    int ndx = 0;
    if (endsWith(n,"_A") || endsWith(n,"_B") || endsWith(n,"_C") || endsWith(n,"_D"))
    {
        ndx = (int)(n.back() - 'A');
        n = n.substr(0,n.size()-2);
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

    br.b.cursor = Arch::getBelByLocation(Loc(x, y, 0)).index;
    br.e.cursor = br.b.cursor;

    if (br.e.cursor != -1) {
        while (br.e.cursor < chip_info->num_bels && chip_info->bel_data[br.e.cursor].x == x &&
               chip_info->bel_data[br.e.cursor].y == y)
            br.e.cursor++;
    }

    return br;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = chip_info->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = chip_info->bel_data[bel.index].bel_wires.get();

    if (num_bel_wires < 7) {
        for (int i = 0; i < num_bel_wires; i++) {
            if (bel_wires[i].port == pin.index)
                return PortType(bel_wires[i].type);
        }
    } else {
        int b = 0, e = num_bel_wires - 1;
        while (b <= e) {
            int i = (b + e) / 2;
            if (bel_wires[i].port == pin.index)
                return PortType(bel_wires[i].type);
            if (bel_wires[i].port > pin.index)
                e = i - 1;
            else
                b = i + 1;
        }
    }

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
    } else if (bel_type == id_PS7) {
        // e.g. Convert DDRARB[0] -> DDRARB0
        boost::erase_all(pin_name, "[");
        boost::erase_all(pin_name, "]");
    }

    auto site_index = torc_info->bel_to_site_index[bel.index];
    const auto &site = torc_info->sites.getSite(site_index);
    auto &tw = site.getPinTilewire(pin_name);

    if (tw.isUndefined())
        log_error("no wire found for site '%s' pin '%s' \n", torc_info->bel_to_name(bel.index).c_str(),
                  pin_name.c_str());

    return torc_info->tilewire_to_wire(tw);

    //    NPNR_ASSERT(bel != BelId());
    //
    //    int num_bel_wires = chip_info->bel_data[bel.index].num_bel_wires;
    //    const BelWirePOD *bel_wires = chip_info->bel_data[bel.index].bel_wires.get();
    //
    //    if (num_bel_wires < 7) {
    //        for (int i = 0; i < num_bel_wires; i++) {
    //            if (bel_wires[i].port == pin.index) {
    //                ret.index = bel_wires[i].wire_index;
    //                break;
    //            }
    //        }
    //    } else {
    //        int b = 0, e = num_bel_wires - 1;
    //        while (b <= e) {
    //            int i = (b + e) / 2;
    //            if (bel_wires[i].port == pin.index) {
    //                ret.index = bel_wires[i].wire_index;
    //                break;
    //            }
    //            if (bel_wires[i].port > pin.index)
    //                e = i - 1;
    //            else
    //                b = i + 1;
    //        }
    //    }
    //
    //return ret;
}

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;

/*    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = chip_info->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = chip_info->bel_data[bel.index].bel_wires.get();

    for (int i = 0; i < num_bel_wires; i++)
        ret.push_back(IdString(bel_wires[i].port));
*/
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
    //    switch (chip_info->wire_data[wire.index].type) {
    //    case WireInfoPOD::WIRE_TYPE_NONE:
    //        return IdString();
    //    case WireInfoPOD::WIRE_TYPE_GLB2LOCAL:
    //        return id("GLB2LOCAL");
    //    case WireInfoPOD::WIRE_TYPE_GLB_NETWK:
    //        return id("GLB_NETWK");
    //    case WireInfoPOD::WIRE_TYPE_LOCAL:
    //        return id("LOCAL");
    //    case WireInfoPOD::WIRE_TYPE_LUTFF_IN:
    //        return id("LUTFF_IN");
    //    case WireInfoPOD::WIRE_TYPE_LUTFF_IN_LUT:
    //        return id("LUTFF_IN_LUT");
    //    case WireInfoPOD::WIRE_TYPE_LUTFF_LOUT:
    //        return id("LUTFF_LOUT");
    //    case WireInfoPOD::WIRE_TYPE_LUTFF_OUT:
    //        return id("LUTFF_OUT");
    //    case WireInfoPOD::WIRE_TYPE_LUTFF_COUT:
    //        return id("LUTFF_COUT");
    //    case WireInfoPOD::WIRE_TYPE_LUTFF_GLOBAL:
    //        return id("LUTFF_GLOBAL");
    //    case WireInfoPOD::WIRE_TYPE_CARRY_IN_MUX:
    //        return id("CARRY_IN_MUX");
    //    case WireInfoPOD::WIRE_TYPE_SP4_V:
    //        return id("SP4_V");
    //    case WireInfoPOD::WIRE_TYPE_SP4_H:
    //        return id("SP4_H");
    //    case WireInfoPOD::WIRE_TYPE_SP12_V:
    //        return id("SP12_V");
    //    case WireInfoPOD::WIRE_TYPE_SP12_H:
    //        return id("SP12_H");
    //    }
    return IdString();
}

// -----------------------------------------------------------------------
std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const
{
    std::vector<std::pair<IdString, std::string>> ret;
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

    //#if 1
    //    int x = chip_info->pip_data[pip.index].x;
    //    int y = chip_info->pip_data[pip.index].y;
    //
    //    std::string src_name = chip_info->wire_data[chip_info->pip_data[pip.index].src].name.get();
    //    std::replace(src_name.begin(), src_name.end(), '/', '.');
    //
    //    std::string dst_name = chip_info->wire_data[chip_info->pip_data[pip.index].dst].name.get();
    //    std::replace(dst_name.begin(), dst_name.end(), '/', '.');
    //
    //    return id("X" + std::to_string(x) + "/Y" + std::to_string(y) + "/" + src_name + ".->." + dst_name);
    //#else
    //    return id(chip_info->pip_data[pip.index].name.get());
    //#endif
}

std::vector<std::pair<IdString, std::string>> Arch::getPipAttrs(PipId pip) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    return ret;
}

// -----------------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const { return getBelByName(id(pin)); }

std::string Arch::getBelPackagePin(BelId bel) const
{
    //    for (int i = 0; i < package_info->num_pins; i++) {
    //        if (package_info->pins[i].bel_index == bel.index) {
    //            return std::string(package_info->pins[i].name.get());
    //        }
    //    }
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
    case GroupId::TYPE_FRAME:
        suffix = "tile";
        break;
    case GroupId::TYPE_MAIN_SW:
        suffix = "main_sw";
        break;
    case GroupId::TYPE_LOCAL_SW:
        suffix = "local_sw";
        break;
    case GroupId::TYPE_LC0_SW:
        suffix = "lc0_sw";
        break;
    case GroupId::TYPE_LC1_SW:
        suffix = "lc1_sw";
        break;
    case GroupId::TYPE_LC2_SW:
        suffix = "lc2_sw";
        break;
    case GroupId::TYPE_LC3_SW:
        suffix = "lc3_sw";
        break;
    case GroupId::TYPE_LC4_SW:
        suffix = "lc4_sw";
        break;
    case GroupId::TYPE_LC5_SW:
        suffix = "lc5_sw";
        break;
    case GroupId::TYPE_LC6_SW:
        suffix = "lc6_sw";
        break;
    case GroupId::TYPE_LC7_SW:
        suffix = "lc7_sw";
        break;
    default:
        return IdString();
    }

    return id("X" + std::to_string(group.x) + "/Y" + std::to_string(group.y) + "/" + suffix);
}

std::vector<GroupId> Arch::getGroups() const
{
    std::vector<GroupId> ret;
/*
    for (int y = 0; y < chip_info->height; y++) {
        for (int x = 0; x < chip_info->width; x++) {
            TileType type = chip_info->tile_grid[y * chip_info->width + x];
            if (type == TILE_NONE)
                continue;

            GroupId group;
            group.type = GroupId::TYPE_FRAME;
            group.x = x;
            group.y = y;
            // ret.push_back(group);

            group.type = GroupId::TYPE_MAIN_SW;
            ret.push_back(group);

            group.type = GroupId::TYPE_LOCAL_SW;
            ret.push_back(group);

            if (type == TILE_LOGIC) {
                group.type = GroupId::TYPE_LC0_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC1_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC2_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC3_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC4_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC5_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC6_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC7_SW;
                ret.push_back(group);
            }
        }
    }*/
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
    return ret;
}

std::vector<GroupId> Arch::getGroupGroups(GroupId group) const
{
    std::vector<GroupId> ret;
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
    } else if (cell->type == id_IOB33) {
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

// outside of any namespace
BOOST_SERIALIZATION_SPLIT_FREE(Segments::SegmentReference)
BOOST_SERIALIZATION_SPLIT_FREE(CompactSegmentIndex)
BOOST_SERIALIZATION_SPLIT_FREE(TileIndex)
BOOST_SERIALIZATION_SPLIT_FREE(Arc)
BOOST_SERIALIZATION_SPLIT_FREE(Tilewire)
BOOST_SERIALIZATION_SPLIT_FREE(WireIndex)
BOOST_SERIALIZATION_SPLIT_FREE(SiteIndex)
BOOST_SERIALIZATION_SPLIT_FREE(NEXTPNR_NAMESPACE::IdString)

namespace boost { namespace serialization {

template<class Archive>
inline void load_construct_data(
    Archive & ar, NEXTPNR_NAMESPACE::TorcInfo * t, const unsigned int file_version
){
    const auto& inDeviceName = static_cast<const nextpnr_binary_iarchive&>(ar).inDeviceName;
    const auto& inPackageName = static_cast<const nextpnr_binary_iarchive&>(ar).inPackageName;
    ::new(t)NEXTPNR_NAMESPACE::TorcInfo(inDeviceName, inPackageName);
}

template<class Archive>
void save(Archive& ar, const Segments::SegmentReference& o, unsigned int) {
    ar & o.getCompactSegmentIndex();
    ar & o.getAnchorTileIndex();
}
template<class Archive>
void load(Archive& ar, Segments::SegmentReference& o, unsigned int) {
    CompactSegmentIndex i;
    TileIndex j;
    ar & i;
    ar & j;
    o = Segments::SegmentReference(i, j);
}
#define SERIALIZE_POD(__T__) \
template<class Archive> \
void save(Archive& ar, const __T__& o, unsigned int) { \
    ar & static_cast<__T__::pod>(o); \
} \
template<class Archive> \
void load(Archive& ar, __T__& o, unsigned int) { \
    __T__::pod i; \
    ar & i; \
    o = __T__(i); \
}
SERIALIZE_POD(CompactSegmentIndex)
SERIALIZE_POD(TileIndex)
SERIALIZE_POD(WireIndex)
SERIALIZE_POD(SiteIndex)
template<class Archive>
void save(Archive& ar, const Arc& o, unsigned int) {
    ar & o.getSourceTilewire();
    ar & o.getSinkTilewire();
}
template<class Archive>
void load(Archive& ar, Arc& o, unsigned int) {
    Tilewire s, t;
    ar & s;
    ar & t;
    o = Arc(s, t);
}
template<class Archive>
void save(Archive& ar, const Tilewire& o, unsigned int) {
    ar & o.getTileIndex();
    ar & o.getWireIndex();
}
template<class Archive>
void load(Archive& ar, Tilewire& o, unsigned int) {
    TileIndex i;
    WireIndex j;
    ar & i;
    ar & j;
    o.setTileIndex(TileIndex(i));
    o.setWireIndex(WireIndex(j));
}
template<class Archive>
void serialize(Archive& ar, NEXTPNR_NAMESPACE::Loc& o, unsigned int) {
    ar & o.x;
    ar & o.y;
    ar & o.z;
}
template<class Archive>
void serialize(Archive& ar, NEXTPNR_NAMESPACE::DelayInfo& o, unsigned int) {
    ar & o.delay;
}
template<class Archive>
void save(Archive& ar, const NEXTPNR_NAMESPACE::IdString& o, unsigned int) {
    const std::string i = o.str(static_cast<const nextpnr_binary_oarchive&>(ar).ctx);
    ar & i;
}
template<class Archive>
void load(Archive& ar, NEXTPNR_NAMESPACE::IdString& o, unsigned int) {
    std::string i;
    ar & i;
    o = static_cast<nextpnr_binary_iarchive&>(ar).ctx->id(i);
}
#define SERIALIZE_INDEX(__T__) \
template<class Archive> \
void serialize(Archive& ar, __T__& o, unsigned int) { \
    ar & o.index; \
}
SERIALIZE_INDEX(NEXTPNR_NAMESPACE::BelId)
SERIALIZE_INDEX(NEXTPNR_NAMESPACE::WireId)
SERIALIZE_INDEX(NEXTPNR_NAMESPACE::PipId)
}} // namespace boost::serialization

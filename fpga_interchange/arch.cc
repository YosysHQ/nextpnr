/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018-19  David Shah <david@symbioticeda.com>
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
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <cmath>
#include <cstring>
#include <queue>
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "timing.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

static std::pair<std::string, std::string> split_identifier_name(const std::string &name)
{
    size_t first_slash = name.find('/');
    NPNR_ASSERT(first_slash != std::string::npos);
    return std::make_pair(name.substr(0, first_slash), name.substr(first_slash + 1));
};

static std::pair<std::string, std::string> split_identifier_name_dot(const std::string &name)
{
    size_t first_dot = name.find('.');
    NPNR_ASSERT(first_dot != std::string::npos);
    return std::make_pair(name.substr(0, first_dot), name.substr(first_dot + 1));
};

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);

#include "constids.inc"

#undef X
}

// -----------------------------------------------------------------------

static const ChipInfoPOD *get_chip_info(const RelPtr<ChipInfoPOD> *ptr) { return ptr->get(); }

Arch::Arch(ArchArgs args) : args(args)
{
    try {
        blob_file.open(args.chipdb);
        if (args.chipdb.empty() || !blob_file.is_open())
            log_error("Unable to read chipdb %s\n", args.chipdb.c_str());
        const char *blob = reinterpret_cast<const char *>(blob_file.data());
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(blob));
    } catch (...) {
        log_error("Unable to read chipdb %s\n", args.chipdb.c_str());
    }

    tileStatus.resize(chip_info->num_tiles);
    for (int i = 0; i < chip_info->num_tiles; i++) {
        tileStatus[i].boundcells.resize(chip_info->tile_types[chip_info->tiles[i].type].num_bels);
    }
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const { return chip_info->name.get(); }

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const { return IdString(); }

// -----------------------------------------------------------------------

void Arch::setup_byname() const
{
    if (tile_by_name.empty()) {
        for (int i = 0; i < chip_info->num_tiles; i++) {
            tile_by_name[chip_info->tiles[i].name.get()] = i;
        }
    }

    if (site_by_name.empty()) {
        for (int i = 0; i < chip_info->num_tiles; i++) {
            auto &tile = chip_info->tiles[i];
            auto &tile_type = chip_info->tile_types[tile.type];
            for (int j = 0; j < tile_type.number_sites; j++) {
                auto &site = chip_info->sites[tile.sites[j]];
                site_by_name[site.name.get()] = std::make_pair(i, j);
            }
        }
    }
}

BelId Arch::getBelByName(IdString name) const
{
    BelId ret;

    setup_byname();

    auto split = split_identifier_name(name.str(this));

    int tile, site;
    std::tie(tile, site) = site_by_name.at(split.first);
    auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];
    IdString belname = id(split.second);
    for (int i = 0; i < tile_info.num_bels; i++) {
        if (tile_info.bel_data[i].site == site && tile_info.bel_data[i].name == belname.index) {
            ret.tile = tile;
            ret.index = i;
            break;
        }
    }

    return ret;
}

BelRange Arch::getBelsByTile(int x, int y) const
{
    BelRange br;

    br.b.cursor_tile = getTileIndex(x, y);
    br.e.cursor_tile = br.b.cursor_tile;
    br.b.cursor_index = 0;
    br.e.cursor_index = chip_info->tile_types[chip_info->tiles[br.b.cursor_tile].type].num_bels;
    br.b.chip = chip_info;
    br.e.chip = chip_info;
    if (br.e.cursor_index == -1)
        ++br.e.cursor_index;
    else
        ++br.e;
    return br;
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = locInfo(bel).bel_data[bel.index].num_bel_wires;
    const int32_t *ports = locInfo(bel).bel_data[bel.index].ports.get();
    for (int i = 0; i < num_bel_wires; i++) {
        if (ports[i] == pin.index) {
            const int32_t *wires = locInfo(bel).bel_data[bel.index].wires.get();
            int32_t wire_index = wires[i];
            return canonicalWireId(chip_info, bel.tile, wire_index);
        }
    }

    // Port could not be found!
    return WireId();
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = locInfo(bel).bel_data[bel.index].num_bel_wires;
    const int32_t *ports = locInfo(bel).bel_data[bel.index].ports.get();

    for (int i = 0; i < num_bel_wires; i++) {
        if (ports[i] == pin.index) {
            const int32_t *types = locInfo(bel).bel_data[bel.index].types.get();
            return PortType(types[i]);
        }
    }


    return PORT_INOUT;
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdString name) const
{
    if (wire_by_name_cache.count(name))
        return wire_by_name_cache.at(name);
    WireId ret;
    setup_byname();

    const std::string &s = name.str(this);
    auto sp = split_identifier_name(s.substr(8));
    auto iter = site_by_name.find(sp.first);
    if (iter != site_by_name.end()) {
        int tile;
        int site;
        std::tie(tile, site) = iter->second;
        auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];
        IdString wirename = id(sp.second);
        for (int i = 0; i < tile_info.num_wires; i++) {
            if (tile_info.wire_data[i].site == site && tile_info.wire_data[i].name == wirename.index) {
                ret.tile = tile;
                ret.index = i;
                break;
            }
        }
    } else {
        auto sp = split_identifier_name(s);
        int tile = tile_by_name.at(sp.first);
        auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];
        IdString wirename = id(sp.second);
        for (int i = 0; i < tile_info.num_wires; i++) {
            if (tile_info.wire_data[i].site == -1 && tile_info.wire_data[i].name == wirename.index) {
                ret.tile = tile;
                ret.index = i;
                break;
            }
        }
    }

    wire_by_name_cache[name] = ret;

    return ret;
}

IdString Arch::getWireType(WireId wire) const { return id(""); }
std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const
{
    return {};
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    if (pip_by_name_cache.count(name))
        return pip_by_name_cache.at(name);

    PipId ret;
    setup_byname();

    const std::string &s = name.str(this);
    auto sp = split_identifier_name(s.substr(8));
    auto iter = site_by_name.find(sp.first);
    if (iter != site_by_name.end()) {
        int tile;
        int site;
        std::tie(tile, site) = iter->second;
        auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];
        auto sp3 = split_identifier_name(sp.second);
        IdString belname = id(sp3.first);
        IdString pinname = id(sp3.second);
        for (int i = 0; i < tile_info.num_pips; i++) {
            if (tile_info.pip_data[i].site == site && tile_info.pip_data[i].bel == belname.index &&
                tile_info.pip_data[i].extra_data == pinname.index) {
                ret.tile = tile;
                ret.index = i;
                break;
            }
        }
    } else {
        int tile = tile_by_name.at(sp.first);
        auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];

        auto spn = split_identifier_name_dot(sp.second);
        int fromwire = std::stoi(spn.first), towire = std::stoi(spn.second);

        for (int i = 0; i < tile_info.num_pips; i++) {
            if (tile_info.pip_data[i].src_index == fromwire &&
                tile_info.pip_data[i].dst_index == towire) {
                ret.tile = tile;
                ret.index = i;
                break;
            }
        }
    }

    pip_by_name_cache[name] = ret;

    return ret;
}

IdString Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());
    if (locInfo(pip).pip_data[pip.index].site != -1) {
        auto site_index = chip_info->tiles[pip.tile].sites[locInfo(pip).pip_data[pip.index].site];
        auto &site = chip_info->sites[site_index];
        return id(site.name.get() + std::string("/") + IdString(locInfo(pip).pip_data[pip.index].bel).str(this) + "/" +
                  IdString(locInfo(pip).wire_data[locInfo(pip).pip_data[pip.index].src_index].name).str(this));
    } else {
        return id(std::string(chip_info->tiles[pip.tile].name.get()) + "/" +
                  std::to_string(locInfo(pip).pip_data[pip.index].src_index) + "." +
                  std::to_string(locInfo(pip).pip_data[pip.index].dst_index));
    }
}

IdString Arch::getPipType(PipId pip) const { return id("PIP"); }

std::vector<std::pair<IdString, std::string>> Arch::getPipAttrs(PipId pip) const { return {}; }

// -----------------------------------------------------------------------

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    NPNR_ASSERT(bel != BelId());

    // FIXME: The std::vector here can be replaced by a int32_t -> IdString
    // range wrapper.
    int num_bel_wires = locInfo(bel).bel_data[bel.index].num_bel_wires;
    const int32_t *ports = locInfo(bel).bel_data[bel.index].ports.get();

    for (int i = 0; i < num_bel_wires; i++) {
        ret.push_back(IdString(ports[i]));
    }

    return ret;
}

BelId Arch::getBelByLocation(Loc loc) const
{
    BelId bi;
    if (loc.x >= chip_info->width || loc.y >= chip_info->height)
        return BelId();
    bi.tile = getTileIndex(loc);
    auto &li = locInfo(bi);

    if(loc.z >= li.num_bels) {
        return BelId();
    } else {
        bi.index = loc.z;
        return bi;
    }
}

std::vector<std::pair<IdString, std::string>> Arch::getBelAttrs(BelId bel) const { return {}; }

// -----------------------------------------------------------------------

delay_t Arch::estimateDelay(WireId src, WireId dst, bool debug) const
{
    // FIXME: Implement when adding timing-driven place and route.
    return 0;
}

ArcBounds Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    int dst_tile = dst.tile == -1 ? chip_info->nodes[dst.index].tile_wires[0].tile : dst.tile;
    int src_tile = src.tile == -1 ? chip_info->nodes[src.index].tile_wires[0].tile : src.tile;

    int x0, x1, y0, y1;
    x0 = src_tile % chip_info->width;
    x1 = x0;
    y0 = src_tile / chip_info->width;
    y1 = y0;
    auto expand = [&](int x, int y) {
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    };

    expand(dst_tile % chip_info->width, dst_tile / chip_info->width);

    if (source_locs.count(src))
        expand(source_locs.at(src).x, source_locs.at(src).y);

    if (sink_locs.count(dst)) {
        expand(sink_locs.at(dst).x, sink_locs.at(dst).y);
    }

    return {x0, y0, x1, y1};
}

delay_t Arch::getBoundingBoxCost(WireId src, WireId dst, int distance) const
{
    // FIXME: Implement when adding timing-driven place and route.
    return 0;
}

delay_t Arch::getWireRipupDelayPenalty(WireId wire) const
{
    return getRipupDelayPenalty();
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    // FIXME: Implement when adding timing-driven place and route.
    return 0;
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

// -----------------------------------------------------------------------

bool Arch::pack()
{
    // FIXME: Implement this
    return false;
}

bool Arch::place()
{
    // FIXME: Implement this
    return false;
}

bool Arch::route()
{
    // FIXME: Implement this
    return false;
}

// -----------------------------------------------------------------------

std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const
{
    return {};
}

DecalXY Arch::getBelDecal(BelId bel) const
{
    DecalXY decalxy;
    return decalxy;
}

DecalXY Arch::getWireDecal(WireId wire) const
{
    DecalXY decalxy;
    return decalxy;
}

DecalXY Arch::getPipDecal(PipId pip) const { return {}; };

DecalXY Arch::getGroupDecal(GroupId pip) const { return {}; };

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    // FIXME: Implement when adding timing-driven place and route.
    return false;
}

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    // FIXME: Implement when adding timing-driven place and route.
    return TMG_IGNORE;
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    // FIXME: Implement when adding timing-driven place and route.
    TimingClockingInfo info;
    return info;
}

#ifdef WITH_HEAP
const std::string Arch::defaultPlacer = "heap";
#else
const std::string Arch::defaultPlacer = "sa";
#endif

const std::vector<std::string> Arch::availablePlacers = {"sa",
#ifdef WITH_HEAP
                                                         "heap"
#endif
};

const std::string Arch::defaultRouter = "router2";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

NEXTPNR_NAMESPACE_END

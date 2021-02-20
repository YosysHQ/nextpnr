/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Wolf <claire@symbioticeda.com>
 *  Copyright (C) 2018-19  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2021  Symbiflow Authors
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
#include "constraints.impl.h"
#include "fpga_interchange.h"
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "timing.h"
#include "util.h"
#include "xdc.h"

NEXTPNR_NAMESPACE_BEGIN
struct SiteBelPair
{
    std::string site;
    IdString bel;

    SiteBelPair() {}
    SiteBelPair(std::string site, IdString bel) : site(site), bel(bel) {}

    bool operator==(const SiteBelPair &other) const { return site == other.site && bel == other.bel; }
};
NEXTPNR_NAMESPACE_END

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX SiteBelPair>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX SiteBelPair &site_bel) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<std::string>()(site_bel.site));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(site_bel.bel));
        return seed;
    }
};

NEXTPNR_NAMESPACE_BEGIN

static std::pair<std::string, std::string> split_identifier_name_dot(const std::string &name)
{
    size_t first_dot = name.find('.');
    NPNR_ASSERT(first_dot != std::string::npos);
    return std::make_pair(name.substr(0, first_dot), name.substr(first_dot + 1));
};

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx) {}

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

    // Read strings from constids into IdString database, checking that list
    // is unique and matches expected constid value.
    const RelSlice<RelPtr<char>> &constids = *chip_info->constids;
    for (size_t i = 0; i < constids.size(); ++i) {
        IdString::initialize_add(this, constids[i].get(), i + 1);
    }

    // Sanity check cell name ids.
    const CellMapPOD &cell_map = *chip_info->cell_map;
    int32_t first_cell_id = cell_map.cell_names[0];
    for (int32_t i = 0; i < cell_map.cell_names.ssize(); ++i) {
        log_assert(cell_map.cell_names[i] == i + first_cell_id);
    }

    io_port_types.emplace(this->id("$nextpnr_ibuf"));
    io_port_types.emplace(this->id("$nextpnr_obuf"));
    io_port_types.emplace(this->id("$nextpnr_iobuf"));

    if (!this->args.package.empty()) {
        IdString package = this->id(this->args.package);
        package_index = -1;
        for (size_t i = 0; i < chip_info->packages.size(); ++i) {
            if (IdString(chip_info->packages[i].package) == package) {
                NPNR_ASSERT(package_index == -1);
                package_index = i;
            }
        }

        if (package_index == -1) {
            log_error("Could not find package '%s' in chipdb.\n", this->args.package.c_str());
        }
    } else {
        // Default to first package.
        NPNR_ASSERT(chip_info->packages.size() > 0);
        if (chip_info->packages.size() == 1) {
            IdString package_name(chip_info->packages[0].package);
            this->args.package = package_name.str(this);
            package_index = 0;
        } else {
            log_info(
                    "Package must be specified (with --package arg) when multiple packages are available, packages:\n");
            for (const auto &package : chip_info->packages) {
                log_info(" - %s\n", IdString(package.package).c_str(this));
            }
            log_error("--package is required!\n");
        }
    }

    std::unordered_set<SiteBelPair> site_bel_pads;
    for (const auto &package_pin : chip_info->packages[package_index].pins) {
        IdString site(package_pin.site);
        IdString bel(package_pin.bel);
        site_bel_pads.emplace(SiteBelPair(site.str(this), bel));
    }

    for (BelId bel : getBels()) {
        auto &bel_data = bel_info(chip_info, bel);
        const SiteInstInfoPOD &site = get_site_inst(bel);
        auto iter = site_bel_pads.find(SiteBelPair(site.site_name.get(), IdString(bel_data.name)));
        if (iter != site_bel_pads.end()) {
            pads.emplace(bel);
        }
    }

    explain_constraints = false;

    int tile_type_index = 0;
    size_t max_tag_count = 0;
    for (const TileTypeInfoPOD &tile_type : chip_info->tile_types) {
        max_tag_count = std::max(max_tag_count, tile_type.tags.size());

        auto &type_definition = constraints.definitions[tile_type_index++];
        for (const ConstraintTagPOD &tag : tile_type.tags) {
            type_definition.emplace_back();
            auto &definition = type_definition.back();
            definition.prefix = IdString(tag.tag_prefix);
            definition.default_state = IdString(tag.default_state);
            NPNR_ASSERT(tag.states.size() < kMaxState);

            definition.states.reserve(tag.states.size());
            for (auto state : tag.states) {
                definition.states.push_back(IdString(state));
            }
        }

        // Logic BELs (e.g. placable BELs) should always appear first in the
        // bel data list.
        //
        // When iterating over BELs this property is depended on to skip
        // non-placable BELs (e.g. routing BELs and site ports).
        bool in_logic_bels = true;
        for (const BelInfoPOD &bel_info : tile_type.bel_data) {
            if (in_logic_bels && bel_info.category != BEL_CATEGORY_LOGIC) {
                in_logic_bels = false;
            }

            if (!in_logic_bels) {
                NPNR_ASSERT(bel_info.category != BEL_CATEGORY_LOGIC);
            }
        }
    }

    default_tags.resize(max_tag_count);
}


void Arch::init() {
    dedicated_interconnect.init(getCtx());
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const { return chip_info->name.get(); }

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const { return IdString(); }

// -----------------------------------------------------------------------

void Arch::setup_byname() const
{
    if (tile_by_name.empty()) {
        for (int i = 0; i < chip_info->tiles.ssize(); i++) {
            tile_by_name[id(chip_info->tiles[i].name.get())] = i;
        }
    }

    if (site_by_name.empty()) {
        for (int i = 0; i < chip_info->tiles.ssize(); i++) {
            auto &tile = chip_info->tiles[i];
            auto &tile_type = chip_info->tile_types[tile.type];
            for (size_t j = 0; j < tile_type.site_types.size(); j++) {
                auto &site = chip_info->sites[tile.sites[j]];
                site_by_name[id(site.name.get())] = std::make_pair(i, j);
            }
        }
    }
}

BelId Arch::getBelByName(IdStringList name) const
{
    BelId ret;
    if (name.ids.size() != 2) {
        return BelId();
    }

    setup_byname();

    int tile, site;
    std::tie(tile, site) = site_by_name.at(name.ids[0]);
    auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];
    IdString belname = name.ids[1];
    for (int i = 0; i < tile_info.bel_data.ssize(); i++) {
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

    br.b.cursor_tile = get_tile_index(x, y);
    br.e.cursor_tile = br.b.cursor_tile;
    br.b.cursor_index = 0;
    br.e.cursor_index = chip_info->tile_types[chip_info->tiles[br.b.cursor_tile].type].bel_data.size();
    br.b.chip = chip_info;
    br.e.chip = chip_info;

    if (br.b != br.e) {
        ++br.e;
    }
    return br;
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int pin_index = get_bel_pin_index(bel, pin);

    auto &bel_data = bel_info(chip_info, bel);
    NPNR_ASSERT(pin_index >= 0 && pin_index < bel_data.num_bel_wires);

    const int32_t *wires = bel_data.wires.get();
    int32_t wire_index = wires[pin_index];
    if (wire_index < 0) {
        // This BEL pin is not connected.
        return WireId();
    } else {
        return canonical_wire(chip_info, bel.tile, wire_index);
    }
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int pin_index = get_bel_pin_index(bel, pin);
    auto &bel_data = bel_info(chip_info, bel);
    NPNR_ASSERT(pin_index >= 0 && pin_index < bel_data.num_bel_wires);
    const int32_t *types = bel_data.types.get();
    return PortType(types[pin_index]);
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdStringList name) const
{
    WireId ret;
    if (name.ids.size() != 2) {
        return WireId();
    }

    setup_byname();

    auto iter = site_by_name.find(name.ids[0]);
    if (iter != site_by_name.end()) {
        int tile;
        int site;
        std::tie(tile, site) = iter->second;
        auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];
        IdString wirename = name.ids[1];
        for (int i = 0; i < tile_info.wire_data.ssize(); i++) {
            if (tile_info.wire_data[i].site == site && tile_info.wire_data[i].name == wirename.index) {
                ret.tile = tile;
                ret.index = i;
                break;
            }
        }
    } else {
        int tile = tile_by_name.at(name.ids[0]);
        auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];
        IdString wirename = name.ids[1];
        for (int i = 0; i < tile_info.wire_data.ssize(); i++) {
            if (tile_info.wire_data[i].site == -1 && tile_info.wire_data[i].name == wirename.index) {
                int32_t node = chip_info->tiles[tile].tile_wire_to_node[i];
                if (node == -1) {
                    // Not a nodal wire
                    ret.tile = tile;
                    ret.index = i;
                } else {
                    // Is a nodal wire, set tile to -1
                    ret.tile = -1;
                    ret.index = node;
                }
                break;
            }
        }
    }

    return ret;
}

IdString Arch::getWireType(WireId wire) const { return id(""); }
std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const { return {}; }

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdStringList name) const
{
    // PIP name structure:
    // Tile PIP: <tile name>/<source wire name>.<destination wire name>
    // Site PIP: <site name>/<bel name>/<input bel pin name>
    // Site pin: <site name>/<bel name>
    // Psuedo site PIP: <site name>/<source wire name>.<destination wire name>

    setup_byname();

    if (name.ids.size() == 3) {
        // This is a Site PIP.
        IdString site_name = name.ids[0];
        IdString belname = name.ids[1];
        IdString pinname = name.ids[2];

        int tile;
        int site;
        std::tie(tile, site) = site_by_name.at(site_name);
        auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];

        std::array<IdString, 2> ids{name.ids[0], belname};
        BelId bel = getBelByName(IdStringList(ids));
        NPNR_ASSERT(bel != BelId());

        int pin_index = get_bel_pin_index(bel, pinname);
        NPNR_ASSERT(pin_index >= 0);

        for (int i = 0; i < tile_info.pip_data.ssize(); i++) {
            if (tile_info.pip_data[i].site == site && tile_info.pip_data[i].bel == bel.index &&
                tile_info.pip_data[i].extra_data == pin_index) {

                PipId ret;
                ret.tile = tile;
                ret.index = i;
                return ret;
            }
        }
    } else {
        auto iter = site_by_name.find(name.ids[0]);
        if (iter != site_by_name.end()) {
            // This is either a site pin or a psuedo site pip.
            // psuedo site pips are <site>/<src site wire>.<dst site wire>
            // site pins are <site>/<bel>
            int tile;
            int site;
            std::tie(tile, site) = iter->second;
            auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];

            std::string pip_second = name.ids[1].str(this);
            auto split = pip_second.find('.');
            if (split == std::string::npos) {
                // This is a site pin!
                BelId bel = getBelByName(name);
                NPNR_ASSERT(bel != BelId());

                for (int i = 0; i < tile_info.pip_data.ssize(); i++) {
                    if (tile_info.pip_data[i].site == site && tile_info.pip_data[i].bel == bel.index) {

                        PipId ret;
                        ret.tile = tile;
                        ret.index = i;
                        return ret;
                    }
                }
            } else {
                // This is a psuedo site pip!
                IdString src_site_wire = id(pip_second.substr(0, split));
                IdString dst_site_wire = id(pip_second.substr(split + 1));
                int32_t src_index = -1;
                int32_t dst_index = -1;

                for (int i = 0; i < tile_info.wire_data.ssize(); i++) {
                    if (tile_info.wire_data[i].site == site && tile_info.wire_data[i].name == src_site_wire.index) {
                        src_index = i;
                        if (dst_index != -1) {
                            break;
                        }
                    }
                    if (tile_info.wire_data[i].site == site && tile_info.wire_data[i].name == dst_site_wire.index) {
                        dst_index = i;
                        if (src_index != -1) {
                            break;
                        }
                    }
                }

                NPNR_ASSERT(src_index != -1);
                NPNR_ASSERT(dst_index != -1);

                for (int i = 0; i < tile_info.pip_data.ssize(); i++) {
                    if (tile_info.pip_data[i].site == site && tile_info.pip_data[i].src_index == src_index &&
                        tile_info.pip_data[i].dst_index == dst_index) {

                        PipId ret;
                        ret.tile = tile;
                        ret.index = i;
                        return ret;
                    }
                }
            }
        } else {
            int tile = tile_by_name.at(name.ids[0]);
            auto &tile_info = chip_info->tile_types[chip_info->tiles[tile].type];

            std::string pip_second = name.ids[1].str(this);
            auto spn = split_identifier_name_dot(pip_second);
            auto src_wire_name = id(spn.first);
            auto dst_wire_name = id(spn.second);

            int32_t src_index = -1;
            int32_t dst_index = -1;
            for (int i = 0; i < tile_info.wire_data.ssize(); i++) {
                if (tile_info.wire_data[i].site == -1 && tile_info.wire_data[i].name == src_wire_name.index) {
                    src_index = i;
                    if (dst_index != -1) {
                        break;
                    }
                }
                if (tile_info.wire_data[i].site == -1 && tile_info.wire_data[i].name == dst_wire_name.index) {
                    dst_index = i;
                    if (src_index != -1) {
                        break;
                    }
                }
            }

            NPNR_ASSERT(src_index != -1);
            NPNR_ASSERT(dst_index != -1);

            for (int i = 0; i < tile_info.pip_data.ssize(); i++) {
                if (tile_info.pip_data[i].src_index == src_index && tile_info.pip_data[i].dst_index == dst_index) {

                    PipId ret;
                    ret.tile = tile;
                    ret.index = i;
                    return ret;
                }
            }
        }
    }

    return PipId();
}

IdStringList Arch::getPipName(PipId pip) const
{
    // PIP name structure:
    // Tile PIP: <tile name>/<source wire name>.<destination wire name>
    // Psuedo site PIP: <site name>/<input site wire>.<output site wire>
    // Site PIP: <site name>/<bel name>/<input bel pin name>
    // Site pin: <site name>/<bel name>
    NPNR_ASSERT(pip != PipId());
    auto &tile = chip_info->tiles[pip.tile];
    auto &tile_type = loc_info(chip_info, pip);
    auto &pip_info = tile_type.pip_data[pip.index];
    if (pip_info.site != -1) {
        // This is either a site pin or a site pip.
        auto &site = get_site_inst(pip);
        auto &bel = tile_type.bel_data[pip_info.bel];
        IdString bel_name(bel.name);
        if (bel.category == BEL_CATEGORY_LOGIC) {
            // This is a psuedo pip
            IdString src_wire_name = IdString(tile_type.wire_data[pip_info.src_index].name);
            IdString dst_wire_name = IdString(tile_type.wire_data[pip_info.dst_index].name);
            IdString pip = id(src_wire_name.str(this) + "." + dst_wire_name.str(this));
            std::array<IdString, 2> ids{id(site.name.get()), pip};
            return IdStringList(ids);

        } else if (bel.category == BEL_CATEGORY_ROUTING) {
            // This is a site pip.
            IdString pin_name(bel.ports[pip_info.extra_data]);
            std::array<IdString, 3> ids{id(site.name.get()), bel_name, pin_name};
            return IdStringList(ids);
        } else {
            NPNR_ASSERT(bel.category == BEL_CATEGORY_SITE_PORT);
            // This is a site pin, just the name of the BEL is a unique identifier.
            std::array<IdString, 2> ids{id(site.name.get()), bel_name};
            return IdStringList(ids);
        }
    } else {
        // This is a tile pip.
        IdString src_wire_name = IdString(tile_type.wire_data[pip_info.src_index].name);
        IdString dst_wire_name = IdString(tile_type.wire_data[pip_info.dst_index].name);
        IdString pip = id(src_wire_name.str(this) + "." + dst_wire_name.str(this));
        std::array<IdString, 2> ids{id(std::string(tile.name.get())), pip};
        return IdStringList(ids);
    }
}

IdString Arch::getPipType(PipId pip) const { return id("PIP"); }

std::vector<std::pair<IdString, std::string>> Arch::getPipAttrs(PipId pip) const { return {}; }

// -----------------------------------------------------------------------

BelId Arch::getBelByLocation(Loc loc) const
{
    BelId bi;
    if (loc.x >= chip_info->width || loc.y >= chip_info->height)
        return BelId();
    bi.tile = get_tile_index(loc);
    auto &li = loc_info(chip_info, bi);

    if (loc.z >= li.bel_data.ssize()) {
        return BelId();
    } else {
        bi.index = loc.z;
        return bi;
    }
}

std::vector<std::pair<IdString, std::string>> Arch::getBelAttrs(BelId bel) const { return {}; }

// -----------------------------------------------------------------------

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

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

// -----------------------------------------------------------------------

bool Arch::pack()
{
    pack_ports();
    return true;
}

bool Arch::place()
{
    std::string placer = str_or_default(settings, id("placer"), defaultPlacer);

    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.criticalityExponent = 7;
        cfg.alpha = 0.08;
        cfg.beta = 0.4;
        cfg.placeAllAtOnce = true;
        cfg.hpwl_scale_x = 1;
        cfg.hpwl_scale_y = 2;
        cfg.spread_scale_x = 2;
        cfg.spread_scale_y = 1;
        cfg.solverTolerance = 0.6e-6;
        if (!placer_heap(getCtx(), cfg))
            return false;
    } else if (placer == "sa") {
        if (!placer1(getCtx(), Placer1Cfg(getCtx())))
            return false;
    } else {
        log_error("FPGA interchange architecture does not support placer '%s'\n", placer.c_str());
    }

    getCtx()->attrs[getCtx()->id("step")] = std::string("place");
    archInfoToAttributes();
    return true;
}

bool Arch::route()
{
    std::string router = str_or_default(settings, id("router"), defaultRouter);

    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("FPGA interchange architecture does not support router '%s'\n", router.c_str());
    }
    getCtx()->attrs[getCtx()->id("step")] = std::string("route");
    archInfoToAttributes();
    return result;
}

// -----------------------------------------------------------------------

std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const { return {}; }

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

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    // FIXME: Implement something to push the A* router in the right direction.
    return 0;
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    // FIXME: Implement when adding timing-driven place and route.
    return 0;
}

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
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

// -----------------------------------------------------------------------

void Arch::read_logical_netlist(const std::string &filename)
{
    FpgaInterchange::read_logical_netlist(getCtx(), filename);
}
void Arch::write_physical_netlist(const std::string &filename) const
{
    FpgaInterchange::write_physical_netlist(getCtx(), filename);
}

void Arch::parse_xdc(const std::string &filename)
{
    TclInterp interp(getCtx());
    auto result = Tcl_EvalFile(interp.interp, filename.c_str());
    if (result != TCL_OK) {
        log_error("Error in %s:%d => %s\n", filename.c_str(), Tcl_GetErrorLine(interp.interp),
                  Tcl_GetStringResult(interp.interp));
    }
}

std::string Arch::get_part() const
{
    // FIXME: Need a map between device / package / speed grade and part.
    return chip_info->name.get() + args.package + "-1";
}

// -----------------------------------------------------------------------

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

void Arch::map_cell_pins(CellInfo *cell, int32_t mapping) const
{
    cell->cell_mapping = mapping;
    cell->cell_bel_pins.clear();

    const CellBelMapPOD &cell_pin_map = chip_info->cell_map->cell_bel_map[mapping];

    for (const auto &pin_map : cell_pin_map.common_pins) {
        IdString cell_pin(pin_map.cell_pin);
        IdString bel_pin(pin_map.bel_pin);

        if (cell_pin.str(this) == "GND") {
            // FIXME: Tie this pin to the GND net
            continue;
        }
        if (cell_pin.str(this) == "VCC") {
            // FIXME: Tie this pin to the VCC net
            continue;
        }

        cell->cell_bel_pins[cell_pin].push_back(bel_pin);
    }

    for (const auto &parameter_pin_map : cell_pin_map.parameter_pins) {
        IdString param_key(parameter_pin_map.key);
        std::string param_value = IdString(parameter_pin_map.value).c_str(this);

        auto iter = cell->params.find(param_key);
        if (iter == cell->params.end()) {
            continue;
        }

        if (param_value != iter->second.as_string()) {
            continue;
        }

        for (const auto &pin_map : parameter_pin_map.pins) {
            IdString cell_pin(pin_map.cell_pin);
            IdString bel_pin(pin_map.bel_pin);

            if (cell_pin.str(this) == "GND") {
                // FIXME: Tie this pin to the GND net
                continue;
            }
            if (cell_pin.str(this) == "VCC") {
                // FIXME: Tie this pin to the VCC net
                continue;
            }

            cell->cell_bel_pins[cell_pin].push_back(bel_pin);
        }
    }
}

void Arch::map_port_pins(BelId bel, CellInfo *cell) const
{
    IdStringRange pins = getBelPins(bel);
    IdString pin = get_only_value(pins);

    NPNR_ASSERT(cell->ports.size() == 1);
    cell->cell_bel_pins[cell->ports.begin()->first].clear();
    cell->cell_bel_pins[cell->ports.begin()->first].push_back(pin);
}

bool Arch::is_net_within_site(const NetInfo &net) const
{
    if (net.driver.cell == nullptr || net.driver.cell->bel == BelId()) {
        return false;
    }

    BelId driver = net.driver.cell->bel;
    int32_t site = bel_info(chip_info, driver).site;
    NPNR_ASSERT(site >= 0);

    for (const auto &user : net.users) {
        if (user.cell == nullptr || user.cell->bel == BelId()) {
            return false;
        }
        BelId user_bel = user.cell->bel;

        if (user_bel.tile != driver.tile) {
            return false;
        }

        if (bel_info(chip_info, user_bel).site != site) {
            return false;
        }
    }

    return true;
}

size_t Arch::get_cell_type_index(IdString cell_type) const
{
    const CellMapPOD &cell_map = *chip_info->cell_map;
    int cell_offset = cell_type.index - cell_map.cell_names[0];
    if ((cell_offset < 0 || cell_offset >= cell_map.cell_names.ssize())) {
        log_error("Cell %s is not a placable element.\n", cell_type.c_str(this));
    }
    NPNR_ASSERT(cell_map.cell_names[cell_offset] == cell_type.index);

    return cell_offset;
}

// Instance constraint templates.
template void Arch::ArchConstraints::bindBel(Arch::ArchConstraints::TagState *, const Arch::ConstraintRange);
template void Arch::ArchConstraints::unbindBel(Arch::ArchConstraints::TagState *, const Arch::ConstraintRange);
template bool Arch::ArchConstraints::isValidBelForCellType(const Context *, uint32_t,
                                                           const Arch::ArchConstraints::TagState *,
                                                           const Arch::ConstraintRange, IdString, IdString, BelId,
                                                           bool) const;

NEXTPNR_NAMESPACE_END

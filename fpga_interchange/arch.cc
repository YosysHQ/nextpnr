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

#include "arch.h"

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

// Include tcl.h late because it messed with defines and let them leave the
// scope of the header.
#include <tcl.h>

//#define DEBUG_BINDING
//#define USE_LOOKAHEAD
//#define DEBUG_CELL_PIN_MAPPING

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

    id_GND = id("GND");
    id_VCC = id("VCC");
    id_BUFGCTRL = id("BUFGCTRL");
    id_BUFG = id("BUFG");

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

    // Initially LutElement vectors for each tile type.
    tile_type_index = 0;
    lut_elements.resize(chip_info->tile_types.size());
    for (const TileTypeInfoPOD &tile_type : chip_info->tile_types) {
        std::vector<LutElement> &elements = lut_elements[tile_type_index++];
        elements.reserve(tile_type.lut_elements.size());
        for (auto &lut_element : tile_type.lut_elements) {
            elements.emplace_back();

            LutElement &element = elements.back();
            element.width = lut_element.width;
            for (auto &lut_bel : lut_element.lut_bels) {
                auto result = element.lut_bels.emplace(IdString(lut_bel.name), LutBel());
                NPNR_ASSERT(result.second);
                LutBel &lut = result.first->second;

                lut.low_bit = lut_bel.low_bit;
                lut.high_bit = lut_bel.high_bit;

                lut.pins.reserve(lut_bel.pins.size());
                for (size_t i = 0; i < lut_bel.pins.size(); ++i) {
                    IdString pin(lut_bel.pins[i]);
                    lut.pins.push_back(pin);
                    lut.pin_to_index[pin] = i;
                }
            }

            element.compute_pin_order();
        }
    }

    // Map lut cell types to their LutCellPOD
    for (const LutCellPOD &lut_cell : chip_info->cell_map->lut_cells) {
        IdString cell_type(lut_cell.cell);
        auto result = lut_cells.emplace(cell_type, &lut_cell);
        NPNR_ASSERT(result.second);
    }

    raw_bin_constant = std::regex("[01]+", std::regex_constants::ECMAScript | std::regex_constants::optimize);
    verilog_bin_constant =
            std::regex("([0-9]+)'b([01]+)", std::regex_constants::ECMAScript | std::regex_constants::optimize);
    verilog_hex_constant =
            std::regex("([0-9]+)'h([0-9a-fA-F]+)", std::regex_constants::ECMAScript | std::regex_constants::optimize);

    default_tags.resize(max_tag_count);
}

void Arch::init()
{
#ifdef USE_LOOKAHEAD
    lookahead.init(getCtx(), getCtx());
#endif
    dedicated_interconnect.init(getCtx());
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const { return chip_info->name.get(); }

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const { return IdString(); }

// -----------------------------------------------------------------------

void Arch::setup_byname() const
{
    by_name_mutex.lock();

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

    by_name_mutex.unlock();
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
        auto tile_type_idx = chip_info->tiles[tile].type;
        auto &tile_info = chip_info->tile_types[tile_type_idx];

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
            auto tile_type_idx = chip_info->tiles[tile].type;
            auto &tile_info = chip_info->tile_types[tile_type_idx];

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
            size_t tile_type_idx = chip_info->tiles[tile].type;
            auto &tile_info = chip_info->tile_types[tile_type_idx];

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

    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;

    int src_x, src_y;
    get_tile_x_y(src_tile, &src_x, &src_y);

    int dst_x, dst_y;
    get_tile_x_y(dst_tile, &dst_x, &dst_y);

    auto expand = [&](int x, int y) {
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    };

    expand(src_x, src_y);
    expand(dst_x, dst_y);

    if (source_locs.count(src))
        expand(source_locs.at(src).x, source_locs.at(src).y);

    if (sink_locs.count(dst)) {
        expand(sink_locs.at(dst).x, sink_locs.at(dst).y);
    }

    x0 -= 2;
    y0 -= 2;
    x1 += 2;
    y1 += 2;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= chip_info->width) {
        x1 = chip_info->width - 1;
    }
    if (y1 >= chip_info->height) {
        y1 = chip_info->height - 1;
    }

    return {x0, y0, x1, y1};
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

// -----------------------------------------------------------------------

bool Arch::pack()
{
    decode_lut_cells();
    merge_constant_nets();
    pack_ports();
    return true;
}

bool Arch::place()
{
    std::string placer = str_or_default(settings, id("placer"), defaultPlacer);

    // Re-map BEL pins without constant pins
    for (BelId bel : getBels()) {
        CellInfo *cell = getBoundBelCell(bel);
        if (cell != nullptr && cell->cell_mapping != -1) {
            map_cell_pins(cell, cell->cell_mapping, /*bind_constants=*/false);
        }
    }

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

    // Re-map BEL pins with constant pins
    for (BelId bel : getBels()) {
        CellInfo *cell = getBoundBelCell(bel);
        if (cell != nullptr && cell->cell_mapping != -1) {
            map_cell_pins(cell, cell->cell_mapping, /*bind_constants=*/true);
        }
    }

    absl::flat_hash_set<WireId> wires_to_unbind;
    for (auto &net_pair : nets) {
        for (auto &wire_pair : net_pair.second->wires) {
            WireId wire = wire_pair.first;
            if (wire_pair.second.strength != STRENGTH_PLACER) {
                // Only looking for bound placer wires
                continue;
            }

            const TileWireInfoPOD &wire_data = wire_info(wire);
            NPNR_ASSERT(wire_data.site != -1);

            wires_to_unbind.emplace(wire);
        }
    }

    for (WireId wire : wires_to_unbind) {
        unbindWire(wire);
    }

    for (auto &tile_pair : tileStatus) {
        for (auto &site_router : tile_pair.second.sites) {
            if (site_router.cells_in_site.empty()) {
                continue;
            }

            site_router.bindSiteRouting(getCtx());
        }
    }

    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("FPGA interchange architecture does not support router '%s'\n", router.c_str());
    }

    if (result) {
        result = route_vcc_to_unused_lut_pins();
    }

    getCtx()->attrs[getCtx()->id("step")] = std::string("route");
    archInfoToAttributes();

    return result;
}

bool Arch::route_vcc_to_unused_lut_pins()
{
    std::string router = str_or_default(settings, id("router"), defaultRouter);

    absl::flat_hash_map<WireId, const NetInfo *> bound_wires;
    for (auto &net_pair : nets) {
        const NetInfo *net = net_pair.second.get();
        for (auto &wire_pair : net->wires) {
            auto result = bound_wires.emplace(wire_pair.first, net);
            NPNR_ASSERT(result.first->second == net);

            PipId pip = wire_pair.second.pip;
            if (pip == PipId()) {
                continue;
            }

            const PipInfoPOD &pip_data = pip_info(chip_info, pip);
#ifdef DEBUG_LUT_MAPPING
            if (getCtx()->verbose) {
                log_info("Pip %s in use, has %zu pseudo wires!\n", nameOfPip(pip), pip_data.pseudo_cell_wires.size());
            }
#endif

            WireId wire;
            wire.tile = pip.tile;
            for (int32_t wire_index : pip_data.pseudo_cell_wires) {
                wire.index = wire_index;
                auto result = bound_wires.emplace(wire, net);
                NPNR_ASSERT(result.first->second == net);
            }
        }
    }

    // Fixup LUT vcc pins.
    IdString vcc_net_name(chip_info->constants->vcc_net_name);
    for (BelId bel : getBels()) {
        CellInfo *cell = getBoundBelCell(bel);
        if (cell == nullptr) {
            continue;
        }

        if (cell->lut_cell.vcc_pins.empty()) {
            continue;
        }

        for (auto bel_pin : cell->lut_cell.vcc_pins) {
            PortInfo port_info;
            port_info.name = bel_pin;
            port_info.type = PORT_IN;
            port_info.net = nullptr;

            WireId lut_pin_wire = getBelPinWire(bel, bel_pin);
            auto iter = bound_wires.find(lut_pin_wire);
            if (iter != bound_wires.end()) {
#ifdef DEBUG_LUT_MAPPING
                if (getCtx()->verbose) {
                    log_info("%s is now used as a LUT route-through, not tying to VCC\n", nameOfWire(lut_pin_wire));
                }
#endif
                continue;
            }

#ifdef DEBUG_LUT_MAPPING
            if (getCtx()->verbose) {
                log_info("%s is an unused LUT pin, tying to VCC\n", nameOfWire(lut_pin_wire));
            }
#endif

            auto result = cell->ports.emplace(bel_pin, port_info);
            if (result.second) {
                cell->cell_bel_pins[bel_pin].push_back(bel_pin);
                connectPort(vcc_net_name, cell->name, bel_pin);
                cell->const_ports.emplace(bel_pin);
            } else {
                NPNR_ASSERT(result.first->second.net == getNetByAlias(vcc_net_name));
                auto result2 = cell->cell_bel_pins.emplace(bel_pin, std::vector<IdString>({bel_pin}));
                NPNR_ASSERT(result2.first->second.at(0) == bel_pin);
                NPNR_ASSERT(result2.first->second.size() == 1);
            }
        }
    }

    if (router == "router1") {
        return router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        return true;
    } else {
        log_error("FPGA interchange architecture does not support router '%s'\n", router.c_str());
    }
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
#ifdef USE_LOOKAHEAD
    return lookahead.estimateDelay(getCtx(), src, dst);
#else
    return 0;
#endif
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    // FIXME: Implement when adding timing-driven place and route.
    int src_x, src_y;
    get_tile_x_y(net_info->driver.cell->bel.tile, &src_x, &src_y);

    int dst_x, dst_y;
    get_tile_x_y(sink.cell->bel.tile, &dst_x, &dst_y);

    delay_t base = 30 * std::min(std::abs(dst_x - src_x), 18) + 10 * std::max(std::abs(dst_x - src_x) - 18, 0) +
                   60 * std::min(std::abs(dst_y - src_y), 6) + 20 * std::max(std::abs(dst_y - src_y) - 6, 0) + 300;

    base = (base * 3) / 2;
    return base;
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

bool parameter_compare(const std::string &database_param, const std::string &netlist_param)
{

    bool result = false;
    try {
        size_t idx;
        int database_value = std::stoi(database_param, &idx, 0);
        int netlist_value = std::stoi(netlist_param, &idx, 2);

        result = database_value == netlist_value;
    } catch (std::invalid_argument &e) {
        result = database_param == netlist_param;
    } catch (std::out_of_range &e) {
        result = database_param == netlist_param;
    }

#ifdef DEBUG_CELL_PIN_MAPPING
    log_info("%s == %s => %d\n", database_param.c_str(), netlist_param.c_str(), result);
#endif
    return result;
}

void Arch::map_cell_pins(CellInfo *cell, int32_t mapping, bool bind_constants)
{
    cell->cell_mapping = mapping;
    if (cell->lut_cell.pins.empty()) {
        cell->cell_bel_pins.clear();
    } else {
        std::vector<IdString> cell_pin_to_remove;
        for (auto port_pair : cell->cell_bel_pins) {
            if (!cell->lut_cell.lut_pins.count(port_pair.first)) {
                cell_pin_to_remove.push_back(port_pair.first);
            }
        }

        for (IdString cell_pin : cell_pin_to_remove) {
            NPNR_ASSERT(cell->cell_bel_pins.erase(cell_pin));
        }
    }
    for (IdString const_port : cell->const_ports) {
        NPNR_ASSERT(cell->ports.erase(const_port));
    }

    const CellBelMapPOD &cell_pin_map = chip_info->cell_map->cell_bel_map[mapping];

    IdString gnd_net_name(chip_info->constants->gnd_net_name);
    IdString vcc_net_name(chip_info->constants->vcc_net_name);

    for (const auto &pin_map : cell_pin_map.common_pins) {
        IdString cell_pin(pin_map.cell_pin);
        IdString bel_pin(pin_map.bel_pin);

        // Skip assigned LUT pins, as they are already mapped!
        if (cell->lut_cell.lut_pins.count(cell_pin) && cell->cell_bel_pins.count(cell_pin)) {
            continue;
        }

        if (cell_pin == id_GND) {
            if (bind_constants) {
                PortInfo port_info;
                port_info.name = bel_pin;
                port_info.type = PORT_IN;
                port_info.net = nullptr;

                auto result = cell->ports.emplace(bel_pin, port_info);
                if (result.second) {
                    cell->cell_bel_pins[bel_pin].push_back(bel_pin);
                    connectPort(gnd_net_name, cell->name, bel_pin);
                    cell->const_ports.emplace(bel_pin);
                } else {
                    NPNR_ASSERT(result.first->second.net == getNetByAlias(gnd_net_name));
                    auto result2 = cell->cell_bel_pins.emplace(bel_pin, std::vector<IdString>({bel_pin}));
                    NPNR_ASSERT(result2.first->second.at(0) == bel_pin);
                    NPNR_ASSERT(result2.first->second.size() == 1);
                }
            }
            continue;
        }

        if (cell_pin == id_VCC) {
            if (bind_constants) {
                PortInfo port_info;
                port_info.name = bel_pin;
                port_info.type = PORT_IN;
                port_info.net = nullptr;

                auto result = cell->ports.emplace(bel_pin, port_info);
                if (result.second) {
                    cell->cell_bel_pins[bel_pin].push_back(bel_pin);
                    connectPort(vcc_net_name, cell->name, bel_pin);
                    cell->const_ports.emplace(bel_pin);
                } else {
                    NPNR_ASSERT(result.first->second.net == getNetByAlias(vcc_net_name));
                    auto result2 = cell->cell_bel_pins.emplace(bel_pin, std::vector<IdString>({bel_pin}));
                    NPNR_ASSERT(result2.first->second.at(0) == bel_pin);
                    NPNR_ASSERT(result2.first->second.size() == 1);
                }
            }
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

        if (!parameter_compare(param_value, iter->second.as_string())) {
            continue;
        }

#ifdef DEBUG_CELL_PIN_MAPPING
        log_info("parameter match on param_key %s\n", param_key.c_str(this));
#endif

        for (const auto &pin_map : parameter_pin_map.pins) {
            IdString cell_pin(pin_map.cell_pin);
            IdString bel_pin(pin_map.bel_pin);
#ifdef DEBUG_CELL_PIN_MAPPING
            log_info(" %s => %s\n", cell_pin.c_str(this), bel_pin.c_str(this));
#endif

            // Skip assigned LUT pins, as they are already mapped!
            if (cell->lut_cell.lut_pins.count(cell_pin) && cell->cell_bel_pins.count(cell_pin)) {
                continue;
            }

            if (cell_pin == id_GND) {
                if (bind_constants) {
                    PortInfo port_info;
                    port_info.name = bel_pin;
                    port_info.type = PORT_IN;
                    port_info.net = nullptr;

                    auto result = cell->ports.emplace(bel_pin, port_info);
                    if (result.second) {
                        cell->cell_bel_pins[bel_pin].push_back(bel_pin);
                        connectPort(gnd_net_name, cell->name, bel_pin);
                        cell->const_ports.emplace(bel_pin);
                    } else {
                        NPNR_ASSERT(result.first->second.net == getNetByAlias(gnd_net_name));
                        auto result2 = cell->cell_bel_pins.emplace(bel_pin, std::vector<IdString>({bel_pin}));
                        NPNR_ASSERT(result2.first->second.at(0) == bel_pin);
                        NPNR_ASSERT(result2.first->second.size() == 1);
                    }
                }
                continue;
            }

            if (cell_pin == id_VCC) {
                if (bind_constants) {
                    PortInfo port_info;
                    port_info.name = bel_pin;
                    port_info.type = PORT_IN;
                    port_info.net = nullptr;

                    auto result = cell->ports.emplace(bel_pin, port_info);
                    if (result.second) {
                        cell->cell_bel_pins[bel_pin].push_back(bel_pin);
                        connectPort(vcc_net_name, cell->name, bel_pin);
                        cell->const_ports.emplace(bel_pin);
                    } else {
                        NPNR_ASSERT(result.first->second.net == getNetByAlias(vcc_net_name));
                        auto result2 = cell->cell_bel_pins.emplace(bel_pin, std::vector<IdString>({bel_pin}));
                        NPNR_ASSERT(result2.first->second.at(0) == bel_pin);
                        NPNR_ASSERT(result2.first->second.size() == 1);
                    }
                }
                continue;
            }

            cell->cell_bel_pins[cell_pin].push_back(bel_pin);
        }
    }

#ifdef DEBUG_CELL_PIN_MAPPING
    log_info("Pin mapping for cell %s (type: %s)\n", cell->name.c_str(getCtx()), cell->type.c_str(getCtx()));
    for (auto &pin_pair : cell->cell_bel_pins) {
        log_info(" %s =>", pin_pair.first.c_str(getCtx()));
        for (IdString bel_pin : pin_pair.second) {
            log(" %s", bel_pin.c_str(getCtx()));
        }
        log("\n");
    }
#endif
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

void Arch::merge_constant_nets()
{
    NetInfo *gnd_net = nullptr;
    NetInfo *vcc_net = nullptr;

    bool need_gnd_source = false;
    bool need_vcc_source = false;

    IdString gnd_net_name(chip_info->constants->gnd_net_name);
    IdString gnd_cell_type(chip_info->constants->gnd_cell_name);
    IdString gnd_cell_port(chip_info->constants->gnd_cell_port);

    auto gnd_iter = nets.find(gnd_net_name);
    if (gnd_iter != nets.end()) {
        NPNR_ASSERT(gnd_iter->second->driver.cell != nullptr);
        NPNR_ASSERT(gnd_iter->second->driver.cell->type == gnd_cell_type);
        NPNR_ASSERT(gnd_iter->second->driver.port == gnd_cell_port);

        gnd_net = gnd_iter->second.get();
    } else {
        gnd_net = createNet(gnd_net_name);
        need_gnd_source = true;
    }

    IdString vcc_net_name(chip_info->constants->vcc_net_name);
    IdString vcc_cell_type(chip_info->constants->vcc_cell_name);
    IdString vcc_cell_port(chip_info->constants->vcc_cell_port);

    auto vcc_iter = nets.find(vcc_net_name);
    if (vcc_iter != nets.end()) {
        NPNR_ASSERT(vcc_iter->second->driver.cell != nullptr);
        NPNR_ASSERT(vcc_iter->second->driver.cell->type == vcc_cell_type);
        NPNR_ASSERT(vcc_iter->second->driver.port == vcc_cell_port);

        vcc_net = vcc_iter->second.get();
    } else {
        vcc_net = createNet(vcc_net_name);
        need_vcc_source = true;
    }

    std::vector<IdString> other_gnd_nets;
    std::vector<IdString> other_vcc_nets;

    for (auto &net_pair : nets) {
        if (net_pair.first == gnd_net_name) {
            NPNR_ASSERT(net_pair.second.get() == gnd_net);
            continue;
        }

        if (net_pair.first == vcc_net_name) {
            NPNR_ASSERT(net_pair.second.get() == vcc_net);
            continue;
        }

        NetInfo *net = net_pair.second.get();
        if (net->driver.cell == nullptr) {
            continue;
        }

        if (net->driver.cell->type == gnd_cell_type) {
            NPNR_ASSERT(net->driver.port == gnd_cell_port);

            other_gnd_nets.push_back(net_pair.first);

            if (need_gnd_source) {
                IdString driver_cell = net->driver.cell->name;
                disconnectPort(driver_cell, gnd_cell_port);
                connectPort(gnd_net_name, driver_cell, gnd_cell_port);
                need_gnd_source = false;
            }

            NPNR_ASSERT(net->driver.port == gnd_cell_port);
            std::vector<PortRef> users_copy = net->users;
            for (const PortRef &port_ref : users_copy) {
                IdString cell = port_ref.cell->name;
                disconnectPort(cell, port_ref.port);
                connectPort(gnd_net_name, cell, port_ref.port);
            }

            continue;
        }

        if (net->driver.cell->type == vcc_cell_type) {
            NPNR_ASSERT(net->driver.port == vcc_cell_port);

            other_vcc_nets.push_back(net_pair.first);

            if (need_vcc_source) {
                IdString driver_cell = net->driver.cell->name;
                disconnectPort(driver_cell, vcc_cell_port);
                connectPort(vcc_net_name, driver_cell, vcc_cell_port);
                need_vcc_source = false;
            }

            NPNR_ASSERT(net->driver.port == vcc_cell_port);
            std::vector<PortRef> users_copy = net->users;
            for (const PortRef &port_ref : users_copy) {
                IdString cell = port_ref.cell->name;
                disconnectPort(cell, port_ref.port);
                connectPort(vcc_net_name, cell, port_ref.port);
            }
        }
    }

    for (IdString other_gnd_net : other_gnd_nets) {
        NetInfo *net = getNetByAlias(other_gnd_net);
        NPNR_ASSERT(net->users.empty());
        if (net->driver.cell) {
            PortRef driver = net->driver;
            IdString cell_to_remove = driver.cell->name;
            disconnectPort(driver.cell->name, driver.port);
            NPNR_ASSERT(cells.erase(cell_to_remove));
        }
    }

    for (IdString other_vcc_net : other_vcc_nets) {
        NetInfo *net = getNetByAlias(other_vcc_net);
        NPNR_ASSERT(net->users.empty());
        if (net->driver.cell) {
            PortRef driver = net->driver;
            IdString cell_to_remove = driver.cell->name;
            disconnectPort(driver.cell->name, driver.port);
            NPNR_ASSERT(cells.erase(cell_to_remove));
        }
    }

    for (IdString other_gnd_net : other_gnd_nets) {
        NPNR_ASSERT(nets.erase(other_gnd_net));
        gnd_net->aliases.push_back(other_gnd_net);
        net_aliases[other_gnd_net] = gnd_net_name;
    }

    for (IdString other_vcc_net : other_vcc_nets) {
        NPNR_ASSERT(nets.erase(other_vcc_net));
        vcc_net->aliases.push_back(other_vcc_net);
        net_aliases[other_vcc_net] = vcc_net_name;
    }

    if (need_gnd_source) {
        CellInfo *gnd_cell = createCell(gnd_cell_type, gnd_cell_type);
        gnd_cell->addOutput(gnd_cell_port);
        connectPort(gnd_net_name, gnd_cell_type, gnd_cell_port);
    }

    if (need_vcc_source) {
        CellInfo *vcc_cell = createCell(vcc_cell_type, vcc_cell_type);
        vcc_cell->addOutput(vcc_cell_port);
        connectPort(vcc_net_name, vcc_cell_type, vcc_cell_port);
    }
}

const std::vector<IdString> &Arch::getBelPinsForCellPin(const CellInfo *cell_info, IdString pin) const
{
    auto iter = cell_info->cell_bel_pins.find(pin);
    if (iter == cell_info->cell_bel_pins.end()) {
        return no_pins;
    } else {
        return iter->second;
    }
}

void Arch::report_invalid_bel(BelId bel, CellInfo *cell) const
{
    int32_t mapping = bel_info(chip_info, bel).pin_map[get_cell_type_index(cell->type)];
    NPNR_ASSERT(mapping < 0);
    log_error("Cell %s (%s) cannot be placed at BEL %s (mapping %d)\n", cell->name.c_str(this), cell->type.c_str(this),
              nameOfBel(bel), mapping);
}

void Arch::read_lut_equation(DynamicBitarray<> *equation, const Property &equation_parameter) const
{
    equation->fill(false);
    std::string eq_str = equation_parameter.as_string();
    std::smatch results;
    if (std::regex_match(eq_str, results, raw_bin_constant)) {
        size_t bit_idx = 0;
        const std::string &bits = results[0];
        NPNR_ASSERT(bits.size() <= equation->size());
        for (auto bit = bits.rbegin(); bit != bits.rend(); ++bit) {
            if (*bit == '0') {
                equation->set(bit_idx++, false);
            } else {
                NPNR_ASSERT(*bit == '1');
                equation->set(bit_idx++, true);
            }
        }
    } else if (std::regex_match(eq_str, results, verilog_bin_constant)) {
        int iwidth = std::stoi(results[1]);
        NPNR_ASSERT(iwidth >= 0);
        size_t width = iwidth;
        std::string bits = results[2];
        NPNR_ASSERT(width <= equation->size());
        NPNR_ASSERT(bits.size() <= width);
        size_t bit_idx = 0;
        for (auto bit = bits.rbegin(); bit != bits.rend(); ++bit) {
            if (*bit == '0') {
                equation->set(bit_idx++, false);
            } else {
                NPNR_ASSERT(*bit == '1');
                equation->set(bit_idx++, true);
            }
        }
    } else {
        NPNR_ASSERT(false);
    }
}

void Arch::decode_lut_cells()
{
    for (auto &cell_pair : cells) {
        CellInfo *cell = cell_pair.second.get();
        auto iter = lut_cells.find(cell->type);
        if (iter == lut_cells.end()) {
            cell->lut_cell.pins.clear();
            cell->lut_cell.equation.clear();
            continue;
        }

        const LutCellPOD &lut_cell = *iter->second;

        cell->lut_cell.pins.reserve(lut_cell.input_pins.size());
        for (uint32_t pin : lut_cell.input_pins) {
            cell->lut_cell.pins.push_back(IdString(pin));
            cell->lut_cell.lut_pins.emplace(IdString(pin));
        }

        IdString equation_parameter(lut_cell.parameter);
        const Property &equation = cell->params.at(equation_parameter);
        cell->lut_cell.equation.resize(1 << cell->lut_cell.pins.size());
        read_lut_equation(&cell->lut_cell.equation, equation);
    }
}

void Arch::remove_pip_pseudo_wires(PipId pip, NetInfo *net)
{
    WireId wire;
    wire.tile = pip.tile;
    const PipInfoPOD &pip_data = pip_info(chip_info, pip);
    for (int32_t wire_index : pip_data.pseudo_cell_wires) {
        NPNR_ASSERT(wire_index != -1);
        wire.index = wire_index;

        auto iter = wire_to_net.find(wire);
        NPNR_ASSERT(iter != wire_to_net.end());
        // This wire better already have been assigned to this net!
        if (iter->second != net) {
            if (iter->second == nullptr) {
                log_error("Wire %s part of pseudo pip %s but net is null\n", nameOfWire(wire), nameOfPip(pip));
            } else {
                log_error("Wire %s part of pseudo pip %s but net is '%s' instead of net '%s'\n", nameOfWire(wire),
                          nameOfPip(pip), iter->second->name.c_str(this), net->name.c_str(this));
            }
        }

        auto wire_iter = net->wires.find(wire);
        if (wire_iter != net->wires.end()) {
#ifdef DEBUG_BINDING
            if (getCtx()->verbose) {
                log_info("Removing %s from net %s, but it's in net wires\n", nameOfWire(wire), net->name.c_str(this));
            }
#endif
            // This wire is part of net->wires, make sure it has no pip,
            // but leave it alone.  It will get cleaned up via
            // unbindWire.
            if (wire_iter->second.pip != PipId() && wire_iter->second.pip != pip) {
                log_error("Wire %s report source'd from pip %s, which is not %s\n", nameOfWire(wire),
                          nameOfPip(wire_iter->second.pip), nameOfPip(pip));
            }
            NPNR_ASSERT(wire_iter->second.pip == PipId() || wire_iter->second.pip == pip);
        } else {
            // This wire is not in net->wires, update wire_to_net.
#ifdef DEBUG_BINDING
            if (getCtx()->verbose) {
                log_info("Removing %s from net %s in remove_pip_pseudo_wires\n", nameOfWire(wire),
                         net->name.c_str(this));
            }
#endif
            iter->second = nullptr;
        }
    }
}

void Arch::assign_net_to_wire(WireId wire, NetInfo *net, const char *src, bool require_empty)
{
#ifdef DEBUG_BINDING
    if (getCtx()->verbose) {
        log_info("Assigning wire %s to %s from %s\n", nameOfWire(wire), net->name.c_str(this), src);
    }
#endif
    NPNR_ASSERT(net != nullptr);
    auto result = wire_to_net.emplace(wire, net);
    if (!result.second) {
        // This wire was already in the map, make sure this assignment was
        // legal.
        if (require_empty) {
            NPNR_ASSERT(result.first->second == nullptr);
        } else {
            NPNR_ASSERT(result.first->second == nullptr || result.first->second == net);
        }
        result.first->second = net;
    }
}

void Arch::unassign_wire(WireId wire)
{
    NPNR_ASSERT(wire != WireId());
#ifdef DEBUG_BINDING
    if (getCtx()->verbose) {
        log_info("unassign_wire %s\n", nameOfWire(wire));
    }
#endif

    auto iter = wire_to_net.find(wire);
    NPNR_ASSERT(iter != wire_to_net.end());

    NetInfo *net = iter->second;
    NPNR_ASSERT(net != nullptr);

    auto &net_wires = net->wires;
    auto it = net_wires.find(wire);
    NPNR_ASSERT(it != net_wires.end());

    auto pip = it->second.pip;
    if (pip != PipId()) {
#ifdef DEBUG_BINDING
        if (getCtx()->verbose) {
            log_info("Removing pip %s because it was used to reach wire %s\n", nameOfPip(pip), nameOfWire(wire));
        }
#endif
        auto pip_iter = pip_to_net.find(pip);
        NPNR_ASSERT(pip_iter != pip_to_net.end());
        NPNR_ASSERT(pip_iter->second == net);
        pip_iter->second = nullptr;
        remove_pip_pseudo_wires(pip, net);
    }

    net_wires.erase(it);
#ifdef DEBUG_BINDING
    if (getCtx()->verbose) {
        log_info("Removing %s from net %s in unassign_wire\n", nameOfWire(wire), net->name.c_str(this));
    }
#endif
    iter->second = nullptr;
}

void Arch::unbindPip(PipId pip)
{
    NPNR_ASSERT(pip != PipId());
#ifdef DEBUG_BINDING
    if (getCtx()->verbose) {
        log_info("unbindPip %s\n", nameOfPip(pip));
    }
#endif

    auto pip_iter = pip_to_net.find(pip);
    NPNR_ASSERT(pip_iter != pip_to_net.end());
    NetInfo *net = pip_iter->second;
    NPNR_ASSERT(net != nullptr);

    WireId dst = getPipDstWire(pip);
    auto wire_iter = wire_to_net.find(dst);
    NPNR_ASSERT(wire_iter != wire_to_net.end());
    NPNR_ASSERT(wire_iter->second == net);

    remove_pip_pseudo_wires(pip, net);

    // Clear the net now.
    pip_iter->second = nullptr;
#ifdef DEBUG_BINDING
    if (getCtx()->verbose) {
        log_info("Removing %s from net %s in unbindPip\n", nameOfWire(dst), net->name.c_str(this));
    }
#endif
    wire_iter->second = nullptr;
    NPNR_ASSERT(net->wires.erase(dst) == 1);

    refreshUiPip(pip);
    refreshUiWire(dst);
}

void Arch::bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
{
    NPNR_ASSERT(pip != PipId());
#ifdef DEBUG_BINDING
    if (getCtx()->verbose) {
        log_info("bindPip %s (%d/%d) to net %s\n", nameOfPip(pip), pip.tile, pip.index, net->name.c_str(this));
    }
#endif
    WireId dst = getPipDstWire(pip);
    NPNR_ASSERT(dst != WireId());

    {
        // Pip should not already be assigned to anything.
        auto result = pip_to_net.emplace(pip, net);
        if (!result.second) {
            NPNR_ASSERT(result.first->second == nullptr);
            result.first->second = net;
        }
    }

    assign_net_to_wire(dst, net, "bindPip", /*require_empty=*/true);
    assign_pip_pseudo_wires(pip, net);

    {
        auto result = net->wires.emplace(dst, PipMap{pip, strength});
        NPNR_ASSERT(result.second);
    }

    refreshUiPip(pip);
    refreshUiWire(dst);
}

void Arch::bindWire(WireId wire, NetInfo *net, PlaceStrength strength)
{
    NPNR_ASSERT(wire != WireId());
#ifdef DEBUG_BINDING
    if (getCtx()->verbose) {
        log_info("bindWire %s to net %s\n", nameOfWire(wire), net->name.c_str(this));
    }
#endif
    assign_net_to_wire(wire, net, "bindWire", /*require_empty=*/true);
    auto &pip_map = net->wires[wire];
    pip_map.pip = PipId();
    pip_map.strength = strength;
    refreshUiWire(wire);
}

bool Arch::checkPipAvail(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());
    auto pip_iter = pip_to_net.find(pip);
    if (pip_iter != pip_to_net.end() && pip_iter->second != nullptr) {
        NPNR_ASSERT(pip_iter->first == pip);
#ifdef DEBUG_BINDING
        if (getCtx()->verbose) {
            log_info("Pip %s (%d/%d) is not available, tied to net %s\n", getCtx()->nameOfPip(pip), pip.tile, pip.index,
                     pip_iter->second->name.c_str(getCtx()));
        }
#endif
        return false;
    }

    WireId src = getPipSrcWire(pip);
    WireId dst = getPipDstWire(pip);

    auto wire_iter = wire_to_net.find(dst);
    if (wire_iter != wire_to_net.end()) {
        NetInfo *net = wire_iter->second;
        if (net != nullptr) {
            auto net_iter = net->wires.find(dst);
            if (net_iter != net->wires.end()) {
                // dst is already driven in this net, do not allow!
                return false;
            }
        }
    }

    // If this pip is a route-though, make sure all of the route-though
    // wires are unbound.
    const PipInfoPOD &pip_data = pip_info(chip_info, pip);
    WireId wire;
    wire.tile = pip.tile;
    for (int32_t wire_index : pip_data.pseudo_cell_wires) {
        wire.index = wire_index;
        NPNR_ASSERT(src != wire);
        NPNR_ASSERT(dst != wire);

        NetInfo *net = getConflictingWireNet(wire);
        if (net != nullptr) {
#ifdef DEBUG_BINDING
            if (getCtx()->verbose) {
                log_info("Pip %s is not available because wire %s is tied to net %s\n", getCtx()->nameOfPip(pip),
                         getCtx()->nameOfWire(wire), net->name.c_str(getCtx()));
            }
#endif
            return false;
        }
    }

    // FIXME: This pseudo pip check is incomplete, because constraint
    // failures will not be detected.  However the current FPGA
    // interchange schema does not provide a cell type to place.

    return true;
}

// Instance constraint templates.
template void Arch::ArchConstraints::bindBel(Arch::ArchConstraints::TagState *, const Arch::ConstraintRange);
template void Arch::ArchConstraints::unbindBel(Arch::ArchConstraints::TagState *, const Arch::ConstraintRange);
template bool Arch::ArchConstraints::isValidBelForCellType(const Context *, uint32_t,
                                                           const Arch::ArchConstraints::TagState *,
                                                           const Arch::ConstraintRange, IdString, IdString, BelId,
                                                           bool) const;

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Wolf <claire@symbioticeda.com>
 *  Copyright (C) 2018-19  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#ifndef FPGA_INTERCHANGE_ARCH_H
#define FPGA_INTERCHANGE_ARCH_H

#include <boost/iostreams/device/mapped_file.hpp>
#include <iostream>
#include <regex>

#include "PhysicalNetlist.capnp.h"
#include "arch_api.h"
#include "constraints.h"
#include "nextpnr_types.h"
#include "relptr.h"

#include "arch_iterators.h"
#include "cell_parameters.h"
#include "chipdb.h"
#include "dedicated_interconnect.h"
#include "lookahead.h"
#include "site_router.h"
#include "site_routing_cache.h"

NEXTPNR_NAMESPACE_BEGIN

struct ArchArgs
{
    std::string chipdb;
    std::string package;
    bool rebuild_lookahead;
    bool dont_write_lookahead;
};

struct ArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = BelRange;
    using TileBelsRangeT = BelRange;
    using BelAttrsRangeT = std::vector<std::pair<IdString, std::string>>;
    using BelPinsRangeT = IdStringRange;
    using CellBelPinRangeT = const std::vector<IdString> &;
    // Wires
    using AllWiresRangeT = WireRange;
    using DownhillPipRangeT = DownhillPipRange;
    using UphillPipRangeT = UphillPipRange;
    using WireBelPinRangeT = BelPinRange;
    using WireAttrsRangeT = std::vector<std::pair<IdString, std::string>>;
    // Pips
    using AllPipsRangeT = AllPipRange;
    using PipAttrsRangeT = std::vector<std::pair<IdString, std::string>>;
    // Groups
    using AllGroupsRangeT = std::vector<GroupId>;
    using GroupBelsRangeT = std::vector<BelId>;
    using GroupWiresRangeT = std::vector<WireId>;
    using GroupPipsRangeT = std::vector<PipId>;
    using GroupGroupsRangeT = std::vector<GroupId>;
    // Decals
    using DecalGfxRangeT = std::vector<GraphicElement>;
    // Placement validity
    using CellTypeRangeT = const IdStringRange;
    using BelBucketRangeT = const BelBucketRange;
    using BucketBelRangeT = FilteredBelRange;
};

static constexpr size_t kMaxState = 8;

struct TileStatus
{
    std::vector<ExclusiveStateGroup<kMaxState>> tags;
    std::vector<CellInfo *> boundcells;
    std::vector<SiteRouter> sites;
};

struct Arch : ArchAPI<ArchRanges>
{
    boost::iostreams::mapped_file_source blob_file;
    const ChipInfoPOD *chip_info;
    int32_t package_index;

    // Guard initialization of "by_name" maps if accessed from multiple
    // threads on a "const Context *".
    mutable std::mutex by_name_mutex;
    mutable std::unordered_map<IdString, int> tile_by_name;
    mutable std::unordered_map<IdString, std::pair<int, int>> site_by_name;

    std::unordered_map<WireId, NetInfo *> wire_to_net;
    std::unordered_map<PipId, NetInfo *> pip_to_net;

    DedicatedInterconnect dedicated_interconnect;
    std::unordered_map<int32_t, TileStatus> tileStatus;

    ArchArgs args;
    Arch(ArchArgs args);
    virtual ~Arch();
    void init();

    std::string getChipName() const final;

    IdString archId() const final { return id(chip_info->name.get()); }
    ArchArgs archArgs() const final { return args; }
    IdString archArgsToId(ArchArgs args) const final;

    // -------------------------------------------------

    uint32_t get_tile_index(int x, int y) const { return (y * chip_info->width + x); }
    uint32_t get_tile_index(Loc loc) const { return get_tile_index(loc.x, loc.y); }
    template <typename TileIndex, typename CoordIndex>
    void get_tile_x_y(TileIndex tile_index, CoordIndex *x, CoordIndex *y) const
    {
        *x = tile_index % chip_info->width;
        *y = tile_index / chip_info->width;
    }

    template <typename TileIndex> void get_tile_loc(TileIndex tile_index, Loc *loc) const
    {
        get_tile_x_y(tile_index, &loc->x, &loc->y);
    }

    int getGridDimX() const final { return chip_info->width; }
    int getGridDimY() const final { return chip_info->height; }
    int getTileBelDimZ(int x, int y) const final
    {
        return chip_info->tile_types[chip_info->tiles[get_tile_index(x, y)].type].bel_data.size();
    }
    int getTilePipDimZ(int x, int y) const final
    {
        return chip_info->tile_types[chip_info->tiles[get_tile_index(x, y)].type].site_types.size();
    }
    char getNameDelimiter() const final { return '/'; }

    std::string get_part() const;

    // -------------------------------------------------

    void setup_byname() const;

    BelId getBelByName(IdStringList name) const final;

    IdStringList getBelName(BelId bel) const final
    {
        NPNR_ASSERT(bel != BelId());
        const SiteInstInfoPOD &site = get_site_inst(bel);
        std::array<IdString, 2> ids{id(site.name.get()), IdString(bel_info(chip_info, bel).name)};
        return IdStringList(ids);
    }

    uint32_t getBelChecksum(BelId bel) const final { return bel.index; }

    void map_cell_pins(CellInfo *cell, int32_t mapping, bool bind_constants);
    void map_port_pins(BelId bel, CellInfo *cell) const;

    TileStatus &get_tile_status(int32_t tile)
    {
        auto result = tileStatus.emplace(tile, TileStatus());
        if (result.second) {
            auto &tile_type = chip_info->tile_types[chip_info->tiles[tile].type];
            result.first->second.boundcells.resize(tile_type.bel_data.size());
            result.first->second.tags.resize(default_tags.size());

            result.first->second.sites.reserve(tile_type.site_types.size());
            for (size_t i = 0; i < tile_type.site_types.size(); ++i) {
                result.first->second.sites.push_back(SiteRouter(i));
            }
        }

        return result.first->second;
    }

    const SiteRouter &get_site_status(const TileStatus &tile_status, const BelInfoPOD &bel_data) const
    {
        return tile_status.sites.at(bel_data.site);
    }

    SiteRouter &get_site_status(TileStatus &tile_status, const BelInfoPOD &bel_data)
    {
        return tile_status.sites.at(bel_data.site);
    }

    BelId get_vcc_bel() const
    {
        auto &constants = *chip_info->constants;
        BelId bel;
        bel.tile = constants.vcc_bel_tile;
        bel.index = constants.vcc_bel_index;
        return bel;
    }

    BelId get_gnd_bel() const
    {
        auto &constants = *chip_info->constants;
        BelId bel;
        bel.tile = constants.gnd_bel_tile;
        bel.index = constants.gnd_bel_index;
        return bel;
    }

    PhysicalNetlist::PhysNetlist::NetType get_net_type(NetInfo *net) const
    {
        NPNR_ASSERT(net != nullptr);
        IdString gnd_cell_name(chip_info->constants->gnd_cell_name);
        IdString gnd_cell_port(chip_info->constants->gnd_cell_port);

        IdString vcc_cell_name(chip_info->constants->vcc_cell_name);
        IdString vcc_cell_port(chip_info->constants->vcc_cell_port);
        if (net->driver.cell->type == gnd_cell_name && net->driver.port == gnd_cell_port) {
            return PhysicalNetlist::PhysNetlist::NetType::GND;
        } else if (net->driver.cell->type == vcc_cell_name && net->driver.port == vcc_cell_port) {
            return PhysicalNetlist::PhysNetlist::NetType::VCC;
        } else {
            return PhysicalNetlist::PhysNetlist::NetType::SIGNAL;
        }
    }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) final
    {
        NPNR_ASSERT(bel != BelId());

        TileStatus &tile_status = get_tile_status(bel.tile);
        NPNR_ASSERT(tile_status.boundcells[bel.index] == nullptr);

        const auto &bel_data = bel_info(chip_info, bel);
        NPNR_ASSERT(bel_data.category == BEL_CATEGORY_LOGIC);

        if (io_port_types.count(cell->type) == 0) {
            int32_t mapping = bel_info(chip_info, bel).pin_map[get_cell_type_index(cell->type)];
            if (mapping < 0) {
                report_invalid_bel(bel, cell);
            }
            NPNR_ASSERT(mapping >= 0);

            if (cell->cell_mapping != mapping) {
                map_cell_pins(cell, mapping, /*bind_constants=*/false);
            }
            constraints.bindBel(tile_status.tags.data(), get_cell_constraints(bel, cell->type));
        } else {
            map_port_pins(bel, cell);
            // FIXME: Probably need to actually constraint io port cell/bel,
            // but the current BBA emission doesn't support that.  This only
            // really matters if the placer can choose IO port locations.
        }

        get_site_status(tile_status, bel_data).bindBel(cell);

        tile_status.boundcells[bel.index] = cell;

        cell->bel = bel;
        cell->belStrength = strength;

        refreshUiBel(bel);
    }

    void unbindBel(BelId bel) final
    {
        NPNR_ASSERT(bel != BelId());

        TileStatus &tile_status = get_tile_status(bel.tile);
        NPNR_ASSERT(tile_status.boundcells[bel.index] != nullptr);

        CellInfo *cell = tile_status.boundcells[bel.index];
        tile_status.boundcells[bel.index] = nullptr;

        cell->bel = BelId();
        cell->belStrength = STRENGTH_NONE;

        // FIXME: Probably need to actually constraint io port cell/bel,
        // but the current BBA emission doesn't support that.  This only
        // really matters if the placer can choose IO port locations.
        if (io_port_types.count(cell->type) == 0) {
            constraints.unbindBel(tile_status.tags.data(), get_cell_constraints(bel, cell->type));
        }

        const auto &bel_data = bel_info(chip_info, bel);
        get_site_status(tile_status, bel_data).unbindBel(cell);

        refreshUiBel(bel);
    }

    bool checkBelAvail(BelId bel) const final
    {
        // FIXME: This could consult the constraint system to see if this BEL
        // is blocked (e.g. site type is wrong).
        return getBoundBelCell(bel) == nullptr;
    }

    CellInfo *getBoundBelCell(BelId bel) const final
    {
        NPNR_ASSERT(bel != BelId());
        auto iter = tileStatus.find(bel.tile);
        if (iter == tileStatus.end()) {
            return nullptr;
        } else {
            return iter->second.boundcells[bel.index];
        }
    }

    CellInfo *getConflictingBelCell(BelId bel) const final
    {
        NPNR_ASSERT(bel != BelId());
        // FIXME: This could consult the constraint system to see why this BEL
        // is blocked.
        return getBoundBelCell(bel);
    }

    BelRange getBels() const final
    {
        BelRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.chip = chip_info;
        ++range.b; //-1 and then ++ deals with the case of no Bels in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        return range;
    }

    Loc getBelLocation(BelId bel) const final
    {
        NPNR_ASSERT(bel != BelId());
        Loc loc;
        get_tile_x_y(bel.tile, &loc.x, &loc.y);
        loc.z = bel.index;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const final;
    BelRange getBelsByTile(int x, int y) const final;

    bool getBelGlobalBuf(BelId bel) const final
    {
        auto &bel_data = bel_info(chip_info, bel);
        IdString bel_name(bel_data.name);

        // Note: Check profiles and see if this should be something other than
        // a linear scan.  Expectation is that for most arches, this will be
        // fast enough.
        for (int32_t global_bel : chip_info->cell_map->global_buffers) {
            IdString global_bel_name(global_bel);
            if (bel_name == global_bel_name) {
                return true;
            }
        }

        return false;
    }

    bool getBelHidden(BelId bel) const final { return bel_info(chip_info, bel).category != BEL_CATEGORY_LOGIC; }

    IdString getBelType(BelId bel) const final
    {
        NPNR_ASSERT(bel != BelId());
        return IdString(bel_info(chip_info, bel).type);
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId bel) const final;

    int get_bel_pin_index(BelId bel, IdString pin) const
    {
        NPNR_ASSERT(bel != BelId());
        int num_bel_wires = bel_info(chip_info, bel).num_bel_wires;
        const int32_t *ports = bel_info(chip_info, bel).ports.get();
        for (int i = 0; i < num_bel_wires; i++) {
            if (ports[i] == pin.index) {
                return i;
            }
        }

        return -1;
    }

    WireId getBelPinWire(BelId bel, IdString pin) const final;
    PortType getBelPinType(BelId bel, IdString pin) const final;

    IdStringRange getBelPins(BelId bel) const final
    {
        NPNR_ASSERT(bel != BelId());

        int num_bel_wires = bel_info(chip_info, bel).num_bel_wires;
        const int32_t *ports = bel_info(chip_info, bel).ports.get();

        IdStringRange str_range;
        str_range.b.cursor = &ports[0];
        str_range.e.cursor = &ports[num_bel_wires];

        return str_range;
    }

    const std::vector<IdString> &getBelPinsForCellPin(const CellInfo *cell_info, IdString pin) const final;

    // -------------------------------------------------

    WireId getWireByName(IdStringList name) const final;

    const TileWireInfoPOD &wire_info(WireId wire) const
    {
        if (wire.tile == -1) {
            const TileWireRefPOD &wr = chip_info->nodes[wire.index].tile_wires[0];
            return chip_info->tile_types[chip_info->tiles[wr.tile].type].wire_data[wr.index];
        } else {
            return loc_info(chip_info, wire).wire_data[wire.index];
        }
    }

    IdStringList getWireName(WireId wire) const final
    {
        NPNR_ASSERT(wire != WireId());
        if (wire.tile != -1) {
            const auto &tile_type = loc_info(chip_info, wire);
            if (tile_type.wire_data[wire.index].site != -1) {
                const SiteInstInfoPOD &site = get_site_inst(wire);
                std::array<IdString, 2> ids{id(site.name.get()), IdString(tile_type.wire_data[wire.index].name)};
                return IdStringList(ids);
            }
        }

        int32_t tile = wire.tile == -1 ? chip_info->nodes[wire.index].tile_wires[0].tile : wire.tile;
        IdString tile_name = id(chip_info->tiles[tile].name.get());
        std::array<IdString, 2> ids{tile_name, IdString(wire_info(wire).name)};
        return IdStringList(ids);
    }

    IdString getWireType(WireId wire) const final;
    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId wire) const final;

    uint32_t getWireChecksum(WireId wire) const final { return wire.index; }

    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) final;

    void unbindWire(WireId wire) final
    {
        NPNR_ASSERT(wire != WireId());
        unassign_wire(wire);
        refreshUiWire(wire);
    }

    bool checkWireAvail(WireId wire) const final
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() || w2n->second == nullptr;
    }

    NetInfo *getBoundWireNet(WireId wire) const final
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() ? nullptr : w2n->second;
    }

    WireId getConflictingWireWire(WireId wire) const final { return wire; }

    NetInfo *getConflictingWireNet(WireId wire) const final
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() ? nullptr : w2n->second;
    }

    DelayQuad getWireDelay(WireId wire) const final { return DelayQuad(0); }

    TileWireRange get_tile_wire_range(WireId wire) const
    {
        TileWireRange range;
        range.b.chip = chip_info;
        range.b.baseWire = wire;
        range.b.cursor = -1;
        ++range.b;

        range.e.chip = chip_info;
        range.e.baseWire = wire;
        if (wire.tile == -1) {
            range.e.cursor = chip_info->nodes[wire.index].tile_wires.size();
        } else {
            range.e.cursor = 1;
        }
        return range;
    }

    BelPinRange getWireBelPins(WireId wire) const final
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());

        TileWireRange twr = get_tile_wire_range(wire);
        range.b.chip = chip_info;
        range.b.twi = twr.b;
        range.b.twi_end = twr.e;
        range.b.cursor = -1;
        ++range.b;

        range.e.chip = chip_info;
        range.e.twi = twr.e;
        range.e.twi_end = twr.e;
        range.e.cursor = 0;
        return range;
    }

    WireRange getWires() const final
    {
        WireRange range;
        range.b.chip = chip_info;
        range.b.cursor_tile = -1;
        range.b.cursor_index = 0;
        range.e.chip = chip_info;
        range.e.cursor_tile = chip_info->tiles.size();
        range.e.cursor_index = 0;
        return range;
    }

    // -------------------------------------------------

    PipId getPipByName(IdStringList name) const final;
    IdStringList getPipName(PipId pip) const final;
    IdString getPipType(PipId pip) const final;
    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId pip) const final;

    void assign_net_to_wire(WireId wire, NetInfo *net, const char *src, bool require_empty);

    void assign_pip_pseudo_wires(PipId pip, NetInfo *net)
    {
        NPNR_ASSERT(net != nullptr);
        WireId wire;
        wire.tile = pip.tile;
        const PipInfoPOD &pip_data = pip_info(chip_info, pip);
        for (int32_t wire_index : pip_data.pseudo_cell_wires) {
            wire.index = wire_index;
            assign_net_to_wire(wire, net, "pseudo", /*require_empty=*/true);
        }
    }

    void remove_pip_pseudo_wires(PipId pip, NetInfo *net);

    void unassign_wire(WireId wire);

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) final;

    void unbindPip(PipId pip) final;

    bool checkPipAvail(PipId pip) const final;
    bool checkPipAvailForNet(PipId pip, NetInfo *net) const final;

    NetInfo *getBoundPipNet(PipId pip) const final
    {
        NPNR_ASSERT(pip != PipId());
        auto p2n = pip_to_net.find(pip);
        return p2n == pip_to_net.end() ? nullptr : p2n->second;
    }

    WireId getConflictingPipWire(PipId pip) const final
    {
        // FIXME: This doesn't account for pseudo pips.
        return getPipDstWire(pip);
    }

    NetInfo *getConflictingPipNet(PipId pip) const final
    {
        // FIXME: This doesn't account for pseudo pips.
        auto p2n = pip_to_net.find(pip);
        return p2n == pip_to_net.end() ? nullptr : p2n->second;
    }

    AllPipRange getPips() const final
    {
        AllPipRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.chip = chip_info;
        ++range.b; //-1 and then ++ deals with the case of no wries in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        return range;
    }

    Loc getPipLocation(PipId pip) const final
    {
        Loc loc;
        get_tile_loc(pip.tile, &loc);
        loc.z = 0;
        return loc;
    }

    uint32_t getPipChecksum(PipId pip) const final { return pip.index; }

    WireId getPipSrcWire(PipId pip) const final NPNR_ALWAYS_INLINE
    {
        return canonical_wire(chip_info, pip.tile, loc_info(chip_info, pip).pip_data[pip.index].src_index);
    }

    WireId getPipDstWire(PipId pip) const final NPNR_ALWAYS_INLINE
    {
        return canonical_wire(chip_info, pip.tile, loc_info(chip_info, pip).pip_data[pip.index].dst_index);
    }

    DelayQuad getPipDelay(PipId pip) const final
    {
        // FIXME: Implement when adding timing-driven place and route.
        return DelayQuad(100);
    }

    DownhillPipRange getPipsDownhill(WireId wire) const final
    {
        DownhillPipRange range;
        NPNR_ASSERT(wire != WireId());
        TileWireRange twr = get_tile_wire_range(wire);
        range.b.chip = chip_info;
        range.b.twi = twr.b;
        range.b.twi_end = twr.e;
        range.b.cursor = -1;
        ++range.b;
        range.e.chip = chip_info;
        range.e.twi = twr.e;
        range.e.twi_end = twr.e;
        range.e.cursor = 0;
        return range;
    }

    UphillPipRange getPipsUphill(WireId wire) const final
    {
        UphillPipRange range;
        NPNR_ASSERT(wire != WireId());
        TileWireRange twr = get_tile_wire_range(wire);
        range.b.chip = chip_info;
        range.b.twi = twr.b;
        range.b.twi_end = twr.e;
        range.b.cursor = -1;
        ++range.b;
        range.e.chip = chip_info;
        range.e.twi = twr.e;
        range.e.twi_end = twr.e;
        range.e.cursor = 0;
        return range;
    }

    // -------------------------------------------------

    // FIXME: Use groups to get access to sites.
    GroupId getGroupByName(IdStringList name) const final { return GroupId(); }
    IdStringList getGroupName(GroupId group) const final { return IdStringList(); }
    std::vector<GroupId> getGroups() const final { return {}; }
    std::vector<BelId> getGroupBels(GroupId group) const final { return {}; }
    std::vector<WireId> getGroupWires(GroupId group) const final { return {}; }
    std::vector<PipId> getGroupPips(GroupId group) const final { return {}; }
    std::vector<GroupId> getGroupGroups(GroupId group) const final { return {}; }

    // -------------------------------------------------
    delay_t estimateDelay(WireId src, WireId dst) const final;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const final;
    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const final;
    delay_t getDelayEpsilon() const final { return 20; }
    delay_t getRipupDelayPenalty() const final { return 120; }
    float getDelayNS(delay_t v) const final { return v * 0.001; }
    delay_t getDelayFromNS(float ns) const final { return delay_t(ns * 1000); }
    uint32_t getDelayChecksum(delay_t v) const final { return v; }
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const final;

    // -------------------------------------------------

    void place_iobufs(WireId pad_wire, NetInfo *net, const std::unordered_set<CellInfo *> &tightly_attached_bels,
                      std::unordered_set<CellInfo *> *placed_cells);
    void pack_ports();
    void decode_lut_cells();
    bool pack() final;
    bool place() final;
    bool route() final;
    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const final;

    DecalXY getBelDecal(BelId bel) const final;
    DecalXY getWireDecal(WireId wire) const final;
    DecalXY getPipDecal(PipId pip) const final;
    DecalXY getGroupDecal(GroupId group) const final;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists. This only considers combinational delays, as required by the Arch API
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const final;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const final;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const final;

    // -------------------------------------------------

    const BelBucketRange getBelBuckets() const final
    {
        BelBucketRange bel_bucket_range;
        bel_bucket_range.b.cursor.cursor = chip_info->bel_buckets.begin();
        bel_bucket_range.e.cursor.cursor = chip_info->bel_buckets.end();
        return bel_bucket_range;
    }

    BelBucketId getBelBucketForBel(BelId bel) const final
    {
        BelBucketId bel_bucket;
        bel_bucket.name = IdString(bel_info(chip_info, bel).bel_bucket);
        return bel_bucket;
    }

    const IdStringRange getCellTypes() const final
    {
        const CellMapPOD &cell_map = *chip_info->cell_map;

        IdStringRange id_range;
        id_range.b.cursor = cell_map.cell_names.begin();
        id_range.e.cursor = cell_map.cell_names.end();

        return id_range;
    }

    IdString getBelBucketName(BelBucketId bucket) const final { return bucket.name; }

    BelBucketId getBelBucketByName(IdString name) const final
    {
        for (BelBucketId bel_bucket : getBelBuckets()) {
            if (bel_bucket.name == name) {
                return bel_bucket;
            }
        }

        NPNR_ASSERT_FALSE("Failed to find BEL bucket for name.");
        return BelBucketId();
    }

    size_t get_cell_type_index(IdString cell_type) const;

    BelBucketId getBelBucketForCellType(IdString cell_type) const final
    {
        if (io_port_types.count(cell_type)) {
            BelBucketId bucket;
            bucket.name = id("IOPORTS");
            return bucket;
        }

        BelBucketId bucket;
        const CellMapPOD &cell_map = *chip_info->cell_map;
        bucket.name = IdString(cell_map.cell_bel_buckets[get_cell_type_index(cell_type)]);
        return bucket;
    }

    FilteredBelRange getBelsInBucket(BelBucketId bucket) const final
    {
        BelRange range = getBels();
        FilteredBelRange filtered_range(range.begin(), range.end(),
                                        [this, bucket](BelId bel) { return getBelBucketForBel(bel) == bucket; });

        return filtered_range;
    }

    bool isValidBelForCellType(IdString cell_type, BelId bel) const final
    {
        if (io_port_types.count(cell_type)) {
            return pads.count(bel) > 0;
        }

        const auto &bel_data = bel_info(chip_info, bel);
        if (bel_data.category != BEL_CATEGORY_LOGIC) {
            return false;
        }

        auto cell_type_index = get_cell_type_index(cell_type);
        return bel_data.pin_map[cell_type_index] != -1;
    }

    bool is_cell_valid_constraints(const CellInfo *cell, const TileStatus &tile_status, bool explain) const
    {
        if (io_port_types.count(cell->type)) {
            return true;
        }

        BelId bel = cell->bel;
        NPNR_ASSERT(bel != BelId());

        return constraints.isValidBelForCellType(getCtx(), get_constraint_prototype(bel), tile_status.tags.data(),
                                                 get_cell_constraints(bel, cell->type),
                                                 id(chip_info->tiles[bel.tile].name.get()), cell->name, bel, explain);
    }

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel) const final
    {
        auto iter = tileStatus.find(bel.tile);
        if (iter == tileStatus.end()) {
            return true;
        }
        const TileStatus &tile_status = iter->second;
        const CellInfo *cell = tile_status.boundcells[bel.index];
        if (cell != nullptr) {
            if (!dedicated_interconnect.isBelLocationValid(bel, cell)) {
                return false;
            }

            if (io_port_types.count(cell->type)) {
                // FIXME: Probably need to actually constraint io port cell/bel,
                // but the current BBA emission doesn't support that.  This only
                // really matters if the placer can choose IO port locations.
                return true;
            }

            if (!is_cell_valid_constraints(cell, tile_status, explain_constraints)) {
                return false;
            }
        }
        // Still check site status if cell is nullptr; as other bels in the site could be illegal (for example when
        // dedicated paths can no longer be used after ripping up a cell)
        auto &bel_data = bel_info(chip_info, bel);
        return get_site_status(tile_status, bel_data).checkSiteRouting(getCtx(), tile_status);
    }

    IdString get_bel_tiletype(BelId bel) const { return IdString(loc_info(chip_info, bel).name); }

    std::unordered_map<WireId, Loc> sink_locs, source_locs;
    // -------------------------------------------------
    void assignArchInfo() final {}

    // -------------------------------------------------

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;

    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    // -------------------------------------------------
    void read_logical_netlist(const std::string &filename);
    void write_physical_netlist(const std::string &filename) const;
    void parse_xdc(const std::string &filename);

    std::unordered_set<IdString> io_port_types;
    std::unordered_set<BelId> pads;

    bool is_site_port(PipId pip) const
    {
        const PipInfoPOD &pip_data = pip_info(chip_info, pip);
        if (pip_data.site == -1) {
            return false;
        }

        BelId bel;
        bel.tile = pip.tile;
        bel.index = pip_data.bel;

        const BelInfoPOD &bel_data = bel_info(chip_info, bel);

        return bel_data.category == BEL_CATEGORY_SITE_PORT;
    }

    // Is the driver and all users of this net located within the same site?
    //
    // Returns false if any element of the net is not placed.
    bool is_net_within_site(const NetInfo &net) const;

    using ArchConstraints = Constraints<kMaxState>;
    ArchConstraints constraints;
    std::vector<ArchConstraints::TagState> default_tags;
    bool explain_constraints;

    struct StateRange
    {
        const int32_t *b;
        const int32_t *e;

        const int32_t *begin() const { return b; }
        const int32_t *end() const { return e; }
    };

    struct Constraint : ArchConstraints::Constraint<StateRange>
    {
        const CellConstraintPOD *constraint;
        Constraint(const CellConstraintPOD *constraint) : constraint(constraint) {}

        size_t tag() const final { return constraint->tag; }

        ArchConstraints::ConstraintType constraint_type() const final
        {
            return Constraints<kMaxState>::ConstraintType(constraint->constraint_type);
        }

        ArchConstraints::ConstraintStateType state() const final
        {
            NPNR_ASSERT(constraint_type() == Constraints<kMaxState>::CONSTRAINT_TAG_IMPLIES);
            NPNR_ASSERT(constraint->states.size() == 1);
            return constraint->states[0];
        }

        StateRange states() const final
        {
            StateRange range;
            range.b = constraint->states.get();
            range.e = range.b + constraint->states.size();

            return range;
        }
    };

    struct ConstraintIterator
    {
        const CellConstraintPOD *constraint;
        ConstraintIterator() {}

        ConstraintIterator operator++()
        {
            ++constraint;
            return *this;
        }

        bool operator!=(const ConstraintIterator &other) const { return constraint != other.constraint; }

        bool operator==(const ConstraintIterator &other) const { return constraint == other.constraint; }

        Constraint operator*() const { return Constraint(constraint); }
    };

    struct ConstraintRange
    {
        ConstraintIterator b, e;

        ConstraintIterator begin() const { return b; }
        ConstraintIterator end() const { return e; }
    };

    uint32_t get_constraint_prototype(BelId bel) const { return chip_info->tiles[bel.tile].type; }

    ConstraintRange get_cell_constraints(BelId bel, IdString cell_type) const
    {
        const auto &bel_data = bel_info(chip_info, bel);
        NPNR_ASSERT(bel_data.category == BEL_CATEGORY_LOGIC);

        int32_t mapping = bel_data.pin_map[get_cell_type_index(cell_type)];
        NPNR_ASSERT(mapping >= 0);

        auto &cell_bel_map = chip_info->cell_map->cell_bel_map[mapping];
        ConstraintRange range;
        range.b.constraint = cell_bel_map.constraints.get();
        range.e.constraint = range.b.constraint + cell_bel_map.constraints.size();

        return range;
    }

    const char *get_site_name(int32_t tile, size_t site) const
    {
        return site_inst_info(chip_info, tile, site).name.get();
    }

    const char *get_site_name(BelId bel) const
    {
        auto &bel_data = bel_info(chip_info, bel);
        return get_site_name(bel.tile, bel_data.site);
    }

    const SiteInstInfoPOD &get_site_inst(BelId bel) const
    {
        auto &bel_data = bel_info(chip_info, bel);
        return site_inst_info(chip_info, bel.tile, bel_data.site);
    }

    const SiteInstInfoPOD &get_site_inst(WireId wire) const
    {
        auto &wire_data = wire_info(wire);
        NPNR_ASSERT(wire_data.site != -1);
        return site_inst_info(chip_info, wire.tile, wire_data.site);
    }

    const SiteInstInfoPOD &get_site_inst(PipId pip) const
    {
        auto &pip_data = pip_info(chip_info, pip);
        return site_inst_info(chip_info, pip.tile, pip_data.site);
    }

    // Is this bel synthetic (e.g. added during import process)?
    //
    // This is generally used for constant networks, but can also be used for
    // static partitions.
    bool is_bel_synthetic(BelId bel) const
    {
        const BelInfoPOD &bel_data = bel_info(chip_info, bel);

        return bel_data.synthetic != 0;
    }

    // Is this pip synthetic (e.g. added during import process)?
    //
    // This is generally used for constant networks, but can also be used for
    // static partitions.
    bool is_pip_synthetic(PipId pip) const
    {
        auto &pip_data = pip_info(chip_info, pip);
        if (pip_data.site == -1) {
            return pip_data.extra_data == -1;
        } else {
            BelId bel;
            bel.tile = pip.tile;
            bel.index = pip_data.bel;
            return is_bel_synthetic(bel);
        }
    }

    bool is_same_site(WireId wire_a, WireId wire_b) const
    {
        if (wire_a.tile == -1) {
            return false;
        }

        if (wire_a.tile != wire_b.tile) {
            return false;
        }

        auto &wire_a_data = wire_info(wire_a);
        auto &wire_b_data = wire_info(wire_b);

        return wire_a_data.site == wire_b_data.site && wire_a_data.site != -1;
    }

    bool is_wire_in_site(WireId wire) const
    {
        if (wire.tile == -1) {
            return false;
        }

        auto &wire_data = wire_info(wire);
        return wire_data.site != -1;
    }

    // Does this pip always invert its signal?
    bool is_inverting(PipId pip) const;

    // Can this pip optional invert its signal?
    bool can_invert(PipId pip) const;

    void merge_constant_nets();
    void report_invalid_bel(BelId bel, CellInfo *cell) const;

    std::vector<IdString> no_pins;
    IdString gnd_cell_pin;
    IdString vcc_cell_pin;
    std::vector<std::vector<LutElement>> lut_elements;
    std::unordered_map<IdString, const LutCellPOD *> lut_cells;

    std::regex raw_bin_constant;
    std::regex verilog_bin_constant;
    std::regex verilog_hex_constant;
    void read_lut_equation(DynamicBitarray<> *equation, const Property &equation_parameter) const;

    IdString id_GND;
    IdString id_VCC;
    Lookahead lookahead;
    mutable RouteNodeStorage node_storage;
    mutable SiteRoutingCache site_routing_cache;
    CellParameters cell_parameters;

    std::string chipdb_hash;
    std::string get_chipdb_hash() const;

    // Masking moves BEL pins from cell_bel_pins to masked_cell_bel_pins for
    // the purposes routing.  The idea is that masked BEL pins are already
    // handled during site routing, and they shouldn't be visible to the
    // router.
    void mask_bel_pins_on_site_wire(NetInfo *net, WireId wire);

    // This removes pips and wires bound by the site router, and unmasks all
    // BEL pins masked during site routing.
    void remove_site_routing();

    // This unmasks any BEL pins that were masked when site routing was bound.
    void unmask_bel_pins();

    void explain_bel_status(BelId bel) const;
};

NEXTPNR_NAMESPACE_END

#endif /* FPGA_INTERCHANGE_ARCH_H */

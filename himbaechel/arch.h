/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-2023  gatecat <gatecat@ds0.me>
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

#ifndef HIMBAECHEL_ARCH_H
#define HIMBAECHEL_ARCH_H

#include <boost/iostreams/device/mapped_file.hpp>
#include <iostream>

#include "base_arch.h"
#include "chipdb.h"
#include "himbaechel_api.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
inline const TileTypePOD &chip_tile_info(const ChipInfoPOD *chip, int tile)
{
    return chip->tile_types[chip->tile_insts[tile].type];
}
inline const BelDataPOD &chip_bel_info(const ChipInfoPOD *chip, BelId bel)
{
    return chip_tile_info(chip, bel.tile).bels[bel.index];
}
inline const TileWireDataPOD &chip_wire_info(const ChipInfoPOD *chip, WireId wire)
{
    return chip_tile_info(chip, wire.tile).wires[wire.index];
}
inline const PipDataPOD &chip_pip_info(const ChipInfoPOD *chip, PipId pip)
{
    return chip_tile_info(chip, pip.tile).pips[pip.index];
}
inline const TileRoutingShapePOD &chip_tile_shape(const ChipInfoPOD *chip, int tile)
{
    return chip->tile_shapes[chip->tile_insts[tile].shape];
}
inline uint32_t node_shape_idx(const RelNodeRefPOD &node_entry)
{
    return uint16_t(node_entry.dy) | (uint32_t(node_entry.wire) << 16);
}
inline const NodeShapePOD &chip_node_shape(const ChipInfoPOD *chip, int tile, int node)
{
    auto &node_entry = chip->tile_shapes[chip->tile_insts[tile].shape].wire_to_node[node];
    NPNR_ASSERT(node_entry.dx_mode == RelNodeRefPOD::MODE_IS_ROOT);
    uint32_t node_shape = node_shape_idx(node_entry);
    return chip->node_shapes[node_shape];
}
inline void tile_xy(const ChipInfoPOD *chip, int tile, int &x, int &y)
{
    x = tile % chip->width;
    y = tile / chip->width;
}
inline int tile_by_xy(const ChipInfoPOD *chip, int x, int y) { return y * chip->width + x; }
inline int rel_tile(const ChipInfoPOD *chip, int base, int dx, int dy)
{
    int x = base % chip->width;
    int y = base / chip->width;
    if (dx == RelNodeRefPOD::MODE_ROW_CONST) {
        return y * chip->width;
    } else if (dx == RelNodeRefPOD::MODE_GLB_CONST) {
        return 0;
    } else {
        return (x + dx) + (y + dy) * chip->width;
    }
}

inline bool is_root_wire(const ChipInfoPOD *chip, int tile, int index)
{
    auto &shape = chip_tile_shape(chip, tile);
    if (index >= shape.wire_to_node.ssize())
        return true;
    auto &node_entry = shape.wire_to_node[index];
    return (node_entry.dx_mode == RelNodeRefPOD::MODE_IS_ROOT || node_entry.dx_mode == RelNodeRefPOD::MODE_TILE_WIRE);
}

inline bool is_nodal_wire(const ChipInfoPOD *chip, int tile, int index)
{
    auto &shape = chip_tile_shape(chip, tile);
    if (index >= shape.wire_to_node.ssize())
        return false;
    auto &node_entry = shape.wire_to_node[index];
    return (node_entry.dx_mode == RelNodeRefPOD::MODE_IS_ROOT);
}

// Shared code between bel and pip iterators
template <typename Tid, typename Tdata, RelSlice<Tdata> TileTypePOD::*ptr> struct TileObjIterator
{
    const ChipInfoPOD *chip;
    int cursor_tile;
    int cursor_index;
    bool single_tile;

    TileObjIterator(const ChipInfoPOD *chip, int tile, int index, bool single_tile = false)
            : chip(chip), cursor_tile(tile), cursor_index(index), single_tile(single_tile)
    {
    }

    TileObjIterator operator++()
    {
        cursor_index++;
        if (!single_tile) {
            while (cursor_tile < chip->tile_insts.ssize() &&
                   cursor_index >= (chip_tile_info(chip, cursor_tile).*ptr).ssize()) {
                cursor_index = 0;
                cursor_tile++;
            }
        }
        return *this;
    }
    TileObjIterator operator++(int)
    {
        TileObjIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const TileObjIterator<Tid, Tdata, ptr> &other) const
    {
        return cursor_index != other.cursor_index || cursor_tile != other.cursor_tile;
    }

    bool operator==(const TileObjIterator<Tid, Tdata, ptr> &other) const
    {
        return cursor_index == other.cursor_index && cursor_tile == other.cursor_tile;
    }

    Tid operator*() const
    {
        Tid ret;
        ret.tile = cursor_tile;
        ret.index = cursor_index;
        return ret;
    }
};

template <typename Tid, typename Tdata, RelSlice<Tdata> TileTypePOD::*ptr> struct TileObjRange
{
    using iterator = TileObjIterator<Tid, Tdata, ptr>;
    explicit TileObjRange(const ChipInfoPOD *chip) : b(chip, 0, -1), e(chip, chip->tile_insts.size(), 0)
    {
        // this deals with the case of no objects in tile 0
        ++b;
    }
    TileObjRange(const ChipInfoPOD *chip, int tile)
            : b(chip, tile, 0, true), e(chip, tile, (chip_tile_info(chip, tile).*ptr).ssize(), true)
    {
    }

    iterator b, e;
    iterator begin() const { return b; }
    iterator end() const { return e; }
};

struct TileWireIterator
{
    const ChipInfoPOD *chip;
    WireId base;
    int node_shape;
    int cursor;

    TileWireIterator(const ChipInfoPOD *chip, WireId base, int node_shape, int cursor)
            : chip(chip), base(base), node_shape(node_shape), cursor(cursor){};

    void operator++() { cursor++; }
    bool operator!=(const TileWireIterator &other) const { return cursor != other.cursor; }

    // Returns a *denormalised* identifier always pointing to a tile wire rather than a node
    WireId operator*() const
    {
        if (node_shape != -1) {
            WireId tw;
            const auto &node_wire = chip->node_shapes[node_shape].tile_wires[cursor];
            tw.tile = rel_tile(chip, base.tile, node_wire.dx, node_wire.dy);
            tw.index = node_wire.wire;
            return tw;
        } else {
            return base;
        }
    }
};

struct TileWireRange
{
    // is nodal
    TileWireRange(const ChipInfoPOD *chip, WireId base, int node_shape)
            : b(chip, base, node_shape, -1), e(chip, base, node_shape, chip->node_shapes[node_shape].tile_wires.ssize())
    {
        // this deals with more complex iteration possibilities simpler, like the first entry being empty
        NPNR_ASSERT(node_shape != -1);
        ++b;
    };

    // is not nodal
    explicit TileWireRange(WireId w) : b(nullptr, w, -1, 0), e(nullptr, w, -1, 1){};

    TileWireIterator b, e;
    TileWireIterator begin() const { return b; }
    TileWireIterator end() const { return e; }
};

struct WireIterator
{
    const ChipInfoPOD *chip;
    int cursor_tile = 0;
    int cursor_index = -1;

    WireIterator(const ChipInfoPOD *chip, int tile, int index) : chip(chip), cursor_tile(tile), cursor_index(index){};

    WireIterator operator++()
    {
        // Iterate over tile wires, skipping wires that aren't normalised (i.e. they are part of another wire's node)
        do {
            cursor_index++;
            while (cursor_tile < chip->tile_insts.ssize() &&
                   cursor_index >= chip_tile_info(chip, cursor_tile).wires.ssize()) {
                cursor_index = 0;
                cursor_tile++;
            }

        } while (cursor_tile < chip->tile_insts.ssize() && !is_root_wire(chip, cursor_tile, cursor_index));

        return *this;
    }
    WireIterator operator++(int)
    {
        WireIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const WireIterator &other) const
    {
        return cursor_index != other.cursor_index || cursor_tile != other.cursor_tile;
    }

    bool operator==(const WireIterator &other) const
    {
        return cursor_index == other.cursor_index && cursor_tile == other.cursor_tile;
    }

    WireId operator*() const
    {
        WireId ret;
        ret.tile = cursor_tile;
        ret.index = cursor_index;
        return ret;
    }
};

struct WireRange
{
    explicit WireRange(const ChipInfoPOD *chip) : b(chip, 0, -1), e(chip, chip->tile_insts.ssize(), 0)
    {
        // covers the case of no wires in tile 0
        ++b;
    }
    WireIterator b, e;
    WireIterator begin() const { return b; }
    WireIterator end() const { return e; }
};

// -----------------------------------------------------------------------

template <RelSlice<int32_t> TileWireDataPOD::*ptr> struct UpdownhillPipIterator
{
    const ChipInfoPOD *chip;
    TileWireIterator twi, twi_end;
    int cursor = -1;

    UpdownhillPipIterator(const ChipInfoPOD *chip, TileWireIterator twi, TileWireIterator twi_end, int cursor)
            : chip(chip), twi(twi), twi_end(twi_end), cursor(cursor){};

    void operator++()
    {
        cursor++;
        while (true) {
            if (!(twi != twi_end))
                break;
            WireId w = *twi;
            if (cursor < (chip_wire_info(chip, w).*ptr).ssize())
                break;
            ++twi;
            cursor = 0;
        }
    }
    bool operator!=(const UpdownhillPipIterator<ptr> &other) const
    {
        return twi != other.twi || cursor != other.cursor;
    }

    PipId operator*() const
    {
        PipId ret;
        WireId w = *twi;
        ret.tile = w.tile;
        ret.index = (chip_wire_info(chip, w).*ptr)[cursor];
        return ret;
    }
};

template <RelSlice<int32_t> TileWireDataPOD::*ptr> struct UpDownhillPipRange
{
    using iterator = UpdownhillPipIterator<ptr>;
    UpDownhillPipRange(const ChipInfoPOD *chip, const TileWireRange &twr)
            : b(chip, twr.begin(), twr.end(), -1), e(chip, twr.end(), twr.end(), 0)
    {
        ++b;
    }
    iterator b, e;
    iterator begin() const { return b; }
    iterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct BelPinIterator
{
    const ChipInfoPOD *chip;
    TileWireIterator twi, twi_end;
    int cursor = -1;

    BelPinIterator(const ChipInfoPOD *chip, TileWireIterator twi, TileWireIterator twi_end, int cursor)
            : chip(chip), twi(twi), twi_end(twi_end), cursor(cursor){};

    void operator++()
    {
        cursor++;
        while (true) {
            if (!(twi != twi_end))
                break;
            WireId w = *twi;
            if (cursor < chip_wire_info(chip, w).bel_pins.ssize())
                break;
            ++twi;
            cursor = 0;
        }
    }
    bool operator!=(const BelPinIterator &other) const { return twi != other.twi || cursor != other.cursor; }

    BelPin operator*() const
    {
        BelPin ret;
        WireId w = *twi;
        auto &bp_data = chip_wire_info(chip, w).bel_pins[cursor];
        ret.bel.tile = w.tile;
        ret.bel.index = bp_data.bel;
        ret.pin = IdString(bp_data.pin);
        return ret;
    }
};

struct BelPinRange
{
    using iterator = BelPinIterator;
    BelPinRange(const ChipInfoPOD *chip, const TileWireRange &twr)
            : b(chip, twr.begin(), twr.end(), -1), e(chip, twr.end(), twr.end(), 0)
    {
        ++b;
    }
    iterator b, e;
    iterator begin() const { return b; }
    iterator end() const { return e; }
};

}; // namespace

struct ArchArgs
{
    std::string uarch;
    std::string chipdb_override;
    std::string device;
    dict<std::string, std::string> options;
};

typedef TileObjRange<BelId, BelDataPOD, &TileTypePOD::bels> BelRange;
typedef TileObjRange<PipId, PipDataPOD, &TileTypePOD::pips> AllPipRange;

typedef UpDownhillPipRange<&TileWireDataPOD::pips_uphill> UphillPipRange;
typedef UpDownhillPipRange<&TileWireDataPOD::pips_downhill> DownhillPipRange;

struct ArchRanges : BaseArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = BelRange;
    using TileBelsRangeT = BelRange;
    using BelPinsRangeT = std::vector<IdString>;
    using CellBelPinRangeT = const std::vector<IdString> &;
    // Wires
    using AllWiresRangeT = WireRange;
    using DownhillPipRangeT = DownhillPipRange;
    using UphillPipRangeT = UphillPipRange;
    using WireBelPinRangeT = BelPinRange;
    // Pips
    using AllPipsRangeT = AllPipRange;
};

struct Arch : BaseArch<ArchRanges>
{
    ArchArgs args;
    Arch(ArchArgs args);
    ~Arch(){};

    void load_chipdb(const std::string &path);
    void set_speed_grade(const std::string &speed);
    void set_package(const std::string &package);

    void late_init();

    // Database references
    boost::iostreams::mapped_file_source blob_file;
    const ChipInfoPOD *chip_info;
    const PackageInfoPOD *package_info = nullptr;
    const SpeedGradePOD *speed_grade = nullptr;

    // Unlike Viaduct, we are not -generic based and therefore uarch must be non-nullptr
    std::unique_ptr<HimbaechelAPI> uarch;

    std::string getChipName() const override;
    ArchArgs archArgs() const override { return args; }
    IdString archArgsToId(ArchArgs args) const override;
    // -------------------------------------------------

    int getGridDimX() const override { return chip_info->width; }
    int getGridDimY() const override { return chip_info->height; }
    int getTileBelDimZ(int, int) const override { return 1024; } // TODO ?
    int getTilePipDimZ(int, int) const override { return 1; }    // TODO ?
    char getNameDelimiter() const override { return '/'; }       // TODO ?

    // -------------------------------------------------

    BelId getBelByName(IdStringList name) const override;
    IdStringList getBelName(BelId bel) const override;
    BelRange getBels() const override { return BelRange(chip_info); }
    Loc getBelLocation(BelId bel) const override
    {
        Loc loc;
        tile_xy(chip_info, bel.tile, loc.x, loc.y);
        loc.z = chip_bel_info(chip_info, bel).z;
        return loc;
    }
    BelId getBelByLocation(Loc loc) const override
    {
        int tile = tile_by_xy(chip_info, loc.x, loc.y);
        auto &tile_data = chip_tile_info(chip_info, tile);
        for (size_t i = 0; i < tile_data.bels.size(); i++) {
            if (tile_data.bels[i].z == loc.z)
                return BelId(tile, i);
        }
        return BelId();
    }
    BelRange getBelsByTile(int x, int y) const override { return BelRange(chip_info, tile_by_xy(chip_info, x, y)); }
    bool getBelGlobalBuf(BelId bel) const override
    {
        return chip_bel_info(chip_info, bel).flags & BelDataPOD::FLAG_GLOBAL;
    }
    IdString getBelType(BelId bel) const override { return IdString(chip_bel_info(chip_info, bel).bel_type); }

    WireId getBelPinWire(BelId bel, IdString pin) const override;
    PortType getBelPinType(BelId bel, IdString pin) const override;
    std::vector<IdString> getBelPins(BelId bel) const override;

    bool getBelHidden(BelId bel) const final { return chip_bel_info(chip_info, bel).flags & BelDataPOD::FLAG_HIDDEN; }

    // -------------------------------------------------

    WireId getWireByName(IdStringList name) const override;
    IdStringList getWireName(WireId wire) const override;
    IdString getWireType(WireId wire) const override { return IdString(chip_wire_info(chip_info, wire).wire_type); }
    DelayQuad getWireDelay(WireId wire) const override { return DelayQuad(0); } // TODO
    BelPinRange getWireBelPins(WireId wire) const override { return BelPinRange(chip_info, get_tile_wire_range(wire)); }
    IdString getWireConstantValue(WireId wire) const override
    {
        return IdString(chip_wire_info(chip_info, wire).const_value);
    }
    WireRange getWires() const override { return WireRange(chip_info); }
    bool checkWireAvail(WireId wire) const override
    {
        if (!uarch->checkWireAvail(wire))
            return false;
        return BaseArch::checkWireAvail(wire);
    }
    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) override
    {
        uarch->notifyWireChange(wire, net);
        BaseArch::bindWire(wire, net, strength);
    }
    void unbindWire(WireId wire) override
    {
        uarch->notifyWireChange(wire, nullptr);
        BaseArch::unbindWire(wire);
    }

    // -------------------------------------------------

    PipId getPipByName(IdStringList name) const override;
    IdStringList getPipName(PipId pip) const override;
    AllPipRange getPips() const override { return AllPipRange(chip_info); }
    Loc getPipLocation(PipId pip) const override
    {
        Loc loc;
        tile_xy(chip_info, pip.tile, loc.x, loc.y);
        loc.z = 0;
        return loc;
    }
    IdString getPipType(PipId pip) const override;
    WireId getPipSrcWire(PipId pip) const override
    {
        return normalise_wire(pip.tile, chip_pip_info(chip_info, pip).src_wire);
    }
    WireId getPipDstWire(PipId pip) const override
    {
        return normalise_wire(pip.tile, chip_pip_info(chip_info, pip).dst_wire);
    }
    DelayQuad getPipDelay(PipId pip) const override
    {
        auto &pip_data = chip_pip_info(chip_info, pip);
        auto pip_tmg = get_pip_timing(pip_data);
        if (pip_tmg != nullptr) {
            // TODO: multi corner analysis
            WireId src = getPipSrcWire(pip);
            uint64_t input_res = fast_pip_delays ? 0 : (drive_res.count(src) ? drive_res.at(src) : 0);
            uint64_t input_cap = fast_pip_delays ? 0 : (load_cap.count(src) ? load_cap.at(src) : 0);
            auto src_tmg = get_node_timing(src);
            if (src_tmg != nullptr)
                input_res += (src_tmg->res.slow_max / 2);
            // Scale delay (fF * mOhm -> ps)
            delay_t total_delay = (input_res * input_cap) / uint64_t(1e6);
            total_delay += pip_tmg->int_delay.slow_max;

            WireId dst = getPipDstWire(pip);
            auto dst_tmg = get_node_timing(dst);
            if (dst_tmg != nullptr) {
                total_delay +=
                        ((pip_tmg->out_res.slow_max + uint64_t(dst_tmg->res.slow_max) / 2) * dst_tmg->cap.slow_max) /
                        uint64_t(1e6);
            }

            return DelayQuad(total_delay);
        } else {
            // Pip with no specified delay. Return a notional value so the router still has something to work with.
            return DelayQuad(100);
        }
    }
    DownhillPipRange getPipsDownhill(WireId wire) const override
    {
        return DownhillPipRange(chip_info, get_tile_wire_range(wire));
    }
    UphillPipRange getPipsUphill(WireId wire) const override
    {
        return UphillPipRange(chip_info, get_tile_wire_range(wire));
    }

    bool checkPipAvail(PipId pip) const override
    {
        if (!uarch->checkPipAvail(pip))
            return false;
        return BaseArch::checkPipAvail(pip);
    }

    bool checkPipAvailForNet(PipId pip, const NetInfo *net) const override
    {
        if (!uarch->checkPipAvailForNet(pip, net))
            return false;
        return BaseArch::checkPipAvailForNet(pip, net);
    }
    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) override
    {
        if (!fast_pip_delays) {
            auto &pip_data = chip_pip_info(chip_info, pip);
            auto pip_tmg = get_pip_timing(pip_data);
            if (pip_tmg != nullptr) {
                WireId src = getPipSrcWire(pip);
                load_cap[src] += pip_tmg->in_cap.slow_max;
                drive_res[getPipDstWire(pip)] =
                        (((pip_tmg->flags & 1) || !drive_res.count(src)) ? 0 : drive_res.at(src)) +
                        pip_tmg->out_res.slow_max;
            }
        }
        uarch->notifyPipChange(pip, net);
        BaseArch::bindPip(pip, net, strength);
    }
    void unbindPip(PipId pip) override
    {
        if (!fast_pip_delays) {
            auto &pip_data = chip_pip_info(chip_info, pip);
            auto pip_tmg = get_pip_timing(pip_data);
            if (pip_tmg != nullptr) {
                load_cap[getPipSrcWire(pip)] -= pip_tmg->in_cap.slow_max;
            }
        }
        uarch->notifyPipChange(pip, nullptr);
        BaseArch::unbindPip(pip);
    }

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const override { return uarch->estimateDelay(src, dst); }
    delay_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const override
    {
        return uarch->predictDelay(src_bel, src_pin, dst_bel, dst_pin);
    }
    delay_t getDelayEpsilon() const override { return 20; }       // TODO
    delay_t getRipupDelayPenalty() const override { return 120; } // TODO
    float getDelayNS(delay_t v) const override { return v * 0.001; }
    delay_t getDelayFromNS(float ns) const override { return delay_t(ns * 1000); }
    uint32_t getDelayChecksum(delay_t v) const override { return v; }
    BoundingBox getRouteBoundingBox(WireId src, WireId dst) const override
    {
        return uarch->getRouteBoundingBox(src, dst);
    }

    // -------------------------------------------------

    void assignArchInfo() override;
    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override
    {
        return uarch->isBelLocationValid(bel, explain_invalid);
    }

    // ------------------------------------------------

    const std::vector<IdString> &getBelPinsForCellPin(const CellInfo *cell_info, IdString pin) const override
    {
        return cell_info->cell_bel_pins.at(pin);
    }
    void update_cell_bel_pins(CellInfo *cell);

    // ------------------------------------------------

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) override
    {
        uarch->notifyBelChange(bel, cell);
        BaseArch::bindBel(bel, cell, strength); // TODO: faster?
    }

    void unbindBel(BelId bel) override
    {
        uarch->notifyBelChange(bel, nullptr);
        BaseArch::unbindBel(bel); // TODO: faster?
        // TODO: fast tile status and bind
    }

    bool checkBelAvail(BelId bel) const override
    {
        // TODO: fast tile status and bind
        if (!uarch->checkBelAvail(bel))
            return false;
        return BaseArch::checkBelAvail(bel);
    }

    // getBoundBelCell: BaseArch

    // ------------------------------------------------

    BelBucketId getBelBucketForCellType(IdString cell_type) const override
    {
        return uarch->getBelBucketForCellType(cell_type);
    }
    BelBucketId getBelBucketForBel(BelId bel) const override { return uarch->getBelBucketForBel(bel); }
    bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        return uarch->isValidBelForCellType(cell_type, bel);
    }

    // ------------------------------------------------

    // Cluster methods
    CellInfo *getClusterRootCell(ClusterId cluster) const override { return uarch->getClusterRootCell(cluster); }

    BoundingBox getClusterBounds(ClusterId cluster) const override { return uarch->getClusterBounds(cluster); }

    Loc getClusterOffset(const CellInfo *cell) const override { return uarch->getClusterOffset(cell); }

    bool isClusterStrict(const CellInfo *cell) const override { return uarch->isClusterStrict(cell); }

    bool getClusterPlacement(ClusterId cluster, BelId root_bel,
                             std::vector<std::pair<CellInfo *, BelId>> &placement) const override
    {
        return uarch->getClusterPlacement(cluster, root_bel, placement);
    }
    // ------------------------------------------------

    bool pack() override;
    bool place() override;
    bool route() override;

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    // -------------------------------------------------

    WireId normalise_wire(int32_t tile, int32_t wire) const
    {
        auto &ts = chip_tile_shape(chip_info, tile);
        if (wire >= ts.wire_to_node.ssize())
            return WireId(tile, wire);
        auto &w2n = ts.wire_to_node[wire];
        if (w2n.dx_mode == RelNodeRefPOD::MODE_TILE_WIRE || w2n.dx_mode == RelNodeRefPOD::MODE_IS_ROOT)
            return WireId(tile, wire);
        return WireId(rel_tile(chip_info, tile, w2n.dx_mode, w2n.dy), w2n.wire);
    }

    TileWireRange get_tile_wire_range(WireId wire) const
    {
        auto &ts = chip_tile_shape(chip_info, wire.tile);
        if (wire.index >= ts.wire_to_node.ssize())
            return TileWireRange(wire);
        auto &w2n = ts.wire_to_node[wire.index];
        if (w2n.dx_mode != RelNodeRefPOD::MODE_TILE_WIRE) {
            NPNR_ASSERT(w2n.dx_mode == RelNodeRefPOD::MODE_IS_ROOT);
            return TileWireRange(chip_info, wire, node_shape_idx(w2n));
        } else {
            return TileWireRange(wire);
        }
    }

    // -------------------------------------------------

    const PipTimingPOD *get_pip_timing(const PipDataPOD &pip_data) const
    {
        int32_t idx = pip_data.timing_idx;
        if (speed_grade && idx >= 0 && idx < speed_grade->pip_classes.ssize())
            return &(speed_grade->pip_classes[idx]);
        else
            return nullptr;
    }

    const NodeTimingPOD *get_node_timing(WireId wire) const
    {
        int idx = -1;
        if (!speed_grade)
            return nullptr;
        if (is_nodal_wire(chip_info, wire.tile, wire.index)) {
            auto &shape = chip_node_shape(chip_info, wire.tile, wire.index);
            idx = shape.timing_idx;
        } else {
            auto &wire_data = chip_wire_info(chip_info, wire);
            idx = wire_data.timing_idx;
        }
        if (idx >= 0 && idx < speed_grade->node_classes.ssize())
            return &(speed_grade->node_classes[idx]);
        else
            return nullptr;
    }

    // -------------------------------------------------

    // Given cell type and variant, get the index inside the speed grade timing data
    int get_cell_timing_idx(IdString type_variant) const;
    // Return true and set delay if a comb path exists in a given cell timing index
    bool lookup_cell_delay(int type_idx, IdString from_port, IdString to_port, DelayQuad &delay) const;
    // Get setup and hold time and associated clock for a given cell timing index and signal
    const RelSlice<CellPinRegArcPOD> *lookup_cell_seq_timings(int type_idx, IdString port) const;
    // Attempt to look up port type based on timing database
    TimingPortClass lookup_port_tmg_type(int type_idx, IdString port, PortType dir) const;

    // -------------------------------------------------

    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const override;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override;

    // -------------------------------------------------
    void init_tiles();
    void set_fast_pip_delays(bool fast_mode);
    std::vector<IdString> tile_name;
    dict<IdString, int> tile_name2idx;

    // -------------------------------------------------
    IdString get_tile_type(int tile) const;
    const PadInfoPOD *get_package_pin(IdString pin) const;
    const PadInfoPOD *get_bel_package_pin(BelId bel) const;
    BelId get_package_pin_bel(IdString pin) const;

    // Load capacitance and drive resistance for nodes
    // TODO: does this `dict` hurt routing performance too much?
    bool fast_pip_delays = false;
    dict<WireId, uint64_t> drive_res;
    dict<WireId, uint64_t> load_cap;
};

NEXTPNR_NAMESPACE_END

#endif

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#ifndef NEXTPNR_H
#error Include "arch.h" via "nextpnr.h" only.
#endif

#include <boost/iostreams/device/mapped_file.hpp>

NEXTPNR_NAMESPACE_BEGIN

/**** Everything in this section must be kept in sync with leuctra/make_bba.py ****/

template <typename T> struct RelPtr
{
    int32_t offset;

    // void set(const T *ptr) {
    //     offset = reinterpret_cast<const char*>(ptr) -
    //              reinterpret_cast<const char*>(this);
    // }

    const T *get() const { return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset); }

    const T &operator[](size_t index) const { return get()[index]; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }
};

/************************ Per-device structures. ************************/

NPNR_PACKED_STRUCT(struct PortIdPOD {
    int16_t tile_x;
    int16_t tile_y;
    int32_t port_idx;
});

NPNR_PACKED_STRUCT(struct BelIdPOD {
    int16_t tile_x;
    int16_t tile_y;
    int32_t bel_idx;
});

// Represents a single bel on a device.
NPNR_PACKED_STRUCT(struct BelPOD {
    enum BelFlags : uint32_t {
        // Differential positive / negative terminal.
        FLAG_IO_DIFF_P = 0x1,
        FLAG_IO_DIFF_N = 0x2,
	// IOB without output buffer (Spartan 3E, 3A).
	FLAG_IO_INPUT_ONLY = 0x4,
	// Left or right IOB (Spartan 3A).
	FLAR_IO_LR = 0x8,
	// Low-capacitance IOB (Virtex 4).
	FLAG_IO_LOWCAP = 0x10,
	// High Performance vs High Range IO (Series 7) -- applies to IOBs, [IO]LOGIC, [IO]DELAY.
	FLAG_IO_HP = 0x20,
	FLAG_IO_HR = 0x40,
	// VREF pad (cannot be used if VREF IO standard used in the same bank).
	FLAG_IO_VREF = 0x80,
	// Positive / negative DCI calibration pad (cannot be used if DCI IO standard used in the same bank).
	FLAG_IO_VP = 0x100,
	FLAG_IO_VN = 0x200,
	// Multi-function pin used by the configuration interface (cannot be used if Persist option given).
	FLAG_IO_PERSIST = 0x400,
	// Subtype for slices.
	FLAG_SLICEX = 0x800,
	FLAG_SLICEL = 0x1000,
	FLAG_SLICEM = 0x2000,
	// For Virtex 6 and Series 7 18-kbit RAM: set if this bel can fit a FIFO18E1.
	FLAG_FIFO = 0x4000,
    };
    int32_t io_bank;
    BelFlags flags;
    RelPtr<BelIdPOD> related;
    RelPtr<BelIdPOD> conflicts;
});

// Represents a single tile on a device.
NPNR_PACKED_STRUCT(struct TilePOD {
    int32_t tile_type_idx;
    RelPtr<BelPOD> bels;
    RelPtr<PortIdPOD> conns;
});

NPNR_PACKED_STRUCT(struct PackagePinPOD {
    int32_t name_id;
    BelIdPOD bel;
});

NPNR_PACKED_STRUCT(struct PackageInfoPOD {
    int32_t name_id;
    int32_t num_pins;
    RelPtr<PackagePinPOD> pin_data;
});

// Represents a single device die.
NPNR_PACKED_STRUCT(struct DevicePOD {
    // Width and height in tiles.
    int16_t width;
    int16_t height;
    RelPtr<TilePOD> tiles;
    int32_t num_packages;
    RelPtr<PackageInfoPOD> packages;
});

/************************ Per-family structures. ************************/

// Represents a BEL type or cell type pin.
NPNR_PACKED_STRUCT(struct BelTypePinPOD {
    enum BelTypePinFlags : uint32_t {
        // Both can be set for an inout pin.
        FLAG_INPUT = 0x1,
        FLAG_OUTPUT = 0x2,
        FLAG_CLOCK = 0x4,
        FLAG_INVERTIBLE = 0x8,
	// Participates in global interconnect (all global outputs can reach
	// all global inputs).
	FLAG_ROUTE_GLOBAL = 0x10,
	// Drives or can be driven from the clock interconnect.
	FLAG_ROUTE_CLOCK = 0x20,
	// Has dedicated routing.
	FLAG_ROUTE_DEDICATED = 0x40,
    };
    int32_t name_id;
    BelTypePinFlags flags;
});

// Represents a type of BEL available in a given family.  May fit several cell types.
NPNR_PACKED_STRUCT(struct BelTypePOD {
    enum BelTypeFlags : uint32_t {
        // This BEL type is associated with a physical pad on the die.
        FLAG_HAS_PAD = 0x1,
	FLAG_IS_GLOBAL_BUF = 0x2,
    };
    int32_t name_id;
    BelTypeFlags flags;
    int32_t num_pins;
    RelPtr<BelTypePinPOD> pins;
    int32_t num_related;
    RelPtr<int32_t> related_name_ids;
    int32_t num_conflicts;
});

NPNR_PACKED_STRUCT(struct TileTypeWireBelXrefPOD {
    int32_t bel_idx;
    int32_t pin_idx;
});

NPNR_PACKED_STRUCT(struct TileTypeWirePortXrefPOD {
    int32_t port_idx;
    int32_t wire_idx;
});

// Represents a wire in a tile type.
NPNR_PACKED_STRUCT(struct TileTypeWirePOD {
    int32_t name_id;
    int32_t type_name_id;
    // A list of bel pins referencing this wire.
    int32_t num_bel_xrefs;
    RelPtr<TileTypeWireBelXrefPOD> bel_xrefs;
    // A list of pips referencing this wire as dst.
    int32_t num_pip_dst_xrefs;
    RelPtr<int32_t> pip_dst_xrefs;
    // A list of pips referencing this wire as src.
    int32_t num_pip_src_xrefs;
    RelPtr<int32_t> pip_src_xrefs;
    // A list of ports referencing this wire.
    int32_t num_port_xrefs;
    RelPtr<TileTypeWirePortXrefPOD> port_xrefs;
});

// Represents a bel in a tile type.
NPNR_PACKED_STRUCT(struct TileTypeBelPOD {
    int32_t bel_type_idx;
    int32_t name_id;
    RelPtr<int32_t> pin_wires;
});

// Represents a pip in a tile type.
NPNR_PACKED_STRUCT(struct TileTypePipPOD {
    enum TileTypePipFlags : uint32_t {
        FLAG_ALWAYS_ON = 0x1,
        FLAG_THROUGH_BEL = 0x2,
    };
    TileTypePipFlags flags;
    int32_t wire_src;
    int32_t wire_dst;
    int32_t bel_through;
});

// Represents a port in a tile type.
NPNR_PACKED_STRUCT(struct TileTypePortPOD {
    enum TileTypePortFlags : uint32_t {
        FLAG_DIR_S = 0x1,
        FLAG_DIR_N = 0x2,
        FLAG_DIR_E = 0x4,
        FLAG_DIR_W = 0x8,
    };
    TileTypePortFlags flags;
    int32_t name_id;
    int32_t num_wires;
    RelPtr<int32_t> wires;
});

// Represents a tile type available in a given family.
NPNR_PACKED_STRUCT(struct TileTypePOD {
    enum TileTypeFlags : uint32_t {
        // This tile type is empty space and shouldn't be drawn in GUI.
        FLAG_EMPTY = 0x1,
        // This tile type is a full node in the main interconnect grid.
        FLAG_INT = 0x2,
    };
    TileTypeFlags flags;
    int32_t name_id;
    // How many extra grid slots this tile type takes up (for GUI).
    // The extra slots should be filled with FLAG_EMPTY tiles in the db.
    // A normal unit tile has 0 in all 4 fields.
    int16_t extend_up;
    int16_t extend_down;
    int16_t extend_left;
    int16_t extend_right;
    int32_t num_wires;
    // Sorted by name_id.
    RelPtr<TileTypeWirePOD> wires;
    int32_t num_bels;
    // Sorted by name_id.
    RelPtr<TileTypeBelPOD> bels;
    int32_t num_pips;
    RelPtr<TileTypePipPOD> pips;
    int32_t num_ports;
    RelPtr<TileTypePortPOD> ports;
});

NPNR_PACKED_STRUCT(struct DeviceCatalogueEntryPOD {
    // Device name (what the user selects).
    int32_t name_id;
    RelPtr<DevicePOD> device;
});

NPNR_PACKED_STRUCT(struct FamilyPOD {
    // Must be equal to DB_FORMAT_TAG_CURRENT, used to identify db format revision.
    uint32_t format_tag;
    // Family name.
    int32_t name_id;
    // Devices in this family (to be loaded from separate bbas).
    int32_t num_devices;
    RelPtr<DeviceCatalogueEntryPOD> devices;
    // The initial IdString mapping.
    int32_t num_idstrings;
    RelPtr<RelPtr<char>> idstrings;
    // A description of available bel types.
    int32_t num_bel_types;
    RelPtr<BelTypePOD> bel_types;
    // A descripion of available tile types.
    int32_t num_tile_types;
    RelPtr<TileTypePOD> tile_types;
});

#define DB_FORMAT_TAG_CURRENT 0x3

/************************ End of chipdb section. ************************/

struct BelIterator
{
    const DevicePOD *device;
    const FamilyPOD *family;
    int cursor_index;
    int cursor_tile;

    BelIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < device->width * device->height &&
               cursor_index >= family->tile_types[device->tiles[cursor_tile].tile_type_idx].num_bels) {
            cursor_index = 0;
            cursor_tile++;
        }
        return *this;
    }
    BelIterator operator++(int)
    {
        BelIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const BelIterator &other) const
    {
        return cursor_index != other.cursor_index || cursor_tile != other.cursor_tile;
    }

    bool operator==(const BelIterator &other) const
    {
        return cursor_index == other.cursor_index && cursor_tile == other.cursor_tile;
    }

    BelId operator*() const
    {
        BelId ret;
        ret.location.x = cursor_tile % device->width;
        ret.location.y = cursor_tile / device->width;
        ret.index = cursor_index;
        return ret;
    }
};

struct BelRange
{
    BelIterator b, e;
    BelIterator begin() const { return b; }
    BelIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct Arch;

struct BelPinIterator
{
    const Arch *arch;
    const TileTypeWireBelXrefPOD *ptr = nullptr;
    Location bel_loc;
    void operator++() { ptr++; }
    bool operator!=(const BelPinIterator &other) const { return ptr != other.ptr; }

    BelPin operator*() const;
};

struct BelPinRange
{
    BelPinIterator b, e;
    BelPinIterator begin() const { return b; }
    BelPinIterator end() const { return e; }
};


// -----------------------------------------------------------------------

struct WireIterator
{
    const DevicePOD *device;
    const FamilyPOD *family;
    int cursor_index;
    int cursor_tile;

    WireIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < device->width * device->height &&
               cursor_index >= family->tile_types[device->tiles[cursor_tile].tile_type_idx].num_wires) {
            cursor_index = 0;
            cursor_tile++;
        }
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
        ret.location.x = cursor_tile % device->width;
        ret.location.y = cursor_tile / device->width;
        ret.index = cursor_index;
        return ret;
    }
};

struct WireRange
{
    WireIterator b, e;
    WireIterator begin() const { return b; }
    WireIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct AllPipIterator
{
    const DevicePOD *device;
    const FamilyPOD *family;
    int cursor_tile;
    PipKind cursor_kind;
    int cursor_index;
    int cursor_subindex;

    AllPipIterator operator++()
    {
	cursor_subindex++;
	while (true) {
	    auto &tt = family->tile_types[device->tiles[cursor_tile].tile_type_idx];
	    if (cursor_tile >= device->width * device->height)
		break;
	    if (cursor_kind == PIP_KIND_PIP) {
		if (cursor_subindex != 0) {
		    cursor_subindex = 0;
		    cursor_index++;
		}
		if (cursor_index >= tt.num_pips) {
		    cursor_kind = PIP_KIND_PORT;
		    cursor_index = 0;
		}
	    }
	    if (cursor_kind == PIP_KIND_PORT) {
		if (cursor_index >= tt.num_ports) {
		    cursor_kind = PIP_KIND_PIP;
		    cursor_index = 0;
		    cursor_tile++;
		    continue;
		}
		// Skip unconnected ports.
		auto &conn = device->tiles[cursor_tile].conns[cursor_index];
		if (conn.port_idx == -1) {
		    cursor_index++;
		    continue;
		}
		if (cursor_subindex >= tt.ports[cursor_index].num_wires) {
		    cursor_subindex = 0;
		    cursor_index++;
		    continue;
		}
		// Skip unconnected wires in a port.
		if (tt.ports[cursor_index].wires[cursor_subindex] == -1) {
		    cursor_subindex++;
		    continue;
		}
		auto &other_tile = device->tiles[conn.tile_x + device->width * conn.tile_y];
		auto &other_tt = family->tile_types[other_tile.tile_type_idx];
		if (other_tt.ports[conn.port_idx].wires[cursor_subindex] == -1) {
		    cursor_subindex++;
		    continue;
		}
	    }
	    break;
	}
        return *this;
    }
    AllPipIterator operator++(int)
    {
        AllPipIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const AllPipIterator &other) const
    {
        return cursor_subindex != other.cursor_subindex || cursor_index != other.cursor_index || cursor_kind != other.cursor_kind || cursor_tile != other.cursor_tile;
    }

    bool operator==(const AllPipIterator &other) const
    {
        return cursor_subindex == other.cursor_subindex && cursor_index == other.cursor_index && cursor_kind == other.cursor_kind && cursor_tile == other.cursor_tile;
    }

    PipId operator*() const
    {
        PipId ret;
        ret.location.x = cursor_tile % device->width;
        ret.location.y = cursor_tile / device->width;
        ret.kind = cursor_kind;
        ret.index = cursor_index;
        ret.subindex = cursor_subindex;
        return ret;
    }
};

struct AllPipRange
{
    AllPipIterator b, e;
    AllPipIterator begin() const { return b; }
    AllPipIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct PipIterator
{
    enum {
	STAGE_PIPS,
	STAGE_PORTS,
	STAGE_END,
    } stage;
    enum {
	MODE_DOWNHILL,
	MODE_UPHILL,
    } mode;
    int cursor_index;
    WireId wire;
    const Arch *arch;

    void operator++();

    bool operator!=(const PipIterator &other) const { return stage != other.stage || cursor_index != other.cursor_index; }

    PipId operator*() const;
};

struct PipRange
{
    PipIterator b, e;
    PipIterator begin() const { return b; }
    PipIterator end() const { return e; }
};


struct ArchArgs
{
    std::string device;
    std::string package;
    std::string speed;
};

struct Arch : BaseCtx
{
    enum Family {
	FAMILY_XC4000E, // Also known as Spartan.
	FAMILY_XC4000EX, // Also xc4000xl.
	FAMILY_XC4000XLA,
	FAMILY_XC4000XV,
	FAMILY_SPARTANXL,
	FAMILY_VIRTEX, // Also known as Spartan 2.
	FAMILY_VIRTEXE, // Also known as Spartan 2E.
	FAMILY_VIRTEX2,
	FAMILY_VIRTEX2P,
	FAMILY_SPARTAN3,
	FAMILY_SPARTAN3E,
	FAMILY_SPARTAN3A,
	FAMILY_SPARTAN3ADSP,
	FAMILY_VIRTEX4,
	FAMILY_VIRTEX5,
	FAMILY_VIRTEX6,
	FAMILY_SPARTAN6,
	FAMILY_SERIES7,
	FAMILY_ULTRASCALE,
	FAMILY_ULTRASCALE_PLUS,
    } family;

    boost::iostreams::mapped_file_source mmap;

    const FamilyPOD *family_info;
    const DevicePOD *device_info;
    const PackageInfoPOD *package_info;

    mutable std::unordered_map<IdString, BelId> bel_by_name;
    mutable std::unordered_map<IdString, WireId> wire_by_name;
    mutable std::unordered_map<IdString, PipId> pip_by_name;

    std::unordered_map<BelId, CellInfo *> bel_to_cell;
    std::unordered_map<WireId, NetInfo *> wire_to_net;
    std::unordered_map<PipId, NetInfo *> pip_to_net;

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName() const
    {
	return args.device;
    }

    IdString archId() const { return id("leuctra"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) { return id(args.device); }

    const TilePOD &getTile(int x, int y) const
    {
	 return device_info->tiles[x + device_info->width * y];
    }

    const TilePOD &getTile(Location loc) const
    {
	return getTile(loc.x, loc.y);
    }

    const TileTypePOD &getTileType(int x, int y) const
    {
	 return family_info->tile_types[getTile(x, y).tile_type_idx];
    }

    const TileTypePOD &getTileType(Location loc) const
    {
	return getTileType(loc.x, loc.y);
    }

    const TileTypeBelPOD &getTileTypeBel(BelId bel) const
    {
	return getTileType(bel.location).bels[bel.index];
    }

    const TileTypeWirePOD &getTileTypeWire(WireId wire) const
    {
	return getTileType(wire.location).wires[wire.index];
    }

    const BelTypePOD &getBelTypeInfo(BelId bel) const
    {
	return family_info->bel_types[getTileTypeBel(bel).bel_type_idx];
    }

    // -------------------------------------------------

    int getGridDimX() const { return device_info->width; };
    int getGridDimY() const { return device_info->height; };
    int getTileBelDimZ(int x, int y) const
    {
	return getTileType(x, y).num_bels;
    };
    int getTilePipDimZ(int, int) const { return 1; };

    // -------------------------------------------------

    BelId getBelByName(IdString name) const;

    IdString getBelName(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        std::stringstream name;
        name << "X" << bel.location.x << "/Y" << bel.location.y << "/" << IdString(getTileTypeBel(bel).name_id).str(this);
        return id(name.str());
    }

    uint32_t getBelChecksum(BelId bel) const { return bel.index; }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(bel_to_cell[bel] == nullptr);
        bel_to_cell[bel] = cell;
        cell->bel = bel;
        cell->belStrength = strength;
        refreshUiBel(bel);
    }

    void unbindBel(BelId bel)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(bel_to_cell[bel] != nullptr);
        bel_to_cell[bel]->bel = BelId();
        bel_to_cell[bel]->belStrength = STRENGTH_NONE;
        bel_to_cell[bel] = nullptr;
        refreshUiBel(bel);
    }

    Loc getBelLocation(BelId bel) const
    {
        Loc loc;
        loc.x = bel.location.x;
        loc.y = bel.location.y;
        loc.z = bel.index;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const
    {
	BelId res;
	res.location.x = loc.x;
	res.location.y = loc.y;
	res.index = loc.z;
	return res;
    }

    BelRange getBelsByTile(int x, int y) const
    {
        BelRange range;
        range.b.cursor_tile = x + y * device_info->width;
        range.b.cursor_index = -1;
        range.b.device = device_info;
        range.b.family = family_info;
        ++range.b;
        range.e.cursor_tile = x + y * device_info->width;
        range.e.cursor_index = getTileType(x, y).num_bels - 1;
        range.e.device = device_info;
        range.e.family = family_info;
	++range.e;
        return range;
    }

    bool getBelGlobalBuf(BelId bel) const
    {
	return getBelTypeInfo(bel).flags & BelTypePOD::FLAG_IS_GLOBAL_BUF;
    }

    bool checkBelAvail(BelId bel) const
    {
	return getConflictingBelCell(bel) == nullptr;
    }

    CellInfo *getBoundBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
	if (bel_to_cell.find(bel) == bel_to_cell.end())
	    return nullptr;
	else
            return bel_to_cell.at(bel);
    }

    CellInfo *getConflictingBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        CellInfo *ret = getBoundBelCell(bel);
	if (ret != nullptr)
	    return ret;
	auto &bt = getBelTypeInfo(bel);
	auto &tile = getTile(bel.location);
	for (int i = 0; i < bt.num_conflicts; i++) {
	    auto &other_pod = tile.bels[bel.index].conflicts[i];
	    BelId other;
	    other.location.x = other_pod.tile_x;
	    other.location.y = other_pod.tile_y;
	    other.index = other_pod.bel_idx;
	    ret = getBoundBelCell(other);
	    if (ret != nullptr)
		return ret;
	}
	return nullptr;
    }

    BelRange getBels() const
    {
        BelRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.device = device_info;
        range.b.family = family_info;
        ++range.b; //-1 and then ++ deals with the case of no Bels in the first tile
        range.e.cursor_tile = device_info->width * device_info->height;
        range.e.cursor_index = 0;
        range.e.device = device_info;
        range.e.family = family_info;
        return range;
    }

    IdString getBelType(BelId bel) const
    {
	return IdString(getBelTypeInfo(bel).name_id);
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId) const
    {
	// TODO: dump IO attrs here.
        std::vector<std::pair<IdString, std::string>> ret;
        return ret;
    }

    WireId getBelPinWire(BelId bel, IdString pin) const;

    BelPinRange getWireBelPins(WireId wire) const
    {
	BelPinRange res;
	auto &ttw = getTileTypeWire(wire);
	res.b.ptr = ttw.bel_xrefs.get();
	res.e.ptr = ttw.bel_xrefs.get() + ttw.num_bel_xrefs;
	res.b.bel_loc = res.e.bel_loc = wire.location;
	res.b.arch = res.e.arch = this;
	return res;
    }

    std::vector<IdString> getBelPins(BelId bel) const;

    // -------------------------------------------------

    WireId getWireByName(IdString name) const;

    IdString getWireName(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        std::stringstream name;
        name << "X" << wire.location.x << "/Y" << wire.location.y << "/" << getWireBasename(wire).str(this);
        return id(name.str());
    }

    IdString getWireType(WireId wire) const
    {
	return IdString(getTileTypeWire(wire).type_name_id);
    }

    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId) const
    {
        std::vector<std::pair<IdString, std::string>> ret;
        return ret;
    }

    uint32_t getWireChecksum(WireId wire) const { return wire.index; }

    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire] == nullptr);
        wire_to_net[wire] = net;
        net->wires[wire].pip = PipId();
        net->wires[wire].strength = strength;
    }

    void unbindWire(WireId wire)
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire] != nullptr);

        auto &net_wires = wire_to_net[wire]->wires;
        auto it = net_wires.find(wire);
        NPNR_ASSERT(it != net_wires.end());

        auto pip = it->second.pip;
        if (pip != PipId()) {
            pip_to_net[pip] = nullptr;
        }

        net_wires.erase(it);
        wire_to_net[wire] = nullptr;
    }

    bool checkWireAvail(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        return wire_to_net.find(wire) == wire_to_net.end() || wire_to_net.at(wire) == nullptr;
    }

    NetInfo *getBoundWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        if (wire_to_net.find(wire) == wire_to_net.end())
            return nullptr;
        else
            return wire_to_net.at(wire);
    }

    WireId getConflictingWireWire(WireId wire) const { return wire; }

    NetInfo *getConflictingWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        if (wire_to_net.find(wire) == wire_to_net.end())
            return nullptr;
        else
            return wire_to_net.at(wire);
    }

    DelayInfo getWireDelay(WireId wire) const
    {
        DelayInfo delay;
        delay.min_delay = 0;
        delay.max_delay = 0;
        return delay;
    }

    WireRange getWires() const
    {
        WireRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.device = device_info;
        range.b.family = family_info;
        ++range.b; //-1 and then ++ deals with the case of no wries in the first tile
        range.e.cursor_tile = device_info->width * device_info->height;
        range.e.cursor_index = 0;
        range.e.device = device_info;
        range.e.family = family_info;
        return range;
    }

    IdString getWireBasename(WireId wire) const
    {
	return IdString(getTileTypeWire(wire).name_id);
    }

    WireId getWireByLocAndBasename(Location loc, std::string basename) const
    {
	IdString basename_id = id(basename);
        WireId wireId;
        wireId.location = loc;
	auto &tt = getTileType(loc);
        for (int i = 0; i < tt.num_wires; i++) {
	    if (tt.wires[i].name_id == basename_id.index) {
                wireId.index = i;
                return wireId;
            }
        }
        return WireId();
    }

    // -------------------------------------------------

    PipId getPipByName(IdString name) const;
    IdString getPipName(PipId pip) const;

    IdString getPipType(PipId pip) const
    {
	// TODO: more pip kinds?
	if (pip.kind == PIP_KIND_PIP)
	    return id("pip");
	else
	    return id("port");
    }

    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId) const
    {
        std::vector<std::pair<IdString, std::string>> ret;
        return ret;
    }

    uint32_t getPipChecksum(PipId pip) const { return pip.index; }

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] == nullptr);

        pip_to_net[pip] = net;

        WireId dst = getPipDstWire(pip);
        NPNR_ASSERT(wire_to_net[dst] == nullptr);
        wire_to_net[dst] = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
    }

    void unbindPip(PipId pip)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] != nullptr);

        WireId dst = getPipDstWire(pip);
        NPNR_ASSERT(wire_to_net[dst] != nullptr);
        wire_to_net[dst] = nullptr;
        pip_to_net[pip]->wires.erase(dst);

        pip_to_net[pip] = nullptr;
    }

    bool checkPipAvail(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        return pip_to_net.find(pip) == pip_to_net.end() || pip_to_net.at(pip) == nullptr;
    }

    NetInfo *getBoundPipNet(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        if (pip_to_net.find(pip) == pip_to_net.end())
            return nullptr;
        else
            return pip_to_net.at(pip);
    }

    WireId getConflictingPipWire(PipId pip) const { return WireId(); }

    NetInfo *getConflictingPipNet(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        if (pip_to_net.find(pip) == pip_to_net.end())
            return nullptr;
        else
            return pip_to_net.at(pip);
    }

    AllPipRange getPips() const
    {
        AllPipRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_kind = PIP_KIND_PIP;
        range.b.cursor_index = 0;
        range.b.cursor_subindex = -1;
        range.b.device = device_info;
        range.b.family = family_info;
        ++range.b; //-1 and then ++ deals with the case of no wries in the first tile
        range.e.cursor_tile = device_info->width * device_info->height;
        range.e.cursor_kind = PIP_KIND_PIP;
        range.e.cursor_index = 0;
        range.e.cursor_subindex = 0;
        range.e.device = device_info;
        range.e.family = family_info;
        return range;
    }

    WireId getPipSrcWire(PipId pip) const
    {
        WireId wire;
	auto &tt = getTileType(pip.location);
	if (pip.kind == PIP_KIND_PIP) {
	    wire.location = pip.location;
	    wire.index = tt.pips[pip.index].wire_src;
	} else {
	    auto &tile = getTile(pip.location);
	    auto &conn = tile.conns[pip.index];
	    wire.location.x = conn.tile_x;
	    wire.location.y = conn.tile_y;
	    auto &other_tt = getTileType(wire.location);
	    wire.index = other_tt.ports[conn.port_idx].wires[pip.subindex];
	}
	return wire;
    }

    WireId getPipDstWire(PipId pip) const
    {
        WireId wire;
	auto &tt = getTileType(pip.location);
	if (pip.kind == PIP_KIND_PIP) {
	    wire.location = pip.location;
	    wire.index = tt.pips[pip.index].wire_dst;
	} else {
	    wire.location = pip.location;
	    wire.index = tt.ports[pip.index].wires[pip.subindex];
	}
	return wire;
    }

    DelayInfo getPipDelay(PipId pip) const
    {
	// TODO: delays.
	DelayInfo del;
	del.min_delay = 11;
	del.max_delay = 13;
	return del;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
	PipRange ret;
	ret.b.arch = this;
	ret.b.wire = wire;
	ret.b.mode = PipIterator::MODE_DOWNHILL;
	ret.b.stage = PipIterator::STAGE_PIPS;
	ret.b.cursor_index = -1;
	++ret.b;
	ret.e.arch = this;
	ret.e.wire = wire;
	ret.e.mode = PipIterator::MODE_DOWNHILL;
	ret.e.stage = PipIterator::STAGE_END;
	ret.e.cursor_index = 0;
	return ret;
    }

    PipRange getPipsUphill(WireId wire) const
    {
	PipRange ret;
	ret.b.arch = this;
	ret.b.wire = wire;
	ret.b.mode = PipIterator::MODE_UPHILL;
	ret.b.stage = PipIterator::STAGE_PIPS;
	ret.b.cursor_index = -1;
	++ret.b;
	ret.e.arch = this;
	ret.e.wire = wire;
	ret.e.mode = PipIterator::MODE_UPHILL;
	ret.e.stage = PipIterator::STAGE_END;
	ret.e.cursor_index = 0;
	return ret;
    }

    PipRange getWireAliases(WireId wire) const
    {
	// TODO: consider always-on intra-tile pips to be aliases?
	PipRange ret;
	ret.b.arch = this;
	ret.b.wire = wire;
	ret.b.mode = PipIterator::MODE_DOWNHILL;
	ret.b.stage = PipIterator::STAGE_PORTS;
	ret.b.cursor_index = -1;
	++ret.b;
	ret.e.arch = this;
	ret.e.wire = wire;
	ret.e.mode = PipIterator::MODE_DOWNHILL;
	ret.e.stage = PipIterator::STAGE_END;
	ret.e.cursor_index = 0;
	return ret;
    }

    Loc getPipLocation(PipId pip) const
    {
        Loc loc;
        loc.x = pip.location.x;
        loc.y = pip.location.y;
        loc.z = 0;
        return loc;
    }

    BelId getPackagePinBel(const std::string &pin) const;
    std::string getBelPackagePin(BelId bel) const;
    int getPioBelBank(BelId bel) const;
    // For getting GCLK, PLL, Vref, etc, pins
    std::string getPioFunctionName(BelId bel) const;
    BelId getPioByFunctionName(const std::string &name) const;

    PortType getBelPinType(BelId bel, IdString pin) const;

    // -------------------------------------------------

    GroupId getGroupByName(IdString name) const { return GroupId(); }
    IdString getGroupName(GroupId group) const { return IdString(); }
    std::vector<GroupId> getGroups() const { return std::vector<GroupId>(); }
    std::vector<BelId> getGroupBels(GroupId group) const { return std::vector<BelId>(); }
    std::vector<WireId> getGroupWires(GroupId group) const { return std::vector<WireId>(); }
    std::vector<PipId> getGroupPips(GroupId group) const { return std::vector<PipId>(); }
    std::vector<GroupId> getGroupGroups(GroupId group) const { return std::vector<GroupId>(); }

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const;
    delay_t getDelayEpsilon() const { return 20; }
    delay_t getRipupDelayPenalty() const { return 200; }
    float getDelayNS(delay_t v) const { return v * 0.001; }
    DelayInfo getDelayFromNS(float ns) const
    {
        DelayInfo del;
        del.min_delay = delay_t(ns * 1000);
        del.max_delay = delay_t(ns * 1000);
        return del;
    }
    uint32_t getDelayChecksum(delay_t v) const { return v; }
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const;

    // -------------------------------------------------

    bool pack();
    bool place();
    bool route();

    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const;

    DecalXY getBelDecal(BelId bel) const;
    DecalXY getWireDecal(WireId wire) const;
    DecalXY getPipDecal(PipId pip) const;
    DecalXY getGroupDecal(GroupId group) const;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const;

    // -------------------------------------------------
    // Placement validity checks
    // TODO: validate bel subtype (SLICEM vs SLICEL, IOBM vs IOBS, ...).
    bool isValidBelForCell(CellInfo *cell, BelId bel) const { return true; }
    bool isBelLocationValid(BelId bel) const { return true; }

    //void assignArchInfo();
};

NEXTPNR_NAMESPACE_END

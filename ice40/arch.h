/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski  <q3k@symbioticeda.com>
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

#include <boost/thread/shared_lock_guard.hpp>
#include <boost/thread/shared_mutex.hpp>

NEXTPNR_NAMESPACE_BEGIN

/**** Everything in this section must be kept in sync with chipdb.py ****/

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

NPNR_PACKED_STRUCT(struct BelWirePOD {
    int32_t wire_index;
    PortPin port;
});

NPNR_PACKED_STRUCT(struct BelInfoPOD {
    RelPtr<char> name;
    BelType type;
    int32_t num_bel_wires;
    RelPtr<BelWirePOD> bel_wires;
    int8_t x, y, z;
    int8_t padding_0;
});

NPNR_PACKED_STRUCT(struct BelPortPOD {
    int32_t bel_index;
    PortPin port;
});

NPNR_PACKED_STRUCT(struct PipInfoPOD {
    int32_t src, dst;
    int32_t delay;
    int8_t x, y;
    int16_t switch_mask;
    int32_t switch_index;
});

NPNR_PACKED_STRUCT(struct WireSegmentPOD {
    int8_t x, y;
    int16_t index;
});

NPNR_PACKED_STRUCT(struct WireInfoPOD {
    RelPtr<char> name;
    int32_t num_uphill, num_downhill;
    RelPtr<int32_t> pips_uphill, pips_downhill;

    int32_t num_bels_downhill;
    BelPortPOD bel_uphill;
    RelPtr<BelPortPOD> bels_downhill;

    int32_t num_segments;
    RelPtr<WireSegmentPOD> segments;

    int8_t x, y;
    WireType type;
    int8_t padding_0;
});

NPNR_PACKED_STRUCT(struct PackagePinPOD {
    RelPtr<char> name;
    int32_t bel_index;
});

NPNR_PACKED_STRUCT(struct PackageInfoPOD {
    RelPtr<char> name;
    int32_t num_pins;
    RelPtr<PackagePinPOD> pins;
});

enum TileType : uint32_t
{
    TILE_NONE = 0,
    TILE_LOGIC = 1,
    TILE_IO = 2,
    TILE_RAMB = 3,
    TILE_RAMT = 4,
    TILE_DSP0 = 5,
    TILE_DSP1 = 6,
    TILE_DSP2 = 7,
    TILE_DSP3 = 8,
    TILE_IPCON = 9
};

NPNR_PACKED_STRUCT(struct ConfigBitPOD { int8_t row, col; });

NPNR_PACKED_STRUCT(struct ConfigEntryPOD {
    RelPtr<char> name;
    int32_t num_bits;
    RelPtr<ConfigBitPOD> bits;
});

NPNR_PACKED_STRUCT(struct TileInfoPOD {
    int8_t cols, rows;
    int16_t num_config_entries;
    RelPtr<ConfigEntryPOD> entries;
});

static const int max_switch_bits = 5;

NPNR_PACKED_STRUCT(struct SwitchInfoPOD {
    int32_t num_bits;
    int8_t x, y;
    ConfigBitPOD cbits[max_switch_bits];
});

NPNR_PACKED_STRUCT(struct IerenInfoPOD {
    int8_t iox, ioy, ioz;
    int8_t ierx, iery, ierz;
});

NPNR_PACKED_STRUCT(struct BitstreamInfoPOD {
    int32_t num_switches, num_ierens;
    RelPtr<TileInfoPOD> tiles_nonrouting;
    RelPtr<SwitchInfoPOD> switches;
    RelPtr<IerenInfoPOD> ierens;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    int32_t width, height;
    int32_t num_bels, num_wires, num_pips;
    int32_t num_switches, num_packages;
    RelPtr<BelInfoPOD> bel_data;
    RelPtr<WireInfoPOD> wire_data;
    RelPtr<PipInfoPOD> pip_data;
    RelPtr<TileType> tile_grid;
    RelPtr<BitstreamInfoPOD> bits_info;
    RelPtr<PackageInfoPOD> packages_data;
});

#if defined(_MSC_VER)
extern const char *chipdb_blob_384;
extern const char *chipdb_blob_1k;
extern const char *chipdb_blob_5k;
extern const char *chipdb_blob_8k;
#else
extern const char chipdb_blob_384[];
extern const char chipdb_blob_1k[];
extern const char chipdb_blob_5k[];
extern const char chipdb_blob_8k[];
#endif

/************************ End of chipdb section. ************************/

struct BelIterator
{
    int cursor;

    BelIterator operator++()
    {
        cursor++;
        return *this;
    }
    BelIterator operator++(int)
    {
        BelIterator prior(*this);
        cursor++;
        return prior;
    }

    bool operator!=(const BelIterator &other) const { return cursor != other.cursor; }

    bool operator==(const BelIterator &other) const { return cursor == other.cursor; }

    BelId operator*() const
    {
        BelId ret;
        ret.index = cursor;
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

struct BelPinIterator
{
    const BelPortPOD *ptr = nullptr;

    void operator++() { ptr++; }
    bool operator!=(const BelPinIterator &other) const { return ptr != other.ptr; }

    BelPin operator*() const
    {
        BelPin ret;
        ret.bel.index = ptr->bel_index;
        ret.pin = ptr->port;
        return ret;
    }
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
    int cursor = -1;

    void operator++() { cursor++; }
    bool operator!=(const WireIterator &other) const { return cursor != other.cursor; }

    WireId operator*() const
    {
        WireId ret;
        ret.index = cursor;
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
    int cursor = -1;

    void operator++() { cursor++; }
    bool operator!=(const AllPipIterator &other) const { return cursor != other.cursor; }

    PipId operator*() const
    {
        PipId ret;
        ret.index = cursor;
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
    const int *cursor = nullptr;

    void operator++() { cursor++; }
    bool operator!=(const PipIterator &other) const { return cursor != other.cursor; }

    PipId operator*() const
    {
        PipId ret;
        ret.index = *cursor;
        return ret;
    }
};

struct PipRange
{
    PipIterator b, e;
    PipIterator begin() const { return b; }
    PipIterator end() const { return e; }
};

struct ArchArgs
{
    enum ArchArgsTypes
    {
        NONE,
        LP384,
        LP1K,
        LP8K,
        HX1K,
        HX8K,
        UP5K
    } type = NONE;
    std::string package;
};

/// Forward declare proxy classes for Arch.

class ArchRWProxyMethods;
class ArchRProxyMethods;
class ArchRWProxy;
class ArchRProxy;


/// Arch/Context
// Arch is the main state class of the PnR algorithms. It keeps note of mapped
// cells/nets, locked switches, etc.
// 
// In order to mutate state in Arch, you can do one of two things:
//   - directly call one of the wrapper methods to mutate state
//   - get a read or readwrite proxy to the Arch, and call methods on it

class Arch : public BaseCtx
{
    // We let proxy methods access our state.
    friend class ArchRWProxyMethods;
    friend class ArchRProxyMethods;
    // We let proxy objects access our mutex.
    friend class ArchRWProxy;
    friend class ArchRProxy;
private:
    // All of the following...
    std::vector<IdString> bel_to_cell;
    std::vector<IdString> wire_to_net;
    std::vector<IdString> pip_to_net;
    std::vector<IdString> switches_locked;
    mutable std::unordered_map<IdString, int> bel_by_name;
    mutable std::unordered_map<IdString, int> wire_by_name;
    mutable std::unordered_map<IdString, int> pip_by_name;

    // ... are guarded by the following lock:
    mutable boost::shared_mutex mtx_;

public:
    const ChipInfoPOD *chip_info;
    const PackageInfoPOD *package_info;

    ArchArgs args;
    Arch(ArchArgs args);

    // Get a readwrite proxy to arch - this will keep a readwrite lock on the
    // entire architecture until the proxy object goes out of scope.
    ArchRWProxy rwproxy(void);
    // Get a read-only proxy to arch - this will keep a  read lock on the
    // entire architecture until the proxy object goes out of scope. Other read
    // locks can be taken while this one still exists. Ie., the UI can draw
    // elements while the PnR is going a RO operation.
    ArchRProxy rproxy(void) const;

    std::string getChipName();

    IdString archId() const { return id("ice40"); }
    IdString archArgsToId(ArchArgs args) const;

    IdString belTypeToId(BelType type) const;
    BelType belTypeFromId(IdString id) const;

    IdString portPinToId(PortPin type) const;
    PortPin portPinFromId(IdString id) const;

    // -------------------------------------------------

    /// Wrappers around getting a r(w)proxy and calling a single method.
    // Deprecated: please acquire a proxy yourself and call the methods
    // you want on it.
    // Warning: these will content with locks taken by the r(w)proxies, and
    // thus can cause difficult to debug deadlocks - we'll be getting rid of
    // them because of that.
    void unbindWire(WireId wire);
    void unbindPip(PipId pip);
    void unbindBel(BelId bel);
    void bindWire(WireId wire, IdString net, PlaceStrength strength);
    void bindPip(PipId pip, IdString net, PlaceStrength strength);
    void bindBel(BelId bel, IdString cell, PlaceStrength strength);
    bool checkWireAvail(WireId wire) const;
    bool checkPipAvail(PipId pip) const;
    bool checkBelAvail(BelId bel) const;
    WireId getWireByName(IdString name) const;
    WireId getWireBelPin(BelId bel, PortPin pin) const;
    PipId getPipByName(IdString name) const;
    IdString getConflictingWireNet(WireId wire) const;
    IdString getConflictingPipNet(PipId pip) const;
    IdString getConflictingBelCell(BelId bel) const;
    IdString getBoundWireNet(WireId wire) const;
    IdString getBoundPipNet(PipId pip) const;
    IdString getBoundBelCell(BelId bel) const;
    BelId getBelByName(IdString name) const;
 
    // -------------------------------------------------

    /// Methods to get chip info - don't need to use a wrapper, as these are
    /// static per lifetime of object.

    IdString getBelName(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return id(chip_info->bel_data[bel.index].name.get());
    }

    uint32_t getBelChecksum(BelId bel) const
    {
        return bel.index;
    }

    BelRange getBels() const
    {
        BelRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info->num_bels;
        return range;
    }

    BelRange getBelsByType(BelType type) const
    {
        BelRange range;
// FIXME
#if 0
        if (type == "TYPE_A") {
            range.b.cursor = bels_type_a_begin;
            range.e.cursor = bels_type_a_end;
        }
        ...
#endif
        return range;
    }

    BelRange getBelsAtSameTile(BelId bel) const;

    BelType getBelType(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return chip_info->bel_data[bel.index].type;
    }


    BelPin getBelPinUphill(WireId wire) const
    {
        BelPin ret;
        NPNR_ASSERT(wire != WireId());

        if (chip_info->wire_data[wire.index].bel_uphill.bel_index >= 0) {
            ret.bel.index = chip_info->wire_data[wire.index].bel_uphill.bel_index;
            ret.pin = chip_info->wire_data[wire.index].bel_uphill.port;
        }

        return ret;
    }

    BelPinRange getBelPinsDownhill(WireId wire) const
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.ptr = chip_info->wire_data[wire.index].bels_downhill.get();
        range.e.ptr = range.b.ptr + chip_info->wire_data[wire.index].num_bels_downhill;
        return range;
    }

    // -------------------------------------------------

    IdString getWireName(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        return id(chip_info->wire_data[wire.index].name.get());
    }

    uint32_t getWireChecksum(WireId wire) const { return wire.index; }

    AllPipRange getPips() const
    {
        AllPipRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info->num_pips;
        return range;
    }
    
    IdString getPipName(PipId pip) const;

    uint32_t getPipChecksum(PipId pip) const { return pip.index; }

    WireId getPipSrcWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = chip_info->pip_data[pip.index].src;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = chip_info->pip_data[pip.index].dst;
        return wire;
    }

    DelayInfo getPipDelay(PipId pip) const
    {
        DelayInfo delay;
        NPNR_ASSERT(pip != PipId());
        delay.delay = chip_info->pip_data[pip.index].delay;
        return delay;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = chip_info->wire_data[wire.index].pips_downhill.get();
        range.e.cursor = range.b.cursor + chip_info->wire_data[wire.index].num_downhill;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = chip_info->wire_data[wire.index].pips_uphill.get();
        range.e.cursor = range.b.cursor + chip_info->wire_data[wire.index].num_uphill;
        return range;
    }

    PipRange getWireAliases(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = nullptr;
        range.e.cursor = nullptr;
        return range;
    }

    WireRange getWires() const
    {
        WireRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info->num_wires;
        return range;
    }


    BelId getPackagePinBel(const std::string &pin) const;
    std::string getBelPackagePin(BelId bel) const;

    // -------------------------------------------------

    // TODO(q3k) move this to archproxies?
    GroupId getGroupByName(IdString name) const;
    IdString getGroupName(GroupId group) const;
    std::vector<GroupId> getGroups() const;
    std::vector<BelId> getGroupBels(GroupId group) const;
    std::vector<WireId> getGroupWires(GroupId group) const;
    std::vector<PipId> getGroupPips(GroupId group) const;
    std::vector<GroupId> getGroupGroups(GroupId group) const;

    // -------------------------------------------------

    // These are also specific to the chip and not state, so they're available
    // on arch directly.
    void estimatePosition(BelId bel, int &x, int &y, bool &gb) const;
    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t getDelayEpsilon() const { return 20; }
    delay_t getRipupDelayPenalty() const { return 200; }
    float getDelayNS(delay_t v) const { return v * 0.001; }
    uint32_t getDelayChecksum(delay_t v) const { return v; }

    // -------------------------------------------------

    bool pack();
    bool place();
    bool route();

    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const;

    DecalXY getFrameDecal() const;
    DecalXY getBelDecal(BelId bel) const;
    DecalXY getWireDecal(WireId wire) const;
    DecalXY getPipDecal(PipId pip) const;
    DecalXY getGroupDecal(GroupId group) const;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, delay_t &delay) const;
    // Get the associated clock to a port, or empty if the port is combinational
    IdString getPortClock(const CellInfo *cell, IdString port) const;
    // Return true if a port is a clock
    bool isClockPort(const CellInfo *cell, IdString port) const;
    // Return true if a port is a net
    bool isGlobalNet(const NetInfo *net) const;

    // -------------------------------------------------

    IdString id_glb_buf_out;
    IdString id_icestorm_lc, id_sb_io, id_sb_gb;
    IdString id_cen, id_clk, id_sr;
    IdString id_i0, id_i1, id_i2, id_i3;
    IdString id_dff_en, id_neg_clk;
};

// Read-only methods on Arch that require state access.
class ArchRProxyMethods {
    // We let proxy objects access our private constructors.
    friend class ArchRProxy;
    friend class ArchRWProxy;
private:
    const Arch *parent_;
    ArchRProxyMethods(const Arch *parent) : parent_(parent) {}
    ArchRProxyMethods(ArchRProxyMethods &&other) noexcept : parent_(other.parent_) {}
    ArchRProxyMethods(const ArchRProxyMethods &other) : parent_(other.parent_) {}

public:
    ~ArchRProxyMethods() noexcept { }
    
    /// Perform placement validity checks, returning false on failure (all implemented in arch_place.cc)

    // Whether or not a given cell can be placed at a given Bel
    // This is not intended for Bel type checks, but finer-grained constraints
    // such as conflicting set/reset signals, etc
    bool isValidBelForCell(CellInfo *cell, BelId bel) const;

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel) const;

    // Helper function for above
    bool logicCellsCompatible(const std::vector<const CellInfo *> &cells) const;

    bool checkWireAvail(WireId wire) const;
    bool checkPipAvail(PipId pip) const;
    bool checkBelAvail(BelId bel) const;

    WireId getWireByName(IdString name) const;
    WireId getWireBelPin(BelId bel, PortPin pin) const;
    PipId getPipByName(IdString name) const;
    
    IdString getConflictingWireNet(WireId wire) const;
    IdString getConflictingPipNet(PipId pip) const;
    IdString getConflictingBelCell(BelId bel) const;

    IdString getBoundWireNet(WireId wire) const;
    IdString getBoundPipNet(PipId pip) const;
    IdString getBoundBelCell(BelId bel) const;

    BelId getBelByName(IdString name) const;
};

// A proxy object that keeps an Arch shared/readonly lock until it goes out
// of scope. All const/read-only ArchRProxyMethods are available on it.
class ArchRProxy : public ArchRProxyMethods {
    friend class Arch;
    friend class ArchRWProxy;
private:
    boost::shared_mutex *lock_;
    ArchRProxy(const Arch *parent) : ArchRProxyMethods(parent), lock_(&parent->mtx_)
    {
        lock_->lock_shared();
    }

public:
    ~ArchRProxy() {
        if (lock_ != nullptr) {
            lock_->unlock_shared();
        }
    }
    ArchRProxy(ArchRProxy &&other) : ArchRProxyMethods(other), lock_(other.lock_)
    {
        other.lock_ = nullptr;
    }
};

// State mutating methods on Arch.
class ArchRWProxyMethods {
    // We let proxy objects access our private constructors.
    friend class ArchRWProxy;
private:
    Arch *parent_;
    ArchRWProxyMethods(Arch *parent) : parent_(parent) {}
    ArchRWProxyMethods(ArchRWProxyMethods &&other) : parent_(other.parent_) {}
    ArchRWProxyMethods(const ArchRWProxyMethods &other) : parent_(other.parent_) {}
public:
    ~ArchRWProxyMethods() {}

    void unbindWire(WireId wire);
    void unbindPip(PipId pip);
    void unbindBel(BelId bel);
    void bindWire(WireId wire, IdString net, PlaceStrength strength);
    void bindPip(PipId pip, IdString net, PlaceStrength strength);
    void bindBel(BelId bel, IdString cell, PlaceStrength strength);
    // Returned pointer is valid as long as Proxy object exists.
    CellInfo *getCell(IdString cell);
};

// A proxy object that keeps an Arch readwrite lock until it goes out of scope.
// All ArchRProxyMethods and ArchRWProxyMethods are available on it.
class ArchRWProxy : public ArchRProxyMethods, public ArchRWProxyMethods {
    friend class Arch;
private:
    boost::shared_mutex *lock_;
    ArchRWProxy(Arch *parent) : ArchRProxyMethods(parent), ArchRWProxyMethods(parent), lock_(&parent->mtx_) {
        lock_->lock();
    }

public:
    ArchRWProxy(ArchRWProxy &&other) : ArchRProxyMethods(other), ArchRWProxyMethods(other), lock_(other.lock_)
    {
        other.lock_ = nullptr;
    }
    ~ArchRWProxy()
    {
        if (lock_ != nullptr) {
            lock_->unlock();
        }
    }
    

};

NEXTPNR_NAMESPACE_END

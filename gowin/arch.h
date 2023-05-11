/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2020  Pepijn de Vos <pepijn@symbioticeda.com>
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

#ifndef GOWIN_ARCH_H
#define GOWIN_ARCH_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "base_arch.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

#include "globals.h"

NEXTPNR_NAMESPACE_BEGIN

template <typename T> struct RelPtr
{
    int32_t offset;

    // void set(const T *ptr) {
    //     offset = reinterpret_cast<const char*>(ptr) -
    //              reinterpret_cast<const char*>(this);
    // }

    const T *get() const { return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset); }

    T *get_mut() const
    {
        return const_cast<T *>(reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset));
    }

    const T &operator[](std::size_t index) const { return get()[index]; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }

    RelPtr(const RelPtr &) = delete;
    RelPtr &operator=(const RelPtr &) = delete;
};

NPNR_PACKED_STRUCT(struct PairPOD {
    uint16_t dest_id;
    uint16_t src_id;
});

NPNR_PACKED_STRUCT(struct BelsPOD {
    uint16_t type_id;
    uint16_t num_ports;
    RelPtr<PairPOD> ports;
});

NPNR_PACKED_STRUCT(struct TilePOD /*TidePOD*/ {
    uint32_t num_bels;
    RelPtr<BelsPOD> bels;
    uint32_t num_pips;
    RelPtr<PairPOD> pips;
    uint32_t num_clock_pips;
    RelPtr<PairPOD> clock_pips;
    uint32_t num_aliases;
    RelPtr<PairPOD> aliases;
});

NPNR_PACKED_STRUCT(struct GlobalAliasPOD {
    uint16_t dest_row;
    uint16_t dest_col;
    uint16_t dest_id;
    uint16_t src_row;
    uint16_t src_col;
    uint16_t src_id;
});

NPNR_PACKED_STRUCT(struct TimingPOD {
    uint32_t name_id;
    // input, output
    uint32_t ff;
    uint32_t fr;
    uint32_t rf;
    uint32_t rr;
});

NPNR_PACKED_STRUCT(struct TimingGroupPOD {
    uint32_t name_id;
    uint32_t num_timings;
    RelPtr<TimingPOD> timings;
});

NPNR_PACKED_STRUCT(struct TimingGroupsPOD {
    TimingGroupPOD lut;
    TimingGroupPOD alu;
    TimingGroupPOD sram;
    TimingGroupPOD dff;
    // TimingGroupPOD dl;
    // TimingGroupPOD iddroddr;
    // TimingGroupPOD pll;
    // TimingGroupPOD dll;
    TimingGroupPOD bram;
    // TimingGroupPOD dsp;
    TimingGroupPOD fanout;
    TimingGroupPOD glbsrc;
    TimingGroupPOD hclk;
    TimingGroupPOD iodelay;
    // TimingGroupPOD io;
    // TimingGroupPOD iregoreg;
    TimingGroupPOD wire;
});

NPNR_PACKED_STRUCT(struct TimingClassPOD {
    uint32_t name_id;
    uint32_t num_groups;
    RelPtr<TimingGroupsPOD> groups;
});

NPNR_PACKED_STRUCT(struct PartnumberPOD {
    uint32_t name_id;
    uint32_t package_id;
    uint32_t device_id;
    uint32_t speed_id;
});

NPNR_PACKED_STRUCT(struct PinPOD {
    uint16_t index_id;
    uint16_t loc_id;
    uint32_t num_cfgs;
    RelPtr<uint32_t> cfgs;
});

NPNR_PACKED_STRUCT(struct PackagePOD {
    uint32_t name_id;
    uint32_t num_pins;
    RelPtr<PinPOD> pins;
});

NPNR_PACKED_STRUCT(struct VariantPOD {
    uint32_t name_id;
    uint32_t num_packages;
    RelPtr<PackagePOD> packages;
});

NPNR_PACKED_STRUCT(struct DatabasePOD {
    RelPtr<char> family;
    uint32_t version;
    uint16_t rows;
    uint16_t cols;
    RelPtr<RelPtr<TilePOD>> grid;
    uint32_t num_aliases;
    RelPtr<GlobalAliasPOD> aliases;
    uint32_t num_speeds;
    RelPtr<TimingClassPOD> speeds;
    uint32_t num_partnumbers;
    RelPtr<PartnumberPOD> partnumber_packages;
    uint32_t num_variants;
    RelPtr<VariantPOD> variants;
    uint16_t num_constids;
    uint16_t num_ids;
    RelPtr<RelPtr<char>> id_strs;
});

struct ArchArgs
{
    std::string family;
    std::string partnumber;
    // y = mx + c relationship between distance and delay for interconnect
    // delay estimates
    double delayScale = 0.4, delayOffset = 0.4;
    bool gui;
};

struct WireInfo;

struct PipInfo
{
    IdString name, type;
    std::map<IdString, std::string> attrs;
    NetInfo *bound_net;
    WireId srcWire, dstWire;
    DelayQuad delay;
    DecalXY decalxy_active, decalxy_inactive;
    Loc loc;
};

struct WireInfo
{
    IdString name, type;
    std::map<IdString, std::string> attrs;
    NetInfo *bound_net;
    std::vector<PipId> downhill, uphill;
    BelPin uphill_bel_pin;
    std::vector<BelPin> downhill_bel_pins;
    std::vector<BelPin> bel_pins;
    DecalXY decalxy_active, decalxy_inactive;
    int x, y;
};

struct PinInfo
{
    IdString name;
    WireId wire;
    PortType type;
};

struct BelInfo
{
    IdString name, type;
    std::map<IdString, std::string> attrs;
    CellInfo *bound_cell;
    dict<IdString, PinInfo> pins;
    std::vector<IdString> pin_cfgs;
    DecalXY decalxy_active, decalxy_inactive;
    int x, y, z;
    bool gb;
};

struct GroupInfo
{
    IdString name;
    std::vector<BelId> bels;
    std::vector<WireId> wires;
    std::vector<PipId> pips;
    std::vector<GroupId> groups;
    DecalXY decalxy;
};

struct CellDelayKey
{
    IdString from, to;
    inline bool operator==(const CellDelayKey &other) const { return from == other.from && to == other.to; }
    unsigned int hash() const { return mkhash(from.hash(), to.hash()); }
};

struct CellTiming
{
    dict<IdString, TimingPortClass> portClasses;
    dict<CellDelayKey, DelayQuad> combDelays;
    dict<IdString, std::vector<TimingClockingInfo>> clockingInfo;
};

struct ArchRanges : BaseArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = const std::vector<BelId> &;
    using TileBelsRangeT = const std::vector<BelId> &;
    using BelAttrsRangeT = const std::map<IdString, std::string> &;
    using BelPinsRangeT = std::vector<IdString>;
    using CellBelPinRangeT = std::array<IdString, 1>;
    // Wires
    using AllWiresRangeT = const std::vector<WireId> &;
    using DownhillPipRangeT = const std::vector<PipId> &;
    using UphillPipRangeT = const std::vector<PipId> &;
    using WireBelPinRangeT = const std::vector<BelPin> &;
    using WireAttrsRangeT = const std::map<IdString, std::string> &;
    // Pips
    using AllPipsRangeT = const std::vector<PipId> &;
    using PipAttrsRangeT = const std::map<IdString, std::string> &;
    // Groups
    using AllGroupsRangeT = std::vector<GroupId>;
    using GroupBelsRangeT = const std::vector<BelId> &;
    using GroupWiresRangeT = const std::vector<WireId> &;
    using GroupPipsRangeT = const std::vector<PipId> &;
    using GroupGroupsRangeT = const std::vector<GroupId> &;
};

enum class PLL // fixed PLL locations
{
    left,
    right
};

struct Arch : BaseArch<ArchRanges>
{
    std::string family;
    std::string device;
    const PackagePOD *package;
    const TimingGroupsPOD *speed;

    dict<IdString, WireInfo> wires;
    dict<IdString, PipInfo> pips;
    dict<IdString, BelInfo> bels;
    dict<GroupId, GroupInfo> groups;

    // These functions include useful errors if not found
    WireInfo &wire_info(IdString wire);
    PipInfo &pip_info(IdString pip);
    BelInfo &bel_info(IdString bel);
    NetInfo &net_info(IdString net);

    std::vector<IdString> bel_ids, wire_ids, pip_ids;

    dict<Loc, BelId> bel_by_loc;
    std::vector<std::vector<std::vector<BelId>>> bels_by_tile;

    dict<DecalId, std::vector<GraphicElement>> decal_graphics;

    int gridDimX = 0, gridDimY = 0;
    std::vector<std::vector<int>> tileBelDimZ;
    std::vector<std::vector<int>> tilePipDimZ;

    dict<IdString, CellTiming> cellTiming;

    void addWire(IdString name, IdString type, int x, int y);
    void addPip(IdString name, IdString type, IdString srcWire, IdString dstWire, DelayQuad delay, Loc loc);

    void addBel(IdString name, IdString type, Loc loc, bool gb);
    void addBelInput(IdString bel, IdString name, IdString wire);
    void addBelOutput(IdString bel, IdString name, IdString wire);
    void addBelInout(IdString bel, IdString name, IdString wire);

    void addGroup(IdString name);
    void addGroupBel(IdString group, IdString bel);
    void addGroupWire(IdString group, IdString wire);
    void addGroupPip(IdString group, IdString pip);
    void addGroupGroup(IdString group, IdString grp);

    void addDecalGraphic(DecalId decal, const GraphicElement &graphic);
    void setWireDecal(WireId wire, DecalXY active, DecalXY inactive);
    void setPipDecal(PipId pip, DecalXY active, DecalXY inactive);
    void setBelDecal(BelId bel, DecalXY active, DecalXY inactive);
    void setDefaultDecals(void);
    void setGroupDecal(GroupId group, DecalXY decalxy);
    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const override;
    DecalXY getBelDecal(BelId bel) const override;
    DecalXY getGroupDecal(GroupId grp) const override;
    DecalXY getPipDecal(PipId pip) const override;
    DecalXY getWireDecal(WireId pip) const override;

    void setWireAttr(IdString wire, IdString key, const std::string &value);
    void setPipAttr(IdString pip, IdString key, const std::string &value);
    void setBelAttr(IdString bel, IdString key, const std::string &value);

    void setDelayScaling(double scale, double offset);

    void addCellTimingClock(IdString cell, IdString port);
    void addCellTimingClass(IdString cell, IdString port, TimingPortClass cls);
    void addCellTimingDelay(IdString cell, IdString fromPort, IdString toPort, DelayQuad delay);
    void addCellTimingSetupHold(IdString cell, IdString port, IdString clock, DelayPair setup, DelayPair hold);
    void addCellTimingClockToOut(IdString cell, IdString port, IdString clock, DelayQuad clktoq);

    IdString wireToGlobal(int &row, int &col, const DatabasePOD *db, IdString &wire);
    DelayQuad getWireTypeDelay(IdString wire);
    void read_cst(std::istream &in);
    void addMuxBels(const DatabasePOD *db, int row, int col);

    // ---------------------------------------------------------------
    // Common Arch API. Every arch must provide the following methods.

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName() const override { return device; }

    ArchArgs archArgs() const override { return args; }
    IdString archArgsToId(ArchArgs args) const override { return id_none; }

    int getGridDimX() const override { return gridDimX; }
    int getGridDimY() const override { return gridDimY; }
    int getTileBelDimZ(int x, int y) const override { return tileBelDimZ[x][y]; }
    int getTilePipDimZ(int x, int y) const override { return tilePipDimZ[x][y]; }
    char getNameDelimiter() const override
    {
        return ' '; /* use a non-existent delimiter as we aren't using IdStringLists yet */
    }

    BelId getBelByName(IdStringList name) const override;
    IdStringList getBelName(BelId bel) const override;
    Loc getBelLocation(BelId bel) const override;
    BelId getBelByLocation(Loc loc) const override;
    const std::vector<BelId> &getBelsByTile(int x, int y) const override;
    bool getBelGlobalBuf(BelId bel) const override;
    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) override;
    void unbindBel(BelId bel) override;
    bool checkBelAvail(BelId bel) const override;
    CellInfo *getBoundBelCell(BelId bel) const override;
    CellInfo *getConflictingBelCell(BelId bel) const override;
    const std::vector<BelId> &getBels() const override;
    IdString getBelType(BelId bel) const override;
    const std::map<IdString, std::string> &getBelAttrs(BelId bel) const override;
    WireId getBelPinWire(BelId bel, IdString pin) const override;
    PortType getBelPinType(BelId bel, IdString pin) const override;
    std::vector<IdString> getBelPins(BelId bel) const override;
    std::array<IdString, 1> getBelPinsForCellPin(const CellInfo *cell_info, IdString pin) const override;
    // Placement validity checks
    virtual bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        return cell_type == id_DUMMY_CELL || cell_type == this->getBelType(bel);
    }

    WireId getWireByName(IdStringList name) const override;
    IdStringList getWireName(WireId wire) const override;
    IdString getWireType(WireId wire) const override;
    const std::map<IdString, std::string> &getWireAttrs(WireId wire) const override;
    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) override;
    void unbindWire(WireId wire) override;
    bool checkWireAvail(WireId wire) const override;
    NetInfo *getBoundWireNet(WireId wire) const override;
    WireId getConflictingWireWire(WireId wire) const override { return wire; }
    NetInfo *getConflictingWireNet(WireId wire) const override;
    DelayQuad getWireDelay(WireId wire) const override { return DelayQuad(0); }
    const std::vector<WireId> &getWires() const override;
    const std::vector<BelPin> &getWireBelPins(WireId wire) const override;

    PipId getPipByName(IdStringList name) const override;
    IdStringList getPipName(PipId pip) const override;
    IdString getPipType(PipId pip) const override;
    const std::map<IdString, std::string> &getPipAttrs(PipId pip) const override;
    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) override;
    void unbindPip(PipId pip) override;
    bool checkPipAvail(PipId pip) const override;
    NetInfo *getBoundPipNet(PipId pip) const override;
    WireId getConflictingPipWire(PipId pip) const override;
    NetInfo *getConflictingPipNet(PipId pip) const override;
    const std::vector<PipId> &getPips() const override;
    Loc getPipLocation(PipId pip) const override;
    WireId getPipSrcWire(PipId pip) const override;
    WireId getPipDstWire(PipId pip) const override;
    DelayQuad getPipDelay(PipId pip) const override;
    const std::vector<PipId> &getPipsDownhill(WireId wire) const override;
    const std::vector<PipId> &getPipsUphill(WireId wire) const override;

    GroupId getGroupByName(IdStringList name) const override;
    IdStringList getGroupName(GroupId group) const override;
    std::vector<GroupId> getGroups() const override;
    const std::vector<BelId> &getGroupBels(GroupId group) const override;
    const std::vector<WireId> &getGroupWires(GroupId group) const override;
    const std::vector<PipId> &getGroupPips(GroupId group) const override;
    const std::vector<GroupId> &getGroupGroups(GroupId group) const override;

    delay_t estimateDelay(WireId src, WireId dst) const override;
    delay_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const override;
    delay_t getDelayEpsilon() const override { return 0.01; }
    delay_t getRipupDelayPenalty() const override { return 0.4; }
    float getDelayNS(delay_t v) const override { return v; }

    delay_t getDelayFromNS(float ns) const override { return ns; }

    uint32_t getDelayChecksum(delay_t v) const override { return 0; }

    BoundingBox getRouteBoundingBox(WireId src, WireId dst) const override;

    bool pack() override;
    bool place() override;
    bool route() override;

    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const override;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override;

    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override;

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    // ---------------------------------------------------------------
    // Internal usage
    void assignArchInfo() override;
    bool cellsCompatible(const CellInfo **cells, int count) const;
    bool haveBelType(int x, int y, IdString bel_type);
    bool allocate_longwire(NetInfo *ni, int lw_idx = -1);
    void fix_longwire_bels();
    void pre_pack(Context *ctx);
    void post_pack(Context *ctx);
    void pre_route(Context *ctx);
    void post_route(Context *ctx);
    void auto_longwires();
    void add_pllvr_ports(DatabasePOD const *db, BelsPOD const *bel, IdString belname, int row, int col);
    void add_rpll_ports(DatabasePOD const *db, BelsPOD const *bel, IdString belname, int row, int col);
    void fix_pll_nets(Context *ctx);
    bool is_GCLKT_iob(const CellInfo *cell);
    void bind_pll_to_bel(CellInfo *ci, PLL loc);

    void mark_used_hclk(Context *ctx);
    IdString apply_local_aliases(int row, int col, const DatabasePOD *db, IdString &wire);

    WireId get_make_port_wire(const DatabasePOD *db, const BelsPOD *bel, int row, int col, IdString port);

    GowinGlobalRouter globals_router;
    void mark_gowin_globals(Context *ctx);
    void route_gowin_globals(Context *ctx);

    // chip db version
    unsigned int const chipdb_version = 2;

    std::vector<IdString> cell_types;

    // clock spines cache
    // spine_id : [wire_id, wire_id, ...]
    dict<IdString, std::vector<IdString>> clockSpinesCache;
    void updateClockSpinesCache(IdString spine_id, IdString wire_id);
    void fixClockSpineDecals(void);

    // XXX GW1N-9C DDR quirk
    bool ddr_has_extra_inputs = false;
    // XXX GW1NR-9 iobuf quirk
    bool gw1n9_quirk = false;

    // 8 Long wires
    uint8_t avail_longwires = 0xff;

    // Permissible combinations of modes in a single slice
    std::map<const IdString, IdString> dff_comp_mode;

    // max global clock wires
    int max_clock;
};

// Bels Z range
namespace BelZ {
enum
{
    ioba_z = 0,      // IOBA
    iobb_z = 1,      // IOBB
    mux_0_z = 10,    // start Z for the MUX2LUT5 bels
    lutram_0_z = 30, // start Z for the LUTRAM bels
    vcc_0_z = 277,   // virtual VCC bel Z
    gnd_0_z = 278,   // virtual VSS bel Z
    osc_z = 280,     // Z for the oscillator bels
    bufs_0_z = 281,  // Z for long wire buffer bel
    pll_z = 289,     // PLL
    pllvr_z = 290,   // PLLVR
    iologic_z = 291, // IOLOGIC
    oser16_z = 293,  // OSER16
    ides16_z = 294,  // IDES16
    free_z = 295     // Must be the last, one can use z starting from this value, adjust accordingly.
};
}

NEXTPNR_NAMESPACE_END

#endif /* GOWIN_ARCH_H */

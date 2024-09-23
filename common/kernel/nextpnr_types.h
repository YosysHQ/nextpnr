/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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

// Types defined in this header use one or more user defined types (e.g. BelId).
// If a new common type is desired that doesn't depend on a user defined type,
// either put it in it's own header, or in nextpnr_base_types.h.
#ifndef NEXTPNR_TYPES_H
#define NEXTPNR_TYPES_H

#include <unordered_map>
#include <unordered_set>

#include "archdefs.h"
#include "hashlib.h"
#include "indexed_store.h"
#include "nextpnr_base_types.h"
#include "nextpnr_namespaces.h"
#include "property.h"

NEXTPNR_NAMESPACE_BEGIN

struct DecalXY
{
    DecalId decal;
    float x = 0, y = 0;

    bool operator==(const DecalXY &other) const { return (decal == other.decal && x == other.x && y == other.y); }
};

struct BelPin
{
    BelId bel;
    IdString pin;
};

struct Region
{
    IdString name;

    bool constr_bels = false;
    bool constr_wires = false;
    bool constr_pips = false;

    pool<BelId> bels;
    pool<WireId> wires;
    pool<Loc> piplocs;
};

struct PipMap
{
    PipId pip = PipId();
    PlaceStrength strength = STRENGTH_NONE;
};

struct CellInfo;

struct PortRef
{
    CellInfo *cell = nullptr;
    IdString port;
};

// Zero checking which works regardless if delay_t is floating or integer
inline bool is_zero_delay(delay_t delay)
{
    if constexpr (std::is_floating_point<delay_t>::value) {
        return std::fpclassify(delay) == FP_ZERO;
    } else {
        return delay == 0;
    }
}

// minimum and maximum delay
struct DelayPair
{
    DelayPair() : min_delay(0), max_delay(0){};
    explicit DelayPair(delay_t delay) : min_delay(delay), max_delay(delay) {}
    DelayPair(delay_t min_delay, delay_t max_delay) : min_delay(min_delay), max_delay(max_delay) {}
    delay_t minDelay() const { return min_delay; }
    delay_t maxDelay() const { return max_delay; }
    delay_t min_delay, max_delay;
    DelayPair operator+(const DelayPair &other) const
    {
        return {min_delay + other.min_delay, max_delay + other.max_delay};
    }
    DelayPair operator-(const DelayPair &other) const
    {
        return {min_delay - other.min_delay, max_delay - other.max_delay};
    }
    DelayPair &operator+=(const DelayPair &rhs)
    {
        min_delay += rhs.min_delay;
        max_delay += rhs.max_delay;
        return *this;
    }
    DelayPair &operator-=(const DelayPair &rhs)
    {
        min_delay -= rhs.min_delay;
        max_delay -= rhs.max_delay;
        return *this;
    }
};

// four-quadrant, min and max rise and fall delay
struct DelayQuad
{
    DelayPair rise, fall;
    DelayQuad() : rise(0), fall(0) {}
    explicit DelayQuad(delay_t delay) : rise(delay), fall(delay) {}
    DelayQuad(delay_t min_delay, delay_t max_delay) : rise(min_delay, max_delay), fall(min_delay, max_delay) {}
    DelayQuad(DelayPair rise, DelayPair fall) : rise(rise), fall(fall) {}
    DelayQuad(delay_t min_rise, delay_t max_rise, delay_t min_fall, delay_t max_fall)
            : rise(min_rise, max_rise), fall(min_fall, max_fall)
    {
    }

    delay_t minRiseDelay() const { return rise.minDelay(); }
    delay_t maxRiseDelay() const { return rise.maxDelay(); }
    delay_t minFallDelay() const { return fall.minDelay(); }
    delay_t maxFallDelay() const { return fall.maxDelay(); }
    delay_t minDelay() const { return std::min<delay_t>(rise.minDelay(), fall.minDelay()); }
    delay_t maxDelay() const { return std::max<delay_t>(rise.maxDelay(), fall.maxDelay()); }

    DelayPair delayPair() const { return DelayPair(minDelay(), maxDelay()); }

    DelayQuad operator+(const DelayQuad &other) const { return {rise + other.rise, fall + other.fall}; }
    DelayQuad operator-(const DelayQuad &other) const { return {rise - other.rise, fall - other.fall}; }
    DelayQuad &operator+=(const DelayQuad &rhs)
    {
        rise += rhs.rise;
        fall += rhs.fall;
        return *this;
    }

    DelayQuad &operator-=(const DelayQuad &rhs)
    {
        rise -= rhs.rise;
        fall -= rhs.fall;
        return *this;
    }
};

struct ClockConstraint;

struct NetInfo : ArchNetInfo
{
    explicit NetInfo(IdString name) : name(name) {}
    IdString name, hierpath;
    int32_t udata = 0;

    PortRef driver;
    indexed_store<PortRef> users;
    dict<IdString, Property> attrs;

    // If this is set to a non-empty ID, then the driver is ignored and it will be routed from any wire with a matching
    // getWireConstantValue
    IdString constant_value;

    // wire -> uphill_pip
    dict<WireId, PipMap> wires;

    std::vector<IdString> aliases; // entries in net_aliases that point to this net

    std::unique_ptr<ClockConstraint> clkconstr;

    Region *region = nullptr;
};

enum PortType
{
    PORT_IN = 0,
    PORT_OUT = 1,
    PORT_INOUT = 2
};

[[maybe_unused]] static const std::string portType_to_str(PortType typ)
{
    switch (typ) {
    case PORT_IN:
        return "PORT_IN";
    case PORT_OUT:
        return "PORT_OUT";
    case PORT_INOUT:
        return "PORT_INOUT";
    default:
        NPNR_ASSERT_FALSE("Impossible PortType");
    }
}

struct PortInfo
{
    IdString name;
    NetInfo *net;
    PortType type;
    store_index<PortRef> user_idx{};
};

struct Context;

enum TimingPortClass
{
    TMG_CLOCK_INPUT,     // Clock input to a sequential cell
    TMG_GEN_CLOCK,       // Generated clock output (PLL, DCC, etc)
    TMG_REGISTER_INPUT,  // Input to a register, with an associated clock (may also have comb. fanout too)
    TMG_REGISTER_OUTPUT, // Output from a register
    TMG_COMB_INPUT,      // Combinational input, no paths end here
    TMG_COMB_OUTPUT,     // Combinational output, no paths start here
    TMG_STARTPOINT,      // Unclocked primary startpoint, such as an IO cell output
    TMG_ENDPOINT,        // Unclocked primary endpoint, such as an IO cell input
    TMG_IGNORE,          // Asynchronous to all clocks, "don't care", and should be ignored (false path) for analysis
};

[[maybe_unused]] static const std::string timingPortClass_to_str(TimingPortClass tmg_class)
{
    switch (tmg_class) {
    case TMG_CLOCK_INPUT:
        return "TMG_CLOCK_INPUT";
    case TMG_GEN_CLOCK:
        return "TMG_GEN_CLOCK";
    case TMG_REGISTER_INPUT:
        return "TMG_REGISTER_INPUT";
    case TMG_REGISTER_OUTPUT:
        return "TMG_REGISTER_OUTPUT";
    case TMG_COMB_INPUT:
        return "TMG_COMB_INPUT";
    case TMG_COMB_OUTPUT:
        return "TMG_COMB_OUTPUT";
    case TMG_STARTPOINT:
        return "TMG_STARTPOINT";
    case TMG_ENDPOINT:
        return "TMG_ENDPOINT";
    case TMG_IGNORE:
        return "TMG_IGNORE";
    default:
        NPNR_ASSERT_FALSE("Impossible TimingPortClass");
    }
}

enum ClockEdge
{
    RISING_EDGE,
    FALLING_EDGE
};

[[maybe_unused]] static const std::string clockEdge_to_str(ClockEdge edge)
{
    switch (edge) {
    case RISING_EDGE:
        return "RISING_EDGE";
    case FALLING_EDGE:
        return "FALLING_EDGE";
    default:
        NPNR_ASSERT_FALSE("Impossible ClockEdge");
    }
}

struct TimingClockingInfo
{
    IdString clock_port; // Port name of clock domain
    ClockEdge edge;
    DelayPair setup, hold; // Input timing checks
    DelayQuad clockToQ;    // Output clock-to-Q time
};

struct PseudoCell
{
    virtual Loc getLocation() const = 0;
    virtual WireId getPortWire(IdString port) const = 0;

    virtual bool getDelay(IdString fromPort, IdString toPort, DelayQuad &delay) const = 0;
    virtual TimingPortClass getPortTimingClass(IdString port, int &clockInfoCount) const = 0;
    virtual TimingClockingInfo getPortClockingInfo(IdString port, int index) const = 0;
    virtual ~PseudoCell(){};
};

struct RegionPlug : PseudoCell
{
    RegionPlug(Loc loc) : loc(loc) {} // 'loc' is a notional location for the placer only
    Loc getLocation() const override { return loc; }
    WireId getPortWire(IdString port) const override { return port_wires.at(port); }

    // TODO: partial reconfiguration region timing
    bool getDelay(IdString /*fromPort*/, IdString /*toPort*/, DelayQuad & /*delay*/) const override { return false; }
    TimingPortClass getPortTimingClass(IdString /*port*/, int & /*clockInfoCount*/) const override
    {
        return TMG_IGNORE;
    }
    TimingClockingInfo getPortClockingInfo(IdString /*port*/, int /*index*/) const override
    {
        return TimingClockingInfo{};
    }

    dict<IdString, WireId> port_wires;
    Loc loc;
};

struct CellInfo : ArchCellInfo
{
    CellInfo(Context *ctx, IdString name, IdString type) : ctx(ctx), name(name), type(type) {}
    Context *ctx = nullptr;

    IdString name, type, hierpath;
    int32_t udata;

    dict<IdString, PortInfo> ports;
    dict<IdString, Property> attrs, params;

    BelId bel;
    PlaceStrength belStrength = STRENGTH_NONE;

    // cell is part of a cluster if != ClusterId
    ClusterId cluster;

    Region *region = nullptr;

    std::unique_ptr<PseudoCell> pseudo_cell{};

    void addInput(IdString name);
    void addOutput(IdString name);
    void addInout(IdString name);

    void setParam(IdString name, Property value);
    void unsetParam(IdString name);
    void setAttr(IdString name, Property value);
    void unsetAttr(IdString name);
    // check whether a bel complies with the cell's region constraint
    bool testRegion(BelId bel) const;

    bool isPseudo() const { return bool(pseudo_cell); }

    Loc getLocation() const;

    NetInfo *getPort(IdString name)
    {
        auto found = ports.find(name);
        return (found == ports.end()) ? nullptr : found->second.net;
    }
    const NetInfo *getPort(IdString name) const
    {
        auto found = ports.find(name);
        return (found == ports.end()) ? nullptr : found->second.net;
    }
    void connectPort(IdString port, NetInfo *net);
    void disconnectPort(IdString port);
    void connectPorts(IdString port, CellInfo *other, IdString other_port);
    void movePortTo(IdString port, CellInfo *other, IdString other_port);
    void renamePort(IdString old_name, IdString new_name);
    void movePortBusTo(IdString old_name, int old_offset, bool old_brackets, CellInfo *new_cell, IdString new_name,
                       int new_offset, bool new_brackets, int width);
    void copyPortTo(IdString port, CellInfo *other, IdString other_port);
    void copyPortBusTo(IdString old_name, int old_offset, bool old_brackets, CellInfo *new_cell, IdString new_name,
                       int new_offset, bool new_brackets, int width);
};

struct ClockConstraint
{
    DelayPair high;
    DelayPair low;
    DelayPair period;
};

struct ClockFmax
{
    float achieved;
    float constraint;
};

struct ClockEvent
{
    IdString clock;
    ClockEdge edge;

    bool operator==(const ClockEvent &other) const { return clock == other.clock && edge == other.edge; }
    unsigned int hash() const { return mkhash(clock.hash(), int(edge)); }
};

struct ClockPair
{
    ClockEvent start, end;

    bool operator==(const ClockPair &other) const { return start == other.start && end == other.end; }
    unsigned int hash() const { return mkhash(start.hash(), end.hash()); }
};

struct CriticalPath
{
    struct Segment
    {

        // Segment type
        enum class Type
        {
            CLK_TO_CLK, // Clock to clock delay
            CLK_SKEW,   // Clock skew
            CLK_TO_Q,   // Clock-to-Q delay
            SOURCE,     // Delayless source
            LOGIC,      // Combinational logic delay
            ROUTING,    // Routing delay
            SETUP,      // Setup time in sink
            HOLD        // Hold time in sink
        };

        [[maybe_unused]] static const std::string type_to_str(Type typ)
        {
            switch (typ) {
            case Type::CLK_TO_CLK:
                return "clk-to-clk";
            case Type::CLK_SKEW:
                return "clk-skew";
            case Type::CLK_TO_Q:
                return "clk-to-q";
            case Type::SOURCE:
                return "source";
            case Type::LOGIC:
                return "logic";
            case Type::ROUTING:
                return "routing";
            case Type::SETUP:
                return "setup";
            case Type::HOLD:
                return "hold";
            default:
                NPNR_ASSERT_FALSE("Impossible Segment::Type");
            }
        }

        // Type
        Type type;
        // Net name (routing only)
        IdString net;
        // From cell.port
        std::pair<IdString, IdString> from;
        // To cell.port
        std::pair<IdString, IdString> to;
        // Segment delay
        delay_t delay;
    };

    // Clock pair
    ClockPair clock_pair;

    // if sum[segments.delay] < 0 this is a hold/min violation
    // if sum[segments.delay] > max_delay this is a setup/max violation
    delay_t max_delay;

    // Individual path segments
    std::vector<Segment> segments;
};

// Holds timing information of a single source to sink path of a net
struct NetSinkTiming
{
    // Clock event pair
    ClockPair clock_pair;
    // Cell and port (the sink)
    std::pair<IdString, IdString> cell_port;
    // Delay
    DelayPair delay;
};

struct TimingResult
{
    // Achieved and target Fmax for all clock domains
    dict<IdString, ClockFmax> clock_fmax;
    // Single domain critical paths
    dict<IdString, CriticalPath> clock_paths;
    // Cross-domain critical paths
    std::vector<CriticalPath> xclock_paths;
    // Domains with no interior paths
    pool<IdString> empty_paths;

    // Detailed net timing data
    dict<IdString, std::vector<NetSinkTiming>> detailed_net_timings;

    // Histogram of slack
    dict<int, unsigned> slack_histogram;

    // Min delay violations, only hold time for now
    std::vector<CriticalPath> min_delay_violations;
};

// Represents the contents of a non-leaf cell in a design
// with hierarchy

struct HierarchicalPort
{
    IdString name;
    PortType dir;
    std::vector<IdString> nets;
    int offset;
    bool upto;
};

struct HierarchicalCell
{
    IdString name, type, parent, fullpath;
    // Name inside cell instance -> global name
    dict<IdString, IdString> leaf_cells, nets;
    // Global name -> name inside cell instance
    dict<IdString, IdString> leaf_cells_by_gname, nets_by_gname;
    // Cell port to net
    dict<IdString, HierarchicalPort> ports;
    // Name inside cell instance -> global name
    dict<IdString, IdString> hier_cells;
};

NEXTPNR_NAMESPACE_END

#endif /* NEXTPNR_TYPES_H */

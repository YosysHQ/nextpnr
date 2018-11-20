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
#include <assert.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/functional/hash.hpp>

#ifndef NEXTPNR_H
#define NEXTPNR_H

#ifdef NEXTPNR_NAMESPACE
#define NEXTPNR_NAMESPACE_PREFIX NEXTPNR_NAMESPACE::
#define NEXTPNR_NAMESPACE_BEGIN namespace NEXTPNR_NAMESPACE {
#define NEXTPNR_NAMESPACE_END }
#define USING_NEXTPNR_NAMESPACE using namespace NEXTPNR_NAMESPACE;
#else
#define NEXTPNR_NAMESPACE_PREFIX
#define NEXTPNR_NAMESPACE_BEGIN
#define NEXTPNR_NAMESPACE_END
#define USING_NEXTPNR_NAMESPACE
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NPNR_ATTRIBUTE(...) __attribute__((__VA_ARGS__))
#define NPNR_NORETURN __attribute__((noreturn))
#define NPNR_DEPRECATED __attribute__((deprecated))
#define NPNR_PACKED_STRUCT(...) __VA_ARGS__ __attribute__((packed))
#elif defined(_MSC_VER)
#define NPNR_ATTRIBUTE(...)
#define NPNR_NORETURN __declspec(noreturn)
#define NPNR_DEPRECATED __declspec(deprecated)
#define NPNR_PACKED_STRUCT(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
#else
#define NPNR_ATTRIBUTE(...)
#define NPNR_NORETURN
#define NPNR_DEPRECATED
#define NPNR_PACKED_STRUCT(...) __VA_ARGS__
#endif

NEXTPNR_NAMESPACE_BEGIN

class assertion_failure : public std::runtime_error
{
  public:
    assertion_failure(std::string msg, std::string expr_str, std::string filename, int line);

    std::string msg;
    std::string expr_str;
    std::string filename;
    int line;
};

NPNR_NORETURN
inline void assert_fail_impl(const char *message, const char *expr_str, const char *filename, int line)
{
    throw assertion_failure(message, expr_str, filename, line);
}

NPNR_NORETURN
inline void assert_fail_impl_str(std::string message, const char *expr_str, const char *filename, int line)
{
    throw assertion_failure(message, expr_str, filename, line);
}

#define NPNR_ASSERT(cond) (!(cond) ? assert_fail_impl(#cond, #cond, __FILE__, __LINE__) : (void)true)
#define NPNR_ASSERT_MSG(cond, msg) (!(cond) ? assert_fail_impl(msg, #cond, __FILE__, __LINE__) : (void)true)
#define NPNR_ASSERT_FALSE(msg) (assert_fail_impl(msg, "false", __FILE__, __LINE__))
#define NPNR_ASSERT_FALSE_STR(msg) (assert_fail_impl_str(msg, "false", __FILE__, __LINE__))

struct BaseCtx;
struct Context;

struct IdString
{
    int index;

    static void initialize_arch(const BaseCtx *ctx);

    static void initialize_add(const BaseCtx *ctx, const char *s, int idx);

    constexpr IdString(int index = 0) : index(index) {}

    void set(const BaseCtx *ctx, const std::string &s);

    IdString(const BaseCtx *ctx, const std::string &s) { set(ctx, s); }

    IdString(const BaseCtx *ctx, const char *s) { set(ctx, s); }

    const std::string &str(const BaseCtx *ctx) const;

    const char *c_str(const BaseCtx *ctx) const;

    bool operator<(const IdString &other) const { return index < other.index; }

    bool operator==(const IdString &other) const { return index == other.index; }

    bool operator!=(const IdString &other) const { return index != other.index; }

    bool empty() const { return index == 0; }
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX IdString>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX IdString &obj) const noexcept
    {
        return std::hash<int>()(obj.index);
    }
};
} // namespace std

NEXTPNR_NAMESPACE_BEGIN

struct GraphicElement
{
    enum type_t
    {
        TYPE_NONE,
        TYPE_LINE,
        TYPE_ARROW,
        TYPE_BOX,
        TYPE_CIRCLE,
        TYPE_LABEL,

        TYPE_MAX
    } type = TYPE_NONE;

    enum style_t
    {
        STYLE_GRID,
        STYLE_FRAME,    // Static "frame". Contrast between STYLE_INACTIVE and STYLE_ACTIVE
        STYLE_HIDDEN,   // Only display when object is selected or highlighted
        STYLE_INACTIVE, // Render using low-contrast color
        STYLE_ACTIVE,   // Render using high-contast color

        // UI highlight groups
        STYLE_HIGHLIGHTED0,
        STYLE_HIGHLIGHTED1,
        STYLE_HIGHLIGHTED2,
        STYLE_HIGHLIGHTED3,
        STYLE_HIGHLIGHTED4,
        STYLE_HIGHLIGHTED5,
        STYLE_HIGHLIGHTED6,
        STYLE_HIGHLIGHTED7,

        STYLE_SELECTED,
        STYLE_HOVER,

        STYLE_MAX
    } style = STYLE_FRAME;

    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, z = 0;
    std::string text;
};

struct Loc
{
    int x = -1, y = -1, z = -1;

    Loc() {}
    Loc(int x, int y, int z) : x(x), y(y), z(z) {}

    bool operator==(const Loc &other) const { return (x == other.x) && (y == other.y) && (z == other.z); }
    bool operator!=(const Loc &other) const { return (x != other.x) || (y != other.y) || (z != other.z); }
};

struct TimingConstrObjectId
{
    int32_t index = -1;

    bool operator==(const TimingConstrObjectId &other) const { return index == other.index; }
    bool operator!=(const TimingConstrObjectId &other) const { return index != other.index; }
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX Loc>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX Loc &obj) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<int>()(obj.x));
        boost::hash_combine(seed, hash<int>()(obj.y));
        boost::hash_combine(seed, hash<int>()(obj.z));
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX TimingConstrObjectId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX TimingConstrObjectId &obj) const noexcept
    {
        return hash<int>()(obj.index);
    }
};

} // namespace std

#include "archdefs.h"

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

struct CellInfo;

struct Region
{
    IdString name;

    bool constr_bels = false;
    bool constr_wires = false;
    bool constr_pips = false;

    std::unordered_set<BelId> bels;
    std::unordered_set<WireId> wires;
    std::unordered_set<Loc> piplocs;
};

enum PlaceStrength
{
    STRENGTH_NONE = 0,
    STRENGTH_WEAK = 1,
    STRENGTH_STRONG = 2,
    STRENGTH_FIXED = 3,
    STRENGTH_LOCKED = 4,
    STRENGTH_USER = 5
};

struct PortRef
{
    CellInfo *cell = nullptr;
    IdString port;
    delay_t budget = 0;
};

struct PipMap
{
    PipId pip = PipId();
    PlaceStrength strength = STRENGTH_NONE;
};

struct ClockConstraint;

struct NetInfo : ArchNetInfo
{
    IdString name;
    int32_t udata = 0;

    PortRef driver;
    std::vector<PortRef> users;
    std::unordered_map<IdString, std::string> attrs;

    // wire -> uphill_pip
    std::unordered_map<WireId, PipMap> wires;

    std::unique_ptr<ClockConstraint> clkconstr;

    TimingConstrObjectId tmg_id;

    Region *region = nullptr;
};

enum PortType
{
    PORT_IN = 0,
    PORT_OUT = 1,
    PORT_INOUT = 2
};

struct PortInfo
{
    IdString name;
    NetInfo *net;
    PortType type;
    TimingConstrObjectId tmg_id;
};

struct CellInfo : ArchCellInfo
{
    IdString name, type;
    int32_t udata;

    std::unordered_map<IdString, PortInfo> ports;
    std::unordered_map<IdString, std::string> attrs, params;

    BelId bel;
    PlaceStrength belStrength = STRENGTH_NONE;

    // cell_port -> bel_pin
    std::unordered_map<IdString, IdString> pins;

    // placement constraints
    CellInfo *constr_parent = nullptr;
    std::vector<CellInfo *> constr_children;
    const int UNCONSTR = INT_MIN;
    int constr_x = UNCONSTR;   // this.x - parent.x
    int constr_y = UNCONSTR;   // this.y - parent.y
    int constr_z = UNCONSTR;   // this.z - parent.z
    bool constr_abs_z = false; // parent.z := 0
    // parent.[xyz] := 0 when (constr_parent == nullptr)

    Region *region = nullptr;
    TimingConstrObjectId tmg_id;
};

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

enum ClockEdge
{
    RISING_EDGE,
    FALLING_EDGE
};

struct TimingClockingInfo
{
    IdString clock_port; // Port name of clock domain
    ClockEdge edge;
    DelayInfo setup, hold; // Input timing checks
    DelayInfo clockToQ;    // Output clock-to-Q time
};

struct ClockConstraint
{
    DelayInfo high;
    DelayInfo low;
    DelayInfo period;

    TimingConstrObjectId domain_tmg_id;
};

struct TimingConstraintObject
{
    TimingConstrObjectId id;
    enum
    {
        ANYTHING,
        CLOCK_DOMAIN,
        NET,
        CELL,
        CELL_PORT
    } type;
    IdString entity; // Name of clock net; net or cell
    IdString port;   // Name of port on a cell
};

struct TimingConstraint
{
    IdString name;

    enum
    {
        FALSE_PATH,
        MIN_DELAY,
        MAX_DELAY,
        MULTICYCLE,
    } type;

    delay_t value;

    std::unordered_set<TimingConstrObjectId> from;
    std::unordered_set<TimingConstrObjectId> to;
};

inline bool operator==(const std::pair<const TimingConstrObjectId, TimingConstraint *> &a,
                       const std::pair<TimingConstrObjectId, TimingConstraint *> &b)
{
    return a.first == b.first && a.second == b.second;
}

struct DeterministicRNG
{
    uint64_t rngstate;

    DeterministicRNG() : rngstate(0x3141592653589793) {}

    uint64_t rng64()
    {
        // xorshift64star
        // https://arxiv.org/abs/1402.6246

        uint64_t retval = rngstate * 0x2545F4914F6CDD1D;

        rngstate ^= rngstate >> 12;
        rngstate ^= rngstate << 25;
        rngstate ^= rngstate >> 27;

        return retval;
    }

    int rng() { return rng64() & 0x3fffffff; }

    int rng(int n)
    {
        assert(n > 0);

        // round up to power of 2
        int m = n - 1;
        m |= (m >> 1);
        m |= (m >> 2);
        m |= (m >> 4);
        m |= (m >> 8);
        m |= (m >> 16);
        m += 1;

        while (1) {
            int x = rng64() & (m - 1);
            if (x < n)
                return x;
        }
    }

    void rngseed(uint64_t seed)
    {
        rngstate = seed ? seed : 0x3141592653589793;
        for (int i = 0; i < 5; i++)
            rng64();
    }

    template <typename T> void shuffle(std::vector<T> &a)
    {
        for (size_t i = 0; i != a.size(); i++) {
            size_t j = i + rng(a.size() - i);
            if (j > i)
                std::swap(a[i], a[j]);
        }
    }

    template <typename T> void sorted_shuffle(std::vector<T> &a)
    {
        std::sort(a.begin(), a.end());
        shuffle(a);
    }
};

struct BaseCtx
{
    // Lock to perform mutating actions on the Context.
    std::mutex mutex;
    std::thread::id mutex_owner;

    // Lock to be taken by UI when wanting to access context - the yield()
    // method will lock/unlock it when its' released the main mutex to make
    // sure the UI is not starved.
    std::mutex ui_mutex;

    // ID String database.
    mutable std::unordered_map<std::string, int> *idstring_str_to_idx;
    mutable std::vector<const std::string *> *idstring_idx_to_str;

    // Project settings and config switches
    std::unordered_map<IdString, std::string> settings;

    // Placed nets and cells.
    std::unordered_map<IdString, std::unique_ptr<NetInfo>> nets;
    std::unordered_map<IdString, std::unique_ptr<CellInfo>> cells;

    // Floorplanning regions
    std::unordered_map<IdString, std::unique_ptr<Region>> region;

    BaseCtx()
    {
        idstring_str_to_idx = new std::unordered_map<std::string, int>;
        idstring_idx_to_str = new std::vector<const std::string *>;
        IdString::initialize_add(this, "", 0);
        IdString::initialize_arch(this);

        TimingConstraintObject wildcard;
        wildcard.id.index = 0;
        wildcard.type = TimingConstraintObject::ANYTHING;
        constraintObjects.push_back(wildcard);
    }

    ~BaseCtx()
    {
        delete idstring_str_to_idx;
        delete idstring_idx_to_str;
    }

    // Must be called before performing any mutating changes on the Ctx/Arch.
    void lock(void)
    {
        mutex.lock();
        mutex_owner = std::this_thread::get_id();
    }

    void unlock(void)
    {
        NPNR_ASSERT(std::this_thread::get_id() == mutex_owner);
        mutex.unlock();
    }

    // Must be called by the UI before rendering data. This lock will be
    // prioritized when processing code calls yield().
    void lock_ui(void)
    {
        ui_mutex.lock();
        mutex.lock();
    }

    void unlock_ui(void)
    {
        mutex.unlock();
        ui_mutex.unlock();
    }

    // Yield to UI by unlocking the main mutex, flashing the UI mutex and
    // relocking the main mutex. Call this when you're performing a
    // long-standing action while holding a lock to let the UI show
    // visualization updates.
    // Must be called with the main lock taken.
    void yield(void)
    {
        unlock();
        ui_mutex.lock();
        ui_mutex.unlock();
        lock();
    }

    IdString id(const std::string &s) const { return IdString(this, s); }

    IdString id(const char *s) const { return IdString(this, s); }

    Context *getCtx() { return reinterpret_cast<Context *>(this); }

    const Context *getCtx() const { return reinterpret_cast<const Context *>(this); }

    const char *nameOf(IdString name) const { return name.c_str(this); }

    template <typename T> const char *nameOf(const T *obj) const
    {
        if (obj == nullptr)
            return "";
        return obj->name.c_str(this);
    }

    const char *nameOfBel(BelId bel) const;
    const char *nameOfWire(WireId wire) const;
    const char *nameOfPip(PipId pip) const;
    const char *nameOfGroup(GroupId group) const;

    // --------------------------------------------------------------

    bool allUiReload = true;
    bool frameUiReload = false;
    std::unordered_set<BelId> belUiReload;
    std::unordered_set<WireId> wireUiReload;
    std::unordered_set<PipId> pipUiReload;
    std::unordered_set<GroupId> groupUiReload;

    void refreshUi() { allUiReload = true; }

    void refreshUiFrame() { frameUiReload = true; }

    void refreshUiBel(BelId bel) { belUiReload.insert(bel); }

    void refreshUiWire(WireId wire) { wireUiReload.insert(wire); }

    void refreshUiPip(PipId pip) { pipUiReload.insert(pip); }

    void refreshUiGroup(GroupId group) { groupUiReload.insert(group); }

    // --------------------------------------------------------------

    // Timing Constraint API

    // constraint name -> constraint
    std::unordered_map<IdString, std::unique_ptr<TimingConstraint>> constraints;
    // object ID -> object
    std::vector<TimingConstraintObject> constraintObjects;
    // object ID -> constraint
    std::unordered_multimap<TimingConstrObjectId, TimingConstraint *> constrsFrom;
    std::unordered_multimap<TimingConstrObjectId, TimingConstraint *> constrsTo;

    TimingConstrObjectId timingWildcardObject();
    TimingConstrObjectId timingClockDomainObject(NetInfo *clockDomain);
    TimingConstrObjectId timingNetObject(NetInfo *net);
    TimingConstrObjectId timingCellObject(CellInfo *cell);
    TimingConstrObjectId timingPortObject(CellInfo *cell, IdString port);

    void addConstraint(std::unique_ptr<TimingConstraint> constr);
    void removeConstraint(IdString constrName);

    // Intended to simplify Python API
    void addClock(IdString net, float freq);
};

NEXTPNR_NAMESPACE_END

#include "arch.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context : Arch, DeterministicRNG
{
    bool verbose = false;
    bool debug = false;
    bool force = false;
    bool timing_driven = true;
    float target_freq = 12e6;
    bool auto_freq = false;
    int slack_redist_iter = 0;

    Context(ArchArgs args) : Arch(args) {}

    // --------------------------------------------------------------

    WireId getNetinfoSourceWire(const NetInfo *net_info) const;
    WireId getNetinfoSinkWire(const NetInfo *net_info, const PortRef &sink) const;
    delay_t getNetinfoRouteDelay(const NetInfo *net_info, const PortRef &sink) const;

    // provided by router1.cc
    bool checkRoutedDesign() const;
    bool getActualRouteDelay(WireId src_wire, WireId dst_wire, delay_t *delay = nullptr,
                             std::unordered_map<WireId, PipId> *route = nullptr, bool useEstimate = true);

    // --------------------------------------------------------------

    uint32_t checksum() const;

    void check() const;
    void archcheck() const;
};

NEXTPNR_NAMESPACE_END

#endif

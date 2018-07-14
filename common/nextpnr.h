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

#include <algorithm>
#include <assert.h>
#include <memory>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <boost/thread/shared_mutex.hpp>

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

inline void except_assert_impl(bool expr, const char *message, const char *expr_str, const char *filename, int line)
{
    if (!expr)
        throw assertion_failure(message, expr_str, filename, line);
}

NPNR_NORETURN
inline void assert_false_impl(std::string message, std::string filename, int line)
{
    throw assertion_failure(message, "false", filename, line);
}

#define NPNR_ASSERT(cond) except_assert_impl((cond), #cond, #cond, __FILE__, __LINE__)
#define NPNR_ASSERT_MSG(cond, msg) except_assert_impl((cond), msg, #cond, __FILE__, __LINE__)
#define NPNR_ASSERT_FALSE(msg) assert_false_impl(msg, __FILE__, __LINE__)

struct BaseCtx;
struct Context;

struct IdString
{
    int index = 0;

    static void initialize_arch(const BaseCtx *ctx);

    static void initialize_add(const BaseCtx *ctx, const char *s, int idx);

    IdString() {}

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
        G_NONE,
        G_LINE,
        G_BOX,
        G_CIRCLE,
        G_LABEL
    } type = G_NONE;

    enum style_t
    {
        G_FRAME,
        G_HIDDEN,
        G_INACTIVE,
        G_ACTIVE,
    } style = G_FRAME;

    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, z = 0;
    std::string text;
};

NEXTPNR_NAMESPACE_END

#include "archdefs.h"

NEXTPNR_NAMESPACE_BEGIN

struct DecalXY
{
    DecalId decal;
    float x = 0, y = 0;
};

struct BelPin
{
    BelId bel;
    PortPin pin;
};

struct CellInfo;

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

struct NetInfo
{
    IdString name;
    PortRef driver;
    std::vector<PortRef> users;
    std::unordered_map<IdString, std::string> attrs;

    // wire -> uphill_pip
    std::unordered_map<WireId, PipMap> wires;
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
};

struct CellInfo
{
    IdString name, type;
    std::unordered_map<IdString, PortInfo> ports;
    std::unordered_map<IdString, std::string> attrs, params;

    BelId bel;
    PlaceStrength belStrength = STRENGTH_NONE;

    // cell_port -> bel_pin
    std::unordered_map<IdString, IdString> pins;
};

struct UIUpdatesRequired
{
    bool allUIReload;
    bool frameUIReload;
    std::unordered_set<BelId> belUIReload;
    std::unordered_set<WireId> wireUIReload;
    std::unordered_set<PipId> pipUIReload;
    std::unordered_set<GroupId> groupUIReload;
};

class ReadContext;
class MutateContext;
class BaseReadCtx;
class BaseMutateCtx;

// Data that every architecture object should contain.
class BaseCtx
{
    friend class ReadContext;
    friend class MutateContext;
    friend class BaseReadCtx;
    friend class BaseMutateCtx;
private:
    mutable boost::shared_mutex mtx_;

    bool allUiReload = false;
    bool frameUiReload = false;
    std::unordered_set<BelId> belUiReload;
    std::unordered_set<WireId> wireUiReload;
    std::unordered_set<PipId> pipUiReload;
    std::unordered_set<GroupId> groupUiReload;

public:
    IdString id(const std::string &s) const { return IdString(this, s); }
    IdString id(const char *s) const { return IdString(this, s); }

    // TODO(q3k): These need to be made private.
    std::unordered_map<IdString, std::unique_ptr<NetInfo>> nets;
    std::unordered_map<IdString, std::unique_ptr<CellInfo>> cells;
    mutable std::unordered_map<std::string, int> *idstring_str_to_idx;
    mutable std::vector<const std::string *> *idstring_idx_to_str;

    BaseCtx()
    {
        idstring_str_to_idx = new std::unordered_map<std::string, int>;
        idstring_idx_to_str = new std::vector<const std::string *>;
        IdString::initialize_add(this, "", 0);
        IdString::initialize_arch(this);

        allUiReload = true;
    }

    ~BaseCtx()
    {
        delete idstring_str_to_idx;
        delete idstring_idx_to_str;
    }

    Context *getCtx() { return reinterpret_cast<Context *>(this); }

    const Context *getCtx() const { return reinterpret_cast<const Context *>(this); }

    // --------------------------------------------------------------

    // Get a readwrite proxy to arch - this will keep a readwrite lock on the
    // entire architecture until the proxy object goes out of scope.
    MutateContext rwproxy(void);
    // Get a read-only proxy to arch - this will keep a  read lock on the
    // entire architecture until the proxy object goes out of scope. Other read
    // locks can be taken while this one still exists. Ie., the UI can draw
    // elements while the PnR is going a RO operation.
    ReadContext rproxy(void) const;

};

// State-accessing read-only methods that every architecture object should
// contain.
class BaseReadCtx
{
protected:
    const BaseCtx *base_;
public:
    BaseReadCtx(const BaseCtx *base) : base_(base) {}
};

// State-accesssing read/write methods that every architecture object should
// contain.
class BaseMutateCtx
{
protected:
    BaseCtx *base_;

public:
    BaseMutateCtx(BaseCtx *base) : base_(base) {}

    void refreshUi(void)
    {
        base_->allUiReload = true;
    }

    void refreshUiFrame(void)
    {
        base_->frameUiReload = true;
    }

    void refreshUiBel(BelId bel)
    {
        base_->belUiReload.insert(bel);
    }

    void refreshUiWire(WireId wire)
    {
        base_->wireUiReload.insert(wire);
    }

    void refreshUiPip(PipId pip)
    {
        base_->pipUiReload.insert(pip);
    }

    void refreshUiGroup(GroupId group)
    {
        base_->groupUiReload.insert(group);
    }

    UIUpdatesRequired getUIUpdatesRequired(void)
    {
        UIUpdatesRequired req;
        req.allUIReload = base_->allUiReload;
        req.frameUIReload = base_->frameUiReload;
        req.belUIReload = base_->belUiReload;
        req.wireUIReload = base_->wireUiReload;
        req.pipUIReload = base_->pipUiReload;
        req.groupUIReload = base_->groupUiReload;

        base_->allUiReload = false;
        base_->frameUiReload = false;
        base_->belUiReload.clear();
        base_->wireUiReload.clear();
        base_->pipUiReload.clear();
        base_->groupUiReload.clear();
        return req;
    }
};

NEXTPNR_NAMESPACE_END

#include "arch.h"

NEXTPNR_NAMESPACE_BEGIN

// Read proxy to access ReadMethods while holding lock on underlying BaseCtx.
class ReadContext : public ArchReadMethods
{
    friend class BaseCtx;
private:
    boost::shared_mutex *lock_;
    ReadContext(const Arch *parent) : ArchReadMethods(parent), lock_(&parent->mtx_)
    {
        lock_->lock_shared();
    }
public:
    ~ReadContext()
    {
        if (lock_ != nullptr) {
            lock_->unlock_shared();
        }
    }
    ReadContext(ReadContext &&other): ArchReadMethods(other), lock_(other.lock_)
    {
        other.lock_ = nullptr;
    }
};

// Read proxy to access MutateMethods while holding lock on underlying BaseCtx.
class MutateContext : public ArchReadMethods, public ArchMutateMethods
{
    friend class BaseCtx;
private:
    boost::shared_mutex *lock_;
    MutateContext(Arch *parent) : ArchReadMethods(parent), ArchMutateMethods(parent), lock_(&parent->mtx_)
    {
        lock_->lock();
    }
public:
    ~MutateContext()
    {
        if (lock_ != nullptr) {
            lock_->unlock();
        }
    }
    MutateContext(MutateContext &&other): ArchReadMethods(other), ArchMutateMethods(other), lock_(other.lock_)
    {
        other.lock_ = nullptr;
    }
};


struct Context : Arch
{
    bool verbose = false;
    bool debug = false;
    bool force = false;
    bool timing_driven = true;
    float target_freq = 12e6;

    Context(ArchArgs args) : Arch(args) {}

    // --------------------------------------------------------------

    // provided by router1.cc
    bool getActualRouteDelay(WireId src_wire, WireId dst_wire, delay_t &delay);

    // --------------------------------------------------------------

    uint64_t rngstate = 0x3141592653589793;

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

    uint32_t checksum() const;

    void check() const;
};

NEXTPNR_NAMESPACE_END

#endif

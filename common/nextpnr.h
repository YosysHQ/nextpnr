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
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

NEXTPNR_NAMESPACE_BEGIN

struct BaseCtx;
struct Context;

struct IdString
{
    int index = 0;

    static BaseCtx *global_ctx;

    static void initialize_arch(const BaseCtx *ctx);
    static void initialize_add(const BaseCtx *ctx, const char *s, int idx);

    IdString() {}

    void set(const BaseCtx *ctx, const std::string &s);

    IdString(const BaseCtx *ctx, const std::string &s) { set(ctx, s); }

    IdString(const BaseCtx *ctx, const char *s) { set(ctx, s); }

    const std::string &str(const BaseCtx *ctx) const;
    const char *c_str(const BaseCtx *ctx) const;

    bool operator<(const IdString &other) const { return index < other.index; }

    bool operator==(const IdString &other) const
    {
        return index == other.index;
    }

    bool operator!=(const IdString &other) const
    {
        return index != other.index;
    }

    bool empty() const { return index == 0; }

    // --- deprecated old API ---

    IdString(const std::string &s) __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        set(global_ctx, s);
    }

    IdString(const char *s) __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        set(global_ctx, s);
    }

    const std::string &global_str() const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return str(global_ctx);
    }

    const std::string &str() const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return str(global_ctx);
    }

    const char *c_str() const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return c_str(global_ctx);
    }

    operator const char *() const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return c_str(global_ctx);
    }

    operator const std::string &() const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return str(global_ctx);
    }

    bool operator==(const std::string &s) const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return str(global_ctx) == s;
    }

    bool operator==(const char *s) const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return str(global_ctx) == s;
    }

    bool operator!=(const std::string &s) const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return str(global_ctx) != s;
    }

    bool operator!=(const char *s) const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return str(global_ctx) != s;
    }

    size_t size() const __attribute__((deprecated))
    {
        assert(global_ctx != nullptr);
        return str(global_ctx).size();
    }
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX IdString>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX IdString &obj) const
            noexcept
    {
        return std::hash<int>()(obj.index);
    }
};
} // namespace std

NEXTPNR_NAMESPACE_BEGIN

struct GraphicElement
{
    enum
    {
        G_NONE,
        G_LINE,
        G_BOX,
        G_CIRCLE,
        G_LABEL
    } type = G_NONE;

    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, z = 0;
    std::string text;
};

NEXTPNR_NAMESPACE_END

#define NEXTPNR_ARCH_TOP
#include "arch.h"
#undef NEXTPNR_ARCH_TOP

NEXTPNR_NAMESPACE_BEGIN

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

struct NetInfo
{
    IdString name;
    PortRef driver;
    std::vector<PortRef> users;
    std::unordered_map<IdString, std::string> attrs;

    // wire -> uphill_pip
    std::unordered_map<WireId, PipId> wires;

    std::unordered_map<PipId, PlaceStrength> pips;
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

struct BaseCtx
{
    // --------------------------------------------------------------

    mutable std::unordered_map<std::string, int> *idstring_str_to_idx;
    mutable std::vector<const std::string *> *idstring_idx_to_str;

    IdString id(const std::string &s) const { return IdString(this, s); }
    IdString id(const char *s) const { return IdString(this, s); }

    // --------------------------------------------------------------

    std::unordered_map<IdString, NetInfo *> nets;
    std::unordered_map<IdString, CellInfo *> cells;

    BaseCtx()
    {
        assert(IdString::global_ctx == nullptr);
        IdString::global_ctx = this;

        idstring_str_to_idx = new std::unordered_map<std::string, int>;
        idstring_idx_to_str = new std::vector<const std::string *>;
        IdString::initialize_add(this, "", 0);
        IdString::initialize_arch(this);
    }
};

NEXTPNR_NAMESPACE_END

#define NEXTPNR_ARCH_BOTTOM
#include "arch.h"
#undef NEXTPNR_ARCH_BOTTOM

NEXTPNR_NAMESPACE_BEGIN

struct Context : Arch
{
    bool verbose = false;
    bool debug = false;
    bool force = false;

    Context(ArchArgs args) : Arch(args) {}

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
};

NEXTPNR_NAMESPACE_END

#endif

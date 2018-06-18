/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

struct Context;

struct IdString
{
    int index = 0;

    static Context *global_ctx;

    static void initialize_arch(Context *ctx);
    static void initialize_add(Context *ctx, const char *s, int idx);

    IdString() {}

    void set(Context *ctx, const std::string &s);

    IdString(Context *ctx, const std::string &s)
    {
        assert(global_ctx != nullptr);
        set(global_ctx, s);
    }

    IdString(Context *ctx, const char *s)
    {
        assert(global_ctx != nullptr);
        set(global_ctx, s);
    }

    const std::string &str(Context *ctx) const;
    const char *c_str(Context *ctx) const;

    bool operator<(const IdString &other) const
    {
        return index < other.index;
    }

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

    IdString(const std::string &s)
    {
        assert(global_ctx != nullptr);
        set(global_ctx, s);
    }

    IdString(const char *s)
    {
        assert(global_ctx != nullptr);
        set(global_ctx, s);
    }

    const std::string &global_str() const
    {
        assert(global_ctx != nullptr);
        return str(global_ctx);
    }

    const std::string &str() const
    {
        assert(global_ctx != nullptr);
        return str(global_ctx);
    }

    const char *c_str() const
    {
        assert(global_ctx != nullptr);
        return c_str(global_ctx);
    }

    operator const char *() const { return c_str(); }
    operator const std::string &() const { return str(); }

    bool operator==(const std::string &s) const { return str() == s; }
    bool operator==(const char *s) const { return str() == s; }

    bool operator!=(const std::string &s) const { return str() != s; }
    bool operator!=(const char *s) const { return str() != s; }

    size_t size() const { return str().size(); }
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX IdString>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX IdString &obj) const
            noexcept
    {
        return obj.index;
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

#include "arch.h"

NEXTPNR_NAMESPACE_BEGIN

struct CellInfo;

struct PortRef
{
    CellInfo *cell = nullptr;
    IdString port;
};

struct NetInfo
{
    IdString name;
    PortRef driver;
    std::vector<PortRef> users;
    std::unordered_map<IdString, std::string> attrs;

    // wire -> uphill_pip
    std::unordered_map<WireId, PipId> wires;
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
    // cell_port -> bel_pin
    std::unordered_map<IdString, IdString> pins;
};

struct Context : Arch
{
    // --------------------------------------------------------------

    std::unordered_map<std::string, int> *idstring_str_to_idx;
    std::vector<const std::string *> *idstring_idx_to_str;

    IdString id(const std::string &s) { return IdString(this, s); }
    IdString id(const char *s) { return IdString(this, s); }

    // --------------------------------------------------------------

    std::unordered_map<IdString, NetInfo *> nets;
    std::unordered_map<IdString, CellInfo *> cells;

    Context(ArchArgs args) : Arch(args)
    {
        assert(IdString::global_ctx == nullptr);
        IdString::global_ctx = this;

        idstring_str_to_idx = new std::unordered_map<std::string, int>;
        idstring_idx_to_str = new std::vector<const std::string *>;
        IdString::initialize_add(this, "", 0);
        IdString::initialize_arch(this);

        // ...
    }
};

NEXTPNR_NAMESPACE_END

#endif

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

struct IdString
{
    int index = 0;

    static std::unordered_map<std::string, int> *database_str_to_idx;
    static std::vector<const std::string *> *database_idx_to_str;

    static void initialize();
    static void initialize_chip();
    static void initialize_add(const char *s, int idx);

    IdString() {}

    IdString(const std::string &s)
    {
        if (database_str_to_idx == nullptr)
            initialize();

        auto it = database_str_to_idx->find(s);
        if (it == database_str_to_idx->end()) {
            index = database_idx_to_str->size();
            auto insert_rc = database_str_to_idx->insert({s, index});
            database_idx_to_str->push_back(&insert_rc.first->first);
        } else {
            index = it->second;
        }
    }

    IdString(const char *s)
    {
        if (database_str_to_idx == nullptr)
            initialize();

        auto it = database_str_to_idx->find(s);
        if (it == database_str_to_idx->end()) {
            index = database_idx_to_str->size();
            auto insert_rc = database_str_to_idx->insert({s, index});
            database_idx_to_str->push_back(&insert_rc.first->first);
        } else {
            index = it->second;
        }
    }

    const std::string &str() const { return *database_idx_to_str->at(index); }
    const char *c_str() const { return str().c_str(); }

    operator const char *() const { return c_str(); }
    operator const std::string &() const { return str(); }

    bool operator<(const IdString &other) const { return index < other.index; }
    bool operator==(const IdString &other) const
    {
        return index == other.index;
    }
    bool operator==(const std::string &s) const { return str() == s; }
    bool operator==(const char *s) const { return str() == s; }

    bool operator!=(const IdString &other) const
    {
        return index != other.index;
    }
    bool operator!=(const std::string &s) const { return str() != s; }
    bool operator!=(const char *s) const { return str() != s; }

    size_t size() const { return str().size(); }
    bool empty() const { return index == 0; }
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
    std::unordered_map<IdString, NetInfo *> nets;
    std::unordered_map<IdString, CellInfo *> cells;

    Context(ArchArgs args) : Arch(args)
    {
        // ...
    }
};

NEXTPNR_NAMESPACE_END

#endif

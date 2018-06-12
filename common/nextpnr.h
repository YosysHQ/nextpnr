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
    std::string data;

    IdString() {}
    IdString(std::string s) : data(s) {}
    IdString(const char *s) : data(s) {}

    const char *c_str() const { return data.c_str(); }
    const std::string &str() const { return data; }

    operator const char *() const { return c_str(); }
    operator const std::string &() const { return str(); }

    bool operator<(const IdString &other) const { return data < other.data; }
    bool operator==(const IdString &other) const { return data == other.data; }
    bool operator==(const std::string &s) const { return data == s; }
    bool operator==(const char *s) const { return data == s; }

    size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX IdString>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX IdString &obj) const
            noexcept
    {
        return std::hash<std::string>()(obj.data);
    }
};
}

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

#include "chip.h"
#include "design.h"

#endif

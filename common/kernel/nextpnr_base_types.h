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

// Theses are the nextpnr types that do **not** depend on user defined types,
// like BelId, etc.
//
// If a common type is required that depends on one of the user defined types,
// add it to nextpnr_types.h, which includes "archdefs.h", or make a new
// header that includes "archdefs.h"
#ifndef NEXTPNR_BASE_TYPES_H
#define NEXTPNR_BASE_TYPES_H

#include <boost/functional/hash.hpp>
#include <string>

#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"

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
        TYPE_LOCAL_ARROW, // Located entirely within the cell boundaries, coordinates in the range [0., 1.]
        TYPE_LOCAL_LINE,

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
    GraphicElement(){};
    GraphicElement(type_t type, style_t style, float x1, float y1, float x2, float y2, float z)
            : type(type), style(style), x1(x1), y1(y1), x2(x2), y2(y2), z(z){};
};

struct Loc
{
    int x = -1, y = -1, z = -1;

    Loc() {}
    Loc(int x, int y, int z) : x(x), y(y), z(z) {}

    bool operator==(const Loc &other) const { return (x == other.x) && (y == other.y) && (z == other.z); }
    bool operator!=(const Loc &other) const { return (x != other.x) || (y != other.y) || (z != other.z); }
    unsigned int hash() const { return mkhash(x, mkhash(y, z)); }
};

struct BoundingBox
{
    int x0 = -1, y0 = -1, x1 = -1, y1 = -1;

    BoundingBox() {}
    BoundingBox(int x0, int y0, int x1, int y1) : x0(x0), y0(y0), x1(x1), y1(y1){};

    int distance(Loc loc) const
    {
        int dist = 0;
        if (loc.x < x0)
            dist += x0 - loc.x;
        if (loc.x > x1)
            dist += loc.x - x1;
        if (loc.y < y0)
            dist += y0 - loc.y;
        if (loc.y > y1)
            dist += loc.y - y1;
        return dist;
    };

    bool contains(int x, int y) const { return x >= x0 && y >= y0 && x <= x1 && y <= y1; }
};

enum PlaceStrength
{
    STRENGTH_NONE = 0,
    STRENGTH_WEAK = 1,
    STRENGTH_STRONG = 2,
    STRENGTH_PLACER = 3,
    STRENGTH_FIXED = 4,
    STRENGTH_LOCKED = 5,
    STRENGTH_USER = 6
};

NEXTPNR_NAMESPACE_END

#endif /* NEXTPNR_BASE_TYPES_H */

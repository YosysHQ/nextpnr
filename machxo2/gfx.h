/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  Miodrag Milanovic <micko@yosyshq.com>
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

#ifndef MACHXO2_GFX_H
#define MACHXO2_GFX_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

enum GfxTileWireId
{
    TILE_WIRE_NONE,
};

void gfxTileBel(std::vector<GraphicElement> &g, int x, int y, int z, int w, int h, IdString bel_type,
                GraphicElement::style_t style);
void gfxTileWire(std::vector<GraphicElement> &g, int x, int y, int w, int h, IdString wire_type, GfxTileWireId tilewire,
                 GraphicElement::style_t style);
void gfxTilePip(std::vector<GraphicElement> &g, int x, int y, int w, int h, WireId src, IdString src_type,
                GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id,
                GraphicElement::style_t style);

NEXTPNR_NAMESPACE_END

#endif

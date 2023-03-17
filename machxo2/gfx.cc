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

#include "gfx.h"

NEXTPNR_NAMESPACE_BEGIN

const float slice_x1 = 0.800;
const float slice_x2_comb = 0.927;
// const float slice_x1_ff = 0.933;
const float slice_x2 = 0.94;
const float slice_y1 = 0.60;
const float slice_y2 = 0.65 + 0.1 / 4;
const float slice_pitch = 0.1;

const float io_cell_v_x1 = 0.76;
const float io_cell_v_x2 = 0.95;
const float io_cell_v_y1 = 0.05;
const float io_cell_gap = 0.10;
const float io_cell_h_x1 = 0.05;
const float io_cell_h_y1 = 0.05;
const float io_cell_h_y2 = 0.24;

void gfxTileBel(std::vector<GraphicElement> &g, int x, int y, int z, int w, int h, IdString bel_type,
                GraphicElement::style_t style)
{
    GraphicElement el;
    el.type = GraphicElement::TYPE_BOX;
    el.style = style;
    if (bel_type == id_TRELLIS_SLICE) {
        el.x1 = x + slice_x1;
        el.x2 = x + slice_x2_comb;
        el.y1 = y + slice_y1 + z * slice_pitch;
        el.y2 = y + slice_y2 + z * slice_pitch;
        g.push_back(el);
        /*    } else if (bel_type == id_TRELLIS_FF) {
                el.x1 = x + slice_x1_ff;
                el.x2 = x + slice_x2;
                el.y1 = y + slice_y1 + z * slice_pitch;
                el.y2 = y + slice_y2 + z * slice_pitch;
                g.push_back(el);*/
    } else if (bel_type.in(id_TRELLIS_IO)) {
        bool top_bottom = (y == 0 || y == (h - 1));
        if (top_bottom) {
            el.x1 = x + io_cell_h_x1 + (z + 2) * io_cell_gap;
            el.x2 = x + io_cell_h_x1 + (z + 2) * io_cell_gap + 0.08f;
            if (y == h - 1) {
                el.y1 = y + 1 - io_cell_h_y1;
                el.y2 = y + 1 - io_cell_h_y2;
            } else {
                el.y1 = y + io_cell_h_y1;
                el.y2 = y + io_cell_h_y2;
            }
        } else {
            if (x == 0) {
                el.x1 = x + 1 - io_cell_v_x1;
                el.x2 = x + 1 - io_cell_v_x2;
            } else {
                el.x1 = x + io_cell_v_x1;
                el.x2 = x + io_cell_v_x2;
            }
            el.y1 = y + io_cell_v_y1 + z * io_cell_gap;
            el.y2 = y + io_cell_v_y1 + z * io_cell_gap + 0.08f;
        }
        g.push_back(el);
    }
}

void gfxTileWire(std::vector<GraphicElement> &g, int x, int y, int w, int h, IdString wire_type, GfxTileWireId tilewire,
                 GraphicElement::style_t style)
{
}

void gfxTilePip(std::vector<GraphicElement> &g, int x, int y, int w, int h, WireId src, IdString src_type,
                GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id,
                GraphicElement::style_t style)
{
}

NEXTPNR_NAMESPACE_END

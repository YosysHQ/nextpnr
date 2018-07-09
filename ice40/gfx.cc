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

#include "gfx.h"

NEXTPNR_NAMESPACE_BEGIN

void gfxTileWire(std::vector<GraphicElement> &g, int x, int y, GfxTileWireId id)
{
    // Horizontal Span-4 Wires

    if (id >= TILE_WIRE_SP4_H_L_36 && id <= TILE_WIRE_SP4_H_L_47) {
        int idx = (id - TILE_WIRE_SP4_H_L_36) + 48;
        GraphicElement el;
        el.type = GraphicElement::G_LINE;

        float y1 = y + 0.03 + 0.0025 * (60 - idx);

        el.x1 = x + 0.0;
        el.x2 = x + 0.9;
        el.y1 = y1;
        el.y2 = y1;
        g.push_back(el);
    }

    if (id >= TILE_WIRE_SP4_H_R_0 && id <= TILE_WIRE_SP4_H_R_47) {
        int idx = id - TILE_WIRE_SP4_H_R_0;
        GraphicElement el;
        el.type = GraphicElement::G_LINE;

        float y1 = y + 0.03 + 0.0025 * (60 - idx);
        float y2 = y + 0.03 + 0.0025 * (60 - (idx ^ 1));
        float y3 = y + 0.03 + 0.0025 * (60 - (idx ^ 1) - 12);

        if (idx >= 12) {
            el.x1 = x;
            el.x2 = x + 0.01;
            el.y1 = y1;
            el.y2 = y1;
            g.push_back(el);

            el.x1 = x + 0.01;
            el.x2 = x + 0.02;
            el.y1 = y1;
            el.y2 = y2;
            g.push_back(el);
        }

        el.x1 = x + 0.02;
        el.x2 = x + 0.9;
        el.y1 = y2;
        el.y2 = y2;
        g.push_back(el);

        el.x1 = x + 0.9;
        el.x2 = x + 1.0;
        el.y1 = y2;
        el.y2 = y3;
        g.push_back(el);
    }

    // Vertical Span-4 Wires

    if (id >= TILE_WIRE_SP4_V_T_36 && id <= TILE_WIRE_SP4_V_T_47) {
        int idx = (id - TILE_WIRE_SP4_V_T_36) + 48;
        GraphicElement el;
        el.type = GraphicElement::G_LINE;

        float x1 = x + 0.03 + 0.0025 * (60 - idx);

        el.y1 = y + 0.0;
        el.y2 = y + 0.9;
        el.x1 = x1;
        el.x2 = x1;
        g.push_back(el);
    }

    if (id >= TILE_WIRE_SP4_V_B_0 && id <= TILE_WIRE_SP4_V_B_47) {
        int idx = id - TILE_WIRE_SP4_V_B_0;
        GraphicElement el;
        el.type = GraphicElement::G_LINE;

        float x1 = x + 0.03 + 0.0025 * (60 - idx);
        float x2 = x + 0.03 + 0.0025 * (60 - (idx ^ 1));
        float x3 = x + 0.03 + 0.0025 * (60 - (idx ^ 1) - 12);

        if (idx >= 12) {
            el.y1 = y;
            el.y2 = y + 0.01;
            el.x1 = x1;
            el.x2 = x1;
            g.push_back(el);

            el.y1 = y + 0.01;
            el.y2 = y + 0.02;
            el.x1 = x1;
            el.x2 = x2;
            g.push_back(el);
        }

        el.y1 = y + 0.02;
        el.y2 = y + 0.9;
        el.x1 = x2;
        el.x2 = x2;
        g.push_back(el);

        el.y1 = y + 0.9;
        el.y2 = y + 1.0;
        el.x1 = x2;
        el.x2 = x3;
        g.push_back(el);
    }

    // Horizontal Span-12 Wires

    if (id >= TILE_WIRE_SP12_H_L_22 && id <= TILE_WIRE_SP12_H_L_23) {
        int idx = (id - TILE_WIRE_SP12_H_L_22) + 24;
        GraphicElement el;
        el.type = GraphicElement::G_LINE;

        float y1 = y + 0.03 + 0.0025 * (90 - idx);

        el.x1 = x + 0.0;
        el.x2 = x + 0.98333;
        el.y1 = y1;
        el.y2 = y1;
        g.push_back(el);
    }

    if (id >= TILE_WIRE_SP12_H_R_0 && id <= TILE_WIRE_SP12_H_R_23) {
        int idx = id - TILE_WIRE_SP12_H_R_0;
        GraphicElement el;
        el.type = GraphicElement::G_LINE;

        float y1 = y + 0.03 + 0.0025 * (90 - idx);
        float y2 = y + 0.03 + 0.0025 * (90 - (idx ^ 1));
        float y3 = y + 0.03 + 0.0025 * (90 - (idx ^ 1) - 2);

        if (idx >= 2) {
            el.x1 = x;
            el.x2 = x + 0.01;
            el.y1 = y1;
            el.y2 = y1;
            g.push_back(el);

            el.x1 = x + 0.01;
            el.x2 = x + 0.02;
            el.y1 = y1;
            el.y2 = y2;
            g.push_back(el);
        }

        el.x1 = x + 0.02;
        el.x2 = x + 0.98333;
        el.y1 = y2;
        el.y2 = y2;
        g.push_back(el);

        el.x1 = x + 0.98333;
        el.x2 = x + 1.0;
        el.y1 = y2;
        el.y2 = y3;
        g.push_back(el);
    }

    // Vertical Span-12 Wires

    if (id >= TILE_WIRE_SP12_V_T_22 && id <= TILE_WIRE_SP12_V_T_23) {
        int idx = (id - TILE_WIRE_SP12_V_T_22) + 24;
        GraphicElement el;
        el.type = GraphicElement::G_LINE;

        float x1 = x + 0.03 + 0.0025 * (90 - idx);

        el.y1 = y + 0.0;
        el.y2 = y + 0.98333;
        el.x1 = x1;
        el.x2 = x1;
        g.push_back(el);
    }

    if (id >= TILE_WIRE_SP12_V_B_0 && id <= TILE_WIRE_SP12_V_B_23) {
        int idx = id - TILE_WIRE_SP12_V_B_0;
        GraphicElement el;
        el.type = GraphicElement::G_LINE;

        float x1 = x + 0.03 + 0.0025 * (90 - idx);
        float x2 = x + 0.03 + 0.0025 * (90 - (idx ^ 1));
        float x3 = x + 0.03 + 0.0025 * (90 - (idx ^ 1) - 2);

        if (idx >= 2) {
            el.y1 = y;
            el.y2 = y + 0.01;
            el.x1 = x1;
            el.x2 = x1;
            g.push_back(el);

            el.y1 = y + 0.01;
            el.y2 = y + 0.02;
            el.x1 = x1;
            el.x2 = x2;
            g.push_back(el);
        }

        el.y1 = y + 0.02;
        el.y2 = y + 0.98333;
        el.x1 = x2;
        el.x2 = x2;
        g.push_back(el);

        el.y1 = y + 0.98333;
        el.y2 = y + 1.0;
        el.x1 = x2;
        el.x2 = x3;
        g.push_back(el);
    }
}

NEXTPNR_NAMESPACE_END

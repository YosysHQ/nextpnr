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

void gfxTileWire(std::vector<GraphicElement> &g, int x, int y, GfxTileWireId id, GraphicElement::style_t style)
{
    GraphicElement el;
    el.type = GraphicElement::G_LINE;
    el.style = style;

    // Horizontal Span-4 Wires

    if (id >= TILE_WIRE_SP4_H_L_36 && id <= TILE_WIRE_SP4_H_L_47) {
        int idx = (id - TILE_WIRE_SP4_H_L_36) + 48;

        float y1 = y + 1.0 - (0.03 + 0.0025 * (60 - (idx ^ 1)));
        float y2 = y + 1.0 - (0.03 + 0.0025 * (60 - idx));

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

        el.x1 = x + 0.02;
        el.x2 = x + 0.9;
        el.y1 = y2;
        el.y2 = y2;
        g.push_back(el);

        el.x1 = x + main_swbox_x1 + 0.0025 * (idx + 35);
        el.x2 = el.x1;
        el.y1 = y2;
        el.y2 = y + main_swbox_y2;
        g.push_back(el);
    }

    if (id >= TILE_WIRE_SP4_H_R_0 && id <= TILE_WIRE_SP4_H_R_47) {
        int idx = id - TILE_WIRE_SP4_H_R_0;

        float y1 = y + 1.0 - (0.03 + 0.0025 * (60 - idx));
        float y2 = y + 1.0 - (0.03 + 0.0025 * (60 - (idx ^ 1)));
        float y3 = y + 1.0 - (0.03 + 0.0025 * (60 - (idx ^ 1) - 12));

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

        el.x1 = x + main_swbox_x1 + 0.0025 * ((idx ^ 1) + 35);
        el.x2 = el.x1;
        el.y1 = y2;
        el.y2 = y + main_swbox_y2;
        g.push_back(el);
    }

    // Vertical Span-4 Wires

    if (id >= TILE_WIRE_SP4_V_T_36 && id <= TILE_WIRE_SP4_V_T_47) {
        int idx = (id - TILE_WIRE_SP4_V_T_36) + 48;

        float x1 = x + 0.03 + 0.0025 * (60 - (idx ^ 1));
        float x2 = x + 0.03 + 0.0025 * (60 - idx);

        el.y1 = y + 1.00;
        el.y2 = y + 0.99;
        el.x1 = x1;
        el.x2 = x1;
        g.push_back(el);

        el.y1 = y + 0.99;
        el.y2 = y + 0.98;
        el.x1 = x1;
        el.x2 = x2;
        g.push_back(el);

        el.y1 = y + 0.98;
        el.y2 = y + 0.10;
        el.x1 = x2;
        el.x2 = x2;
        g.push_back(el);

        el.y1 = y + 1.0 - (0.03 + 0.0025 * (270 - idx));
        el.y2 = el.y1;
        el.x1 = x2;
        el.x2 = x + main_swbox_x1;
        g.push_back(el);
    }

    if (id >= TILE_WIRE_SP4_V_B_0 && id <= TILE_WIRE_SP4_V_B_47) {
        int idx = id - TILE_WIRE_SP4_V_B_0;

        float x1 = x + 0.03 + 0.0025 * (60 - idx);
        float x2 = x + 0.03 + 0.0025 * (60 - (idx ^ 1));
        float x3 = x + 0.03 + 0.0025 * (60 - (idx ^ 1) - 12);

        if (idx >= 12) {
            el.y1 = y + 1.00;
            el.y2 = y + 0.99;
            el.x1 = x1;
            el.x2 = x1;
            g.push_back(el);

            el.y1 = y + 0.99;
            el.y2 = y + 0.98;
            el.x1 = x1;
            el.x2 = x2;
            g.push_back(el);
        }

        el.y1 = y + 0.98;
        el.y2 = y + 0.10;
        el.x1 = x2;
        el.x2 = x2;
        g.push_back(el);

        el.y1 = y + 0.10;
        el.y2 = y;
        el.x1 = x2;
        el.x2 = x3;
        g.push_back(el);

        el.y1 = y + 1.0 - (0.03 + 0.0025 * (145 - (idx ^ 1)));
        el.y2 = el.y1;
        el.x1 = x;
        el.x2 = x2;
        g.push_back(el);

        el.y1 = y + 1.0 - (0.03 + 0.0025 * (270 - (idx ^ 1)));
        el.y2 = el.y1;
        el.x1 = x2;
        el.x2 = x + main_swbox_x1;
        g.push_back(el);
    }

    // Horizontal Span-12 Wires

    if (id >= TILE_WIRE_SP12_H_L_22 && id <= TILE_WIRE_SP12_H_L_23) {
        int idx = (id - TILE_WIRE_SP12_H_L_22) + 24;

        float y1 = y + 1.0 - (0.03 + 0.0025 * (90 - (idx ^ 1)));
        float y2 = y + 1.0 - (0.03 + 0.0025 * (90 - idx));

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

        el.x1 = x + 0.02;
        el.x2 = x + 0.98333;
        el.y1 = y2;
        el.y2 = y2;
        g.push_back(el);

        el.x1 = x + main_swbox_x1 + 0.0025 * (idx + 5);
        el.x2 = el.x1;
        el.y1 = y2;
        el.y2 = y + main_swbox_y2;
        g.push_back(el);
    }

    if (id >= TILE_WIRE_SP12_H_R_0 && id <= TILE_WIRE_SP12_H_R_23) {
        int idx = id - TILE_WIRE_SP12_H_R_0;

        float y1 = y + 1.0 - (0.03 + 0.0025 * (90 - idx));
        float y2 = y + 1.0 - (0.03 + 0.0025 * (90 - (idx ^ 1)));
        float y3 = y + 1.0 - (0.03 + 0.0025 * (90 - (idx ^ 1) - 2));

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

        el.x1 = x + main_swbox_x1 + 0.0025 * ((idx ^ 1) + 5);
        el.x2 = el.x1;
        el.y1 = y2;
        el.y2 = y + main_swbox_y2;
        g.push_back(el);
    }

    // Vertical Right Span-4

    if (id >= TILE_WIRE_SP4_R_V_B_0 && id <= TILE_WIRE_SP4_R_V_B_47) {
        int idx = id - TILE_WIRE_SP4_R_V_B_0;

        float y1 = y + 1.0 - (0.03 + 0.0025 * (145 - (idx ^ 1)));

        el.y1 = y1;
        el.y2 = y1;
        el.x1 = x + main_swbox_x2;
        el.x2 = x + 1.0;
        g.push_back(el);
    }

    // Vertical Span-12 Wires

    if (id >= TILE_WIRE_SP12_V_T_22 && id <= TILE_WIRE_SP12_V_T_23) {
        int idx = (id - TILE_WIRE_SP12_V_T_22) + 24;

        float x1 = x + 0.03 + 0.0025 * (90 - (idx ^ 1));
        float x2 = x + 0.03 + 0.0025 * (90 - idx);

        el.y1 = y + 1.00;
        el.y2 = y + 0.99;
        el.x1 = x1;
        el.x2 = x1;
        g.push_back(el);

        el.y1 = y + 0.99;
        el.y2 = y + 0.98;
        el.x1 = x1;
        el.x2 = x2;
        g.push_back(el);

        el.y1 = y + 0.98;
        el.y2 = y + 0.01667;
        el.x1 = x2;
        el.x2 = x2;
        g.push_back(el);

        el.y1 = y + 1.0 - (0.03 + 0.0025 * (300 - idx));
        el.y2 = el.y1;
        el.x1 = x2;
        el.x2 = x + main_swbox_x1;
        g.push_back(el);
    }

    if (id >= TILE_WIRE_SP12_V_B_0 && id <= TILE_WIRE_SP12_V_B_23) {
        int idx = id - TILE_WIRE_SP12_V_B_0;

        float x1 = x + 0.03 + 0.0025 * (90 - idx);
        float x2 = x + 0.03 + 0.0025 * (90 - (idx ^ 1));
        float x3 = x + 0.03 + 0.0025 * (90 - (idx ^ 1) - 2);

        if (idx >= 2) {
            el.y1 = y + 1.00;
            el.y2 = y + 0.99;
            el.x1 = x1;
            el.x2 = x1;
            g.push_back(el);

            el.y1 = y + 0.99;
            el.y2 = y + 0.98;
            el.x1 = x1;
            el.x2 = x2;
            g.push_back(el);
        }

        el.y1 = y + 0.98;
        el.y2 = y + 0.01667;
        el.x1 = x2;
        el.x2 = x2;
        g.push_back(el);

        el.y1 = y + 0.01667;
        el.y2 = y;
        el.x1 = x2;
        el.x2 = x3;
        g.push_back(el);

        el.y1 = y + 1.0 - (0.03 + 0.0025 * (300 - (idx ^ 1)));
        el.y2 = el.y1;
        el.x1 = x2;
        el.x2 = x + main_swbox_x1;
        g.push_back(el);
    }

    // Global2Local

    if (id >= TILE_WIRE_GLB2LOCAL_0 && id <= TILE_WIRE_GLB2LOCAL_3) {
        int idx = id - TILE_WIRE_GLB2LOCAL_0;
        el.x1 = x + main_swbox_x1 + 0.005 * (idx + 5);
        el.x2 = el.x1;
        el.y1 = y + main_swbox_y1;
        el.y2 = el.y1 - 0.02;
        g.push_back(el);
    }

    // GlobalNets

    if (id >= TILE_WIRE_GLB_NETWK_0 && id <= TILE_WIRE_GLB_NETWK_7) {
        int idx = id - TILE_WIRE_GLB_NETWK_0;
        el.x1 = x + main_swbox_x1 - 0.05;
        el.x2 = x + main_swbox_x1;
        el.y1 = y + main_swbox_y1 + 0.005 * (13 - idx);
        el.y2 = el.y1;
        g.push_back(el);
    }

    // Neighbours

    if (id >= TILE_WIRE_NEIGH_OP_BNL_0 && id <= TILE_WIRE_NEIGH_OP_TOP_7) {
        int idx = id - TILE_WIRE_NEIGH_OP_BNL_0;
        el.y1 = y + main_swbox_y2 - (0.0025 * (idx + 10) + 0.01 * (idx / 8));
        el.y2 = el.y1;
        el.x1 = x + main_swbox_x1 - 0.05;
        el.x2 = x + main_swbox_x1;
        g.push_back(el);
    }

    // Local Tracks

    if (id >= TILE_WIRE_LOCAL_G0_0 && id <= TILE_WIRE_LOCAL_G3_7) {
        int idx = id - TILE_WIRE_LOCAL_G0_0;
        el.x1 = x + main_swbox_x2;
        el.x2 = x + local_swbox_x1;
        float yoff = y + (local_swbox_y1 + local_swbox_y2) / 2 - 0.005 * 16 - 0.075;
        el.y1 = yoff + 0.005 * idx + 0.05 * (idx / 8);
        el.y2 = el.y1;
        g.push_back(el);
    }

    // LC Inputs

    if (id >= TILE_WIRE_LUTFF_0_IN_0 && id <= TILE_WIRE_LUTFF_7_IN_3) {
        int idx = id - TILE_WIRE_LUTFF_0_IN_0;
        int z = idx / 4;
        int input = idx % 4;
        el.x1 = x + local_swbox_x2;
        el.x2 = x + logic_cell_x1;
        el.y1 = y + (logic_cell_y1 + logic_cell_y2) / 2 - 0.0075 + (0.005 * input) + z * logic_cell_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    }

    // LC Outputs

    if (id >= TILE_WIRE_LUTFF_0_OUT && id <= TILE_WIRE_LUTFF_7_OUT) {
        int idx = id - TILE_WIRE_LUTFF_0_OUT;

        float y1 = y + 1.0 - (0.03 + 0.0025 * (152 + idx));

        el.y1 = y1;
        el.y2 = y1;
        el.x1 = x + main_swbox_x2;
        el.x2 = x + 0.97 + 0.0025 * (7 - idx);
        g.push_back(el);

        el.y1 = y1;
        el.y2 = y + (logic_cell_y1 + logic_cell_y2) / 2 + idx * logic_cell_pitch;
        el.x1 = el.x2;
        g.push_back(el);

        el.y1 = el.y2;
        el.x1 = x + logic_cell_x2;
        g.push_back(el);
    }

    // LC Control

    if (id >= TILE_WIRE_LUTFF_GLOBAL_CEN && id <= TILE_WIRE_LUTFF_GLOBAL_S_R) {
        int idx = id - TILE_WIRE_LUTFF_GLOBAL_CEN;

        el.x1 = x + main_swbox_x2 - 0.005 * (idx + 5);
        el.x2 = el.x1;
        el.y1 = y + main_swbox_y1;
        el.y2 = el.y1 - 0.005 * (idx + 2);
        g.push_back(el);

        el.y1 = el.y2;
        el.x2 = x + logic_cell_x2 - 0.005 * (2 - idx + 5);
        g.push_back(el);

        el.y2 = y + logic_cell_y1;
        el.x1 = el.x2;
        g.push_back(el);

        for (int i = 0; i < 7; i++) {
            el.y1 = y + logic_cell_y2 + i * logic_cell_pitch;
            el.y2 = y + logic_cell_y1 + (i + 1) * logic_cell_pitch;
            g.push_back(el);
        }
    }

    // LC Cascade

    if (id >= TILE_WIRE_LUTFF_0_LOUT && id <= TILE_WIRE_LUTFF_6_LOUT) {
        int idx = id - TILE_WIRE_LUTFF_0_LOUT;
        el.x1 = x + logic_cell_x1 + 0.005 * 5;
        el.x2 = el.x1;
        el.y1 = y + logic_cell_y2 + idx * logic_cell_pitch;
        el.y2 = y + logic_cell_y1 + (idx + 1) * logic_cell_pitch;
        g.push_back(el);
    }

    // Carry Chain

    if (id >= TILE_WIRE_LUTFF_0_COUT && id <= TILE_WIRE_LUTFF_7_COUT) {
        int idx = id - TILE_WIRE_LUTFF_0_COUT;
        el.x1 = x + logic_cell_x1 + 0.005 * 3;
        el.x2 = el.x1;
        el.y1 = y + logic_cell_y2 + idx * logic_cell_pitch;
        el.y2 = y + (idx < 7 ? logic_cell_y1 + (idx + 1) * logic_cell_pitch : 1.0);
        g.push_back(el);
    }

    if (id == TILE_WIRE_CARRY_IN) {
        el.x1 = x + logic_cell_x1 + 0.005 * 3;
        el.x2 = el.x1;
        el.y1 = y;
        el.y2 = y + 0.01;
        g.push_back(el);
    }

    if (id == TILE_WIRE_CARRY_IN_MUX) {
        el.x1 = x + logic_cell_x1 + 0.005 * 3;
        el.x2 = el.x1;
        el.y1 = y + 0.02;
        el.y2 = y + logic_cell_y1;
        g.push_back(el);
    }
}

NEXTPNR_NAMESPACE_END

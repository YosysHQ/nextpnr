/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2019  Miodrag Milanovic <micko@yosyshq.com>
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

const float slice_x1 = 0.92;
const float slice_x2 = 0.94;
const float slice_x2_wide = 0.97;
const float slice_y1 = 0.71;
const float slice_y2 = 0.745 + 0.0068;
const float slice_pitch = 0.0374 + 0.0068;

const float slice_comb_dx1 = 0.002;
const float slice_comb_w = 0.007;
const float slice_ff_dx1 = 0.011;
const float slice_ff_w = 0.007;
const float slice_comb_dy1 = 0.002;
const float slice_comb_h = 0.014;
const float slice_comb_dy2 = 0.021;

const float io_cell_v_x1 = 0.76;
const float io_cell_v_x2 = 0.95;
const float io_cell_v_y1 = 0.05;
const float io_cell_gap = 0.10;
const float io_cell_h_x1 = 0.05;
const float io_cell_h_y1 = 0.05;
const float io_cell_h_y2 = 0.24;

const float wire_distance = 0.0017f;
const float wire_distance_small = 0.00085f;

const float wire_length_lut = 0.01f;
const float wire_length = 0.005f;
const float wire_length_long = 0.015f;

const float dll_cell_x1 = 0.2;
const float dll_cell_x2 = 0.8;
const float dll_cell_y1 = 0.2;
const float dll_cell_y2 = 0.8;

void gfxTileBel(std::vector<GraphicElement> &g, int x, int y, int z, int w, int h, IdString bel_type,
                GraphicElement::style_t style)
{
    GraphicElement el;
    el.type = GraphicElement::TYPE_BOX;
    el.style = style;
    if (bel_type == id_TRELLIS_COMB) {
        int lc = (z >> Arch::lc_idx_shift);

        el.x1 = x + slice_x1 + slice_comb_dx1;
        el.x2 = el.x1 + slice_comb_w;
        el.y1 = y + slice_y1 + (lc / 2) * slice_pitch + ((lc % 2) ? slice_comb_dy2 : slice_comb_dy1);
        el.y2 = el.y1 + slice_comb_h;
        g.push_back(el);

        el.style = GraphicElement::STYLE_FRAME;

        if ((lc % 2) == 0) {
            // SLICE frame
            el.x1 = x + slice_x1;
            el.x2 = x + slice_x2;
            el.y1 = y + slice_y1 + (lc / 2) * slice_pitch;
            el.y2 = y + slice_y2 + (lc / 2) * slice_pitch;
            g.push_back(el);

            // SLICE control set switchbox
            el.x1 = x + slice_x2 + 15 * wire_distance;
            el.x2 = el.x1 + wire_distance;
            el.y1 = y + slice_y2 -
                    wire_distance * (TILE_WIRE_CLK3_SLICE - TILE_WIRE_DUMMY_D2 + 5 + (3 - (lc / 2)) * 26) +
                    3 * slice_pitch - 0.0007f;
            el.y2 = el.y1 + wire_distance * 5;
            g.push_back(el);
        }

        // LUT permutation switchbox
        el.x1 = x + slice_x1 - wire_length_lut;
        el.x2 = x + slice_x1 - wire_length;
        int start_wire = (TILE_WIRE_D7 + 24 * (lc / 2) + 4 * (lc % 2));
        el.y2 = y + slice_y2 - wire_distance * (start_wire - TILE_WIRE_FCO + 1 + (lc / 2) * 2) + 3 * slice_pitch +
                0.25 * wire_distance;
        el.y1 = el.y2 - 3.5 * wire_distance;
        g.push_back(el);

    } else if (bel_type == id_TRELLIS_FF) {
        int lc = (z >> Arch::lc_idx_shift);
        el.x1 = x + slice_x1 + slice_ff_dx1;
        el.x2 = el.x1 + slice_ff_w;
        el.y1 = y + slice_y1 + (lc / 2) * slice_pitch + ((lc % 2) ? slice_comb_dy2 : slice_comb_dy1);
        el.y2 = el.y1 + slice_comb_h;
        g.push_back(el);

    } else if (bel_type.in(id_TRELLIS_IO, id_IOLOGIC, id_SIOLOGIC, id_DQSBUFM)) {
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
    } else if (bel_type == id_DCCA) {
        el.x1 = x + switchbox_x1 + (z)*0.025;
        el.y1 = y + 0.14;
        el.x2 = x + switchbox_x1 + (z)*0.025 + 0.020;
        el.y2 = y + 0.18;
        g.push_back(el);
    } else if (bel_type.in(id_DP16KD, id_MULT18X18D, id_ALU54B)) {
        el.x1 = x + slice_x1;
        el.x2 = x + slice_x2_wide;
        el.y1 = y + slice_y1 - 1 * slice_pitch;
        el.y2 = y + slice_y2 + 3 * slice_pitch;
        g.push_back(el);
    } else if (bel_type == id_EHXPLLL) {
        el.x1 = x + slice_x1;
        el.x2 = x + slice_x2_wide;
        el.y1 = y + slice_y1;
        el.y2 = y + slice_y2;
        g.push_back(el);
    } else if (bel_type == id_DCUA) {
        el.x1 = x + slice_x1;
        el.x2 = x + slice_x2_wide;
        el.y1 = y + slice_y2;
        el.y2 = y + 0.25;
        g.push_back(el);
    } else if (bel_type.in(id_EXTREFB, id_PCSCLKDIV, id_DTR, id_USRMCLK, id_SEDGA, id_GSR, id_JTAGG, id_OSCG)) {
        el.x1 = x + slice_x1;
        el.x2 = x + slice_x2_wide;
        el.y1 = y + slice_y1 + (z)*slice_pitch;
        el.y2 = y + slice_y2 + (z)*slice_pitch;
        g.push_back(el);
    } else if (bel_type == id_DDRDLL) {
        el.x1 = x + dll_cell_x1;
        el.x2 = x + dll_cell_x2;
        el.y1 = y + dll_cell_y1;
        el.y2 = y + dll_cell_y2;
        g.push_back(el);
    } else if (bel_type.in(id_DLLDELD, id_CLKDIVF, id_ECLKSYNCB, id_TRELLIS_ECLKBUF, id_ECLKBRIDGECS)) {
        el.x1 = x + 0.1 + z * 0.05;
        el.x2 = x + 0.14 + z * 0.05;
        el.y1 = y + 0.475;
        el.y2 = y + 0.525;
        g.push_back(el);
    }
}

void gfxTileWire(std::vector<GraphicElement> &g, int x, int y, int w, int h, IdString wire_type, GfxTileWireId tilewire,
                 GraphicElement::style_t style)
{
    GraphicElement el;
    el.type = GraphicElement::TYPE_LINE;
    el.style = style;
    if (wire_type == id_WIRE_TYPE_SLICE && tilewire != GfxTileWireId::TILE_WIRE_NONE) {
        if (tilewire >= TILE_WIRE_FCO_SLICE && tilewire <= TILE_WIRE_FCI_SLICE) {
            int gap = (tilewire - TILE_WIRE_FCO_SLICE) / 24;
            int item = (tilewire - TILE_WIRE_FCO_SLICE) % 24;
            el.x1 = x + slice_x1 - wire_length;
            el.x2 = x + slice_x1;
            el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_FCO_SLICE + 1 + gap * 2) + 3 * slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);
            // FX to F connection - top
            if (item == (TILE_WIRE_FXD_SLICE - TILE_WIRE_FCO_SLICE)) {
                el.x2 = el.x1;
                el.y2 = el.y1 - wire_distance;
                g.push_back(el);
            }
            // F5 to F connection - bottom
            if (item == (TILE_WIRE_F5D_SLICE - TILE_WIRE_FCO_SLICE)) {
                el.x2 = el.x1;
                el.y2 = el.y1 + wire_distance;
                g.push_back(el);
            }
            // connection between slices
            if (item == (TILE_WIRE_FCID_SLICE - TILE_WIRE_FCO_SLICE) && tilewire != TILE_WIRE_FCI_SLICE) {
                el.x2 = el.x1;
                el.y2 = el.y1 - wire_distance * 3;
                g.push_back(el);
            }
        }
        if (tilewire >= TILE_WIRE_DUMMY_D2 && tilewire <= TILE_WIRE_WAD0A_SLICE) {
            int gap = (tilewire - TILE_WIRE_DUMMY_D2) / 12;
            el.x1 = x + slice_x2 + wire_length;
            el.x2 = x + slice_x2;
            el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_DUMMY_D2 + 1 + gap * 14) + 3 * slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);
        }
    } else if (wire_type == id_WIRE_TYPE_H02) {
        if (x == 0)
            el.x1 = 0.9;
        else
            el.x1 = x + switchbox_x1 + wire_distance * (20 + (tilewire - TILE_WIRE_H02W0701) + 20 * (x % 3));
        el.x2 = el.x1;
        el.y1 = y + switchbox_y1;
        el.y2 = y + switchbox_y1 - wire_distance * (20 + (tilewire - TILE_WIRE_H02W0701) + 20 * (x % 3));
        if (x != 0 && x != w - 1)
            g.push_back(el);

        if (x == w - 2)
            el.x2 = x + 1 + 0.1;
        else
            el.x2 = x + 1 + switchbox_x1 + wire_distance * (20 + (tilewire - TILE_WIRE_H02W0701) + 20 * (x % 3));
        el.y1 = el.y2;
        if (x != w - 1)
            g.push_back(el);

        el.x1 = el.x2;
        el.y1 = y + switchbox_y1;
        if (x != w - 1 && x != w - 2)
            g.push_back(el);

        if (x == w - 1)
            el.x1 = x + 0.1;
        else
            el.x1 = x + switchbox_x1 + wire_distance * (20 + (tilewire - TILE_WIRE_H02W0701) + 20 * (x % 3));
        if (x == 1)
            el.x2 = x - 1 + 0.9;
        else
            el.x2 = x - 1 + switchbox_x1 + wire_distance * (20 + (tilewire - TILE_WIRE_H02W0701) + 20 * (x % 3));
        el.y2 = y + switchbox_y1 - wire_distance * (20 + (tilewire - TILE_WIRE_H02W0701) + 20 * (x % 3));
        el.y1 = el.y2;
        if (x != 0)
            g.push_back(el);

        el.x1 = el.x2;
        el.y1 = y + switchbox_y1;
        if (x != 0 && x != 1)
            g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_V02) {
        if (y == 0)
            el.y1 = 0.9;
        else
            el.y1 = y + switchbox_y1 + wire_distance * (20 + (tilewire - TILE_WIRE_V02N0701) + 20 * (y % 3));
        el.y2 = el.y1;
        el.x1 = x + switchbox_x1;
        el.x2 = x + switchbox_x1 - wire_distance * (20 + (tilewire - TILE_WIRE_V02N0701) + 20 * (y % 3));
        if (y != 0 && y != h - 1)
            g.push_back(el);

        if (y == h - 2)
            el.y2 = y + 1 + 0.1;
        else
            el.y2 = y + 1 + switchbox_y1 + wire_distance * (20 + (tilewire - TILE_WIRE_V02N0701) + 20 * (y % 3));
        el.x1 = el.x2;
        if (y != h - 1)
            g.push_back(el);

        el.y1 = el.y2;
        el.x1 = x + switchbox_x1;
        if (y != h - 1 && y != h - 2)
            g.push_back(el);

        if (y == h - 1)
            el.y1 = y + 0.1;
        else
            el.y1 = y + switchbox_y1 + wire_distance * (20 + (tilewire - TILE_WIRE_V02N0701) + 20 * (y % 3));
        if (y == 1)
            el.y2 = y - 1 + 0.9;
        else
            el.y2 = y - 1 + switchbox_y1 + wire_distance * (20 + (tilewire - TILE_WIRE_V02N0701) + 20 * (y % 3));
        el.x2 = x + switchbox_x1 - wire_distance * (20 + (tilewire - TILE_WIRE_V02N0701) + 20 * (y % 3));
        el.x1 = el.x2;
        if (y != 0)
            g.push_back(el);

        el.y1 = el.y2;
        el.x1 = x + switchbox_x1;
        if (y != 0 && y != 1)
            g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_H06) {
        if (x == 0)
            el.x1 = 0.9;
        else
            el.x1 = x + switchbox_x1 + wire_distance * (96 + (tilewire - TILE_WIRE_H06W0303) + 10 * (x % 9));
        el.x2 = el.x1;
        el.y1 = y + switchbox_y1;
        el.y2 = y + switchbox_y1 - wire_distance * (96 + (tilewire - TILE_WIRE_H06W0303) + 10 * (x % 9));
        if (x != 0 && x != w - 1)
            g.push_back(el);

        if (x == w - 2 || x == w - 3 || x == w - 4)
            el.x2 = w - 1 + 0.1;
        else
            el.x2 = x + 3 + switchbox_x1 + wire_distance * (96 + (tilewire - TILE_WIRE_H06W0303) + 10 * (x % 9));
        el.y1 = el.y2;
        if (x != w - 1)
            g.push_back(el);

        el.x1 = el.x2;
        el.y1 = y + switchbox_y1;
        if (x != w - 1 && x != w - 2 && x != w - 3 && x != w - 4)
            g.push_back(el);

        if (x == w - 1)
            el.x1 = x + 0.1;
        else
            el.x1 = x + switchbox_x1 + wire_distance * (96 + (tilewire - TILE_WIRE_H06W0303) + 10 * (x % 9));
        if (x == 1 || x == 2 || x == 3)
            el.x2 = 0.9;
        else
            el.x2 = x - 3 + switchbox_x1 + wire_distance * (96 + (tilewire - TILE_WIRE_H06W0303) + 10 * (x % 9));
        el.y2 = y + switchbox_y1 - wire_distance * (96 + (tilewire - TILE_WIRE_H06W0303) + 10 * (x % 9));
        el.y1 = el.y2;
        if (x != 0)
            g.push_back(el);

        el.x1 = el.x2;
        el.y1 = y + switchbox_y1;
        if (x != 0 && x != 1 && x != 2 && x != 3)
            g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_V06) {
        if (y == 0)
            el.y1 = 0.9;
        else
            el.y1 = y + switchbox_y1 + wire_distance * (96 + (tilewire - TILE_WIRE_V06N0303) + 10 * (y % 9));
        el.y2 = el.y1;
        el.x1 = x + switchbox_x1;
        el.x2 = x + switchbox_x1 - wire_distance * (96 + (tilewire - TILE_WIRE_V06N0303) + 10 * (y % 9));
        if (y != 0 && y != h - 1)
            g.push_back(el);

        if (y == h - 2 || y == h - 3 || y == h - 4)
            el.y2 = h - 1 + 0.1;
        else
            el.y2 = y + 3 + switchbox_y1 + wire_distance * (96 + (tilewire - TILE_WIRE_V06N0303) + 10 * (y % 9));
        el.x1 = el.x2;
        if (y != h - 1)
            g.push_back(el);

        el.y1 = el.y2;
        el.x1 = x + switchbox_x1;
        if (y != h - 1 && y != h - 2 && y != h - 3 && y != h - 4)
            g.push_back(el);

        if (y == h - 1)
            el.y1 = y + 0.1;
        else
            el.y1 = y + switchbox_y1 + wire_distance * (96 + (tilewire - TILE_WIRE_V06N0303) + 10 * (y % 9));
        if (y == 1 || y == 2 || y == 3)
            el.y2 = 0.9;
        else
            el.y2 = y - 3 + switchbox_y1 + wire_distance * (96 + (tilewire - TILE_WIRE_V06N0303) + 10 * (y % 9));
        el.x2 = x + switchbox_x1 - wire_distance * (96 + (tilewire - TILE_WIRE_V06N0303) + 10 * (y % 9));
        el.x1 = el.x2;
        if (y != 0)
            g.push_back(el);

        el.y1 = el.y2;
        el.x1 = x + switchbox_x1;
        if (y != 0 && y != 1 && y != 2 && y != 3)
            g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_V01) {
        if (tilewire >= TILE_WIRE_V01N0001 && tilewire <= TILE_WIRE_V01S0100) {
            el.x1 = x + switchbox_x1 + wire_distance * (10 + tilewire - TILE_WIRE_V01N0001);
            el.x2 = el.x1;
            if (y == h - 2)
                el.y1 = y + 1.1;
            else
                el.y1 = y + switchbox_y1 + 1;

            if (y == 0)
                el.y2 = y + 0.9;
            else
                el.y2 = y + switchbox_y2;

            g.push_back(el);
        }
    } else if (wire_type == id_WIRE_TYPE_H01) {
        if (tilewire >= TILE_WIRE_H01E0001 && tilewire <= TILE_WIRE_HL7W0001) {
            if (x == w - 1)
                el.x1 = x + 0.1;
            else
                el.x1 = x + switchbox_x1;
            if (x == 1)
                el.x2 = x - 0.1;
            else
                el.x2 = x + switchbox_x2 - 1;
            el.y1 = y + switchbox_y1 + wire_distance * (10 + tilewire - TILE_WIRE_H01E0001);
            el.y2 = el.y1;
            g.push_back(el);
        }
    } else if (wire_type == id_WIRE_TYPE_V00) {
        int group = (tilewire - TILE_WIRE_V00T0000) / 2;
        el.x1 = x + switchbox_x2 - wire_distance * (8 - ((tilewire - TILE_WIRE_V00T0000) % 2) * 4);
        el.x2 = el.x1;
        if (group) {
            el.y1 = y + switchbox_y1;
            el.y2 = y + switchbox_y1 - wire_distance * 4;
        } else {
            el.y1 = y + switchbox_y2;
            el.y2 = y + switchbox_y2 + wire_distance * 4;
        }
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_H00) {
        int group = (tilewire - TILE_WIRE_H00L0000) / 2;
        el.y1 = y + switchbox_y1 + wire_distance * (8 - ((tilewire - TILE_WIRE_H00L0000) % 2) * 4);
        el.y2 = el.y1;

        if (group) {
            el.x1 = x + switchbox_x2 + wire_distance * 4;
            el.x2 = x + switchbox_x2;
        } else {
            el.x1 = x + switchbox_x1 - wire_distance * 4;
            el.x2 = x + switchbox_x1;
        }
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_NONE) {
        if (tilewire >= TILE_WIRE_NBOUNCE && tilewire <= TILE_WIRE_SBOUNCE) {
            el.x1 = x + switchbox_x2 - wire_distance * 4;
            el.x2 = x + switchbox_x2 - wire_distance * 8;
            if (tilewire == TILE_WIRE_NBOUNCE) {
                el.y1 = y + switchbox_y2 + wire_distance * 4;
                el.y2 = el.y1;
            } else {
                el.y1 = y + switchbox_y1 - wire_distance * 4;
                el.y2 = el.y1;
            }
            g.push_back(el);
        } else if (tilewire >= TILE_WIRE_WBOUNCE && tilewire <= TILE_WIRE_EBOUNCE) {
            el.y1 = y + switchbox_y1 + wire_distance * 4;
            el.y2 = y + switchbox_y1 + wire_distance * 8;
            if (tilewire == TILE_WIRE_WBOUNCE) {
                el.x1 = x + switchbox_x1 - wire_distance * 4;
                el.x2 = el.x1;
            } else {
                el.x1 = x + switchbox_x2 + wire_distance * 4;
                el.x2 = el.x1;
            }
            g.push_back(el);
        } else if (tilewire >= TILE_WIRE_CLK0 && tilewire <= TILE_WIRE_LSR1) {
            el.x1 = x + switchbox_x2;
            el.x2 = x + slice_x2 + 15 * wire_distance + (8 - (tilewire - TILE_WIRE_CLK0)) * wire_distance;
            el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_CLK0 - 5) + 3 * slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);
            el.x1 = el.x2;
            el.y2 = y + slice_y2 - wire_distance * (3 + (tilewire - TILE_WIRE_CLK0));
            g.push_back(el);
            for (int i = 0; i < 4; i++) {
                el.x1 = x + slice_x2 + 15 * wire_distance + wire_distance;
                el.x2 = x + slice_x2 + 15 * wire_distance + (8 - (tilewire - TILE_WIRE_CLK0)) * wire_distance;
                el.y1 = y + slice_y2 -
                        wire_distance * (TILE_WIRE_CLK3_SLICE - TILE_WIRE_DUMMY_D2 + 1 + tilewire - TILE_WIRE_CLK0) +
                        i * slice_pitch;
                el.y2 = el.y1;
                g.push_back(el);
            }
            if (tilewire == TILE_WIRE_CLK1 || tilewire == TILE_WIRE_LSR1) {
                for (int i = 0; i < 2; i++) {
                    el.x1 = x + slice_x2 + 3 * wire_distance;
                    el.x2 = x + slice_x2 + 15 * wire_distance + (8 - (tilewire - TILE_WIRE_CLK0)) * wire_distance;
                    el.y1 = y + slice_y2 -
                            wire_distance *
                                    (TILE_WIRE_CLK3_SLICE - TILE_WIRE_DUMMY_D2 - 1 + (tilewire - TILE_WIRE_CLK0) / 2) +
                            i * slice_pitch;
                    el.y2 = el.y1;
                    g.push_back(el);
                }
            }
        }

        // TRELLIS_IO wires
        else if (tilewire >= TILE_WIRE_JDIA && tilewire <= TILE_WIRE_ECLKD) {
            el.x1 = x + 0.5f;
            el.x2 = x + 0.5f + wire_length;
            bool top = (y == (h - 1));
            if (top)
                el.y1 = y + 1 - (slice_y2 - wire_distance * (tilewire - TILE_WIRE_JDIA + 1) + 3 * slice_pitch);
            else
                el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_JDIA + 1) + 3 * slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);
        }

        else if (tilewire >= TILE_WIRE_JCE0 && tilewire <= TILE_WIRE_JQ7) {
            el.x1 = x + switchbox_x2;
            el.x2 = x + switchbox_x2 + wire_length;
            el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_JCE0 + 1) + 3 * slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);
        }

        else if (tilewire >= TILE_WIRE_FCO && tilewire <= TILE_WIRE_FCI) {
            int gap = (tilewire - TILE_WIRE_FCO) / 24;
            int purpose = (tilewire - TILE_WIRE_FCO) % 24;
            el.x1 = x + switchbox_x2;
            if (purpose >= (TILE_WIRE_D7 - TILE_WIRE_FCO) && purpose <= (TILE_WIRE_A6 - TILE_WIRE_FCO)) {
                // Space for the LUT permutation switchbox
                el.x2 = x + slice_x1 - wire_length_lut;
            } else {
                el.x2 = x + slice_x1 - wire_length;
            }
            el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_FCO + 1 + gap * 2) + 3 * slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);
        }

        else if (tilewire >= TILE_WIRE_MUXCLK3 && tilewire <= TILE_WIRE_MUXLSR0) {
            int gap = (tilewire - TILE_WIRE_MUXCLK3) / 2;
            int part = (tilewire - TILE_WIRE_MUXCLK3) % 2;
            el.x1 = x + slice_x2 + 3 * wire_distance;
            el.x2 = x + slice_x2 + 15 * wire_distance;
            el.y1 = y + slice_y2 - wire_distance * (TILE_WIRE_CLK3_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part + gap * 26) +
                    3 * slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);
        }

        else if (tilewire >= TILE_WIRE_WD3 && tilewire <= TILE_WIRE_WD0) {
            int part = (tilewire - TILE_WIRE_WD3) % 4;
            int group = (tilewire - TILE_WIRE_WD3) / 2;
            el.x1 = x + slice_x2 + wire_length;
            el.x2 = x + slice_x2 + wire_length + wire_distance * (4 - part);
            el.y1 = y + slice_y2 - wire_distance * (TILE_WIRE_WDO3C_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part + 14) +
                    3 * slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);

            el.x1 = el.x2;
            el.y2 = y + slice_y2 -
                    wire_distance * (TILE_WIRE_WD1B_SLICE - TILE_WIRE_DUMMY_D2 + 1 + (part & 1) + 14 * 2) +
                    (3 - group) * slice_pitch;
            g.push_back(el);

            el.x1 = x + slice_x2 + wire_length;
            el.y1 = el.y2;
            g.push_back(el);
        } else if (tilewire >= TILE_WIRE_WAD3 && tilewire <= TILE_WIRE_WAD0) {
            int part = (tilewire - TILE_WIRE_WAD3) % 4;
            el.x1 = x + slice_x2 + wire_length;
            el.x2 = x + slice_x2 + wire_length + wire_distance * (8 - part);
            el.y1 = y + slice_y2 - wire_distance * (TILE_WIRE_WADO3C_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part + 14) +
                    3 * slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);

            el.x1 = el.x2;
            el.y2 = y + slice_y2 - wire_distance * (TILE_WIRE_WAD3B_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part + 14 * 2) +
                    2 * slice_pitch;
            g.push_back(el);

            el.x1 = x + slice_x2 + wire_length;
            el.y1 = el.y2;
            g.push_back(el);

            // middle line
            el.x1 = x + slice_x2 + wire_length;
            el.x2 = x + slice_x2 + wire_length + wire_distance * (8 - part);
            el.y2 = y + slice_y2 - wire_distance * (TILE_WIRE_WAD3B_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part + 14 * 2) +
                    3 * slice_pitch;
            el.y1 = el.y2;
            g.push_back(el);
        }
    } else if (wire_type == id_WIRE_TYPE_G_HPBX) {
        el.x1 = x;
        el.x2 = x + 1;
        el.y1 = y + 0.1f + wire_distance * (tilewire - TILE_WIRE_G_HPBX0000 + 1);
        el.y2 = el.y1;
        g.push_back(el);

        el.x1 = x + switchbox_x1 + wire_distance * (200 + (tilewire - TILE_WIRE_G_HPBX0000));
        el.x2 = el.x1;
        el.y2 = y + switchbox_y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_G_VPTX) {
        el.x1 = x + 0.1f + wire_distance * (tilewire - TILE_WIRE_G_VPTX0000 + 1);
        el.x2 = el.x1;
        el.y1 = y;
        el.y2 = y + 1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_L_HPBX) {
        el.x1 = x - 3;
        el.x2 = x + 0.08f;
        el.y1 = y + wire_distance + wire_distance * (tilewire - TILE_WIRE_L_HPBX0000 + 1);
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_R_HPBX) {
        el.x1 = x + 0.2;
        el.x2 = x + 3;
        el.y1 = y + wire_distance + wire_distance * (tilewire - TILE_WIRE_R_HPBX0000 + 1);
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_PIO) {
        bool top_bottom = (y == 0 || y == (h - 1));
        int gap = 3 - (tilewire - TILE_WIRE_PADDOD_PIO) / 7;
        int num = (tilewire - TILE_WIRE_PADDOD_PIO) % 7;
        if (top_bottom) {
            el.x1 = x + io_cell_h_x1 + (gap + 2) * io_cell_gap + wire_distance * (num + 1);
            el.x2 = el.x1;
            if (y == h - 1) {
                el.y1 = y + 1 - io_cell_h_y2;
                el.y2 = el.y1 - wire_length_long;
            } else {
                el.y1 = y + io_cell_h_y2;
                el.y2 = el.y1 + wire_length_long;
            }
        } else {
            if (x == 0) {
                el.x1 = x + 1 - io_cell_v_x1;
                el.x2 = el.x1 + wire_length_long;
            } else {
                el.x1 = x + io_cell_v_x1;
                el.x2 = el.x1 - wire_length_long;
            }
            el.y1 = y + io_cell_v_y1 + gap * io_cell_gap + wire_distance * (num + 1);
            el.y2 = el.y1;
        }
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_DDRDLL) {
        int num = (tilewire - TILE_WIRE_DDRDEL_DDRDLL);
        el.x1 = x + io_cell_h_x1 + 0.2 + wire_distance * (num + 1);
        el.x2 = el.x1;
        if (y == h - 1) {
            el.y1 = y + dll_cell_y1;
            el.y2 = el.y1 - wire_length_long;
        } else {
            el.y1 = y + dll_cell_y2;
            el.y2 = el.y1 + wire_length_long;
        }
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_CCLK) {
        int num = (tilewire - TILE_WIRE_JPADDI_CCLK);
        el.x1 = x + slice_x1 + wire_distance * (num + 1);
        el.x2 = el.x1;
        el.y1 = y + slice_y2 - 1 * slice_pitch;
        el.y2 = el.y1 - wire_length_long;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_IOLOGIC) {
        int gap = 7 - (tilewire - TILE_WIRE_JLOADND_IOLOGIC) / 42;
        int num = (tilewire - TILE_WIRE_JLOADND_IOLOGIC) % 42;
        if (x == 0) {
            el.x1 = x + 1 - io_cell_v_x1;
            el.x2 = el.x1 + wire_length_long;
        } else {
            el.x1 = x + io_cell_v_x1;
            el.x2 = el.x1 - wire_length_long;
        }
        el.y1 = y + io_cell_v_y1 + gap * io_cell_gap + wire_distance * (num + 1);
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_SIOLOGIC) {
        int gap = (tilewire - TILE_WIRE_JLOADNB_SIOLOGIC) / 20;
        int num = (tilewire - TILE_WIRE_JLOADNB_SIOLOGIC) % 20;
        el.x1 = x + io_cell_h_x1 + (5 - gap) * io_cell_gap + wire_distance * (num + 1);
        el.x2 = el.x1;
        if (y == h - 1) {
            el.y1 = y + 1 - io_cell_h_y2;
            el.y2 = el.y1 - wire_length_long;
        } else {
            el.y1 = y + io_cell_h_y2;
            el.y2 = el.y1 + wire_length_long;
        }
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_DQS) {
        int num = (tilewire - TILE_WIRE_DDRDEL_DQS);
        if (x == 0) {
            el.x1 = x + 1 - io_cell_v_x1;
            el.x2 = el.x1 + wire_length_long;
        } else {
            el.x1 = x + io_cell_v_x1;
            el.x2 = el.x1 - wire_length_long;
        }
        el.y1 = y + io_cell_v_y1 + 8 * io_cell_gap + wire_distance * (num + 1);
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_EBR) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_JADA0_EBR + 1) + 3 * slice_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_MULT18) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance_small * (tilewire - TILE_WIRE_JCLK0_MULT18 + 1) + 3 * slice_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_ALU54) {
        int num = (tilewire - TILE_WIRE_JCLK0_ALU54) % 225;
        int group = (tilewire - TILE_WIRE_JCLK0_ALU54) / 225;
        if (group == 0) {
            el.x1 = x + slice_x1 - wire_length;
            el.x2 = x + slice_x1;
        } else {
            el.x1 = x + slice_x2_wide + wire_length;
            el.x2 = x + slice_x2_wide;
        }
        el.y1 = y + slice_y2 - wire_distance_small * (num + 1) + 3 * slice_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_PLL) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_CLKI_PLL + 1);
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_GSR) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_JCLK_GSR + 1);
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_JTAG) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_JJCE1_JTAG + 1) + 1 * slice_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_OSC) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_SEDSTDBY_OSC + 1) + 2 * slice_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_SED) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_SEDSTDBY_SED + 1) + 3 * slice_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_DTR) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_JSTARTPULSE_DTR + 1);
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_EXTREF) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_REFCLKP_EXTREF + 1) + 1 * slice_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_DCU) {
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (tilewire - TILE_WIRE_CH0_RX_REFCLK_DCU + 1) + 0 * slice_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    } else if (wire_type == id_WIRE_TYPE_PCSCLKDIV) {
        int num = (tilewire - TILE_WIRE_CLKI_PCSCLKDIV1) % 7;
        int group = 1 - (tilewire - TILE_WIRE_CLKI_PCSCLKDIV1) / 7;
        el.x1 = x + slice_x1 - wire_length;
        el.x2 = x + slice_x1;
        el.y1 = y + slice_y2 - wire_distance * (num + 1) + group * slice_pitch;
        el.y2 = el.y1;
        g.push_back(el);
    }
}

void setSource(GraphicElement &el, int x, int y, int w, int h, WireId src, IdString src_type, GfxTileWireId src_id)
{
    if (src_type == id_WIRE_TYPE_H00) {
        int group = (src_id - TILE_WIRE_H00L0000) / 2;
        el.y1 = y + switchbox_y1 + wire_distance * (8 - ((src_id - TILE_WIRE_H00L0000) % 2) * 4);

        if (group) {
            el.x1 = x + switchbox_x2;
        } else {
            el.x1 = x + switchbox_x1;
        }
    }
    if (src_type == id_WIRE_TYPE_H01) {
        if (x == src.location.x)
            el.x1 = x + switchbox_x1;
        else
            el.x1 = x + switchbox_x2;
        el.y1 = y + switchbox_y1 + wire_distance * (10 + src_id - TILE_WIRE_H01E0001);
    }
    if (src_type == id_WIRE_TYPE_H02) {
        el.x1 = x + switchbox_x1 + wire_distance * (20 + (src_id - TILE_WIRE_H02W0701) + 20 * (src.location.x % 3));
        el.y1 = y + switchbox_y1;
    }
    if (src_type == id_WIRE_TYPE_H06) {
        el.x1 = x + switchbox_x1 + wire_distance * (96 + (src_id - TILE_WIRE_H06W0303) + 10 * (src.location.x % 9));
        el.y1 = y + switchbox_y1;
    }
    if (src_type == id_WIRE_TYPE_V00) {
        int group = (src_id - TILE_WIRE_V00T0000) / 2;
        el.x1 = x + switchbox_x2 - wire_distance * (8 - ((src_id - TILE_WIRE_V00T0000) % 2) * 4);
        if (group) {
            el.y1 = y + switchbox_y1;
        } else {
            el.y1 = y + switchbox_y2;
        }
    }
    if (src_type == id_WIRE_TYPE_V01) {
        el.x1 = x + switchbox_x1 + wire_distance * (10 + src_id - TILE_WIRE_V01N0001);
        if (y == src.location.y)
            el.y1 = y + switchbox_y2;
        else
            el.y1 = y + switchbox_y1;
    }
    if (src_type == id_WIRE_TYPE_V02) {
        el.x1 = x + switchbox_x1;
        el.y1 = y + switchbox_y1 + wire_distance * (20 + (src_id - TILE_WIRE_V02N0701) + 20 * (src.location.y % 3));
    }
    if (src_type == id_WIRE_TYPE_V06) {
        el.x1 = x + switchbox_x1;
        el.y1 = y + switchbox_y1 + wire_distance * (96 + (src_id - TILE_WIRE_V06N0303) + 10 * (src.location.y % 9));
    }
    if (src_type == id_WIRE_TYPE_NONE) {
        if (src_id >= TILE_WIRE_CLK0 && src_id <= TILE_WIRE_LSR1) {
            el.x1 = x + switchbox_x2;
            el.y1 = y + slice_y2 - wire_distance * (src_id - TILE_WIRE_CLK0 - 5) + 3 * slice_pitch;
        }
        if (src_id >= TILE_WIRE_FCO && src_id <= TILE_WIRE_FCI) {
            int gap = (src_id - TILE_WIRE_FCO) / 24;
            el.x1 = src.location.x + switchbox_x2;
            el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_FCO + 1 + gap * 2) +
                    3 * slice_pitch;
        }
        if (src_id >= TILE_WIRE_JCE0 && src_id <= TILE_WIRE_JQ7) {
            el.x1 = src.location.x + switchbox_x2 + wire_length;
            el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_JCE0 + 1) + 3 * slice_pitch;
        }
        if (src_id >= TILE_WIRE_JDIA && src_id <= TILE_WIRE_ECLKD) {
            bool top = (src.location.y == (h - 1));
            el.x1 = src.location.x + 0.5f + wire_length;
            if (top)
                el.y1 = src.location.y + 1 -
                        (slice_y2 - wire_distance * (src_id - TILE_WIRE_JDIA + 1) + 3 * slice_pitch);
            else
                el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_JDIA + 1) + 3 * slice_pitch;
        }
    }
    if (src_type == id_WIRE_TYPE_IOLOGIC) {
        int gap = 7 - (src_id - TILE_WIRE_JLOADND_IOLOGIC) / 42;
        int num = (src_id - TILE_WIRE_JLOADND_IOLOGIC) % 42;
        if (src.location.x == 0) {
            el.x1 = src.location.x + 1 - io_cell_v_x1 + wire_length_long;
        } else {
            el.x1 = src.location.x + io_cell_v_x1 - wire_length_long;
        }
        el.y1 = src.location.y + io_cell_v_y1 + gap * io_cell_gap + wire_distance * (num + 1);
    }
    if (src_type == id_WIRE_TYPE_SIOLOGIC) {
        int gap = (src_id - TILE_WIRE_JLOADNB_SIOLOGIC) / 20;
        int num = (src_id - TILE_WIRE_JLOADNB_SIOLOGIC) % 20;
        el.x1 = src.location.x + io_cell_h_x1 + (5 - gap) * io_cell_gap + wire_distance * (num + 1);
        if (src.location.y == h - 1) {
            el.y1 = src.location.y + 1 - io_cell_h_y2 - wire_length_long;
        } else {
            el.y1 = src.location.y + io_cell_h_y2 + wire_length_long;
        }
    }
    if (src_type == id_WIRE_TYPE_PIO) {
        bool top_bottom = (src.location.y == 0 || src.location.y == (h - 1));
        int gap = 3 - (src_id - TILE_WIRE_PADDOD_PIO) / 7;
        int num = (src_id - TILE_WIRE_PADDOD_PIO) % 7;
        if (top_bottom) {
            el.x1 = src.location.x + io_cell_h_x1 + (gap + 2) * io_cell_gap + wire_distance * (num + 1);
            if (src.location.y == h - 1) {
                el.y1 = src.location.y + 1 - io_cell_h_y2 - wire_length_long;
            } else {
                el.y1 = src.location.y + 1 - io_cell_h_y2 + wire_length_long;
            }
        } else {
            if (x == 0) {
                el.x1 = src.location.x + 1 - io_cell_v_x1 + wire_length_long;
            } else {
                el.x1 = src.location.x + io_cell_v_x1 - wire_length_long;
            }
            el.y1 = src.location.y + io_cell_v_y1 + gap * io_cell_gap + wire_distance * (num + 1);
        }
    }
    if (src_type == id_WIRE_TYPE_EBR) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_JADA0_EBR + 1) + 3 * slice_pitch;
    }
    if (src_type == id_WIRE_TYPE_MULT18) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance_small * (src_id - TILE_WIRE_JCLK0_MULT18 + 1) +
                3 * slice_pitch;
    }
    if (src_type == id_WIRE_TYPE_ALU54) {
        int num = (src_id - TILE_WIRE_JCLK0_ALU54) % 225;
        int group = (src_id - TILE_WIRE_JCLK0_ALU54) / 225;
        if (group == 0) {
            el.x1 = src.location.x + slice_x1 - wire_length;
        } else {
            el.x1 = src.location.x + slice_x2_wide + wire_length;
        }
        el.y1 = src.location.y + slice_y2 - wire_distance_small * (num + 1) + 3 * slice_pitch;
    }
    if (src_type == id_WIRE_TYPE_PLL) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_CLKI_PLL + 1);
    }
    if (src_type == id_WIRE_TYPE_GSR) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_JCLK_GSR + 1);
    }
    if (src_type == id_WIRE_TYPE_JTAG) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_JJCE1_JTAG + 1) + 1 * slice_pitch;
    }
    if (src_type == id_WIRE_TYPE_OSC) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_SEDSTDBY_OSC + 1) + 2 * slice_pitch;
    }
    if (src_type == id_WIRE_TYPE_SED) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_SEDSTDBY_SED + 1) + 3 * slice_pitch;
    }
    if (src_type == id_WIRE_TYPE_DTR) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_JSTARTPULSE_DTR + 1);
    }
    if (src_type == id_WIRE_TYPE_EXTREF) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_REFCLKP_EXTREF + 1) + 1 * slice_pitch;
    }
    if (src_type == id_WIRE_TYPE_DCU) {
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_CH0_RX_REFCLK_DCU + 1) +
                0 * slice_pitch;
    }
    if (src_type == id_WIRE_TYPE_PCSCLKDIV) {
        int num = (src_id - TILE_WIRE_CLKI_PCSCLKDIV1) % 7;
        int group = 1 - (src_id - TILE_WIRE_CLKI_PCSCLKDIV1) / 7;
        el.x1 = src.location.x + slice_x1 - wire_length;
        el.y1 = src.location.y + slice_y2 - wire_distance * (num + 1) + group * slice_pitch;
    }
    if (src_type == id_WIRE_TYPE_DQS) {
        int num = (src_id - TILE_WIRE_DDRDEL_DQS);
        if (src.location.x == 0) {
            el.x1 = src.location.x + 1 - io_cell_v_x1 + wire_length_long;
        } else {
            el.x1 = src.location.x + io_cell_v_x1 - wire_length_long;
        }
        el.y1 = src.location.y + io_cell_v_y1 + 8 * io_cell_gap + wire_distance * (num + 1);
    }
    if (src_type == id_WIRE_TYPE_DDRDLL) {
        int num = (src_id - TILE_WIRE_DDRDEL_DDRDLL);
        el.x1 = src.location.x + io_cell_h_x1 + dll_cell_x1 + wire_distance * (num + 1);
        if (src.location.y == h - 1) {
            el.y1 = src.location.y + dll_cell_y1 - wire_length_long;
        } else {
            el.y1 = src.location.y + dll_cell_y2 + wire_length_long;
        }
    }
    if (src_type == id_WIRE_TYPE_CCLK) {
        int num = (src_id - TILE_WIRE_JPADDI_CCLK);
        el.x1 = src.location.x + slice_x1 + wire_distance * (num + 1);
        el.y1 = src.location.y + slice_y2 - 1 * slice_pitch - wire_length_long;
    }
    if (src_type == id_WIRE_TYPE_G_HPBX) {
        el.x1 = x + switchbox_x1 + wire_distance * (200 + (src_id - TILE_WIRE_G_HPBX0000));
        el.y1 = y + switchbox_y1;
    }
}

void setDestination(GraphicElement &el, int x, int y, int w, int h, WireId dst, IdString dst_type, GfxTileWireId dst_id)
{
    if (dst_type == id_WIRE_TYPE_H00) {
        int group = (dst_id - TILE_WIRE_H00L0000) / 2;
        el.y2 = y + switchbox_y1 + wire_distance * (8 - ((dst_id - TILE_WIRE_H00L0000) % 2) * 4);

        if (group) {
            el.x2 = x + switchbox_x2;
        } else {
            el.x2 = x + switchbox_x1;
        }
    }
    if (dst_type == id_WIRE_TYPE_H01) {
        if (x == dst.location.x)
            el.x2 = x + switchbox_x1;
        else
            el.x2 = x + switchbox_x2;
        el.y2 = y + switchbox_y1 + wire_distance * (10 + dst_id - TILE_WIRE_H01E0001);
    }
    if (dst_type == id_WIRE_TYPE_H02) {
        el.x2 = x + switchbox_x1 + wire_distance * (20 + (dst_id - TILE_WIRE_H02W0701) + 20 * (dst.location.x % 3));
        el.y2 = y + switchbox_y1;
    }
    if (dst_type == id_WIRE_TYPE_H06) {
        el.x2 = x + switchbox_x1 + wire_distance * (96 + (dst_id - TILE_WIRE_H06W0303) + 10 * (dst.location.x % 9));
        el.y2 = y + switchbox_y1;
    }
    if (dst_type == id_WIRE_TYPE_V00) {
        int group = (dst_id - TILE_WIRE_V00T0000) / 2;
        el.x2 = x + switchbox_x2 - wire_distance * (8 - ((dst_id - TILE_WIRE_V00T0000) % 2) * 4);
        if (group) {
            el.y2 = y + switchbox_y1;
        } else {
            el.y2 = y + switchbox_y2;
        }
    }
    if (dst_type == id_WIRE_TYPE_V01) {
        el.x2 = x + switchbox_x1 + wire_distance * (10 + dst_id - TILE_WIRE_V01N0001);
        if (y == dst.location.y)
            el.y2 = y + switchbox_y2;
        else
            el.y2 = y + switchbox_y1;
    }
    if (dst_type == id_WIRE_TYPE_V02) {
        el.x2 = x + switchbox_x1;
        el.y2 = y + switchbox_y1 + wire_distance * (20 + (dst_id - TILE_WIRE_V02N0701) + 20 * (dst.location.y % 3));
    }
    if (dst_type == id_WIRE_TYPE_V06) {
        el.x2 = x + switchbox_x1;
        el.y2 = y + switchbox_y1 + wire_distance * (96 + (dst_id - TILE_WIRE_V06N0303) + 10 * (dst.location.y % 9));
    }

    if (dst_type == id_WIRE_TYPE_NONE) {
        if (dst_id >= TILE_WIRE_CLK0 && dst_id <= TILE_WIRE_LSR1) {
            el.x2 = x + switchbox_x2;
            el.y2 = y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_CLK0 - 5) + 3 * slice_pitch;
        }
        if (dst_id >= TILE_WIRE_FCO && dst_id <= TILE_WIRE_FCI) {
            int gap = (dst_id - TILE_WIRE_FCO) / 24;
            el.x2 = x + switchbox_x2;
            el.y2 = y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_FCO + 1 + gap * 2) + 3 * slice_pitch;
        }
        if (dst_id >= TILE_WIRE_JCE0 && dst_id <= TILE_WIRE_JQ7) {
            el.x2 = dst.location.x + switchbox_x2;
            el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_JCE0 + 1) + 3 * slice_pitch;
        }
        if (dst_id >= TILE_WIRE_JDIA && dst_id <= TILE_WIRE_ECLKD) {
            bool top = (dst.location.y == (h - 1));
            el.x2 = dst.location.x + 0.5f;
            if (top)
                el.y2 = dst.location.y + 1 -
                        (slice_y2 - wire_distance * (dst_id - TILE_WIRE_JDIA + 1) + 3 * slice_pitch);
            else
                el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_JDIA + 1) + 3 * slice_pitch;
        }
    }

    if (dst_type == id_WIRE_TYPE_IOLOGIC) {
        int gap = 7 - (dst_id - TILE_WIRE_JLOADND_IOLOGIC) / 42;
        int num = (dst_id - TILE_WIRE_JLOADND_IOLOGIC) % 42;
        if (dst.location.x == 0) {
            el.x2 = dst.location.x + 1 - io_cell_v_x1 + wire_length_long;
        } else {
            el.x2 = dst.location.x + io_cell_v_x1 - wire_length_long;
        }
        el.y2 = dst.location.y + io_cell_v_y1 + gap * io_cell_gap + wire_distance * (num + 1);
    }
    if (dst_type == id_WIRE_TYPE_SIOLOGIC) {
        int gap = (dst_id - TILE_WIRE_JLOADNB_SIOLOGIC) / 20;
        int num = (dst_id - TILE_WIRE_JLOADNB_SIOLOGIC) % 20;
        el.x2 = dst.location.x + io_cell_h_x1 + (5 - gap) * io_cell_gap + wire_distance * (num + 1);
        if (dst.location.y == h - 1) {
            el.y2 = dst.location.y + 1 - io_cell_h_y2 - wire_length_long;
        } else {
            el.y2 = dst.location.y + io_cell_h_y2 + wire_length_long;
        }
    }
    if (dst_type == id_WIRE_TYPE_PIO) {
        bool top_bottom = (dst.location.y == 0 || dst.location.y == (h - 1));
        int gap = 3 - (dst_id - TILE_WIRE_PADDOD_PIO) / 7;
        int num = (dst_id - TILE_WIRE_PADDOD_PIO) % 7;
        if (top_bottom) {
            el.x2 = dst.location.x + io_cell_h_x1 + (gap + 2) * io_cell_gap + wire_distance * (num + 1);
            if (dst.location.y == h - 1) {
                el.y2 = dst.location.y + 1 - io_cell_h_y2 - wire_length_long;
            } else {
                el.y2 = dst.location.y + 1 - io_cell_h_y2 + wire_length_long;
            }
        } else {
            if (x == 0) {
                el.x2 = dst.location.x + 1 - io_cell_v_x1 + wire_length_long;
            } else {
                el.x2 = dst.location.x + io_cell_v_x1 - wire_length_long;
            }
            el.y2 = dst.location.y + io_cell_v_y1 + gap * io_cell_gap + wire_distance * (num + 1);
        }
    }
    if (dst_type == id_WIRE_TYPE_EBR) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_JADA0_EBR + 1) + 3 * slice_pitch;
    }
    if (dst_type == id_WIRE_TYPE_MULT18) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance_small * (dst_id - TILE_WIRE_JCLK0_MULT18 + 1) +
                3 * slice_pitch;
    }
    if (dst_type == id_WIRE_TYPE_ALU54) {
        int num = (dst_id - TILE_WIRE_JCLK0_ALU54) % 225;
        int group = (dst_id - TILE_WIRE_JCLK0_ALU54) / 225;
        if (group == 0) {
            el.x2 = dst.location.x + slice_x1 - wire_length;
        } else {
            el.x2 = dst.location.x + slice_x2_wide + wire_length;
        }
        el.y2 = dst.location.y + slice_y2 - wire_distance_small * (num + 1) + 3 * slice_pitch;
    }
    if (dst_type == id_WIRE_TYPE_PLL) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_CLKI_PLL + 1);
    }
    if (dst_type == id_WIRE_TYPE_GSR) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_JCLK_GSR + 1);
    }
    if (dst_type == id_WIRE_TYPE_JTAG) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_JJCE1_JTAG + 1) + 1 * slice_pitch;
    }
    if (dst_type == id_WIRE_TYPE_OSC) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_SEDSTDBY_OSC + 1) + 2 * slice_pitch;
    }
    if (dst_type == id_WIRE_TYPE_SED) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_SEDSTDBY_SED + 1) + 3 * slice_pitch;
    }
    if (dst_type == id_WIRE_TYPE_DTR) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_JSTARTPULSE_DTR + 1);
    }
    if (dst_type == id_WIRE_TYPE_EXTREF) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_REFCLKP_EXTREF + 1) + 1 * slice_pitch;
    }
    if (dst_type == id_WIRE_TYPE_DCU) {
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_CH0_RX_REFCLK_DCU + 1) +
                0 * slice_pitch;
    }
    if (dst_type == id_WIRE_TYPE_PCSCLKDIV) {
        int num = (dst_id - TILE_WIRE_CLKI_PCSCLKDIV1) % 7;
        int group = 1 - (dst_id - TILE_WIRE_CLKI_PCSCLKDIV1) / 7;
        el.x2 = dst.location.x + slice_x1 - wire_length;
        el.y2 = dst.location.y + slice_y2 - wire_distance * (num + 1) + group * slice_pitch;
    }
    if (dst_type == id_WIRE_TYPE_DQS) {
        int num = (dst_id - TILE_WIRE_DDRDEL_DQS);
        if (dst.location.x == 0) {
            el.x2 = dst.location.x + 1 - io_cell_v_x1 + wire_length_long;
        } else {
            el.x2 = dst.location.x + io_cell_v_x1 - wire_length_long;
        }
        el.y2 = dst.location.y + io_cell_v_y1 + 8 * io_cell_gap + wire_distance * (num + 1);
    }
    if (dst_type == id_WIRE_TYPE_DDRDLL) {
        int num = (dst_id - TILE_WIRE_DDRDEL_DDRDLL);
        el.x2 = dst.location.x + io_cell_h_x1 + dll_cell_x1 + wire_distance * (num + 1);
        if (dst.location.y == h - 1) {
            el.y2 = dst.location.y + dll_cell_y1 - wire_length_long;
        } else {
            el.y2 = dst.location.y + dll_cell_y2 + wire_length_long;
        }
    }
    if (dst_type == id_WIRE_TYPE_CCLK) {
        int num = (dst_id - TILE_WIRE_JPADDI_CCLK);
        el.x2 = dst.location.x + slice_x1 + wire_distance * (num + 1);
        el.y2 = dst.location.y + slice_y2 - 1 * slice_pitch - wire_length_long;
    }
    if (dst_type == id_WIRE_TYPE_G_HPBX) {
        el.x2 = x + switchbox_x1 + wire_distance * (200 + (dst_id - TILE_WIRE_G_HPBX0000));
        el.y2 = y + switchbox_y1;
    }
}

void straightLine(std::vector<GraphicElement> &g, GraphicElement &el, int x, int y, int w, int h, WireId src,
                  IdString src_type, GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id)
{
    setSource(el, x, y, w, h, src, src_type, src_id);
    setDestination(el, x, y, w, h, dst, dst_type, dst_id);
    g.push_back(el);
}

void lutPermPip(std::vector<GraphicElement> &g, GraphicElement &el, int x, int y, int w, int h, WireId src,
                IdString src_type, GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id)
{
    int gap = (src_id - TILE_WIRE_FCO) / 24;
    el.x1 = src.location.x + slice_x1 - wire_length_lut;
    el.y1 = src.location.y + slice_y2 - wire_distance * (src_id - TILE_WIRE_FCO + 1 + gap * 2) + 3 * slice_pitch;
    el.x2 = src.location.x + slice_x1 - wire_length;
    el.y2 = src.location.y + slice_y2 - wire_distance * (dst_id - TILE_WIRE_FCO_SLICE + 1 + gap * 2) + 3 * slice_pitch;
    g.push_back(el);
}

void toSameSideHor(std::vector<GraphicElement> &g, GraphicElement &el, int x, int y, int w, int h, WireId src,
                   IdString src_type, GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id,
                   GraphicElement::style_t style, int idx)
{
    int sign = (src_type == dst_type) ? 1 : -1;
    setSource(el, x, y, w, h, src, src_type, src_id);
    el.x2 = el.x1;
    el.y2 = y + switchbox_y1 + (switchbox_y2 - switchbox_y1) / 2 + sign * wire_distance * idx;
    g.push_back(el);

    GraphicElement el2;
    el2.type = GraphicElement::TYPE_ARROW;
    el2.style = style;

    setDestination(el2, x, y, w, h, dst, dst_type, dst_id);

    el.x1 = el2.x2;
    el.y1 = el.y2;
    g.push_back(el);

    el2.x1 = el.x1;
    el2.y1 = el.y1;
    g.push_back(el2);
}

void toSameSideVer(std::vector<GraphicElement> &g, GraphicElement &el, int x, int y, int w, int h, WireId src,
                   IdString src_type, GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id,
                   GraphicElement::style_t style, int idx)
{
    int sign = (src_type == dst_type) ? 1 : -1;
    setSource(el, x, y, w, h, src, src_type, src_id);
    el.x2 = x + switchbox_x1 + (switchbox_x2 - switchbox_x1) / 2 + sign * wire_distance * idx;
    el.y2 = el.y1;
    g.push_back(el);

    GraphicElement el2;
    el2.type = GraphicElement::TYPE_ARROW;
    el2.style = style;

    setDestination(el2, x, y, w, h, dst, dst_type, dst_id);

    el.x1 = el.x2;
    el.y1 = el2.y2;
    g.push_back(el);

    el2.x1 = el.x1;
    el2.y1 = el.y1;
    g.push_back(el2);
}

void toSameSideH1Ver(std::vector<GraphicElement> &g, GraphicElement &el, int x, int y, int w, int h, WireId src,
                     IdString src_type, GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id,
                     GraphicElement::style_t style, int idx)
{
    setSource(el, x, y, w, h, src, src_type, src_id);
    el.x2 = x + switchbox_x1 + (switchbox_x2 - switchbox_x1) / 2 - wire_distance * idx;
    el.y2 = el.y1;
    g.push_back(el);

    GraphicElement el2;
    el2.type = GraphicElement::TYPE_ARROW;
    el2.style = style;

    setDestination(el2, x, y, w, h, dst, dst_type, dst_id);

    el.x1 = el.x2;
    el.y1 = el2.y2;
    g.push_back(el);

    el2.x1 = el.x1;
    el2.y1 = el.y1;
    g.push_back(el2);
}

void toSameSideH1Hor(std::vector<GraphicElement> &g, GraphicElement &el, int x, int y, int w, int h, WireId src,
                     IdString src_type, GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id,
                     GraphicElement::style_t style, int idx)
{
    setSource(el, x, y, w, h, src, src_type, src_id);

    GraphicElement el2;
    el2.type = GraphicElement::TYPE_ARROW;
    el2.style = style;

    setDestination(el2, x, y, w, h, dst, dst_type, dst_id);
    if (dst_type == id_WIRE_TYPE_H01 || src_type == id_WIRE_TYPE_V01 || dst_type == id_WIRE_TYPE_H00) {
        el.x2 = el.x1;
        el.y2 = el2.y2;
        g.push_back(el);
    } else {
        el.x2 = el2.x2;
        el.y2 = el.y1;
        g.push_back(el);
    }

    el2.x1 = el.x2;
    el2.y1 = el.y2;
    g.push_back(el2);
}

void toSameSideV1Ver(std::vector<GraphicElement> &g, GraphicElement &el, int x, int y, int w, int h, WireId src,
                     IdString src_type, GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id,
                     GraphicElement::style_t style, int idx)
{
    setSource(el, x, y, w, h, src, src_type, src_id);
    el.x2 = el.x1;
    el.y2 = y + switchbox_y1 + (switchbox_y2 - switchbox_y1) / 2 - wire_distance * idx;
    g.push_back(el);

    GraphicElement el2;
    el2.type = GraphicElement::TYPE_ARROW;
    el2.style = style;

    setDestination(el2, x, y, w, h, dst, dst_type, dst_id);

    el.x1 = el2.x2;
    el.y1 = el.y2;
    g.push_back(el);

    el2.x1 = el.x1;
    el2.y1 = el.y1;
    g.push_back(el2);
}
void gfxTilePip(std::vector<GraphicElement> &g, int x, int y, int w, int h, WireId src, IdString src_type,
                GfxTileWireId src_id, WireId dst, IdString dst_type, GfxTileWireId dst_id,
                GraphicElement::style_t style)
{
    GraphicElement el;
    el.type = GraphicElement::TYPE_ARROW;
    el.style = style;

    // To H00
    if (src_type == id_WIRE_TYPE_V02 && dst_type == id_WIRE_TYPE_H00) {
        toSameSideH1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        dst_id - TILE_WIRE_H00L0000 + 30);
    }
    if (src_type == id_WIRE_TYPE_H02 && dst_type == id_WIRE_TYPE_H00) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }

    // To H01
    if (src_type == id_WIRE_TYPE_H06 && dst_type == id_WIRE_TYPE_H01) {
        toSameSideH1Hor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        dst_id - TILE_WIRE_H01E0001);
    }

    // To H02
    if (src_type == id_WIRE_TYPE_H01 && dst_type == id_WIRE_TYPE_H02) {
        toSameSideH1Hor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        dst_id - TILE_WIRE_H02W0701);
    }
    if (src_type == id_WIRE_TYPE_H02 && dst_type == id_WIRE_TYPE_H02) {
        toSameSideHor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                      src_id - TILE_WIRE_H02W0701);
    }
    if (src_type == id_WIRE_TYPE_H06 && dst_type == id_WIRE_TYPE_H02) {
        toSameSideHor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                      src_id - TILE_WIRE_H06W0303);
    }
    if (src_type == id_WIRE_TYPE_V01 && dst_type == id_WIRE_TYPE_H02) {
        if (y == src.location.y) {
            straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
        } else {
            toSameSideV1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                            dst_id - TILE_WIRE_H02W0701);
        }
    }
    if (src_type == id_WIRE_TYPE_V02 && dst_type == id_WIRE_TYPE_H02) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if (src_type == id_WIRE_TYPE_V06 && dst_type == id_WIRE_TYPE_H02) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }

    // To H06
    if (src_type == id_WIRE_TYPE_H01 && dst_type == id_WIRE_TYPE_H06) {
        toSameSideH1Hor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        dst_id - TILE_WIRE_H06W0303);
    }
    if (src_type == id_WIRE_TYPE_H02 && dst_type == id_WIRE_TYPE_H06) {
        toSameSideHor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                      src_id - TILE_WIRE_H02W0701);
    }
    if (src_type == id_WIRE_TYPE_H06 && dst_type == id_WIRE_TYPE_H06) {
        toSameSideHor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                      src_id - TILE_WIRE_H06W0303);
    }
    if (src_type == id_WIRE_TYPE_V01 && dst_type == id_WIRE_TYPE_H06) {
        if (y == src.location.y) {
            straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
        } else {
            toSameSideV1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                            dst_id - TILE_WIRE_H06W0303);
        }
    }
    if (src_type == id_WIRE_TYPE_V06 && dst_type == id_WIRE_TYPE_H06) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }

    // To V00
    if (src_type == id_WIRE_TYPE_V02 && dst_type == id_WIRE_TYPE_V00) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if (src_type == id_WIRE_TYPE_H02 && dst_type == id_WIRE_TYPE_V00) {
        toSameSideV1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        src_id - TILE_WIRE_H02W0701 + 20);
    }

    // To V01
    if (src_type == id_WIRE_TYPE_V06 && dst_type == id_WIRE_TYPE_V01) {
        toSameSideH1Hor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        dst_id - TILE_WIRE_V01N0001);
    }

    // To V02
    if (src_type == id_WIRE_TYPE_H01 && dst_type == id_WIRE_TYPE_V02) {
        if (x == src.location.x) {
            toSameSideH1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                            dst_id - TILE_WIRE_V02N0701);
        } else {
            straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
        }
    }
    if (src_type == id_WIRE_TYPE_H02 && dst_type == id_WIRE_TYPE_V02) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if (src_type == id_WIRE_TYPE_H06 && dst_type == id_WIRE_TYPE_V02) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if (src_type == id_WIRE_TYPE_V01 && dst_type == id_WIRE_TYPE_V02) {
        toSameSideH1Hor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        dst_id - TILE_WIRE_V02N0701);
    }
    if (src_type == id_WIRE_TYPE_V02 && dst_type == id_WIRE_TYPE_V02) {
        toSameSideVer(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                      src_id - TILE_WIRE_V02N0701);
    }
    if (src_type == id_WIRE_TYPE_V06 && dst_type == id_WIRE_TYPE_V02) {
        toSameSideVer(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                      src_id - TILE_WIRE_V06N0303);
    }

    // To V06
    if (src_type == id_WIRE_TYPE_H01 && dst_type == id_WIRE_TYPE_V06) {
        if (x == src.location.x) {
            toSameSideH1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                            dst_id - TILE_WIRE_V06N0303);
        } else {
            straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
        }
    }
    if (src_type == id_WIRE_TYPE_H06 && dst_type == id_WIRE_TYPE_V06) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if (src_type == id_WIRE_TYPE_V01 && dst_type == id_WIRE_TYPE_V06) {
        toSameSideH1Hor(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        dst_id - TILE_WIRE_V06N0303);
    }
    if (src_type == id_WIRE_TYPE_V02 && dst_type == id_WIRE_TYPE_V06) {
        toSameSideVer(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                      src_id - TILE_WIRE_V02N0701);
    }
    if (src_type == id_WIRE_TYPE_V06 && dst_type == id_WIRE_TYPE_V06) {
        toSameSideVer(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                      src_id - TILE_WIRE_V06N0303);
    }

    if (src_type == id_WIRE_TYPE_H00 && dst_type == id_WIRE_TYPE_NONE &&
        (dst_id >= TILE_WIRE_FCO && dst_id <= TILE_WIRE_FCI)) {
        toSameSideH1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style, dst_id - TILE_WIRE_FCO);
    }
    if (src_type == id_WIRE_TYPE_H00 && dst_type == id_WIRE_TYPE_NONE &&
        (dst_id >= TILE_WIRE_JCE0 && dst_id <= TILE_WIRE_JQ7)) {
        toSameSideH1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        dst_id - TILE_WIRE_JCE0);
    }
    if (src_type == id_WIRE_TYPE_H01 && dst_type == id_WIRE_TYPE_NONE &&
        (dst_id >= TILE_WIRE_FCO && dst_id <= TILE_WIRE_FCI)) {
        toSameSideH1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style, dst_id - TILE_WIRE_FCO);
    }
    if (src_type == id_WIRE_TYPE_H01 && dst_type == id_WIRE_TYPE_NONE &&
        (dst_id >= TILE_WIRE_JCE0 && dst_id <= TILE_WIRE_JQ7)) {
        toSameSideH1Ver(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style,
                        dst_id - TILE_WIRE_JCE0);
    }

    if ((src_type.in(id_WIRE_TYPE_H02, id_WIRE_TYPE_V00, id_WIRE_TYPE_V01, id_WIRE_TYPE_V02)) &&
        dst_type == id_WIRE_TYPE_NONE &&
        ((dst_id >= TILE_WIRE_FCO && dst_id <= TILE_WIRE_FCI) ||
         (dst_id >= TILE_WIRE_JCE0 && dst_id <= TILE_WIRE_JQ7))) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if ((dst_type.in(id_WIRE_TYPE_H02, id_WIRE_TYPE_V00, id_WIRE_TYPE_V01, id_WIRE_TYPE_V02)) &&
        src_type == id_WIRE_TYPE_NONE &&
        ((src_id >= TILE_WIRE_FCO && src_id <= TILE_WIRE_FCI) ||
         (src_id >= TILE_WIRE_JCE0 && src_id <= TILE_WIRE_JQ7))) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }

    if (dst_type == id_WIRE_TYPE_NONE && (dst_id >= TILE_WIRE_FCO && dst_id <= TILE_WIRE_FCI) &&
        src_type == id_WIRE_TYPE_NONE && (src_id >= TILE_WIRE_FCO && src_id <= TILE_WIRE_FCI)) {
        toSameSideVer(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style, src_id - TILE_WIRE_FCO);
    }
    if (dst_type == id_WIRE_TYPE_NONE && (dst_id >= TILE_WIRE_JCE0 && dst_id <= TILE_WIRE_JCE0) &&
        src_type == id_WIRE_TYPE_NONE && (src_id >= TILE_WIRE_JCE0 && src_id <= TILE_WIRE_JCE0)) {
        toSameSideVer(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id, style, src_id - TILE_WIRE_JCE0);
    }

    if (dst_type == id_WIRE_TYPE_SLICE && src_type == id_WIRE_TYPE_NONE) {
        if (src_id >= TILE_WIRE_FCO && src_id <= TILE_WIRE_FCI && dst_id >= TILE_WIRE_FCO_SLICE &&
            dst_id <= TILE_WIRE_FCI_SLICE) {
            // LUT permutation pseudo-pip
            int src_purpose = (src_id - TILE_WIRE_FCO) % 24;
            int dst_purpose = (dst_id - TILE_WIRE_FCO_SLICE) % 24;
            if (src_purpose >= (TILE_WIRE_D7 - TILE_WIRE_FCO) && src_purpose <= (TILE_WIRE_A6 - TILE_WIRE_FCO) &&
                dst_purpose >= (TILE_WIRE_D7_SLICE - TILE_WIRE_FCO_SLICE) &&
                dst_purpose <= (TILE_WIRE_A6_SLICE - TILE_WIRE_FCO_SLICE)) {
                lutPermPip(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
            }
        }
    }

    if (src_type == id_WIRE_TYPE_NONE &&
        (dst_type.in(id_WIRE_TYPE_PLL, id_WIRE_TYPE_GSR, id_WIRE_TYPE_JTAG, id_WIRE_TYPE_OSC, id_WIRE_TYPE_SED,
                     id_WIRE_TYPE_DTR, id_WIRE_TYPE_EXTREF, id_WIRE_TYPE_DCU, id_WIRE_TYPE_PCSCLKDIV,
                     id_WIRE_TYPE_DDRDLL, id_WIRE_TYPE_CCLK, id_WIRE_TYPE_DQS, id_WIRE_TYPE_IOLOGIC,
                     id_WIRE_TYPE_SIOLOGIC, id_WIRE_TYPE_EBR, id_WIRE_TYPE_MULT18, id_WIRE_TYPE_ALU54)) &&
        (src_id >= TILE_WIRE_JCE0 && src_id <= TILE_WIRE_JQ7)) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if (dst_type == id_WIRE_TYPE_NONE &&
        (src_type.in(id_WIRE_TYPE_PLL, id_WIRE_TYPE_GSR, id_WIRE_TYPE_JTAG, id_WIRE_TYPE_OSC, id_WIRE_TYPE_SED,
                     id_WIRE_TYPE_DTR, id_WIRE_TYPE_EXTREF, id_WIRE_TYPE_DCU, id_WIRE_TYPE_PCSCLKDIV,
                     id_WIRE_TYPE_DDRDLL, id_WIRE_TYPE_CCLK, id_WIRE_TYPE_DQS, id_WIRE_TYPE_IOLOGIC,
                     id_WIRE_TYPE_SIOLOGIC, id_WIRE_TYPE_EBR, id_WIRE_TYPE_MULT18, id_WIRE_TYPE_ALU54)) &&
        (dst_id >= TILE_WIRE_JCE0 && dst_id <= TILE_WIRE_JQ7)) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }

    if (src_type == id_WIRE_TYPE_NONE && (dst_type.in(id_WIRE_TYPE_IOLOGIC, id_WIRE_TYPE_SIOLOGIC, id_WIRE_TYPE_PIO)) &&
        (src_id >= TILE_WIRE_JDIA && src_id <= TILE_WIRE_ECLKD)) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if (dst_type == id_WIRE_TYPE_NONE && (src_type.in(id_WIRE_TYPE_IOLOGIC, id_WIRE_TYPE_SIOLOGIC, id_WIRE_TYPE_PIO)) &&
        (dst_id >= TILE_WIRE_JDIA && dst_id <= TILE_WIRE_ECLKD)) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if (src_type == id_WIRE_TYPE_NONE && dst_type == id_WIRE_TYPE_NONE &&
        (src_id >= TILE_WIRE_JDIA && src_id <= TILE_WIRE_ECLKD) &&
        (dst_id >= TILE_WIRE_JCE0 && dst_id <= TILE_WIRE_JQ7)) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if (dst_type == id_WIRE_TYPE_NONE && src_type == id_WIRE_TYPE_NONE &&
        (dst_id >= TILE_WIRE_JDIA && dst_id <= TILE_WIRE_ECLKD) &&
        (src_id >= TILE_WIRE_JCE0 && src_id <= TILE_WIRE_JQ7)) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }

    if (dst_type == id_WIRE_TYPE_NONE && src_type == id_WIRE_TYPE_G_HPBX &&
        ((dst_id >= TILE_WIRE_JCE0 && dst_id <= TILE_WIRE_JQ7) ||
         (dst_id >= TILE_WIRE_CLK0 && dst_id <= TILE_WIRE_FCI))) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
    if ((dst_type.in(id_WIRE_TYPE_H01, id_WIRE_TYPE_V01)) && src_type == id_WIRE_TYPE_G_HPBX) {
        straightLine(g, el, x, y, w, h, src, src_type, src_id, dst, dst_type, dst_id);
    }
}

NEXTPNR_NAMESPACE_END

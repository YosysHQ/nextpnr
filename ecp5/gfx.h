/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#ifndef ECP5_GFX_H
#define ECP5_GFX_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

const float switchbox_x1 = 0.51;
const float switchbox_x2 = 0.90;
const float switchbox_y1 = 0.51;
const float switchbox_y2 = 0.90;

const float slice_x1 = 0.92;
const float slice_x2 = 0.94;
const float slice_y1 = 0.71;
const float slice_y2 = 0.745;
const float slice_pitch = 0.04;

const float io_cell_v_x1 = 0.76;
const float io_cell_v_x2 = 0.95;
const float io_cell_v_y1 = 0.05;
const float io_cell_v_y2 = 0.15;
const float io_cell_v_pitch = 0.125;

const float io_cell_h_x1 = 0.05;
const float io_cell_h_x2 = 0.14;
const float io_cell_h_y1 = 0.05;
const float io_cell_h_y2 = 0.24;
const float io_cell_h_pitch = 0.125;

enum GfxTileWireId
{
    TILE_WIRE_NONE,

    TILE_WIRE_D7_SLICE,
    TILE_WIRE_C7_SLICE,
    TILE_WIRE_B7_SLICE,
    TILE_WIRE_A7_SLICE,
    TILE_WIRE_D6_SLICE,
    TILE_WIRE_C6_SLICE,
    TILE_WIRE_B6_SLICE,
    TILE_WIRE_A6_SLICE,
    TILE_WIRE_DI7_SLICE,
    TILE_WIRE_DI6_SLICE,
    TILE_WIRE_M7_SLICE,
    TILE_WIRE_M6_SLICE,
    TILE_WIRE_FXBD_SLICE,
    TILE_WIRE_FXAD_SLICE,
    TILE_WIRE_WRE3_SLICE_DUMMY,
    TILE_WIRE_WCK3_SLICE_DUMMY,
    TILE_WIRE_CE3_SLICE,
    TILE_WIRE_LSR3_SLICE,
    TILE_WIRE_CLK3_SLICE,

    TILE_WIRE_D5_SLICE,
    TILE_WIRE_C5_SLICE,
    TILE_WIRE_B5_SLICE,
    TILE_WIRE_A5_SLICE,
    TILE_WIRE_D4_SLICE,
    TILE_WIRE_C4_SLICE,
    TILE_WIRE_B4_SLICE,
    TILE_WIRE_A4_SLICE,
    TILE_WIRE_DI5_SLICE,
    TILE_WIRE_DI4_SLICE,
    TILE_WIRE_M5_SLICE,
    TILE_WIRE_M4_SLICE,
    TILE_WIRE_FXBC_SLICE,
    TILE_WIRE_FXAC_SLICE,
    TILE_WIRE_WRE2_SLICE_DUMMY,
    TILE_WIRE_WCK2_SLICE_DUMMY,
    TILE_WIRE_CE2_SLICE,
    TILE_WIRE_LSR2_SLICE,
    TILE_WIRE_CLK2_SLICE,

    TILE_WIRE_D3_SLICE,
    TILE_WIRE_C3_SLICE,
    TILE_WIRE_B3_SLICE,
    TILE_WIRE_A3_SLICE,
    TILE_WIRE_D2_SLICE,
    TILE_WIRE_C2_SLICE,
    TILE_WIRE_B2_SLICE,
    TILE_WIRE_A2_SLICE,
    TILE_WIRE_DI3_SLICE,
    TILE_WIRE_DI2_SLICE,
    TILE_WIRE_M3_SLICE,
    TILE_WIRE_M2_SLICE,
    TILE_WIRE_FXBB_SLICE,
    TILE_WIRE_FXAB_SLICE,
    TILE_WIRE_WRE1_SLICE,
    TILE_WIRE_WCK1_SLICE,
    TILE_WIRE_CE1_SLICE,
    TILE_WIRE_LSR1_SLICE,
    TILE_WIRE_CLK1_SLICE,

    TILE_WIRE_D1_SLICE,
    TILE_WIRE_C1_SLICE,
    TILE_WIRE_B1_SLICE,
    TILE_WIRE_A1_SLICE,
    TILE_WIRE_D0_SLICE,
    TILE_WIRE_C0_SLICE,
    TILE_WIRE_B0_SLICE,
    TILE_WIRE_A0_SLICE,
    TILE_WIRE_DI1_SLICE,
    TILE_WIRE_DI0_SLICE,
    TILE_WIRE_M1_SLICE,
    TILE_WIRE_M0_SLICE,
    TILE_WIRE_FXBA_SLICE,
    TILE_WIRE_FXAA_SLICE,
    TILE_WIRE_WRE0_SLICE,
    TILE_WIRE_WCK0_SLICE,
    TILE_WIRE_CE0_SLICE,
    TILE_WIRE_LSR0_SLICE,
    TILE_WIRE_CLK0_SLICE
};

NEXTPNR_NAMESPACE_END

#endif

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#ifndef GATEMATE_EXTRA_DATA_H
#define GATEMATE_EXTRA_DATA_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

NPNR_PACKED_STRUCT(struct GateMateTileExtraDataPOD {
    uint8_t die;
    uint8_t bit_x;
    uint8_t bit_y;
    uint8_t tile_x;
    uint8_t tile_y;
    uint8_t prim_id;
    uint16_t dummy;
});

NPNR_PACKED_STRUCT(struct GateMatePipExtraDataPOD {
    int32_t name;
    uint8_t bits;
    uint8_t value;
    uint8_t flags;
    uint8_t type;
    uint8_t plane;
    uint8_t dummy1;
    uint16_t dummy2;
    uint32_t data;
    uint32_t mask;
    int32_t resource;
});

NPNR_PACKED_STRUCT(struct GateMateBelPinConstraintPOD {
    int32_t name;
    int16_t constr_x;
    int16_t constr_y;
    int16_t constr_z;
    int16_t dummy;
});

NPNR_PACKED_STRUCT(struct GateMateBelExtraDataPOD { RelSlice<GateMateBelPinConstraintPOD> constraints; });

NPNR_PACKED_STRUCT(struct GateMatePadExtraDataPOD {
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t dummy;
});

NPNR_PACKED_STRUCT(struct GateMateTimingExtraDataPOD {
    int32_t name;
    TimingValue delay;
});

NPNR_PACKED_STRUCT(struct GateMateSpeedGradeExtraDataPOD { RelSlice<GateMateTimingExtraDataPOD> timings; });

NPNR_PACKED_STRUCT(struct GateMateDieRegionPOD {
    int32_t name;
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
});

NPNR_PACKED_STRUCT(struct GateMateChipExtraDataPOD { RelSlice<GateMateDieRegionPOD> dies; });

enum MuxFlags
{
    MUX_INVERT = 1,
    MUX_VISIBLE = 2,
    MUX_CONFIG = 4,
    MUX_ROUTING = 8,
};

enum PipExtra
{
    PIP_EXTRA_MUX = 1,
};

enum CPEFunction
{
    C_ADDF = 1,
    C_ADDF2 = 2,
    C_MULT = 3,
    C_MX4 = 4,
    C_EN_CIN = 5,
    C_CONCAT = 6,
    C_ADDCIN = 7,
};

enum CPE_Z
{
    CPE_LT_U_Z = 0,
    CPE_LT_L_Z = 1,
    CPE_FF_U_Z = 2,
    CPE_FF_L_Z = 3,
    CPE_RAMIO_U_Z = 4,
    CPE_RAMIO_L_Z = 5,
    CPE_COMP_Z = 6,
    CPE_CPLINES_Z = 7,
    CPE_LT_FULL_Z = 8,
    CPE_BRIDGE_Z = 9,
    RAM_FULL_Z = 10,
    RAM_HALF_L_Z = 11,
};

enum ClusterPlacement
{
    PLACE_DB_CONSTR = 32,
};

enum PipMask
{
    IS_MULT = 1 << 0,
    IS_ADDF = 1 << 1,
    IS_COMP = 1 << 2,
    C_SELX = 1 << 3,
    C_SELY1 = 1 << 4,
    C_SELY2 = 1 << 5,
    C_SEL_C = 1 << 6,
    C_SEL_P = 1 << 7,
    C_Y12 = 1 << 8,
    C_CX_I = 1 << 9,
    C_CY1_I = 1 << 10,
    C_CY2_I = 1 << 11,
    C_PX_I = 1 << 12,
    C_PY1_I = 1 << 13,
    C_PY2_I = 1 << 14,
};

struct PllCfgRecord
{
    double weight;
    double f_core;
    double f_dco;
    double f_core_delta;
    double core_weight;
    int32_t K;
    int32_t N1;
    int32_t N2;
    int32_t M1;
    int32_t M2;
    int32_t PDIV1;
};

NEXTPNR_NAMESPACE_END

#endif

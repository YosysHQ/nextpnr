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
    uint8_t prim_id;
});

NPNR_PACKED_STRUCT(struct GateMatePipExtraDataPOD {
    int32_t name;
    uint8_t bits;
    uint8_t value;
    uint8_t flags;
    uint8_t type;
});

NPNR_PACKED_STRUCT(struct GateMateBelPinConstraintPOD {
    int32_t name;
    int16_t constr_x;
    int16_t constr_y;
    int16_t constr_z;
    int16_t dummy;
});

NPNR_PACKED_STRUCT(struct GateMateBelExtraDataPOD { RelSlice<GateMateBelPinConstraintPOD> constraints; });

enum MuxFlags
{
    MUX_INVERT = 1,
    MUX_VISIBLE = 2,
    MUX_CONFIG = 4,
    MUX_CPE_INV = 8,
};

enum PipExtra
{
    PIP_EXTRA_MUX = 1,
    PIP_EXTRA_CPE = 2,
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

enum ClusterPlacement
{
    PLACE_DB_CONSTR = 32,
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

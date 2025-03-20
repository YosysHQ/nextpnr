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

NPNR_PACKED_STRUCT(struct GateMateBelExtraDataPOD { int32_t flags; });

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

enum BelExtra
{
    BEL_EXTRA_GPIO_L = 1,
    BEL_EXTRA_GPIO_R = 2,
    BEL_EXTRA_GPIO_T = 4,
    BEL_EXTRA_GPIO_B = 8,    
};

enum CPEFunction
{
    C_ADDF   = 1,
    C_ADDF2  = 2,
    C_MULT   = 3,
    C_MX4    = 4,
    C_EN_CIN = 5,
    C_CONCAT = 6,
    C_ADDCIN = 7,
};

enum ClusterPlacement
{
    NO_PLACE = 0,
    PLACE_CPE_CLK0_OUT = 1024,
    PLACE_CPE_CLK90_OUT,
    PLACE_CPE_CLK180_OUT,
    PLACE_CPE_CLK270_OUT,
    PLACE_USR_GLB,
    PLACE_USR_FB,
    PLACE_USR_CLK_REF,
    PLACE_USR_LOCKED_STDY_RST,
    PLACE_USR_SEL_A_B,
    PLACE_USR_PLL_LOCKED,
    PLACE_USR_PLL_LOCKED_STDY,
    PLACE_GPIO_CPE_OUT1,
    PLACE_GPIO_CPE_OUT2,
    PLACE_GPIO_CPE_OUT3,
    PLACE_GPIO_CPE_OUT4,
    PLACE_USR_RSTN,
    PLACE_CFG_CTRL_RECFG,
    PLACE_CFG_CTRL_CLK,
    PLACE_CFG_CTRL_EN,
    PLACE_CFG_CTRL_VALID,
    PLACE_CFG_CTRL_DATA_0,
    PLACE_CFG_CTRL_DATA_1,
    PLACE_CFG_CTRL_DATA_2,
    PLACE_CFG_CTRL_DATA_3,
    PLACE_CFG_CTRL_DATA_4,
    PLACE_CFG_CTRL_DATA_5,
    PLACE_CFG_CTRL_DATA_6,
    PLACE_CFG_CTRL_DATA_7,
    PLACE_RAM_ADDRA0,
    PLACE_RAM_ADDRA1,
    PLACE_RAM_ADDRA2,
    PLACE_RAM_ADDRA3,
    PLACE_RAM_ADDRA4,
    PLACE_RAM_ADDRA5,
    PLACE_RAM_ADDRA6,
    PLACE_RAM_ADDRA7,
    PLACE_RAM_ADDRA8,
    PLACE_RAM_ADDRA9,
    PLACE_RAM_ADDRA10,
    PLACE_RAM_ADDRA11,
    PLACE_RAM_ADDRA12,
    PLACE_RAM_ADDRA13,
    PLACE_RAM_ADDRA14,
    PLACE_RAM_ADDRA15,
    PLACE_RAM_DOA0,
    PLACE_RAM_DOA1,
    PLACE_RAM_DOA2,
    PLACE_RAM_DOA3,
    PLACE_RAM_DOA4,
    PLACE_RAM_DOA5,
    PLACE_RAM_DOA6,
    PLACE_RAM_DOA7,
    PLACE_RAM_DOA8,
    PLACE_RAM_DOA9,
    PLACE_RAM_DOA10,
    PLACE_RAM_DOA11,
    PLACE_RAM_DOA12,
    PLACE_RAM_DOA13,
    PLACE_RAM_DOA14,
    PLACE_RAM_DOA15,
    PLACE_RAM_DOA16,
    PLACE_RAM_DOA17,
    PLACE_RAM_DOA18,
    PLACE_RAM_DOA19,
    PLACE_RAM_DOA20,
    PLACE_RAM_DOA21,
    PLACE_RAM_DOA22,
    PLACE_RAM_DOA23,
    PLACE_RAM_DOA24,
    PLACE_RAM_DOA25,
    PLACE_RAM_DOA26,
    PLACE_RAM_DOA27,
    PLACE_RAM_DOA28,
    PLACE_RAM_DOA29,
    PLACE_RAM_DOA30,
    PLACE_RAM_DOA31,
    PLACE_RAM_DOA32,
    PLACE_RAM_DOA33,
    PLACE_RAM_DOA34,
    PLACE_RAM_DOA35,
    PLACE_RAM_DOA36,
    PLACE_RAM_DOA37,
    PLACE_RAM_DOA38,
    PLACE_RAM_DOA39,
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

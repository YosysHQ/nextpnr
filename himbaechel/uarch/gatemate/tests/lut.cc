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

#include <vector>
#include "command.h"
#include "testing.h"
#include "uarch/gatemate/pack.h"
#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

USING_NEXTPNR_NAMESPACE

TEST_F(GateMateTest, pack_constants)
{
    GateMatePacker packer(ctx, impl);
    packer.pack_constants();
    ASSERT_EQ(ctx->cells.size(), 2LU);
    packer.remove_constants();
    ASSERT_EQ(ctx->cells.size(), 0LU);
}

// LUT[1:0]	Function
// ==============================
// 00       Constant 0
// 01       NOT A (inverts input)
// 10       A (passes input)
// 11       Constant 1

TEST_F(GateMateTest, remove_lut1_zero)
{
    CellInfo *lut1 = create_cell_ptr(id_CC_LUT1, "lut");
    lut1->params[id_INIT] = Property(0b00, 2);

    CellInfo *obuf = create_cell_ptr(id_CC_OBUF, "obuf");

    direct_connect(lut1, id_O, obuf, id_A);

    ASSERT_EQ(ctx->cells.size(), 1LU);
    ctx->uarch->pack();
    ASSERT_EQ(ctx->cells.size(), 1LU);
}

TEST_F(GateMateTest, remove_lut1_one)
{
    CellInfo *lut1 = create_cell_ptr(id_CC_LUT1, "lut");
    lut1->params[id_INIT] = Property(0b11, 2);

    CellInfo *obuf = create_cell_ptr(id_CC_OBUF, "obuf");

    direct_connect(lut1, id_O, obuf, id_A);

    ASSERT_EQ(ctx->cells.size(), 2LU);
    ctx->uarch->pack();
    ASSERT_EQ(ctx->cells.size(), 1LU);
}

TEST_F(GateMateTest, remove_lut1_pass)
{
    CellInfo *lut1 = create_cell_ptr(id_CC_LUT1, "lut");
    lut1->params[id_INIT] = Property(0b10, 2);

    CellInfo *obuf = create_cell_ptr(id_CC_OBUF, "obuf");
    CellInfo *ibuf = create_cell_ptr(id_CC_IBUF, "ibuf");

    direct_connect(ibuf, id_Y, lut1, id_I0);
    direct_connect(lut1, id_O, obuf, id_A);

    ASSERT_EQ(ctx->cells.size(), 3LU);
    ctx->uarch->pack();
    // Expect IBUF -> CPE -> OBUF
    // LUT removed, but CPE for driving OBUF added
    ASSERT_EQ(ctx->cells.size(), 3LU);
}

TEST_F(GateMateTest, remove_lut1_inv)
{
    CellInfo *lut1 = create_cell_ptr(id_CC_LUT1, "lut");
    lut1->params[id_INIT] = Property(0b01, 2);

    CellInfo *obuf = create_cell_ptr(id_CC_OBUF, "obuf");
    CellInfo *ibuf = create_cell_ptr(id_CC_IBUF, "ibuf");

    direct_connect(ibuf, id_Y, lut1, id_I0);
    direct_connect(lut1, id_O, obuf, id_A);

    ASSERT_EQ(ctx->cells.size(), 3LU);
    ctx->uarch->pack();
    // Expect IBUF -> CPE -> OBUF
    // LUT merged, but CPE for driving OBUF added
    ASSERT_EQ(ctx->cells.size(), 3LU);
}

TEST_F(GateMateTest, remove_lut1_not_driven)
{
    CellInfo *lut1 = create_cell_ptr(id_CC_LUT1, "lut");
    lut1->params[id_INIT] = Property(0b01, 2);

    CellInfo *obuf = create_cell_ptr(id_CC_OBUF, "obuf");
    CellInfo *ibuf = create_cell_ptr(id_CC_IBUF, "ibuf");

    NetInfo *net_in = ctx->createNet(ctx->id("in"));
    ibuf->connectPort(id_Y, net_in);
    lut1->connectPort(id_I0, net_in);
    obuf->connectPort(id_A, net_in);

    ASSERT_EQ(ctx->cells.size(), 3LU);
    ctx->uarch->pack();
    // Expect IBUF -> CPE -> OBUF
    // LUT1 removed as not used, but CPE for driving OBUF added
    ASSERT_EQ(ctx->cells.size(), 3LU);
}

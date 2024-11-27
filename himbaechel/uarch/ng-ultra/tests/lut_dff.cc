/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Beyond Authors.
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
#include "gtest/gtest.h"
#include "nextpnr.h"
#include "command.h"
#include "uarch/ng-ultra/ng_ultra.h"
#include "uarch/ng-ultra/pack.h"
#define HIMBAECHEL_CONSTIDS "uarch/ng-ultra/constids.inc"
#include "himbaechel_constids.h"

USING_NEXTPNR_NAMESPACE

class NGUltraLutDffTest : public ::testing::Test
{
  protected:
    virtual void SetUp()
    {
        init_share_dirname();
        chipArgs.device = "NG-ULTRA";
        ctx = new Context(chipArgs);
        ctx->uarch->init(ctx);
        ctx->late_init();
        impl = (NgUltraImpl*)(ctx->uarch.get());
    }

    virtual void TearDown() { delete ctx; }

    int const_autoidx = 0;
    NetInfo* add_constant_driver(const char *name, char constval)
    {
        IdString cell_name = ctx->idf("%s%s%d", name, (constval == '1' ? "$VCC$" : "$GND$"), const_autoidx++);
        CellInfo *cc = ctx->createCell(cell_name, ctx->id(constval == '1' ? "VCC" : "GND"));
        cc->ports[ctx->id("Y")].name = ctx->id("Y");
        cc->ports[ctx->id("Y")].type = PORT_OUT;
        NetInfo *net = ctx->createNet(cell_name);
        cc->connectPort(ctx->id("Y"), net);
        return net;
    }

    void add_port(CellInfo *cell, const std::string &name, PortType dir)
    {
        IdString id = ctx->id(name);
        cell->ports[id].name = id;
        cell->ports[id].type = dir;
    };

    int evaluate_lut(int I1, int I2, int I3, int I4, int lut_table)
    {
        int S1 = I4 ? (lut_table >> 8) & 0xff : lut_table & 0xff;
        int S2 = I3 ? (S1 >> 4 & 0xf) : S1 & 0xf;
        int S3 = I2 ? (S2 >> 2 & 0x3) : S2 & 0x3;
        int O  = I1 ? (S3 >> 1 & 0x1) : S3 & 0x1;
        return O;
    }

    ArchArgs chipArgs;
    Context *ctx;
    NgUltraImpl *impl;
};

TEST_F(NGUltraLutDffTest, pack_constants)
{
    NgUltraPacker packer(ctx, impl);
    packer.pack_constants();
    ASSERT_EQ(ctx->cells.size(), 2LU);
}

TEST_F(NGUltraLutDffTest, remove_constants)
{
    NgUltraPacker packer(ctx, impl);
    packer.pack_constants();
    impl->remove_constants();
    ASSERT_EQ(ctx->cells.size(), 0LU);
}

TEST_F(NGUltraLutDffTest, remove_unused_gnd)
{
    NgUltraPacker packer(ctx, impl);
    CellInfo *cell = ctx->createCell(ctx->id("TEST"), id_NX_LUT);
    add_port(cell, "I1", PORT_IN);
    add_port(cell, "I2", PORT_IN);
    add_port(cell, "I3", PORT_IN);
    add_port(cell, "I4", PORT_IN);
    cell->connectPort(id_I1, add_constant_driver("TEST",'1'));
    cell->connectPort(id_I2, add_constant_driver("TEST",'1'));
    cell->connectPort(id_I3, add_constant_driver("TEST",'1'));

    ASSERT_EQ(ctx->cells.size(), 4LU);
    packer.pack_constants();
    ASSERT_EQ(ctx->cells.size(), 3LU);
    impl->remove_constants();
    ASSERT_EQ(ctx->cells.size(), 2LU);
    ASSERT_EQ(ctx->cells.find(ctx->id("$PACKER_GND_DRV")),ctx->cells.end());
    ASSERT_NE(ctx->cells.find(ctx->id("$PACKER_VCC_DRV")),ctx->cells.end());
    ASSERT_EQ(ctx->nets.find(ctx->id("$PACKER_GND")),ctx->nets.end());
    ASSERT_NE(ctx->nets.find(ctx->id("$PACKER_VCC")),ctx->nets.end());
}

TEST_F(NGUltraLutDffTest, remove_unused_vcc)
{
    NgUltraPacker packer(ctx, impl);
    CellInfo *cell = ctx->createCell(ctx->id("TEST"), id_NX_LUT);
    add_port(cell, "I1", PORT_IN);
    add_port(cell, "I2", PORT_IN);
    add_port(cell, "I3", PORT_IN);
    add_port(cell, "I4", PORT_IN);
    cell->connectPort(id_I1, add_constant_driver("TEST",'0'));
    cell->connectPort(id_I2, add_constant_driver("TEST",'0'));
    cell->connectPort(id_I3, add_constant_driver("TEST",'0'));

    ASSERT_EQ(ctx->cells.size(), 4LU);
    packer.pack_constants();
    ASSERT_EQ(ctx->cells.size(), 3LU);
    impl->remove_constants();
    ASSERT_EQ(ctx->cells.size(), 2LU);
    ASSERT_NE(ctx->cells.find(ctx->id("$PACKER_GND_DRV")),ctx->cells.end());
    ASSERT_EQ(ctx->cells.find(ctx->id("$PACKER_VCC_DRV")),ctx->cells.end());
    ASSERT_NE(ctx->nets.find(ctx->id("$PACKER_GND")),ctx->nets.end());
    ASSERT_EQ(ctx->nets.find(ctx->id("$PACKER_VCC")),ctx->nets.end());
}

TEST_F(NGUltraLutDffTest, make_init_with_const_input)
{
    NgUltraPacker packer(ctx, impl);
    for (int lut_table=0;lut_table<0x10000;lut_table++) {
        for(int lut=0;lut<16;lut++) {
            int I4 = (lut & 8) ? 1 : 0;
            int I3 = (lut & 4) ? 1 : 0;
            int I2 = (lut & 2) ? 1 : 0;
            int I1 = (lut & 1) ? 1 : 0;

            int tab1 = packer.make_init_with_const_input(lut_table, 0, I1);
            int tab2 = packer.make_init_with_const_input(tab1, 1, I2);
            int tab3 = packer.make_init_with_const_input(tab2, 2, I3);
            int tab4 = packer.make_init_with_const_input(tab3, 3, I4);

            ASSERT_EQ(evaluate_lut(I1,I2,I3,I4,lut_table),evaluate_lut(I1,I2,I3,I4,tab1));
            ASSERT_EQ(evaluate_lut(I1,I2,I3,I4,lut_table),evaluate_lut(I1,I2,I3,I4,tab2));
            ASSERT_EQ(evaluate_lut(I1,I2,I3,I4,lut_table),evaluate_lut(I1,I2,I3,I4,tab3));
            ASSERT_EQ(evaluate_lut(I1,I2,I3,I4,lut_table),evaluate_lut(I1,I2,I3,I4,tab4));
        }
    }
}

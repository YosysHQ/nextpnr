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

#include "testing.h"
#include <vector>
#include "command.h"
#include "uarch/gatemate/pack.h"
#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

void GateMateTest::SetUp()
{
    init_share_dirname();
    chipArgs.device = "CCGM1A1";
    chipArgs.options.emplace("allow-unconstrained", "");
    ctx = new Context(chipArgs);
    ctx->uarch->init(ctx);
    ctx->late_init();
    impl = (GateMateImpl *)(ctx->uarch.get());
}

void GateMateTest::TearDown() { delete ctx; }

CellInfo *GateMateTest::create_cell_ptr(IdString type, std::string name)
{
    CellInfo *cell = ctx->createCell(ctx->id(name), type);

    auto add_port = [&](const IdString id, PortType dir) {
        cell->ports[id].name = id;
        cell->ports[id].type = dir;
    };
    switch (type.index) {
    case id_CC_IBUF.index:
        add_port(id_I, PORT_IN);
        add_port(id_Y, PORT_OUT);
        break;
    case id_CC_OBUF.index:
        add_port(id_A, PORT_IN);
        add_port(id_O, PORT_OUT);
        break;
    case id_CC_TOBUF.index:
        add_port(id_A, PORT_IN);
        add_port(id_T, PORT_IN);
        add_port(id_O, PORT_OUT);
        break;
    case id_CC_IOBUF.index:
        add_port(id_A, PORT_IN);
        add_port(id_T, PORT_IN);
        add_port(id_Y, PORT_OUT);
        add_port(id_IO, PORT_INOUT);
        break;
    case id_CC_LVDS_IBUF.index:
        add_port(id_I_P, PORT_IN);
        add_port(id_I_N, PORT_IN);
        add_port(id_Y, PORT_OUT);
        break;
    case id_CC_LVDS_OBUF.index:
        add_port(id_A, PORT_IN);
        add_port(id_O_P, PORT_OUT);
        add_port(id_O_N, PORT_OUT);
        break;
    case id_CC_LVDS_TOBUF.index:
        add_port(id_A, PORT_IN);
        add_port(id_T, PORT_IN);
        add_port(id_O_P, PORT_OUT);
        add_port(id_O_N, PORT_OUT);
        break;
    case id_CC_LVDS_IOBUF.index:
        add_port(id_A, PORT_IN);
        add_port(id_T, PORT_IN);
        add_port(id_Y, PORT_OUT);
        add_port(id_IO_P, PORT_INOUT);
        add_port(id_IO_N, PORT_INOUT);
        break;
    case id_CC_IDDR.index:
        add_port(id_D, PORT_IN);
        add_port(id_CLK, PORT_IN);
        add_port(id_Q0, PORT_OUT);
        add_port(id_Q1, PORT_OUT);
        break;
    case id_CC_ODDR.index:
        add_port(id_D0, PORT_IN);
        add_port(id_D1, PORT_IN);
        add_port(id_CLK, PORT_IN);
        add_port(id_DDR, PORT_IN);
        add_port(id_Q, PORT_OUT);
        break;
    case id_CC_DFF.index:
        add_port(id_D, PORT_IN);
        add_port(id_CLK, PORT_IN);
        add_port(id_EN, PORT_IN);
        add_port(id_SR, PORT_IN);
        add_port(id_Q, PORT_OUT);
        break;
    case id_CC_DLT.index:
        add_port(id_D, PORT_IN);
        add_port(id_G, PORT_IN);
        add_port(id_SR, PORT_IN);
        add_port(id_Q, PORT_OUT);
        break;
    case id_CC_L2T4.index:
        add_port(id_I0, PORT_IN);
        add_port(id_I1, PORT_IN);
        add_port(id_I2, PORT_IN);
        add_port(id_I3, PORT_IN);
        add_port(id_O, PORT_OUT);
        break;
    case id_CC_L2T5.index:
        add_port(id_I0, PORT_IN);
        add_port(id_I1, PORT_IN);
        add_port(id_I2, PORT_IN);
        add_port(id_I3, PORT_IN);
        add_port(id_I4, PORT_IN);
        add_port(id_O, PORT_OUT);
        break;
    case id_CC_LUT1.index:
        add_port(id_I0, PORT_IN);
        add_port(id_O, PORT_OUT);
        break;
    case id_CC_LUT2.index:
        add_port(id_I0, PORT_IN);
        add_port(id_I1, PORT_IN);
        add_port(id_O, PORT_OUT);
        break;
    case id_CC_MX2.index:
        add_port(id_D0, PORT_IN);
        add_port(id_D1, PORT_IN);
        add_port(id_S0, PORT_IN);
        add_port(id_Y, PORT_OUT);
        break;
    case id_CC_MX4.index:
        add_port(id_D0, PORT_IN);
        add_port(id_D1, PORT_IN);
        add_port(id_D2, PORT_IN);
        add_port(id_D3, PORT_IN);
        add_port(id_S0, PORT_IN);
        add_port(id_S1, PORT_IN);
        add_port(id_Y, PORT_OUT);
        break;
    case id_CC_ADDF.index:
        add_port(id_A, PORT_IN);
        add_port(id_B, PORT_IN);
        add_port(id_CI, PORT_IN);
        add_port(id_CO, PORT_OUT);
        add_port(id_S, PORT_OUT);
        break;
    case id_CC_BUFG.index:
        add_port(id_I, PORT_IN);
        add_port(id_O, PORT_OUT);
        break;
    case id_CC_USR_RSTN.index:
        add_port(id_USR_RSTN, PORT_OUT);
        break;
    case id_CC_PLL_ADV.index:
        add_port(id_USR_SEL_A_B, PORT_IN);
        [[fallthrough]];
    case id_CC_PLL.index:
        add_port(id_CLK_REF, PORT_IN);
        add_port(id_USR_CLK_REF, PORT_IN);
        add_port(id_CLK_FEEDBACK, PORT_IN);
        add_port(id_USR_LOCKED_STDY_RST, PORT_IN);
        add_port(id_USR_PLL_LOCKED_STDY, PORT_OUT);
        add_port(id_USR_PLL_LOCKED, PORT_OUT);
        add_port(id_CLK0, PORT_OUT);
        add_port(id_CLK90, PORT_OUT);
        add_port(id_CLK180, PORT_OUT);
        add_port(id_CLK270, PORT_OUT);
        add_port(id_CLK_REF_OUT, PORT_OUT);
        break;
    case id_CC_CFG_CTRL.index:
        for (int i = 0; i < 8; i++)
            add_port(ctx->idf("DATA[%d]", i), PORT_IN);
        add_port(id_CLK, PORT_IN);
        add_port(id_EN, PORT_IN);
        add_port(id_RECFG, PORT_IN);
        add_port(id_VALID, PORT_IN);
        break;

    default:
        log_error("Trying to create unknown cell type %s\n", type.c_str(ctx));
        break;
    }
    return cell;
}

void GateMateTest::direct_connect(CellInfo *o_cell, IdString o_port, CellInfo *i_cell, IdString i_port)
{
    NetInfo *net = ctx->createNet(ctx->idf("%s_%s", o_cell->name.c_str(ctx), o_port.c_str(ctx)));
    o_cell->connectPort(o_port, net);
    i_cell->connectPort(i_port, net);
}

NEXTPNR_NAMESPACE_END

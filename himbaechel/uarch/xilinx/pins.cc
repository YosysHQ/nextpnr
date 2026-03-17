/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  Myrtle Shah <gatecat@ds0.me>
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

#include <set>

#include "nextpnr.h"

#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

void get_invertible_pins(Context *ctx, dict<IdString, pool<IdString>> &invertible_pins)
{
    // List of pins that have an IS_x_INVERTED attributed, so we can optimise tie-zero to tie-one for these pins
    // See scripts/invertible_pins.py

    // Common and xcup
    invertible_pins[id_BUFGCTRL].insert(id_CE0);
    invertible_pins[id_BUFGCTRL].insert(id_CE1);
    invertible_pins[id_BUFGCTRL].insert(id_S0);
    invertible_pins[id_BUFGCTRL].insert(id_S1);
    invertible_pins[id_BUFGCTRL].insert(id_IGNORE0);
    invertible_pins[id_BUFGCTRL].insert(id_IGNORE1);
    invertible_pins[id_BUFHCE].insert(id_CE);
    invertible_pins[id_FDRE].insert(id_C);
    invertible_pins[id_FDSE].insert(id_C);
    invertible_pins[id_FDCE].insert(id_C);
    invertible_pins[id_FDPE].insert(id_C);

    invertible_pins[id_SRL16E].insert(id_CLK);
    invertible_pins[id_SRLC32E].insert(id_CLK);
    invertible_pins[id_BUFGCE].insert(id_CE);
    invertible_pins[id_BUFGCE].insert(id_I);
    invertible_pins[id_BUFGCE_DIV].insert(id_CE);
    invertible_pins[id_BUFGCE_DIV].insert(id_CLR);
    invertible_pins[id_BUFGCE_DIV].insert(id_I);
    invertible_pins[id_CFGLUT5].insert(id_CLK);
    invertible_pins[id_FIFO18E2].insert(id_RDCLK);
    invertible_pins[id_FIFO18E2].insert(id_RDEN);
    invertible_pins[id_FIFO18E2].insert(id_RSTREG);
    invertible_pins[id_FIFO18E2].insert(id_RST);
    invertible_pins[id_FIFO18E2].insert(id_WRCLK);
    invertible_pins[id_FIFO18E2].insert(id_WREN);
    invertible_pins[id_FIFO36E2].insert(id_RDCLK);
    invertible_pins[id_FIFO36E2].insert(id_RDEN);
    invertible_pins[id_FIFO36E2].insert(id_RSTREG);
    invertible_pins[id_FIFO36E2].insert(id_RST);
    invertible_pins[id_FIFO36E2].insert(id_WRCLK);
    invertible_pins[id_FIFO36E2].insert(id_WREN);
    invertible_pins[id_HARD_SYNC].insert(id_CLK);
    invertible_pins[id_IDDRE1].insert(id_CB);
    invertible_pins[id_IDDRE1].insert(id_C);

    invertible_pins[id_LDCE].insert(id_CLR);
    invertible_pins[id_LDCE].insert(id_G);
    invertible_pins[id_LDPE].insert(id_G);
    invertible_pins[id_LDPE].insert(id_PRE);
    invertible_pins[id_ODDRE1].insert(id_C);
    // invertible_pins[id_ODDRE1].insert(id_D1);
    // invertible_pins[id_ODDRE1].insert(id_D2);

    invertible_pins[id_OR2L].insert(id_SRI);

    // invertible_pins[id_OSERDESE3].insert(id_RST);
    invertible_pins[id_RAM128X1D].insert(id_WCLK);
    invertible_pins[id_RAM128X1S].insert(id_WCLK);
    invertible_pins[id_RAM256X1D].insert(id_WCLK);
    invertible_pins[id_RAM256X1S].insert(id_WCLK);
    invertible_pins[id_RAM32M].insert(id_WCLK);
    invertible_pins[id_RAM32M16].insert(id_WCLK);
    invertible_pins[id_RAM32X1D].insert(id_WCLK);
    invertible_pins[id_RAM32X1S].insert(id_WCLK);
    invertible_pins[id_RAM32X2S].insert(id_WCLK);
    invertible_pins[id_RAM512X1S].insert(id_WCLK);
    invertible_pins[id_RAM64M].insert(id_WCLK);
    invertible_pins[id_RAM64M8].insert(id_WCLK);
    invertible_pins[id_RAM64X1D].insert(id_WCLK);
    invertible_pins[id_RAM64X1S].insert(id_WCLK);
    invertible_pins[id_RAM64X8SW].insert(id_WCLK);

    invertible_pins[id_SYSMONE1].insert(id_CONVSTCLK);
    invertible_pins[id_SYSMONE1].insert(id_DCLK);

    // xc7
    invertible_pins[id_RAMB18E1].insert(id_CLKARDCLK);
    invertible_pins[id_RAMB18E1].insert(id_CLKBWRCLK);
    invertible_pins[id_RAMB18E1].insert(id_ENARDEN);
    invertible_pins[id_RAMB18E1].insert(id_ENBWREN);
    invertible_pins[id_RAMB18E1].insert(id_RSTRAMARSTRAM);
    invertible_pins[id_RAMB18E1].insert(id_RSTRAMB);
    invertible_pins[id_RAMB18E1].insert(id_RSTREGARSTREG);
    invertible_pins[id_RAMB18E1].insert(id_RSTREGB);
    invertible_pins[id_RAMB36E1].insert(id_CLKARDCLK);
    invertible_pins[id_RAMB36E1].insert(id_CLKBWRCLK);
    invertible_pins[id_RAMB36E1].insert(id_ENARDEN);
    invertible_pins[id_RAMB36E1].insert(id_ENBWREN);
    invertible_pins[id_RAMB36E1].insert(id_RSTRAMARSTRAM);
    invertible_pins[id_RAMB36E1].insert(id_RSTRAMB);
    invertible_pins[id_RAMB36E1].insert(id_RSTREGARSTREG);
    invertible_pins[id_RAMB36E1].insert(id_RSTREGB);
    invertible_pins[id_BUFMRCE].insert(id_CE);
    for (int i = 0; i < 4; i++)
        invertible_pins[id_DSP48E1].insert(ctx->idf("ALUMODE[%d]", i));
    invertible_pins[id_DSP48E1].insert(id_CARRYIN);
    for (int i = 0; i < 5; i++)
        invertible_pins[id_DSP48E1].insert(ctx->idf("INMODE[%d]", i));
    for (int i = 0; i < 7; i++)
        invertible_pins[id_DSP48E1].insert(ctx->idf("OPMODE[%d]", i));
    invertible_pins[id_FIFO18E1].insert(id_RDCLK);
    invertible_pins[id_FIFO18E1].insert(id_RDEN);
    invertible_pins[id_FIFO18E1].insert(id_RSTREG);
    invertible_pins[id_FIFO18E1].insert(id_RST);
    invertible_pins[id_FIFO18E1].insert(id_WRCLK);
    invertible_pins[id_FIFO18E1].insert(id_WREN);
    invertible_pins[id_FIFO36E1].insert(id_RDCLK);
    invertible_pins[id_FIFO36E1].insert(id_RDEN);
    invertible_pins[id_FIFO36E1].insert(id_RSTREG);
    invertible_pins[id_FIFO36E1].insert(id_RST);
    invertible_pins[id_FIFO36E1].insert(id_WRCLK);
    invertible_pins[id_FIFO36E1].insert(id_WREN);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_CLKRSVD0);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_CLKRSVD1);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_CPLLLOCKDETCLK);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_DMONITORCLK);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_DRPCLK);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_GTGREFCLK);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_RXUSRCLK2);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_RXUSRCLK);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_SIGVALIDCLK);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_TXPHDLYTSTCLK);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_TXUSRCLK2);
    invertible_pins[id_GTHE2_CHANNEL].insert(id_TXUSRCLK);
    invertible_pins[id_GTHE2_COMMON].insert(id_DRPCLK);
    invertible_pins[id_GTHE2_COMMON].insert(id_GTGREFCLK);
    invertible_pins[id_GTHE2_COMMON].insert(id_QPLLLOCKDETCLK);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_CLKRSVD0);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_CLKRSVD1);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_DMONITORCLK);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_DRPCLK);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_RXUSRCLK2);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_RXUSRCLK);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_SIGVALIDCLK);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_TXPHDLYTSTCLK);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_TXUSRCLK2);
    invertible_pins[id_GTPE2_CHANNEL].insert(id_TXUSRCLK);
    invertible_pins[id_GTPE2_COMMON].insert(id_DRPCLK);
    invertible_pins[id_GTPE2_COMMON].insert(id_GTGREFCLK0);
    invertible_pins[id_GTPE2_COMMON].insert(id_GTGREFCLK1);
    invertible_pins[id_GTPE2_COMMON].insert(id_PLL0LOCKDETCLK);
    invertible_pins[id_GTPE2_COMMON].insert(id_PLL1LOCKDETCLK);
    invertible_pins[id_GTXE2_CHANNEL].insert(id_CPLLLOCKDETCLK);
    invertible_pins[id_GTXE2_CHANNEL].insert(id_DRPCLK);
    invertible_pins[id_GTXE2_CHANNEL].insert(id_GTGREFCLK);
    invertible_pins[id_GTXE2_CHANNEL].insert(id_RXUSRCLK2);
    invertible_pins[id_GTXE2_CHANNEL].insert(id_RXUSRCLK);
    invertible_pins[id_GTXE2_CHANNEL].insert(id_TXPHDLYTSTCLK);
    invertible_pins[id_GTXE2_CHANNEL].insert(id_TXUSRCLK2);
    invertible_pins[id_GTXE2_CHANNEL].insert(id_TXUSRCLK);
    invertible_pins[id_GTXE2_COMMON].insert(id_DRPCLK);
    invertible_pins[id_GTXE2_COMMON].insert(id_GTGREFCLK);
    invertible_pins[id_GTXE2_COMMON].insert(id_QPLLLOCKDETCLK);
    invertible_pins[id_IDDR].insert(id_C);
    // invertible_pins[id_IDDR].insert(id_D);
    invertible_pins[id_IDDR_2CLK].insert(id_CB);
    invertible_pins[id_IDDR_2CLK].insert(id_C);
    // invertible_pins[id_IDDR_2CLK].insert(id_D);
    invertible_pins[id_IDELAYE2].insert(id_C);
    invertible_pins[id_IDELAYE2].insert(id_IDATAIN);
    invertible_pins[id_ODELAYE2].insert(id_C);
    invertible_pins[id_ODELAYE2].insert(id_ODATAIN);
    invertible_pins[id_ISERDESE2].insert(id_CLKB);
    invertible_pins[id_ISERDESE2].insert(id_CLKDIVP);
    invertible_pins[id_ISERDESE2].insert(id_CLKDIV);
    invertible_pins[id_ISERDESE2].insert(id_CLK);
    // invertible_pins[id_ISERDESE2].insert(id_D);
    invertible_pins[id_ISERDESE2].insert(id_OCLKB);
    invertible_pins[id_ISERDESE2].insert(id_OCLK);
    // invertible_pins[id_LDCE].insert(id_CLR);
    invertible_pins[id_LDCE].insert(id_G);
    invertible_pins[id_LDPE].insert(id_G);
    // invertible_pins[id_LDPE].insert(id_PRE);
    invertible_pins[id_MMCME2_ADV].insert(id_CLKINSEL);
    invertible_pins[id_MMCME2_ADV].insert(id_PSEN);
    invertible_pins[id_MMCME2_ADV].insert(id_PSINCDEC);
    invertible_pins[id_MMCME2_ADV].insert(id_PWRDWN);
    invertible_pins[id_MMCME2_ADV].insert(id_RST);
    invertible_pins[id_IDDR].insert(id_CK);
    invertible_pins[id_ODDR].insert(id_CK);
    invertible_pins[id_ODDR].insert(id_D1);
    invertible_pins[id_ODDR].insert(id_D2);
    invertible_pins[id_ODELAYE2].insert(id_C);
    // invertible_pins[id_ODELAYE2].insert(id_ODATAIN);
    invertible_pins[id_OSERDESE2].insert(id_CLKDIV);
    invertible_pins[id_OSERDESE2].insert(id_CLK);
    // invertible_pins[id_OSERDESE2].insert(id_D1);
    // invertible_pins[id_OSERDESE2].insert(id_D2);
    // invertible_pins[id_OSERDESE2].insert(id_D3);
    // invertible_pins[id_OSERDESE2].insert(id_D4);
    // invertible_pins[id_OSERDESE2].insert(id_D5);
    // invertible_pins[id_OSERDESE2].insert(id_D6);
    // invertible_pins[id_OSERDESE2].insert(id_D7);
    // invertible_pins[id_OSERDESE2].insert(id_D8);
    invertible_pins[id_OSERDESE2].insert(id_T1);
    invertible_pins[id_OSERDESE2].insert(id_T2);
    invertible_pins[id_OSERDESE2].insert(id_T3);
    invertible_pins[id_OSERDESE2].insert(id_T4);
    invertible_pins[id_PHASER_IN].insert(id_RST);
    invertible_pins[id_PHASER_IN_PHY].insert(id_RST);
    invertible_pins[id_PHASER_OUT].insert(id_RST);
    invertible_pins[id_PHASER_OUT_PHY].insert(id_RST);
    invertible_pins[id_PHASER_REF].insert(id_RST);
    invertible_pins[id_PHASER_REF].insert(id_PWRDWN);
    invertible_pins[id_PLLE2_ADV].insert(id_CLKINSEL);
    invertible_pins[id_PLLE2_ADV].insert(id_PWRDWN);
    invertible_pins[id_PLLE2_ADV].insert(id_RST);
    invertible_pins[id_XADC].insert(id_CONVSTCLK);
    invertible_pins[id_XADC].insert(id_DCLK);
}

void get_tied_pins(Context *ctx, dict<IdString, dict<IdString, bool>> &tied_pins)
{
    // List of pins that are tied to a fixed value when unused.
    // This doesn't include the PS8, due to the large number of tied-zero pins that are implied by the
    // list of Bel pins and dealt with as a special case in arch_place.cc

    for (IdString ram : {id_RAMB18E2, id_RAMB36E2}) {
        // based on UG573 p37
        for (char port : {'A', 'B'}) {
            tied_pins[ram][ctx->id(std::string("ADDREN") + port)] = true;
            tied_pins[ram][ctx->id(std::string("CASDIMUX") + port)] = false;
            tied_pins[ram][ctx->id(std::string("CASDOMUX") + port)] = false;
            if (ram == id_RAMB18E2) {
                tied_pins[ram][ctx->id(std::string("CASDOMUXEN_") + port)] = true;
                tied_pins[ram][ctx->id(std::string("CASOREGIMUXEN_") + port)] = true;
            }

            tied_pins[ram][ctx->id(std::string("CASOREGIMUX") + port)] = false;
        }

        int wea_width = (ram == id_RAMB18E2 ? 2 : 4);
        int web_width = 4;

        for (int i = 0; i < wea_width; i++)
            tied_pins[ram][ctx->id(std::string("WEA[") + std::to_string(i) + "]")] = true;
        for (int i = 0; i < web_width; i++)
            tied_pins[ram][ctx->id(std::string("WEBWE[") + std::to_string(i) + "]")] = true;

        tied_pins[ram][id_CLKARDCLK] = false;
        tied_pins[ram][id_CLKBWRCLK] = false;
        tied_pins[ram][id_ENARDEN] = false;
        tied_pins[ram][id_ENBWREN] = false;
        tied_pins[ram][id_REGCEAREGCE] = true;
        tied_pins[ram][id_REGCEB] = true;

        tied_pins[ram][id_RSTRAMARSTRAM] = false;
        tied_pins[ram][id_RSTRAMB] = false;
        tied_pins[ram][id_RSTREGARSTREG] = false;
        tied_pins[ram][id_RSTREGB] = false;
        tied_pins[ram][id_SLEEP] = false;

        if (ram == id_RAMB36E2) {
            tied_pins[ram][id_INJECTSBITERR] = false;
            tied_pins[ram][id_INJECTDBITERR] = false;
            tied_pins[ram][id_ECCPIPECE] = true;
        }
    }

    for (IdString ram : {id_RAMB18E1, id_RAMB36E1}) {
        // based on UG573 p37

        int wea_width = (ram == id_RAMB18E1 ? 2 : 4);
        int web_width = 4;

        for (int i = 0; i < wea_width; i++)
            tied_pins[ram][ctx->id(std::string("WEA[") + std::to_string(i) + "]")] = true;
        for (int i = 0; i < web_width; i++)
            tied_pins[ram][ctx->id(std::string("WEBWE[") + std::to_string(i) + "]")] = true;

        tied_pins[ram][id_CLKARDCLK] = false;
        tied_pins[ram][id_CLKBWRCLK] = false;
        tied_pins[ram][id_ENARDEN] = false;
        tied_pins[ram][id_ENBWREN] = false;
        tied_pins[ram][id_REGCEAREGCE] = true;
        tied_pins[ram][id_REGCEB] = true;

        tied_pins[ram][id_RSTRAMARSTRAM] = false;
        tied_pins[ram][id_RSTRAMB] = false;
        tied_pins[ram][id_RSTREGARSTREG] = false;
        tied_pins[ram][id_RSTREGB] = false;
    }

    // BUFGCTRL (by experiment)
    for (int i = 0; i < 2; i++) {
        tied_pins[id_BUFGCTRL][ctx->id("S" + std::to_string(i))] = false;
        tied_pins[id_BUFGCTRL][ctx->id("IGNORE" + std::to_string(i))] = false;
        tied_pins[id_BUFGCTRL][ctx->id("CE" + std::to_string(i))] = false;
    }

    // IO logic primitives
    tied_pins[id_IDDRE1][id_R] = false;
    tied_pins[id_ODDRE1][id_SR] = false;

    tied_pins[id_OSERDESE2][id_RST] = false;
    for (int i = 1; i <= 8; i++)
        tied_pins[id_OSERDESE2][ctx->id("D" + std::to_string(i))] = false;
    for (int i = 1; i <= 4; i++)
        tied_pins[id_OSERDESE2][ctx->id("T" + std::to_string(i))] = false;
    tied_pins[id_OSERDESE2][id_OCE] = true;
    tied_pins[id_OSERDESE2][id_TCE] = true;

    tied_pins[id_IDELAYE2][id_REGRST] = false;
    tied_pins[id_IDELAYE2][id_LDPIPEEN] = false;
    tied_pins[id_IDELAYE2][id_CINVCTRL] = false;

    // IO primitives
    tied_pins[id_IOBUFDSE3][id_DCITERMDISABLE] = false;
    tied_pins[id_IOBUFDSE3][ctx->id("OSC_EN[0]")] = false;
    tied_pins[id_IOBUFDSE3][ctx->id("OSC_EN[1]")] = false;
    for (int i = 0; i < 4; i++)
        tied_pins[id_IOBUFDSE3][ctx->id("OSC[" + std::to_string(i) + "]")] = false;

    // PLL
    tied_pins[id_PLLE2_ADV][id_CLKFBIN] = false;
    tied_pins[id_PLLE2_ADV][id_CLKIN1] = false;
    tied_pins[id_PLLE2_ADV][id_CLKIN2] = false;
    tied_pins[id_PLLE2_ADV][id_CLKINSEL] = true;
    for (int i = 0; i < 7; i++)
        tied_pins[id_PLLE2_ADV][ctx->id("DADDR[" + std::to_string(i) + "]")] = false;
    tied_pins[id_PLLE2_ADV][id_DCLK] = false;
    tied_pins[id_PLLE2_ADV][id_DEN] = false;
    for (int i = 0; i < 16; i++)
        tied_pins[id_PLLE2_ADV][ctx->id("DI[" + std::to_string(i) + "]")] = false;
    tied_pins[id_PLLE2_ADV][id_DWE] = false;
    tied_pins[id_PLLE2_ADV][id_PWRDWN] = false;
    tied_pins[id_PLLE2_ADV][id_RST] = false;

    // Misc clock buffers
    tied_pins[id_BUFGCE_DIV][id_CE] = true;
    tied_pins[id_BUFGCE_DIV][id_CLR] = false;
    tied_pins[id_BUFGCE][id_CE] = true;

    tied_pins[id_DSP48E1][id_CLK] = false;
    tied_pins[id_DSP48E1][id_RSTA] = false;
    tied_pins[id_DSP48E1][id_RSTALLCARRYIN] = false;
    tied_pins[id_DSP48E1][id_RSTALUMODE] = false;
    tied_pins[id_DSP48E1][id_RSTB] = false;
    tied_pins[id_DSP48E1][id_RSTC] = false;
    tied_pins[id_DSP48E1][id_RSTCTRL] = false;
    tied_pins[id_DSP48E1][id_RSTD] = false;
    tied_pins[id_DSP48E1][id_RSTINMODE] = false;
    tied_pins[id_DSP48E1][id_RSTM] = false;
    tied_pins[id_DSP48E1][id_RSTP] = false;

    tied_pins[id_DSP48E1][id_CARRYIN] = false;

    tied_pins[id_DSP48E1][id_CEA1] = false;
    tied_pins[id_DSP48E1][id_CEA2] = false;
    tied_pins[id_DSP48E1][id_CEAD] = false;
    tied_pins[id_DSP48E1][id_CEALUMODE] = false;
    tied_pins[id_DSP48E1][id_CEB1] = false;
    tied_pins[id_DSP48E1][id_CEB2] = false;
    tied_pins[id_DSP48E1][id_CEC] = false;
    tied_pins[id_DSP48E1][id_CECARRYIN] = false;
    tied_pins[id_DSP48E1][id_CECTRL] = false;
    tied_pins[id_DSP48E1][id_CED] = false;
    tied_pins[id_DSP48E1][id_CEINMODE] = false;
    tied_pins[id_DSP48E1][id_CEM] = false;
    tied_pins[id_DSP48E1][id_CEP] = false;
    for (int i = 0; i < 30; i++)
        tied_pins[id_DSP48E1][ctx->id("A[" + std::to_string(i) + "]")] = false;
    for (int i = 0; i < 18; i++)
        tied_pins[id_DSP48E1][ctx->id("B[" + std::to_string(i) + "]")] = false;
    for (int i = 0; i < 48; i++)
        tied_pins[id_DSP48E1][ctx->id("C[" + std::to_string(i) + "]")] = false;
    for (int i = 0; i < 25; i++)
        tied_pins[id_DSP48E1][ctx->id("D[" + std::to_string(i) + "]")] = false;
    for (int i = 0; i < 4; i++)
        tied_pins[id_DSP48E1][ctx->id("ALUMODE[" + std::to_string(i) + "]")] = false;
    for (int i = 0; i < 3; i++)
        tied_pins[id_DSP48E1][ctx->id("CARRYINSEL[" + std::to_string(i) + "]")] = false;
    for (int i = 0; i < 5; i++)
        tied_pins[id_DSP48E1][ctx->id("INMODE[" + std::to_string(i) + "]")] = false;
    for (int i = 0; i < 7; i++)
        tied_pins[id_DSP48E1][ctx->id("OPMODE[" + std::to_string(i) + "]")] = false;
}

// Get a list of logical pins that have both L and U bel pins that need
// to be connected for a 36-bit BRAM
void get_bram36_ul_pins(Context *ctx, std::vector<std::pair<IdString, std::vector<std::string>>> &ul_pins)
{
    BelId spec_bel;
    for (auto bel : ctx->getBels()) {
        if (ctx->getBelType(bel) == id_RAMB36E1_RAMB36E1) {
            spec_bel = bel;
            break;
        }
    }
    NPNR_ASSERT(spec_bel != BelId());
    pool<std::string> belpins;
    for (auto &bp : ctx->getBelPins(spec_bel))
        if (ctx->getBelPinType(spec_bel, bp) == PORT_IN)
            belpins.insert(bp.str(ctx));
    for (auto &bp : belpins) {
        std::string bus_suffix = "";
        std::string root_name = bp;
        if (std::isdigit(bp.back())) {
            auto root_end = bp.find_last_not_of("0123456789");
            bus_suffix = bp.substr(root_end + 1);
            root_name = bp.substr(0, root_end + 1);
        }
        if (root_name.back() != 'L')
            continue;
        std::string base_name = root_name.substr(0, root_name.length() - 1);
        std::string complement = base_name + "U" + bus_suffix;
        if (!belpins.count(complement))
            continue;
        std::string logical_name = bus_suffix.empty() ? base_name : (base_name + "[" + bus_suffix + "]");
        ul_pins.emplace_back(ctx->id(logical_name), std::vector<std::string>{bp, complement});
    }
}

// Gets a list of pins that are to be directly connected to a top level IO pin (only)
void get_top_level_pins(Context *ctx, dict<IdString, pool<IdString>> &toplevel_pins)
{
    toplevel_pins[id_IBUF] = {id_I};
    toplevel_pins[id_IBUF_ANALOG] = {id_I};
    toplevel_pins[id_IBUF_IBUFDISABLE] = {id_I};
    toplevel_pins[id_IBUF_INTERMDISABLE] = {id_I};
    toplevel_pins[id_IBUFE3] = {id_I};

    toplevel_pins[id_IBUFDS] = {id_I, id_IB};
    toplevel_pins[id_IBUFDS_DIFF_OUT] = {id_I, id_IB};
    toplevel_pins[id_IBUFDS_DIFF_OUT_IBUFDISABLE] = {id_I, id_IB};
    toplevel_pins[id_IBUFDS_DIFF_OUT_INTERMDISABLE] = {id_I, id_IB};
    toplevel_pins[id_IBUFDS_GTE3] = {id_I, id_IB};
    toplevel_pins[id_IBUFDS_GTE4] = {id_I, id_IB};
    toplevel_pins[id_IBUFDS_INTERMDISABLE] = {id_I, id_IB};
    toplevel_pins[id_IBUFDSE3] = {id_I, id_IB};

    toplevel_pins[id_IOBUF] = {id_IO};
    toplevel_pins[id_IOBUF_DCIEN] = {id_IO};
    toplevel_pins[id_IOBUF_INTERMDISABLE] = {id_IO};
    toplevel_pins[id_IOBUFE3] = {id_IO};

    toplevel_pins[id_IOBUFDS] = {id_IO, id_IOB};
    toplevel_pins[id_IOBUFDS_DCIEN] = {id_IO, id_IOB};
    toplevel_pins[id_IOBUFDS_DIFF_OUT] = {id_IO, id_IOB};
    toplevel_pins[id_IOBUFDS_DIFF_OUT_DCIEN] = {id_IO, id_IOB};
    toplevel_pins[id_IOBUFDS_DIFF_OUT_INTERMDISABLE] = {id_IO, id_IOB};
    toplevel_pins[id_IOBUFDSE3] = {id_IO, id_IOB};

    toplevel_pins[id_OBUF] = {id_O};
    toplevel_pins[id_OBUFT] = {id_O};

    toplevel_pins[id_OBUFDS] = {id_O, id_OB};
    toplevel_pins[id_OBUFDS_GTE3] = {id_O, id_OB};
    toplevel_pins[id_OBUFDS_GTE3_ADV] = {id_O, id_OB};
    toplevel_pins[id_OBUFDS_GTE4] = {id_O, id_OB};
    toplevel_pins[id_OBUFDS_GTE4_ADV] = {id_O, id_OB};
    toplevel_pins[id_OBUFTDS] = {id_O, id_OB};
}

NEXTPNR_NAMESPACE_END

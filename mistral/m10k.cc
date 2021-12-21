/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Lofty <dan.ravensloft@gmail.com>
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
 */

#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

void Arch::create_m10k(int x, int y)
{
    BelId bel = add_bel(x, y, id_MISTRAL_M10K, id_MISTRAL_M10K);
    add_bel_pin(bel, id_ADDRSTALLA, PORT_IN,
                get_port(CycloneV::M10K, x, y, -1, CycloneV::ADDRSTALLA, 0));
    add_bel_pin(bel, id_ADDRSTALLB, PORT_IN,
                get_port(CycloneV::M10K, x, y, -1, CycloneV::ADDRSTALLB, 0));
    for (int z = 0; z < 2; z++) {
        add_bel_pin(bel, id(stringf("BYTEENABLEA[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::BYTEENABLEA, z));
        add_bel_pin(bel, id(stringf("BYTEENABLEB[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::BYTEENABLEB, z));
        add_bel_pin(bel, id(stringf("ACLR[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::ACLR, z));
        add_bel_pin(bel, id(stringf("RDEN[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::RDEN, z));
        add_bel_pin(bel, id(stringf("WREN[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::WREN, z));
        add_bel_pin(bel, id(stringf("CLKIN[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::CLKIN, z));
        add_bel_pin(bel, id(stringf("CLKIN[%d]", z+6)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::CLKIN, z+6));
    }
    for (int z = 0; z < 4; z++) {
        add_bel_pin(bel, id(stringf("ENABLE[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::ENABLE, z));
    }
    for (int z = 0; z < 12; z++) {
        add_bel_pin(bel, id(stringf("ADDRA[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::ADDRA, z));
        add_bel_pin(bel, id(stringf("ADDRB[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::ADDRB, z));
    }
    for (int z = 0; z < 20; z++) {
        add_bel_pin(bel, id(stringf("DATAAIN[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::DATAAIN, z));
        add_bel_pin(bel, id(stringf("DATABIN[%d]", z)), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::DATABIN, z));
        add_bel_pin(bel, id(stringf("DATAAOUT[%d]", z)), PORT_OUT,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::DATAAOUT, z));
        add_bel_pin(bel, id(stringf("DATABOUT[%d]", z)), PORT_OUT,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::DATABOUT, z));
    }
}

static void assign_lab_pins(Context *ctx, CellInfo *cell)
{
    auto abits = cell->attrs[id_CFG_ABITS].as_int64();
    auto dbits = cell->attrs[id_CFG_DBITS].as_int64();
    NPNR_ASSERT(abits >= 7 && abits <= 13);
    NPNR_ASSERT(dbits == 1 || dbits == 2 || dbits == 5 || dbits == 10 || dbits == 20);

    // Quartus doesn't seem to generate ADDRSTALL[AB], BYTEENABLE[AB][01].

    // It *does* generate ACLR[01] but leaves them unconnected if unused.

    // Enables.
    // RDEN[0] and WREN[1] are left unconnected.
    cell->pin_data[ctx->id("A1EN")].bel_pins = {ctx->id("RDEN[1]")};
    cell->pin_data[ctx->id("B1EN")].bel_pins = {ctx->id("WREN[0]")};

    // Clocks.
    cell->pin_data[ctx->id("CLK1")].bel_pins = {ctx->id("CLKIN[0]")};

    // Enables left unconnected.

    // Address lines.
    int addr_offset = std::max(12 - std::max(abits, int64_t{9}), 0l);
    int bit_offset = 0;
    if (abits == 13) {
        cell->pin_data[ctx->id("A1ADDR[0]")].bel_pins = {ctx->id("DATAAIN[4]")};
        cell->pin_data[ctx->id("B1ADDR[0]")].bel_pins = {ctx->id("DATABIN[19]")};
        bit_offset = 1;
    }
    for (int bit = bit_offset; bit < abits; bit++) {
        cell->pin_data[ctx->id(stringf("A1ADDR[%d]", bit))].bel_pins = {ctx->id(stringf("ADDRA[%d]", bit + addr_offset))};
        cell->pin_data[ctx->id(stringf("B1ADDR[%d]", bit))].bel_pins = {ctx->id(stringf("ADDRB[%d]", bit + addr_offset))};
    }

    // Data lines
    std::vector<int> offsets;
    offsets.push_back(0);
    if (abits >= 10 && dbits <= 10) {
        offsets.push_back(10);
    }
    if (abits >= 11 && dbits <= 5) {
        offsets.push_back(5);
        offsets.push_back(15);
    }
    if (abits >= 12 && dbits <= 2) {
        offsets.push_back(2);
        offsets.push_back(7);
        offsets.push_back(12);
        offsets.push_back(17);
    }
    if (abits == 13 && dbits == 1) {
        offsets.push_back(1);
        offsets.push_back(3);
        offsets.push_back(6);
        offsets.push_back(8);
        offsets.push_back(11);
        offsets.push_back(13);
        offsets.push_back(16);
        offsets.push_back(18);
    }
    for (int bit = 0; bit < dbits; bit++) {
        for (int offset : offsets) {
            cell->pin_data[ctx->id(stringf("A1DATA[%d]", bit))].bel_pins.push_back(ctx->id(stringf("DATAAIN[%d]", bit + offset)));
        }
    }

    for (int bit = 0; bit < dbits; bit++) {
        cell->pin_data[ctx->id(stringf("B1DATA[%d]", bit))].bel_pins = {ctx->id(stringf("DATABOUT[%d]", bit))};
    }
}

NEXTPNR_NAMESPACE_END
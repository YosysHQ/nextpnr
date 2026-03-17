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

#include "nextpnr.h"
#include "pack.h"
#include "pins.h"

#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

CellInfo *XilinxPacker::create_cell(IdString type, IdString name)
{
    CellInfo *cell = ctx->createCell(name, type);

    auto add_port = [&](const std::string &name, PortType dir) {
        IdString id = ctx->id(name);
        cell->ports[id].name = id;
        cell->ports[id].type = dir;
    };
    if (type == id_SLICE_LUTX) {
        for (int i = 1; i <= 6; i++)
            add_port("A" + std::to_string(i), PORT_IN);
        for (int i = 1; i <= 9; i++)
            add_port("WA" + std::to_string(i), PORT_IN);
        add_port("DI1", PORT_IN);
        add_port("DI2", PORT_IN);
        add_port("CLK", PORT_IN);
        add_port("WE", PORT_IN);
        add_port("SIN", PORT_IN);
        add_port("O5", PORT_OUT);
        add_port("O6", PORT_OUT);
        add_port("MC31", PORT_OUT);
    } else if (type == id_SLICE_FFX) {
        add_port("D", PORT_IN);
        add_port("SR", PORT_IN);
        add_port("CE", PORT_IN);
        add_port("CLK", PORT_IN);
        add_port("Q", PORT_OUT);
    } else if (type == id_RAMD64E) {
        for (int i = 0; i < 6; i++)
            add_port("RADR" + std::to_string(i), PORT_IN);
        for (int i = 0; i < 8; i++)
            add_port("WADR" + std::to_string(i), PORT_IN);
        add_port("CLK", PORT_IN);
        add_port("I", PORT_IN);
        add_port("WE", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_RAMD32) {
        for (int i = 0; i < 5; i++)
            add_port("RADR" + std::to_string(i), PORT_IN);
        for (int i = 0; i < 5; i++)
            add_port("WADR" + std::to_string(i), PORT_IN);
        add_port("CLK", PORT_IN);
        add_port("I", PORT_IN);
        add_port("WE", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type.in(id_MUXF7, id_MUXF8, id_MUXF9)) {
        add_port("I0", PORT_IN);
        add_port("I1", PORT_IN);
        add_port("S", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_CARRY8) {
        add_port("CI", PORT_IN);
        add_port("CI_TOP", PORT_IN);

        for (int i = 0; i < 8; i++) {
            add_port("DI[" + std::to_string(i) + "]", PORT_IN);
            add_port("S[" + std::to_string(i) + "]", PORT_IN);
            add_port("CO[" + std::to_string(i) + "]", PORT_OUT);
            add_port("O[" + std::to_string(i) + "]", PORT_OUT);
        }
    } else if (type == id_MUXCY) {
        add_port("CI", PORT_IN);
        add_port("DI", PORT_IN);
        add_port("S", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_XORCY) {
        add_port("CI", PORT_IN);
        add_port("LI", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_PAD) {
        add_port("PAD", PORT_INOUT);
    } else if (type == id_INBUF) {
        add_port("VREF", PORT_IN);
        add_port("PAD", PORT_IN);
        add_port("OSC_EN", PORT_IN);
        for (int i = 0; i < 4; i++)
            add_port("OSC[" + std::to_string(i) + "]", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_IBUFCTRL) {
        add_port("I", PORT_IN);
        add_port("IBUFDISABLE", PORT_IN);
        add_port("T", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type.in(id_OBUF, id_IBUF)) {
        add_port("I", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_OBUFT) {
        add_port("I", PORT_IN);
        add_port("T", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_IOBUF) {
        add_port("I", PORT_IN);
        add_port("T", PORT_IN);
        add_port("O", PORT_OUT);
        add_port("IO", PORT_INOUT);
    } else if (type == id_OBUFT_DCIEN) {
        add_port("I", PORT_IN);
        add_port("T", PORT_IN);
        add_port("DCITERMDISABLE", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_DIFFINBUF) {
        add_port("DIFF_IN_P", PORT_IN);
        add_port("DIFF_IN_N", PORT_IN);
        add_port("OSC_EN[0]", PORT_IN);
        add_port("OSC_EN[1]", PORT_IN);
        for (int i = 0; i < 4; i++)
            add_port("OSC[" + std::to_string(i) + "]", PORT_IN);
        add_port("VREF", PORT_IN);
        add_port("O", PORT_OUT);
        add_port("O_B", PORT_OUT);
    } else if (type == id_HPIO_VREF) {
        for (int i = 0; i < 7; i++)
            add_port("FABRIC_VREF_TUNE[" + std::to_string(i) + "]", PORT_IN);
        add_port("VREF", PORT_OUT);
    } else if (type == id_INV) {
        add_port("I", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_IDELAYCTRL) {
        add_port("REFCLK", PORT_IN);
        add_port("RST", PORT_IN);
        add_port("RDY", PORT_OUT);
    } else if (type == id_IBUF) {
        add_port("I", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_IBUF_INTERMDISABLE) {
        add_port("I", PORT_IN);
        add_port("IBUFDISABLE", PORT_IN);
        add_port("INTERMDISABLE", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_IBUFDS) {
        add_port("I", PORT_IN);
        add_port("IB", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_IBUFDS_INTERMDISABLE_INT) {
        add_port("I", PORT_IN);
        add_port("IB", PORT_IN);
        add_port("IBUFDISABLE", PORT_IN);
        add_port("INTERMDISABLE", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_CARRY4) {
        add_port("CI", PORT_IN);
        add_port("CYINIT", PORT_IN);
        for (int i = 0; i < 4; i++) {
            add_port("DI[" + std::to_string(i) + "]", PORT_IN);
            add_port("S[" + std::to_string(i) + "]", PORT_IN);
            add_port("CO[" + std::to_string(i) + "]", PORT_OUT);
            add_port("O[" + std::to_string(i) + "]", PORT_OUT);
        }
    }
    return cell;
}

CellInfo *XilinxPacker::create_lut(const std::string &name, const std::vector<NetInfo *> &inputs, NetInfo *output,
                                   const Property &init)
{
    CellInfo *cell = ctx->createCell(ctx->id(name), ctx->idf("LUT%d", int(inputs.size())));
    for (size_t i = 0; i < inputs.size(); i++) {
        IdString ip = ctx->idf("I%d", int(i));
        cell->addInput(ip);
        cell->connectPort(ip, inputs.at(i));
    }
    cell->addOutput(id_O);
    cell->connectPort(id_O, output);
    cell->params[id_INIT] = init;
    return cell;
}

NEXTPNR_NAMESPACE_END

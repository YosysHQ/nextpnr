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
    add_bel_pin(bel, id_ADDRSTALLA, PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::ADDRSTALLA, 0));
    add_bel_pin(bel, id_ADDRSTALLB, PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::ADDRSTALLB, 0));
    for (int z = 0; z < 2; z++) {
        add_bel_pin(bel, idf("BYTEENABLEA[%d]", z), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::BYTEENABLEA, z));
        add_bel_pin(bel, idf("BYTEENABLEB[%d]", z), PORT_IN,
                    get_port(CycloneV::M10K, x, y, -1, CycloneV::BYTEENABLEB, z));
        add_bel_pin(bel, idf("ACLR[%d]", z), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::ACLR, z));
        add_bel_pin(bel, idf("RDEN[%d]", z), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::RDEN, z));
        add_bel_pin(bel, idf("WREN[%d]", z), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::WREN, z));
        add_bel_pin(bel, idf("CLKIN[%d]", z), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::CLKIN, z));
        add_bel_pin(bel, idf("CLKIN[%d]", z + 6), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::CLKIN, z + 6));
    }
    for (int z = 0; z < 4; z++) {
        add_bel_pin(bel, idf("ENABLE[%d]", z), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::ENABLE, z));
    }
    for (int z = 0; z < 12; z++) {
        add_bel_pin(bel, idf("ADDRA[%d]", z), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::ADDRA, z));
        add_bel_pin(bel, idf("ADDRB[%d]", z), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::ADDRB, z));
    }
    for (int z = 0; z < 20; z++) {
        add_bel_pin(bel, idf("DATAAIN[%d]", z), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::DATAAIN, z));
        add_bel_pin(bel, idf("DATABIN[%d]", z), PORT_IN, get_port(CycloneV::M10K, x, y, -1, CycloneV::DATABIN, z));
        add_bel_pin(bel, idf("DATAAOUT[%d]", z), PORT_OUT, get_port(CycloneV::M10K, x, y, -1, CycloneV::DATAAOUT, z));
        add_bel_pin(bel, idf("DATABOUT[%d]", z), PORT_OUT, get_port(CycloneV::M10K, x, y, -1, CycloneV::DATABOUT, z));
    }
}

NEXTPNR_NAMESPACE_END

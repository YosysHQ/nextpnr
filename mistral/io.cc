/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

void Arch::create_gpio(int x, int y)
{
    for (int z = 0; z < 4; z++) {
        // Notional pad wire
        WireId pad = add_wire(x, y, idf("PAD[%d]", z));
        BelId bel = add_bel(x, y, idf("IO[%d]", z), id_MISTRAL_IO);
        add_bel_pin(bel, id_PAD, PORT_INOUT, pad);
        if (has_port(CycloneV::GPIO, x, y, z, CycloneV::DATAOUT, 0)) {
            // FIXME: is the port index of zero always correct?
            add_bel_pin(bel, id_I, PORT_IN, get_port(CycloneV::GPIO, x, y, z, CycloneV::DATAOUT, 0));
            add_bel_pin(bel, id_OE, PORT_IN, get_port(CycloneV::GPIO, x, y, z, CycloneV::OEIN, 0));
            add_bel_pin(bel, id_O, PORT_OUT, get_port(CycloneV::GPIO, x, y, z, CycloneV::DATAIN, 0));
        }
        bel_data(bel).block_index = z;
    }
}

bool Arch::is_io_cell(IdString cell_type) const
{
    // Return true if a cell is an IO buffer cell type
    switch (cell_type.index) {
    case ID_MISTRAL_IB:
    case ID_MISTRAL_OB:
    case ID_MISTRAL_IO:
        return true;
    default:
        return false;
    }
}

BelId Arch::get_io_pin_bel(const CycloneV::pin_info_t *pin) const
{
    auto pad = pin->pad;
    CycloneV::pos_t pos = (pad & 0x3FFF);
    return bel_by_block_idx(CycloneV::pos2x(pos), CycloneV::pos2y(pos), id_MISTRAL_IO, (pad >> 14));
}

NEXTPNR_NAMESPACE_END

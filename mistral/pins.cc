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

const dict<IdString, Arch::CellPinsData> Arch::cell_pins_db = {
        // For combinational cells, inversion and tieing can be implemented by manipulating the LUT function
        {id_MISTRAL_ALUT2, {{{}, PINSTYLE_COMB}}},
        {id_MISTRAL_ALUT3, {{{}, PINSTYLE_COMB}}},
        {id_MISTRAL_ALUT4, {{{}, PINSTYLE_COMB}}},
        {id_MISTRAL_ALUT5, {{{}, PINSTYLE_COMB}}},
        {id_MISTRAL_ALUT6, {{{}, PINSTYLE_COMB}}},
        {id_MISTRAL_ALUT_ARITH,
         {// Leave carry chain alone, other than disconnecting a ground constant
          {id_CI, PINSTYLE_CARRY},
          {{}, PINSTYLE_COMB}}},
        {id_MISTRAL_FF,
         {
                 {id_CLK, PINSTYLE_CLK},
                 {id_ENA, PINSTYLE_CE},
                 {id_ACLR, PINSTYLE_RST},
                 {id_SCLR, PINSTYLE_RST},
                 {id_SLOAD, PINSTYLE_RST},
                 {id_SDATA, PINSTYLE_DEDI},
                 {id_DATAIN, PINSTYLE_INP},
         }},
        {id_MISTRAL_MLAB,
         {
                 {id_CLK1, PINSTYLE_CLK},
                 {id_A1EN, PINSTYLE_CE},
         }}};

CellPinStyle Arch::get_cell_pin_style(const CellInfo *cell, IdString port) const
{
    // Look up the pin style in the cell database
    auto fnd_cell = cell_pins_db.find(cell->type);
    if (fnd_cell == cell_pins_db.end())
        return PINSTYLE_NONE;
    auto fnd_port = fnd_cell->second.find(port);
    if (fnd_port != fnd_cell->second.end())
        return fnd_port->second;
    // If there isn't an exact port match, then the empty IdString
    // represents a wildcard default match
    auto fnd_default = fnd_cell->second.find({});
    if (fnd_default != fnd_cell->second.end())
        return fnd_default->second;

    return PINSTYLE_NONE;
}

NEXTPNR_NAMESPACE_END

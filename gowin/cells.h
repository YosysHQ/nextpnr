/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2020  Pepijn de Vos <pepijn@symbioticeda.com>
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

#ifndef GENERIC_CELLS_H
#define GENERIC_CELLS_H

NEXTPNR_NAMESPACE_BEGIN

// Create a generic arch cell and return it
// Name will be automatically assigned if not specified
std::unique_ptr<CellInfo> create_generic_cell(Context *ctx, IdString type, std::string name = "");

// Return true if a cell is a LUT
inline bool is_lut(const BaseCtx *ctx, const CellInfo *cell)
{
    switch (cell->type.index) {
    case ID_LUT1:
    case ID_LUT2:
    case ID_LUT3:
    case ID_LUT4:
        return true;
    default:
        return false;
    }
}

// Return true if a cell is a flipflop
inline bool is_ff(const BaseCtx *ctx, const CellInfo *cell)
{
    switch (cell->type.index) {
    case ID_DFF:
    case ID_DFFE:
    case ID_DFFS:
    case ID_DFFSE:
    case ID_DFFR:
    case ID_DFFRE:
    case ID_DFFP:
    case ID_DFFPE:
    case ID_DFFC:
    case ID_DFFCE:
    case ID_DFFN:
    case ID_DFFNE:
    case ID_DFFNS:
    case ID_DFFNSE:
    case ID_DFFNR:
    case ID_DFFNRE:
    case ID_DFFNP:
    case ID_DFFNPE:
    case ID_DFFNC:
    case ID_DFFNCE:
        return true;
    default:
        return false;
    }
}

inline bool is_lc(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_SLICE; }

// Convert a LUT primitive to (part of) an GENERIC_SLICE, swapping ports
// as needed. Set no_dff if a DFF is not being used, so that the output
// can be reconnected
void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff = true);

// Convert a DFF primitive to (part of) an GENERIC_SLICE, setting parameters
// and reconnecting signals as necessary. If pass_thru_lut is True, the LUT will
// be configured as pass through and D connected to I0, otherwise D will be
// ignored
void dff_to_lc(const Context *ctx, CellInfo *dff, CellInfo *lc, bool pass_thru_lut = false);

// Convert a Gowin IO buffer to a IOB bel
void gwio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *sbio, std::unordered_set<IdString> &todelete_cells);

NEXTPNR_NAMESPACE_END

#endif

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
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

#ifndef MACHXO2_CELLS_H
#define MACHXO2_CELLS_H

NEXTPNR_NAMESPACE_BEGIN

// When packing DFFs, we need context of how it's connected to a LUT to
// properly map DFF ports to FACADE_SLICEs; DI0 input muxes F0 and OFX0,
// and a DFF inside a slice can use either DI0 or M0 as an input.
enum class LutType
{
    None,
    Normal,
    PassThru,
};

// Create a MachXO2 arch cell and return it
// Name will be automatically assigned if not specified
std::unique_ptr<CellInfo> create_machxo2_cell(Context *ctx, IdString type, std::string name = "");

// Return true if a cell is a LUT
inline bool is_lut(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_LUT4; }

// Return true if a cell is a flipflop
inline bool is_ff(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_FACADE_FF; }

inline bool is_lc(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_FACADE_SLICE; }

// Convert a LUT primitive to (part of) an GENERIC_SLICE, swapping ports
// as needed. Set no_dff if a DFF is not being used, so that the output
// can be reconnected
void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff = true);

// Convert a DFF primitive to (part of) an GENERIC_SLICE, setting parameters
// and reconnecting signals as necessary. If pass_thru_lut is True, the LUT will
// be configured as pass through and D connected to I0, otherwise D will be
// ignored
void dff_to_lc(Context *ctx, CellInfo *dff, CellInfo *lc, LutType lut_type = LutType::Normal);

// Convert a nextpnr IO buffer to a GENERIC_IOB
void nxio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *sbio, pool<IdString> &todelete_cells);

NEXTPNR_NAMESPACE_END

#endif

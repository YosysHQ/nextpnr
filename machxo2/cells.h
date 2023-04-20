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

// Create a MachXO2 arch cell and return it
// Name will be automatically assigned if not specified
std::unique_ptr<CellInfo> create_machxo2_cell(Context *ctx, IdString type, std::string name = "");

// Return true if a cell is a LUT
inline bool is_lut(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_LUT4; }

// Return true if a cell is a flipflop
inline bool is_ff(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_TRELLIS_FF; }

inline bool is_carry(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_CCU2D; }

inline bool is_trellis_io(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_TRELLIS_IO; }

inline bool is_dpram(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_TRELLIS_DPR16X4; }

inline bool is_pfumx(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_PFUMX; }

inline bool is_l6mux(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_L6MUX21; }

void lut_to_comb(Context *ctx, CellInfo *lut);
void dram_to_ramw_split(Context *ctx, CellInfo *ram, CellInfo *ramw);
void ccu2_to_comb(Context *ctx, CellInfo *ccu, CellInfo *comb, NetInfo *internal_carry, int i);
void dram_to_comb(Context *ctx, CellInfo *ram, CellInfo *comb, CellInfo *ramw, int index);

// Convert a nextpnr IO buffer to a TRELLIS_IO
void nxio_to_tr(Context *ctx, CellInfo *nxio, CellInfo *trio, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                pool<IdString> &todelete_cells);

NEXTPNR_NAMESPACE_END

#endif

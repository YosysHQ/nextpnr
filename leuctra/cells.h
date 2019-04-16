/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#ifndef LEUCTRA_CELLS_H
#define LEUCTRA_CELLS_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// Create a standard cell and return it
// Name will be automatically assigned if not specified
std::unique_ptr<CellInfo> create_leuctra_cell(Context *ctx, IdString type, std::string name = "");

inline bool is_xilinx_iobuf(const BaseCtx *ctx, const CellInfo *cell) {
   return cell->type == ctx->id("IBUF")
	   || cell->type == ctx->id("IBUFDS")
	   || cell->type == ctx->id("IBUFDS_DIFF_OUT")
	   || cell->type == ctx->id("OBUF")
	   || cell->type == ctx->id("OBUFDS")
	   || cell->type == ctx->id("OBUFT")
	   || cell->type == ctx->id("OBUFTDS")
	   || cell->type == ctx->id("IOBUF")
	   || cell->type == ctx->id("IOBUFDS");
}

inline bool is_xilinx_ff(const BaseCtx *ctx, const CellInfo *cell) {
   return cell->type == ctx->id("FDRE");
}

inline bool is_xilinx_lut(const BaseCtx *ctx, const CellInfo *cell) {
   return cell->type == ctx->id("LUT1")
	   || cell->type == ctx->id("LUT2")
	   || cell->type == ctx->id("LUT3")
	   || cell->type == ctx->id("LUT4")
	   || cell->type == ctx->id("LUT5")
	   || cell->type == ctx->id("LUT6");
}

// Convert a nextpnr IO buffer to an IOB
void nxio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells);
void convert_ff(Context *ctx, CellInfo *orig, CellInfo *ff, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells);
void convert_lut(Context *ctx, CellInfo *orig, CellInfo *lc, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells);

void insert_ilogic_pass(Context *ctx, CellInfo *iob, CellInfo *ilogic);
void insert_ologic_pass(Context *ctx, CellInfo *iob, CellInfo *ologic);

NEXTPNR_NAMESPACE_END

#endif

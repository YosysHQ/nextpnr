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
   return cell->type == ctx->id("FDRE")
	   || cell->type == ctx->id("FDSE")
	   || cell->type == ctx->id("FDCE")
	   || cell->type == ctx->id("FDPE")
	   || cell->type == ctx->id("LDCE")
	   || cell->type == ctx->id("LDPE");
}

inline bool is_xilinx_lut(const BaseCtx *ctx, const CellInfo *cell) {
   return cell->type == ctx->id("INV")
	   || cell->type == ctx->id("LUT1")
	   || cell->type == ctx->id("LUT2")
	   || cell->type == ctx->id("LUT3")
	   || cell->type == ctx->id("LUT4")
	   || cell->type == ctx->id("LUT5")
	   || cell->type == ctx->id("LUT6");
}

// Convert a nextpnr IO buffer to an IOB
void nxio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells);
void convert_ff(Context *ctx, CellInfo *orig, CellInfo *ff, std::vector<std::unique_ptr<CellInfo>> &new_cells,
                std::unordered_set<IdString> &todelete_cells);
CellInfo *convert_lut(Context *ctx, NetInfo *net, std::string name, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells);
std::pair<CellInfo *, CellInfo *> convert_muxf7(Context *ctx, NetInfo *net, std::string name, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells);
void convert_muxf8(Context *ctx, NetInfo *net, std::string name, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells);
CellInfo *convert_carry4(Context *ctx, CellInfo *c4, CellInfo *link, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells);
void fixup_ramb16(Context *ctx, CellInfo *cell, std::vector<std::unique_ptr<CellInfo>> &new_cells,
                std::unordered_set<IdString> &todelete_cells);
void fixup_ramb8(Context *ctx, CellInfo *cell, std::vector<std::unique_ptr<CellInfo>> &new_cells,
                std::unordered_set<IdString> &todelete_cells);

void insert_ilogic_pass(Context *ctx, CellInfo *iob, CellInfo *ilogic);
void insert_ologic_pass(Context *ctx, CellInfo *iob, CellInfo *ologic);

// Gets the constant value driving the given net, returns truee iff it really was a constant.
bool get_const_val(Context *ctx, NetInfo *net, bool &out);
// Connects a given port to a constant driver.
void set_const_port(Context *ctx, CellInfo *cell, IdString port, bool val, std::vector<std::unique_ptr<CellInfo>> &new_cells);
// Takes a port, finds the net driving it, passes through INV cells, collects IS_<port>_INVERTED, collects the final driver and inversion value, returns true iff net connected.
bool get_invertible_port(Context *ctx, CellInfo *cell, IdString port, bool invert, bool invertible, NetInfo *&net, bool &invert_out);
// Takes a port and connects it to the given net, possibly with an inversion.  If invertible is false, emulate it by inserting an INV cell if necessary.
void set_invertible_port(Context *ctx, CellInfo *cell, IdString port, NetInfo *net, bool invert, bool invertible, std::vector<std::unique_ptr<CellInfo>> &new_cells);
// Runs get_inversion, then set_inversion.
bool handle_invertible_port(Context *ctx, CellInfo *cell, IdString port, bool invert, bool invertible, std::vector<std::unique_ptr<CellInfo>> &new_cells);
void handle_invertible_bus(Context *ctx, CellInfo *cell, IdString port, int len, std::vector<std::unique_ptr<CellInfo>> &new_cells);
void handle_bus(Context *ctx, CellInfo *cell, IdString port, int len);

NEXTPNR_NAMESPACE_END

#endif

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

NEXTPNR_NAMESPACE_END

#endif

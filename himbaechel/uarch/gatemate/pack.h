/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#ifndef GATEMATE_PACK_H
#define GATEMATE_PACK_H

#include "gatemate.h"

NEXTPNR_NAMESPACE_BEGIN

struct GateMatePacker
{
    GateMatePacker(Context *ctx, GateMateImpl *uarch) : ctx(ctx), uarch(uarch) { h.init(ctx); };

    void pack_io();
    void pack_cpe();
    void pack_addf();
    void pack_bufg();
    void sort_bufg();
    void pack_pll();
    void pack_misc();
    void pack_constants();
    void dff_to_cpe(CellInfo *dff, CellInfo *cpe);

    void disconnect_if_gnd(CellInfo *cell, IdString input);
    void remove_constants();

    void pll_out(CellInfo *cell, IdString origPort, int placement);

    CellInfo *move_ram_i(CellInfo *cell, IdString origPort, int placement);
    CellInfo *move_ram_o(CellInfo *cell, IdString origPort, int placement);
    // Cell creating
    CellInfo *create_cell_ptr(IdString type, IdString name);
    void flush_cells();

    pool<IdString> packed_cells;

    Context *ctx;
    GateMateImpl *uarch;

    HimbaechelHelpers h;
};

NEXTPNR_NAMESPACE_END
#endif

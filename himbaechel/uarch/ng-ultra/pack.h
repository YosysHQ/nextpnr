/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2023  Miodrag Milanovic <micko@yosyshq.com>
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

#include <algorithm>
#include <boost/optional.hpp>
#include <iterator>
#include <queue>
#include <unordered_set>
#include "chain_utils.h"
#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "ng_ultra.h"

#ifndef NG_ULTRA_PACK_H
#define NG_ULTRA_PACK_H

NEXTPNR_NAMESPACE_BEGIN

struct NgUltraPacker
{
    NgUltraPacker(Context *ctx, NgUltraImpl *uarch) : ctx(ctx), uarch(uarch) { h.init(ctx); };

    // Constants
    void pack_constants();
    void remove_constants();

    // LUTs & FFs
    void update_lut_init();
    void update_dffs();
    void pack_lut_dffs();
    void pack_dffs();
    void pack_cys();
    void pack_rfs();

    // IO
    void pack_iobs();
    void pack_ioms();

    void promote_globals();

private:
    void set_lut_input_if_constant(CellInfo *cell, IdString input);
    void lut_to_fe(CellInfo *lut, CellInfo *fe, bool no_dff, Property lut_table);
    void dff_to_fe(CellInfo *dff, CellInfo *fe, bool pass_thru_lut);

    void exchange_if_constant(CellInfo *cell, IdString input1, IdString input2);
    void pack_cy_input_and_output(CellInfo *cy, IdString cluster, IdString in_port, IdString out_port, int placer, int &lut_only, int &lut_and_ff, int &dff_only);

    void pack_xrf_input_and_output(CellInfo *cy, IdString cluster, IdString in_port, IdString out_port, int &lut_only, int &lut_and_ff, int &dff_only);

    // General helper functions
    void flush_cells();

    // Cell creating
    std::unique_ptr<CellInfo> create_cell(IdString type, IdString name);
    CellInfo *create_cell_ptr(IdString type, IdString name);

    Context *ctx;
    NgUltraImpl *uarch;

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    HimbaechelHelpers h;
};

NEXTPNR_NAMESPACE_END
#endif

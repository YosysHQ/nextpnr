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
    void pack_io_sel();
    void pack_cpe();
    void pack_addf();
    void pack_bufg();
    void sort_bufg();
    void insert_pll_bufg();
    void pack_pll();
    void pack_misc();
    void pack_constants();
    void pack_ram();
    void dff_to_cpe(CellInfo *dff, CellInfo *cpe);
    void insert_bufg(CellInfo *cell, IdString port);

    void disconnect_if_gnd(CellInfo *cell, IdString input);
    void remove_constants();
    void remove_not_used();

    void pll_out(CellInfo *cell, IdString origPort);
    PllCfgRecord get_pll_settings(double f_ref, double f_core, int mode, int low_jitter, bool pdiv0_mux, bool feedback);

    CellInfo *move_ram_i(CellInfo *cell, IdString origPort, bool place = true);
    CellInfo *move_ram_o(CellInfo *cell, IdString origPort, bool place = true);
    CellInfo *move_ram_io(CellInfo *cell, IdString iPort, IdString oPort, bool place = true);
    bool is_gpio_valid_dff(CellInfo *dff);
    BelId get_bank_cpe(int bank);
    // Cell creating
    CellInfo *create_cell_ptr(IdString type, IdString name);
    void flush_cells();

    void ram_ctrl_signal(CellInfo &ci, IdString port, IdString cfg, IdString renamed);
    uint8_t ram_clk_signal(CellInfo &ci, IdString port);

    pool<IdString> packed_cells;
    std::map<NetInfo*, int> global_signals;

    Context *ctx;
    GateMateImpl *uarch;

    HimbaechelHelpers h;
};

NEXTPNR_NAMESPACE_END
#endif

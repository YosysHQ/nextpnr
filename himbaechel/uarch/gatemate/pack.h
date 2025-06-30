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
    void pack_serdes();

    void remove_constants();
    void remove_clocking();
    void remove_not_used();

  private:
    void dff_to_cpe(CellInfo *dff);
    void insert_bufg(CellInfo *cell, IdString port);
    void disconnect_if_gnd(CellInfo *cell, IdString input);
    void pll_out(CellInfo *cell, IdString origPort, Loc fixed);

    PllCfgRecord get_pll_settings(double f_ref, double f_core, int mode, int low_jitter, bool pdiv0_mux, bool feedback);

    std::pair<CellInfo *, CellInfo *> move_ram_i(CellInfo *cell, IdString origPort, bool place = true,
                                                 Loc cpe_loc = Loc());
    std::pair<CellInfo *, CellInfo *> move_ram_o(CellInfo *cell, IdString origPort, bool place = true,
                                                 Loc cpe_loc = Loc());
    std::pair<CellInfo *, CellInfo *> move_ram_io(CellInfo *cell, IdString iPort, IdString oPort, bool place = true,
                                                  Loc cpe_loc = Loc());
    std::pair<CellInfo *, CellInfo *> move_ram_i_fixed(CellInfo *cell, IdString origPort, Loc fixed);
    std::pair<CellInfo *, CellInfo *> move_ram_o_fixed(CellInfo *cell, IdString origPort, Loc fixed);
    std::pair<CellInfo *, CellInfo *> move_ram_io_fixed(CellInfo *cell, IdString iPort, IdString oPort, Loc fixed);
    uint8_t ram_ctrl_signal(CellInfo *cell, IdString port, bool alt);
    uint8_t ram_clk_signal(CellInfo *cell, IdString port);
    bool is_gpio_valid_dff(CellInfo *dff);
    // Cell creating
    CellInfo *create_cell_ptr(IdString type, IdString name);
    void flush_cells();
    void pack_ram_cell(CellInfo &ci, CellInfo *cell, int num, bool is_split);
    void copy_constraint(NetInfo *in_net, NetInfo *out_net);

    pool<IdString> packed_cells;
    std::map<NetInfo *, int> global_signals;
    std::vector<CellInfo *> clkin;
    std::vector<CellInfo *> glbout;

    Context *ctx;
    GateMateImpl *uarch;

    HimbaechelHelpers h;
};

NEXTPNR_NAMESPACE_END
#endif

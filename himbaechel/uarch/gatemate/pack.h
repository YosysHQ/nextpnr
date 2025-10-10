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

enum CPELut
{
    LUT_ZERO = 0b0000,
    LUT_NOR = 0b0001,
    LUT_AND_INV_D1 = 0b0010,
    LUT_INV_D1 = 0b0011,
    LUT_AND_INV_D0 = 0b0100,
    LUT_INV_D0 = 0b0101,
    LUT_XOR = 0b0110,
    LUT_NAND = 0b0111,
    LUT_AND = 0b1000,
    LUT_XNOR = 0b1001,
    LUT_D0 = 0b1010,
    LUT_NAND_INV_D0 = 0b1011,
    LUT_D1 = 0b1100,
    LUT_NAND_INV_D1 = 0b1101,
    LUT_OR = 0b1110,
    LUT_ONE = 0b1111
};

enum MultiDieStrategy
{
    CLOCK_MIRROR,
    REUSE_CLK1,
};

struct GateMatePacker
{
    GateMatePacker(Context *ctx, GateMateImpl *uarch) : ctx(ctx), uarch(uarch) { h.init(ctx); };

    void pack_io();
    void pack_io_sel();
    void pack_cpe();
    void pack_addf();
    void pack_bufg();
    void sort_bufg();
    void insert_clocking();
    void pack_pll();
    void pack_misc();
    void pack_mult();
    void pack_constants();
    void pack_ram();
    void pack_serdes();

    void remove_constants();
    void remove_clocking();
    void remove_double_constrained();

    void cleanup();
    void repack_cpe();
    void repack_ram();
    void reassign_clocks();
    void copy_clocks();

    void set_strategy(MultiDieStrategy strategy) { this->strategy = strategy; }

  private:
    void rename_param(CellInfo *cell, IdString name, IdString new_name, int width);
    void dff_to_cpe(CellInfo *dff);
    void dff_update_params();
    void disconnect_if_gnd(CellInfo *cell, IdString input);
    void pll_out(CellInfo *cell, IdString origPort, Loc fixed);
    void rewire_ram_o(CellInfo *first, IdString port, CellInfo *second);

    void disconnect_not_used();
    void optimize_lut();
    void optimize_mx();
    void optimize_ff();
    void count_cell(CellInfo &ci);
    void move_connections(NetInfo *from_net, NetInfo *to_net);
    void remap_ram_half(CellInfo *half, CellInfo *cell, int num);

    void strategy_mirror();
    void strategy_clk1();

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
    bool are_ffs_compatible(CellInfo *dff, CellInfo *other);

    // Cell creating
    CellInfo *create_cell_ptr(IdString type, IdString name);
    void flush_cells();
    void pack_ram_cell(CellInfo &ci, CellInfo *cell, bool is_split);
    void copy_constraint(const NetInfo *in_net, NetInfo *out_net);

    pool<IdString> packed_cells;

    Context *ctx;
    GateMateImpl *uarch;

    HimbaechelHelpers h;
    NetInfo *net_PACKER_VCC;
    NetInfo *net_PACKER_GND;
    NetInfo *net_SER_CLK;
    int count;
    std::map<IdString, int> count_per_type;
    MultiDieStrategy strategy;
};

NEXTPNR_NAMESPACE_END
#endif

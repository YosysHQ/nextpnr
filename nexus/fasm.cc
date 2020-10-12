/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
 *
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

#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN
namespace {
struct NexusFasmWriter
{
    const Context *ctx;
    std::ostream &out;
    std::vector<std::string> fasm_ctx;

    void push(const std::string &x) { fasm_ctx.push_back(x); }

    void pop() { fasm_ctx.pop_back(); }

    void pop(int N)
    {
        for (int i = 0; i < N; i++)
            fasm_ctx.pop_back();
    }
    bool last_was_blank = true;
    void blank()
    {
        if (!last_was_blank)
            out << std::endl;
        last_was_blank = true;
    }

    void write_prefix()
    {
        for (auto &x : fasm_ctx)
            out << x << ".";
        last_was_blank = false;
    }

    void write_bit(const std::string &name, bool value = true)
    {
        if (value) {
            write_prefix();
            out << name << std::endl;
        }
    }
    void write_comment(const std::string &cmt) { out << "# " << cmt << std::endl; }

    void write_vector(const std::string &name, const std::vector<bool> &value, bool invert = false)
    {
        write_prefix();
        out << name << " = " << int(value.size()) << "'b";
        for (auto bit : boost::adaptors::reverse(value))
            out << ((bit ^ invert) ? '1' : '0');
        out << std::endl;
    }

    void write_int_vector(const std::string &name, uint64_t value, int width, bool invert = false)
    {
        std::vector<bool> bits(width, false);
        for (int i = 0; i < width; i++)
            bits[i] = (value & (1ULL << i)) != 0;
        write_vector(name, bits, invert);
    }

    void write_enum(const CellInfo *cell, const std::string &name, const std::string &defval = "")
    {
        auto fnd = cell->params.find(ctx->id(name));
        if (fnd == cell->params.end()) {
            if (!defval.empty())
                write_bit(stringf("%s.%s", name.c_str(), defval.c_str()));
        } else {
            write_bit(stringf("%s.%s", name.c_str(), fnd->second.c_str()));
        }
    }

    void write_ioattr(const CellInfo *cell, const std::string &name, const std::string &defval = "")
    {
        auto fnd = cell->attrs.find(ctx->id(name));
        if (fnd == cell->attrs.end()) {
            if (!defval.empty())
                write_bit(stringf("%s.%s", name.c_str(), defval.c_str()));
        } else {
            write_bit(stringf("%s.%s", name.c_str(), fnd->second.c_str()));
        }
    }

    NexusFasmWriter(const Context *ctx, std::ostream &out) : ctx(ctx), out(out) {}
    std::string tile_name(int loc, const PhysicalTileInfoPOD &tile)
    {
        int r = loc / ctx->chip_info->width;
        int c = loc % ctx->chip_info->width;
        return stringf("%sR%dC%d__%s", ctx->nameOf(tile.prefix), r, c, ctx->nameOf(tile.tiletype));
    }
    const PhysicalTileInfoPOD &tile_by_type_and_loc(int loc, IdString type)
    {
        auto &ploc = ctx->chip_info->grid[loc];
        for (int i = 0; i < ploc.num_phys_tiles; i++) {
            if (ploc.phys_tiles[i].tiletype == type.index)
                return ploc.phys_tiles[i];
        }
        log_error("No tile of type %s found at location R%dC%d", ctx->nameOf(type), loc / ctx->chip_info->width,
                  loc % ctx->chip_info->width);
    }
    const PhysicalTileInfoPOD &tile_at_loc(int loc)
    {
        auto &ploc = ctx->chip_info->grid[loc];
        NPNR_ASSERT(ploc.num_phys_tiles == 1);
        return ploc.phys_tiles[0];
    }
    std::string escape_name(const std::string &name)
    {
        std::string escaped;
        for (char c : name) {
            if (c == ':')
                escaped += "__";
            else
                escaped += c;
        }
        return escaped;
    }
    void push_tile(int loc, IdString tile_type) { push(tile_name(loc, tile_by_type_and_loc(loc, tile_type))); }
    void push_tile(int loc) { push(tile_name(loc, tile_at_loc(loc))); }
    void push_belname(BelId bel) { push(ctx->nameOf(ctx->bel_data(bel).name)); }
    void write_pip(PipId pip)
    {
        auto &pd = ctx->pip_data(pip);
        if (pd.flags & PIP_FIXED_CONN)
            return;
        std::string tile = tile_name(pip.tile, tile_by_type_and_loc(pip.tile, pd.tile_type));
        std::string source_wire = escape_name(ctx->pip_src_wire_name(pip).str(ctx));
        std::string dest_wire = escape_name(ctx->pip_dst_wire_name(pip).str(ctx));
        write_bit(stringf("%s.PIP.%s.%s", tile.c_str(), dest_wire.c_str(), source_wire.c_str()));
    }
    void write_net(const NetInfo *net)
    {
        write_comment(stringf("Net %s", ctx->nameOf(net)));
        std::set<PipId> sorted_pips;
        for (auto &w : net->wires)
            if (w.second.pip != PipId())
                sorted_pips.insert(w.second.pip);
        for (auto p : sorted_pips)
            write_pip(p);
        blank();
    }
    void write_comb(const CellInfo *cell)
    {
        BelId bel = cell->bel;
        int z = ctx->bel_data(bel).z;
        int k = z & 0x1;
        char slice = 'A' + (z >> 3);
        push_tile(bel.tile, id_PLC);
        push(stringf("SLICE%c", slice));
        if (cell->params.count(id_INIT))
            write_int_vector(stringf("K%d.INIT[15:0]", k), int_or_default(cell->params, id_INIT, 0), 16);
#if 0
        if (cell->lutInfo.is_carry) {
            write_bit("MODE.CCU2");
            write_enum(cell, "INJECT", "NO");
        }
#endif
        pop(2);
    }
    void write_ff(const CellInfo *cell)
    {
        BelId bel = cell->bel;
        int z = ctx->bel_data(bel).z;
        int k = z & 0x1;
        char slice = 'A' + (z >> 3);
        push_tile(bel.tile, id_PLC);
        push(stringf("SLICE%c", slice));
        push(stringf("REG%d", k));
        write_bit("USED.YES");
        write_enum(cell, "REGSET", "RESET");
        write_enum(cell, "LSRMODE", "LSR");
        write_enum(cell, "SEL", "DF");
        pop();
        write_enum(cell, "REGDDR");
        write_enum(cell, "SRMODE");
        write_enum(cell, "CLKMUX");
        write_enum(cell, "CEMUX");
        write_enum(cell, "LSRMUX");
        write_enum(cell, "GSR");
        pop(2);
    }
    void write_io33(const CellInfo *cell)
    {
        BelId bel = cell->bel;
        push_tile(bel.tile);
        push_belname(bel);
        const NetInfo *t = get_net_or_empty(cell, id_T);
        auto tmux = ctx->get_cell_pinmux(cell, id_T);
        bool is_input = false, is_output = false;
        if (tmux == PINMUX_0) {
            is_output = true;
        } else if (tmux == PINMUX_1 || t == nullptr) {
            is_input = true;
        }
        const char *iodir = is_input ? "INPUT" : (is_output ? "OUTPUT" : "BIDIR");
        write_bit(stringf("BASE_TYPE.%s_%s", iodir, str_or_default(cell->attrs, id_IO_TYPE, "LVCMOS33").c_str()));
        write_ioattr(cell, "PULLMODE", "NONE");
        pop(2);
    }
    void write_io18(const CellInfo *cell)
    {
        BelId bel = cell->bel;
        push_tile(bel.tile);
        push_belname(bel);
        push("SEIO18");
        const NetInfo *t = get_net_or_empty(cell, id_T);
        auto tmux = ctx->get_cell_pinmux(cell, id_T);
        bool is_input = false, is_output = false;
        if (tmux == PINMUX_0) {
            is_output = true;
        } else if (tmux == PINMUX_1 || t == nullptr) {
            is_input = true;
        }
        const char *iodir = is_input ? "INPUT" : (is_output ? "OUTPUT" : "BIDIR");
        write_bit(stringf("BASE_TYPE.%s_%s", iodir, str_or_default(cell->attrs, id_IO_TYPE, "LVCMOS18H").c_str()));
        write_ioattr(cell, "PULLMODE", "NONE");
        pop(3);
    }
    void write_osc(const CellInfo *cell)
    {
        BelId bel = cell->bel;
        push_tile(bel.tile);
        push_belname(bel);
        write_enum(cell, "HF_OSC_EN");
        write_enum(cell, "HF_FABRIC_EN");
        write_enum(cell, "HFDIV_FABRIC_EN", "ENABLED");
        write_enum(cell, "LF_FABRIC_EN");
        write_enum(cell, "LF_OUTPUT_EN");
        write_enum(cell, "DEBUG_N", "DISABLED");
        write_int_vector(stringf("HF_CLK_DIV[7:0]"), ctx->parse_lattice_param(cell, id_HF_CLK_DIV, 8, 0).intval, 8);
        pop(2);
    }
    void operator()()
    {
        // Write routing
        for (auto n : sorted(ctx->nets)) {
            write_net(n.second);
        }
        // Write cell config
        for (auto c : sorted(ctx->cells)) {
            const CellInfo *ci = c.second;
            write_comment(stringf("# Cell %s", ctx->nameOf(ci)));
            if (ci->type == id_OXIDE_COMB)
                write_comb(ci);
            else if (ci->type == id_OXIDE_FF)
                write_ff(ci);
            else if (ci->type == id_SEIO33_CORE)
                write_io33(ci);
            else if (ci->type == id_SEIO18_CORE)
                write_io18(ci);
            else if (ci->type == id_OSC_CORE)
                write_osc(ci);
            blank();
        }
    }
};
} // namespace

void Arch::write_fasm(std::ostream &out) const { NexusFasmWriter(getCtx(), out)(); }

NEXTPNR_NAMESPACE_END

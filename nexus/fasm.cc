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
    void write_pip(PipId pip)
    {
        auto &pd = ctx->pip_data(pip);
        if (pd.flags & PIP_FIXED_CONN)
            return;
        std::string tile = tile_name(pip.tile, tile_by_type_and_loc(pip.tile, pd.tile_type));
        std::string source_wire = escape_name(ctx->pip_src_wire_name(pip).str(ctx));
        std::string dest_wire = escape_name(ctx->pip_src_wire_name(pip).str(ctx));
        write_bit(stringf("%s.PIP.%s.%s", tile.c_str(), dest_wire.c_str(), source_wire.c_str()));
    }
    void write_net(const NetInfo *net)
    {
        write_comment(stringf("Net %s", ctx->nameOf(net)));
        std::set<PipId> sorted_pips;
        for (auto &w : net->wires)
            sorted_pips.insert(w.second.pip);
        for (auto p : sorted_pips)
            write_pip(p);
        blank();
    }
    void operator()()
    {
        // Write routing
        for (auto n : sorted(ctx->nets)) {
            write_net(n.second);
        }
    }
};
} // namespace

void Arch::write_fasm(std::ostream &out) const { NexusFasmWriter(getCtx(), out)(); }

NEXTPNR_NAMESPACE_END

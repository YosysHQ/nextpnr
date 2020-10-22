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

    NexusFasmWriter(const Context *ctx, std::ostream &out) : ctx(ctx), out(out) {}

    // Add a 'dot' prefix to the FASM context stack
    void push(const std::string &x) { fasm_ctx.push_back(x); }

    // Remove a prefix from the FASM context stack
    void pop() { fasm_ctx.pop_back(); }

    // Remove N prefices from the FASM context stack
    void pop(int N)
    {
        for (int i = 0; i < N; i++)
            fasm_ctx.pop_back();
    }
    bool last_was_blank = true;
    // Insert a blank line if the last wasn't blank
    void blank()
    {
        if (!last_was_blank)
            out << std::endl;
        last_was_blank = true;
    }
    // Write out all prefices from the stack, interspersed with .
    void write_prefix()
    {
        for (auto &x : fasm_ctx)
            out << x << ".";
        last_was_blank = false;
    }
    // Write a single config bit; if value is true
    void write_bit(const std::string &name, bool value = true)
    {
        if (value) {
            write_prefix();
            out << name << std::endl;
        }
    }
    // Write a FASM attribute
    void write_attribute(const std::string &key, const std::string &value, bool str = true)
    {
        std::string qu = str ? "\"" : "";
        out << "{ " << key << "=" << qu << value << qu << " }" << std::endl;
        last_was_blank = false;
    }
    // Write a FASM comment
    void write_comment(const std::string &cmt) { out << "# " << cmt << std::endl; }
    // Write a FASM bitvector; optionally inverting the values in the process
    void write_vector(const std::string &name, const std::vector<bool> &value, bool invert = false)
    {
        write_prefix();
        out << name << " = " << int(value.size()) << "'b";
        for (auto bit : boost::adaptors::reverse(value))
            out << ((bit ^ invert) ? '1' : '0');
        out << std::endl;
    }
    // Write a FASM bitvector given an integer value
    void write_int_vector(const std::string &name, uint64_t value, int width, bool invert = false)
    {
        std::vector<bool> bits(width, false);
        for (int i = 0; i < width; i++)
            bits[i] = (value & (1ULL << i)) != 0;
        write_vector(name, bits, invert);
    }
    // Write an int vector param
    void write_int_vector_param(const CellInfo *cell, const std::string &name, uint64_t defval, int width,
                                bool invert = false)
    {
        uint64_t value = int_or_default(cell->params, ctx->id(name), defval);
        std::vector<bool> bits(width, false);
        for (int i = 0; i < width; i++)
            bits[i] = (value & (1ULL << i)) != 0;
        write_vector(stringf("%s[%d:0]", name.c_str(), width - 1), bits, invert);
    }
    // Look up an enum value in a cell's parameters and write it to the FASM in name.value format
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
    // Look up an IO attribute in the cell's attributes and write it to the FASM in name.value format
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
    // Gets the full name of a tile
    std::string tile_name(int loc, const PhysicalTileInfoPOD &tile)
    {
        int r = loc / ctx->chip_info->width;
        int c = loc % ctx->chip_info->width;
        return stringf("%sR%dC%d__%s", ctx->nameOf(tile.prefix), r, c, ctx->nameOf(tile.tiletype));
    }
    // Look up a tile by location index and tile type
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
    // Gets the single tile at a location
    const PhysicalTileInfoPOD &tile_at_loc(int loc)
    {
        auto &ploc = ctx->chip_info->grid[loc];
        NPNR_ASSERT(ploc.num_phys_tiles == 1);
        return ploc.phys_tiles[0];
    }
    // Escape an internal prjoxide name for FASM by replacing : with __
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
    // Push a tile name onto the prefix stack
    void push_tile(int loc, IdString tile_type) { push(tile_name(loc, tile_by_type_and_loc(loc, tile_type))); }
    void push_tile(int loc) { push(tile_name(loc, tile_at_loc(loc))); }
    // Push a bel name onto the prefix stack
    void push_belname(BelId bel) { push(ctx->nameOf(ctx->bel_data(bel).name)); }
    // Push the tile group name corresponding to a bel onto the prefix stack
    void push_belgroup(BelId bel)
    {
        int r = bel.tile / ctx->chip_info->width;
        int c = bel.tile % ctx->chip_info->width;
        auto bel_data = ctx->bel_data(bel);
        r += bel_data.rel_y;
        c += bel_data.rel_x;
        std::string s = stringf("R%dC%d_%s", r, c, ctx->nameOf(ctx->bel_data(bel).name));
        push(s);
    }
    // Push a bel's group and name
    void push_bel(BelId bel)
    {
        push_belgroup(bel);
        fasm_ctx.back() += stringf(".%s", ctx->nameOf(ctx->bel_data(bel).name));
    }
    // Write out a pip in tile.dst.src format
    void write_pip(PipId pip)
    {
        auto &pd = ctx->pip_data(pip);
        if (pd.flags & PIP_FIXED_CONN)
            return;
        std::string tile = tile_name(pip.tile, tile_by_type_and_loc(pip.tile, pd.tile_type));
        std::string source_wire = escape_name(ctx->pip_src_wire_name(pip).str(ctx));
        std::string dest_wire = escape_name(ctx->pip_dst_wire_name(pip).str(ctx));
        out << stringf("%s.PIP.%s.%s", tile.c_str(), dest_wire.c_str(), source_wire.c_str()) << std::endl;
    }
    // Write out all the pips corresponding to a net
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
    // Find the CIBMUX output for a signal
    WireId find_cibmux(const CellInfo *cell, IdString pin)
    {
        WireId cursor = ctx->getBelPinWire(cell->bel, pin);
        if (cursor == WireId())
            return WireId();
        for (int i = 0; i < 10; i++) {
            std::string cursor_name = IdString(ctx->wire_data(cursor).name).str(ctx);
            if (cursor_name.find("JCIBMUXOUT") == 0) {
                return cursor;
            }
            for (PipId pip : ctx->getPipsUphill(cursor))
                if (ctx->checkPipAvail(pip)) {
                    cursor = ctx->getPipSrcWire(pip);
                    break;
                }
        }
        return WireId();
    }
    // Write out the mux config for a cell
    void write_cell_muxes(const CellInfo *cell)
    {
        for (auto port : sorted_cref(cell->ports)) {
            // Only relevant to inputs
            if (port.second.type != PORT_IN)
                continue;
            auto pin_style = ctx->get_cell_pin_style(cell, port.first);
            auto pin_mux = ctx->get_cell_pinmux(cell, port.first);
            // Invertible pins
            if (pin_style & PINOPT_INV) {
                if (pin_mux == PINMUX_INV || pin_mux == PINMUX_0)
                    write_bit(stringf("%sMUX.INV", ctx->nameOf(port.first)));
                else if (pin_mux == PINMUX_SIG)
                    write_bit(stringf("%sMUX.%s", ctx->nameOf(port.first), ctx->nameOf(port.first)));
            }
            // Pins that must be explictly enabled
            if ((pin_style & PINBIT_GATED) && (pin_mux == PINMUX_SIG))
                write_bit(stringf("%sMUX.%s", ctx->nameOf(port.first), ctx->nameOf(port.first)));
            // Pins that must be explictly set to 1 rather than just left floating
            if ((pin_style & PINBIT_1) && (pin_mux == PINMUX_1))
                write_bit(stringf("%sMUX.1", ctx->nameOf(port.first)));
            // Handle CIB muxes - these must be set such that floating pins really are floating to VCC and not connected
            // to another CIB signal
            if ((pin_style & PINBIT_CIBMUX) && port.second.net == nullptr) {
                WireId cibmuxout = find_cibmux(cell, port.first);
                if (cibmuxout != WireId()) {
                    write_comment(stringf("CIBMUX for unused pin %s", ctx->nameOf(port.first)));
                    bool found = false;
                    for (PipId pip : ctx->getPipsUphill(cibmuxout)) {
                        if (ctx->checkPipAvail(pip) && ctx->checkWireAvail(ctx->getPipSrcWire(pip))) {
                            write_pip(pip);
                            found = true;
                            break;
                        }
                    }
                    NPNR_ASSERT(found);
                }
            }
        }
    }

    // Write config for an OXIDE_COMB cell
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
    // Write config for an OXIDE_FF cell
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
        write_cell_muxes(cell);
        pop(2);
    }

    // Write out config for an OXIDE_RAMW cell
    void write_ramw(const CellInfo *cell)
    {
        BelId bel = cell->bel;
        push_tile(bel.tile, id_PLC);
        push("SLICEC");
        write_bit("MODE.RAMW");
        write_cell_muxes(cell);
        pop(2);
    }

    std::unordered_set<BelId> used_io;

    // Write config for an SEIO33_CORE cell
    void write_io33(const CellInfo *cell)
    {
        BelId bel = cell->bel;
        used_io.insert(bel);
        push_bel(bel);
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
        write_cell_muxes(cell);
        pop();
    }
    // Write config for an SEIO18_CORE cell
    void write_io18(const CellInfo *cell)
    {
        BelId bel = cell->bel;
        used_io.insert(bel);
        push_bel(bel);
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
        write_cell_muxes(cell);
        pop(2);
    }
    // Write config for an OSC_CORE cell
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
        write_cell_muxes(cell);
        pop(2);
    }
    // Write config for an OXIDE_EBR cell
    void write_bram(const CellInfo *cell)
    {
        // EBR configuration
        BelId bel = cell->bel;
        push_bel(bel);
        int wid = int_or_default(cell->params, id_WID, 0);
        std::string mode = str_or_default(cell->params, id_MODE, "");

        write_bit(stringf("MODE.%s_MODE", mode.c_str()));
        write_enum(cell, "INIT_DATA", "STATIC");
        write_enum(cell, "GSR", "DISABLED");

        write_int_vector("WID[10:0]", wid, 11);

        push(stringf("%s_MODE", mode.c_str()));

        if (mode == "DP16K") {
            write_int_vector_param(cell, "CSDECODE_A", 7, 3, true);
            write_int_vector_param(cell, "CSDECODE_B", 7, 3, true);
            write_enum(cell, "ASYNC_RST_RELEASE_A");
            write_enum(cell, "ASYNC_RST_RELEASE_B");
            write_enum(cell, "DATA_WIDTH_A");
            write_enum(cell, "DATA_WIDTH_B");
            write_enum(cell, "OUTREG_A");
            write_enum(cell, "OUTREG_B");
            write_enum(cell, "RESETMODE_A");
            write_enum(cell, "RESETMODE_B");
        } else if (mode == "PDP16K" || mode == "PDPSC16K") {
            write_int_vector_param(cell, "CSDECODE_W", 7, 3, true);
            write_int_vector_param(cell, "CSDECODE_R", 7, 3, true);
            write_enum(cell, "ASYNC_RST_RELEASE");
            write_enum(cell, "DATA_WIDTH_W");
            write_enum(cell, "DATA_WIDTH_R");
            write_enum(cell, "OUTREG");
            write_enum(cell, "RESETMODE");
        }

        pop();
        push("DP16K_MODE"); // muxes always use the DP16K perspective
        write_cell_muxes(cell);
        pop(2);
        blank();

        // EBR initialisation
        if (wid > 0) {
            push(stringf("IP_EBR_WID%d", wid));
            for (int i = 0; i < 64; i++) {
                IdString param = ctx->id(stringf("INITVAL_%02X", i));
                if (!cell->params.count(param))
                    continue;
                auto &prop = cell->params.at(param);
                std::string value;
                if (prop.is_string) {
                    NPNR_ASSERT(prop.str.substr(0, 2) == "0x");
                    // Lattice-style hex string
                    value = prop.str.substr(2);
                    value = stringf("320'h%s", value.c_str());
                } else {
                    // True Verilog bitvector
                    value = stringf("320'b%s", prop.str.c_str());
                }
                write_bit(stringf("INITVAL_%02X[319:0] = %s", i, value.c_str()));
            }
            pop();
        }
    }
    // Write out FASM for unused bels where needed
    void write_unused()
    {
        write_comment("# Unused bels");

        // DSP primitives are configured to a default mode; even if unused
        static const std::unordered_map<IdString, std::vector<std::string>> dsp_defconf = {
                {id_MULT9_CORE,
                 {
                         "GSR.ENABLED",
                         "MODE.NONE",
                         "RSTAMUX.RSTA",
                         "RSTPMUX.RSTP",
                 }},
                {id_PREADD9_CORE,
                 {
                         "GSR.ENABLED",
                         "MODE.NONE",
                         "RSTBMUX.RSTB",
                         "RSTCLMUX.RSTCL",
                 }},
                {id_REG18_CORE,
                 {
                         "GSR.ENABLED",
                         "MODE.NONE",
                         "RSTPMUX.RSTP",
                 }},
                {id_ACC54_CORE,
                 {
                         "ACCUBYPS.BYPASS",
                         "MODE.NONE",
                 }},
        };

        for (BelId bel : ctx->getBels()) {
            IdString type = ctx->getBelType(bel);
            if (type == id_SEIO33_CORE && !used_io.count(bel)) {
                push_bel(bel);
                write_bit("BASE_TYPE.NONE");
                pop();
                blank();
            } else if (type == id_SEIO18_CORE && !used_io.count(bel)) {
                push_bel(bel);
                push("SEIO18");
                write_bit("BASE_TYPE.NONE");
                pop(2);
                blank();
            } else if (dsp_defconf.count(type) && ctx->getBoundBelCell(bel) == nullptr) {
                push_bel(bel);
                for (const auto &cbit : dsp_defconf.at(type))
                    write_bit(cbit);
                pop();
                blank();
            }
        }
    }
    // Write out placeholder bankref config
    void write_bankcfg()
    {
        for (int i = 0; i < 8; i++) {
            if (i >= 3 && i <= 5)
                continue; // 1.8V banks, skip for now
            write_bit(stringf("GLOBAL.BANK%d.VCC.3V3", i));
        }
        blank();
    }
    // Write out FASM for the whole design
    void operator()()
    {
        // Write device config
        write_attribute("oxide.device", ctx->device);
        write_attribute("oxide.device_variant", ctx->variant);
        blank();
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
            else if (ci->type == id_RAMW)
                write_ramw(ci);
            else if (ci->type == id_SEIO33_CORE)
                write_io33(ci);
            else if (ci->type == id_SEIO18_CORE)
                write_io18(ci);
            else if (ci->type == id_OSC_CORE)
                write_osc(ci);
            else if (ci->type == id_OXIDE_EBR)
                write_bram(ci);
            blank();
        }
        // Write config for unused bels
        write_unused();
        // Write bank config
        write_bankcfg();
    }
};
} // namespace

void Arch::write_fasm(std::ostream &out) const { NexusFasmWriter(getCtx(), out)(); }

NEXTPNR_NAMESPACE_END

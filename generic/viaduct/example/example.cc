/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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
#include "viaduct_api.h"
#include "viaduct_helpers.h"

#define GEN_INIT_CONSTIDS
#define VIADUCT_CONSTIDS "viaduct/example/constids.inc"
#include "viaduct_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct ExampleImpl : ViaductAPI
{
    ~ExampleImpl(){};
    void init(Context *ctx) override
    {
        init_uarch_constids(ctx);
        ViaductAPI::init(ctx);
        h.init(ctx);
        if (with_gui)
            init_bel_decals();
        init_wires();
        init_bels();
        init_pips();
    }

    void pack() override
    {
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
                CellTypePort(id_INBUF, id_PAD),
                CellTypePort(id_OUTBUF, id_PAD),
        };
        h.remove_nextpnr_iobs(top_ports);
        // Replace constants with LUTs
        const dict<IdString, Property> vcc_params = {{id_INIT, Property(0xFFFF, 16)}};
        const dict<IdString, Property> gnd_params = {{id_INIT, Property(0x0000, 16)}};
        h.replace_constants(CellTypePort(id_LUT4, id_F), CellTypePort(id_LUT4, id_F), vcc_params, gnd_params);
        // Constrain directly connected LUTs and FFs together to use dedicated resources
        int lutffs = h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT4, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1);
        log_info("Constrained %d LUTFF pairs.\n", lutffs);
    }

    void prePlace() override { assign_cell_info(); }

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override
    {
        Loc l = ctx->getBelLocation(bel);
        if (is_io(l.x, l.y)) {
            return true;
        } else {
            return slice_valid(l.x, l.y, l.z / 2);
        }
    }

  private:
    ViaductHelpers h;
    // Configuration
    // Grid size including IOBs at edges
    const int X = 32, Y = 32;
    // SLICEs per tile
    const int N = 8;
    // LUT input count
    const int K = 4;
    // Number of local wires
    const int Wl = N * (K + 1) + 8;
    // 1/Fc for bel input wire pips; local wire pips and neighbour pips
    const int Si = 4, Sq = 4, Sl = 8;

    // For fast wire lookups
    struct TileWires
    {
        std::vector<WireId> clk, q, f, d, i;
        std::vector<WireId> l;
        std::vector<WireId> pad;
    };

    std::vector<std::vector<TileWires>> wires_by_tile;

    // Create wires to attach to bels and pips
    void init_wires()
    {
        log_info("Creating wires...\n");
        wires_by_tile.resize(Y);
        for (int y = 0; y < Y; y++) {
            auto &row_wires = wires_by_tile.at(y);
            row_wires.resize(X);
            for (int x = 0; x < X; x++) {
                auto &w = row_wires.at(x);
                for (int z = 0; z < N; z++) {
                    // Clock input
                    w.clk.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("CLK%d", z)), ctx->id("CLK"), x, y));
                    // FF input
                    w.d.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("D%d", z)), ctx->id("D"), x, y));
                    // FF and LUT outputs
                    w.q.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("Q%d", z)), ctx->id("Q"), x, y));
                    w.f.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("F%d", z)), ctx->id("F"), x, y));
                    // LUT inputs
                    for (int i = 0; i < K; i++)
                        w.i.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("L%dI%d", z, i)), ctx->id("I"), x, y));
                }
                // Local wires
                for (int l = 0; l < Wl; l++)
                    w.l.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("LOCAL%d", l)), ctx->id("LOCAL"), x, y));
                // Pad wires for IO
                if (is_io(x, y) && x != y)
                    for (int z = 0; z < 2; z++)
                        w.pad.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("PAD%d", z)), id_PAD, x, y));
            }
        }
    }
    bool is_io(int x, int y) const
    {
        // IO are on the edges of the device
        return (x == 0) || (x == (X - 1)) || (y == 0) || (y == (Y - 1));
    }
    // Create IO bels in an IO tile
    void add_io_bels(int x, int y)
    {
        auto &w = wires_by_tile.at(y).at(x);
        for (int z = 0; z < 2; z++) {
            BelId b = ctx->addBel(h.xy_id(x, y, ctx->idf("IO%d", z)), id_IOB, Loc(x, y, z), false, false);
            ctx->addBelInout(b, id_PAD, w.pad.at(z));
            ctx->addBelInput(b, id_I, w.i.at(z * K + 0));
            ctx->addBelInput(b, id_EN, w.i.at(z * K + 1));
            ctx->addBelOutput(b, id_O, w.q.at(z));
        }
    }
    PipId add_pip(Loc loc, WireId src, WireId dst, delay_t delay = 0.05)
    {
        IdStringList name = IdStringList::concat(ctx->getWireName(dst), ctx->getWireName(src));
        return ctx->addPip(name, ctx->id("PIP"), src, dst, delay, loc);
    }

    static constexpr float lut_x1 = 0.8f;
    static constexpr float lut_w = 0.07f;
    static constexpr float ff_x1 = 0.9f;
    static constexpr float ff_w = 0.05f;
    static constexpr float bel_y1 = 0.2f;
    static constexpr float bel_h = 0.03f;
    static constexpr float bel_dy = 0.05f;
    void init_bel_decals()
    {
        for (int z = 0; z < N; z++) {
            float y1 = bel_y1 + z * bel_dy;
            float y2 = y1 + bel_h;
            ctx->addDecalGraphic(IdStringList(ctx->idf("LUT%d", z)),
                                 GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, lut_x1, y1,
                                                lut_x1 + lut_w, y2, 10.0));
            ctx->addDecalGraphic(IdStringList(ctx->idf("FF%d", z)),
                                 GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, ff_x1, y1,
                                                ff_x1 + ff_w, y2, 10.0));
        }
    }

    // Create LUT and FF bels in a logic tile
    void add_slice_bels(int x, int y)
    {
        auto &w = wires_by_tile.at(y).at(x);
        for (int z = 0; z < N; z++) {
            // Create LUT bel
            BelId lut = ctx->addBel(h.xy_id(x, y, ctx->idf("SLICE%d_LUT", z)), id_LUT4, Loc(x, y, z * 2), false, false);
            for (int k = 0; k < K; k++)
                ctx->addBelInput(lut, ctx->idf("I[%d]", k), w.i.at(z * K + k));
            ctx->addBelOutput(lut, id_F, w.f.at(z));
            // FF data can come from LUT output or LUT I3
            add_pip(Loc(x, y, 0), w.f.at(z), w.d.at(z));
            add_pip(Loc(x, y, 0), w.i.at(z * K + (K - 1)), w.d.at(z));
            // Create DFF bel
            BelId dff =
                    ctx->addBel(h.xy_id(x, y, ctx->idf("SLICE%d_FF", z)), id_DFF, Loc(x, y, z * 2 + 1), false, false);
            ctx->addBelInput(dff, id_CLK, w.clk.at(z));
            ctx->addBelInput(dff, id_D, w.d.at(z));
            ctx->addBelOutput(dff, id_Q, w.q.at(z));
            if (with_gui) {
                ctx->setBelDecal(lut, x, y, IdStringList(ctx->idf("LUT%d", z)));
                ctx->setBelDecal(dff, x, y, IdStringList(ctx->idf("FF%d", z)));
            }
        }
    }
    // Create bels according to tile type
    void init_bels()
    {
        log_info("Creating bels...\n");
        for (int y = 0; y < Y; y++) {
            for (int x = 0; x < X; x++) {
                if (is_io(x, y)) {
                    if (x == y)
                        continue; // don't put IO in corners
                    add_io_bels(x, y);
                } else {
                    add_slice_bels(x, y);
                }
            }
        }
    }

    // Create PIPs inside a tile; following an example synthetic routing pattern
    void add_tile_pips(int x, int y)
    {
        auto &w = wires_by_tile.at(y).at(x);
        Loc loc(x, y, 0);
        auto create_input_pips = [&](WireId dst, int offset, int skip) {
            for (int i = (offset % skip); i < Wl; i += skip)
                add_pip(loc, w.l.at(i), dst, 0.05);
        };
        for (int z = 0; z < N; z++) {
            create_input_pips(w.clk.at(z), 0, Si);
            for (int k = 0; k < K; k++)
                create_input_pips(w.i.at(z * K + k), k, Si);
        }
        auto create_output_pips = [&](WireId dst, int offset, int skip) {
            for (int z = (offset % skip); z < N; z += skip) {
                add_pip(loc, w.f.at(z), dst, 0.05);
                add_pip(loc, w.q.at(z), dst, 0.05);
            }
        };
        auto create_neighbour_pips = [&](WireId dst, int nx, int ny, int offset, int skip) {
            if (nx < 0 || nx >= X)
                return;
            if (ny < 0 || ny >= Y)
                return;
            auto &nw = wires_by_tile.at(ny).at(nx);
            for (int i = (offset % skip); i < Wl; i += skip)
                add_pip(loc, dst, nw.l.at(i), 0.1);
        };
        for (int i = 0; i < Wl; i++) {
            WireId dst = w.l.at(i);
            create_output_pips(dst, i % Sq, Sq);
            create_neighbour_pips(dst, x - 1, y - 1, (i + 1) % Sl, Sl);
            create_neighbour_pips(dst, x - 1, y, (i + 2) % Sl, Sl);
            create_neighbour_pips(dst, x - 1, y + 1, (i + 3) % Sl, Sl);
            create_neighbour_pips(dst, x, y - 1, (i + 4) % Sl, Sl);
            create_neighbour_pips(dst, x, y + 1, (i + 5) % Sl, Sl);
            create_neighbour_pips(dst, x + 1, y - 1, (i + 6) % Sl, Sl);
            create_neighbour_pips(dst, x + 1, y, (i + 7) % Sl, Sl);
            create_neighbour_pips(dst, x + 1, y + 1, (i + 8) % Sl, Sl);
        }
    }
    void init_pips()
    {
        log_info("Creating pips...\n");
        for (int y = 0; y < Y; y++)
            for (int x = 0; x < X; x++)
                add_tile_pips(x, y);
    }
    // Validity checking
    struct ExampleCellInfo
    {
        const NetInfo *lut_f = nullptr, *ff_d = nullptr;
        bool lut_i3_used = false;
    };
    std::vector<ExampleCellInfo> fast_cell_info;
    void assign_cell_info()
    {
        fast_cell_info.resize(ctx->cells.size());
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            auto &fc = fast_cell_info.at(ci->flat_index);
            if (ci->type == id_LUT4) {
                fc.lut_f = ci->getPort(id_F);
                fc.lut_i3_used = (ci->getPort(ctx->idf("I[%d]", K - 1)) != nullptr);
            } else if (ci->type == id_DFF) {
                fc.ff_d = ci->getPort(id_D);
            }
        }
    }
    bool slice_valid(int x, int y, int z) const
    {
        const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2)));
        const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2 + 1)));
        if (!lut || !ff)
            return true; // always valid if only LUT or FF used
        const auto &lut_data = fast_cell_info.at(lut->flat_index);
        const auto &ff_data = fast_cell_info.at(ff->flat_index);
        // In our example arch; the FF D can either be driven from LUT F or LUT I3
        // so either; FF D must equal LUT F or LUT I3 must be unused
        if (ff_data.ff_d == lut_data.lut_f)
            return true;
        if (lut_data.lut_i3_used)
            return false;
        return true;
    }
    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override
    {
        if (cell_type.in(id_INBUF, id_OUTBUF))
            return id_IOB;
        return cell_type;
    }
    bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        IdString bel_type = ctx->getBelType(bel);
        if (bel_type == id_IOB)
            return cell_type.in(id_INBUF, id_OUTBUF);
        else
            return (bel_type == cell_type);
    }
};

struct ExampleArch : ViaductArch
{
    ExampleArch() : ViaductArch("example"){};
    std::unique_ptr<ViaductAPI> create(const dict<std::string, std::string> &args)
    {
        return std::make_unique<ExampleImpl>();
    }
} exampleArch;
} // namespace

NEXTPNR_NAMESPACE_END

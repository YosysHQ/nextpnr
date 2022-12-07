/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2022  Lofty <dan.ravensloft@gmail.com>
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
#define VIADUCT_CONSTIDS "viaduct/okami/constids.inc"
#include "viaduct_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct OkamiImpl : ViaductAPI
{
    ~OkamiImpl(){};
    void init(Context *ctx) override
    {
        init_uarch_constids(ctx);
        ViaductAPI::init(ctx);
        h.init(ctx);
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
        int lutffs = h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT4, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1,
                                            false);
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
    const int M = 32;
    const int X = M, Y = M;
    // SLICEs per tile
    const int N = 8;
    // LUT input count
    const int K = 4;
    // Number of tile input buses
    const int InputMuxCount = 10; // >= 6 for attosoc; >= 10 for arbiter
    // Number of output wires in a direction
    const int OutputMuxCount = 8; // >= 5 for attosoc; >= 8 for arbiter

    // For fast wire lookups
    struct TileWires
    {
        std::vector<WireId> clk, q, f, d;
        std::vector<WireId> slice_inputs;
        std::vector<WireId> slice_outputs;
        std::vector<WireId> tile_inputs_north, tile_inputs_east, tile_inputs_south, tile_inputs_west;
        std::vector<WireId> tile_outputs_north, tile_outputs_east, tile_outputs_south, tile_outputs_west;
        std::vector<WireId> pad;
    };

    std::vector<std::vector<TileWires>> wires_by_tile;

    // Create wires to attach to bels and pips
    void init_wires()
    {
        NPNR_ASSERT(X >= 3);
        NPNR_ASSERT(Y >= 3);
        NPNR_ASSERT(K >= 2);
        NPNR_ASSERT(N >= 1);
        NPNR_ASSERT(InputMuxCount >= OutputMuxCount);

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
                        w.slice_inputs.push_back(
                                ctx->addWire(h.xy_id(x, y, ctx->idf("L%dI%d", z, i)), ctx->id("I"), x, y));
                    w.slice_outputs.push_back(
                            ctx->addWire(h.xy_id(x, y, ctx->idf("SLICEOUT[%d]", z)), ctx->id("SLICEOUT"), x, y));
                }
                // Tile inputs
                for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
                    w.tile_inputs_north.push_back(
                            ctx->addWire(h.xy_id(x, y, ctx->idf("TILEINN[%d]", tile_input)), ctx->id("TILEINN"), x, y));
                    w.tile_inputs_east.push_back(
                            ctx->addWire(h.xy_id(x, y, ctx->idf("TILEINE[%d]", tile_input)), ctx->id("TILEINE"), x, y));
                    w.tile_inputs_south.push_back(
                            ctx->addWire(h.xy_id(x, y, ctx->idf("TILEINS[%d]", tile_input)), ctx->id("TILEINS"), x, y));
                    w.tile_inputs_west.push_back(
                            ctx->addWire(h.xy_id(x, y, ctx->idf("TILEINW[%d]", tile_input)), ctx->id("TILEINW"), x, y));
                }
                // Tile outputs
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++) {
                    w.tile_outputs_north.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("TILEOUTN[%d]", tile_output)),
                                                                ctx->id("TILEOUTN"), x, y));
                    w.tile_outputs_east.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("TILEOUTE[%d]", tile_output)),
                                                               ctx->id("TILEOUTE"), x, y));
                    w.tile_outputs_south.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("TILEOUTS[%d]", tile_output)),
                                                                ctx->id("TILEOUTS"), x, y));
                    w.tile_outputs_west.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("TILEOUTW[%d]", tile_output)),
                                                               ctx->id("TILEOUTW"), x, y));
                }
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
            ctx->addBelInput(b, id_I, w.slice_inputs.at(z * K + 0));
            ctx->addBelInput(b, id_EN, w.slice_inputs.at(z * K + 1));
            ctx->addBelOutput(b, id_O, w.slice_outputs.at(z));
        }
    }
    PipId add_pip(Loc loc, WireId src, WireId dst, delay_t delay = 0.05)
    {
        IdStringList name = IdStringList::concat(ctx->getWireName(dst), ctx->getWireName(src));
        return ctx->addPip(name, ctx->id("PIP"), src, dst, delay, loc);
    }
    // Create LUT and FF bels in a logic tile
    void add_slice_bels(int x, int y)
    {
        auto &w = wires_by_tile.at(y).at(x);
        for (int z = 0; z < N; z++) {
            // Create LUT bel
            BelId lut = ctx->addBel(h.xy_id(x, y, ctx->idf("SLICE%d_LUT", z)), id_LUT4, Loc(x, y, z * 2), false, false);
            for (int k = 0; k < K; k++)
                ctx->addBelInput(lut, ctx->idf("I[%d]", k), w.slice_inputs.at(z * K + k));
            ctx->addBelOutput(lut, id_F, w.f.at(z));
            // FF data can come from LUT output or LUT I3
            add_pip(Loc(x, y, 0), w.f.at(z), w.d.at(z));
            add_pip(Loc(x, y, 0), w.slice_inputs.at(z * K + (K - 1)), w.d.at(z));
            // Create DFF bel
            BelId dff =
                    ctx->addBel(h.xy_id(x, y, ctx->idf("SLICE%d_FF", z)), id_DFF, Loc(x, y, z * 2 + 1), false, false);
            ctx->addBelInput(dff, id_CLK, w.clk.at(z));
            ctx->addBelInput(dff, id_D, w.d.at(z));
            ctx->addBelOutput(dff, id_Q, w.q.at(z));
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
    void add_io_pips(int x, int y)
    {
        auto &w = wires_by_tile.at(y).at(x);
        Loc loc(x, y, 0);

        const uint16_t tile_input_config[8] = {
                0b0000'0000'0000'0001, 0b0000'0000'0000'0001, 0b0000'0000'0000'0001, 0b0000'0000'0000'0001,
                0b0000'0000'0000'0010, 0b0000'0000'0000'0010, 0b0000'0000'0000'0010, 0b0000'0000'0000'0010,
        };

        // Tile inputs
        for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
            auto &dst = w.tile_inputs_north.at(tile_input);
            // North
            for (int step = 1; step <= 4; step++) {
                if (y - step <= 0 || x == 0 || x == X - 1)
                    break;
                auto &w = wires_by_tile.at(y - step).at(x);
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++)
                    if ((1 << tile_input) & tile_input_config[tile_output])
                        add_pip(loc, w.tile_outputs_north.at(tile_output), dst);
            }
        }

        for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
            auto &dst = w.tile_inputs_east.at(tile_input);
            // East
            for (int step = 1; step <= 4; step++) {
                if (x - step <= 0 || y == 0 || y == Y - 1)
                    break;
                auto &w = wires_by_tile.at(y).at(x - step);
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++)
                    if ((1 << tile_input) & tile_input_config[tile_output])
                        add_pip(loc, w.tile_outputs_east.at(tile_output), dst);
            }
        }

        for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
            auto &dst = w.tile_inputs_south.at(tile_input);
            // South
            for (int step = 1; step <= 4; step++) {
                if (y + step >= Y || x == 0 || x == X - 1)
                    break;
                auto &w = wires_by_tile.at(y + step).at(x);
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++)
                    if ((1 << tile_input) & tile_input_config[tile_output])
                        add_pip(loc, w.tile_outputs_south.at(tile_output), dst);
            }
        }

        for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
            auto &dst = w.tile_inputs_west.at(tile_input);
            // West
            for (int step = 1; step <= 4; step++) {
                if (x + step >= X || y == 0 || y == Y - 1)
                    break;
                auto &w = wires_by_tile.at(y).at(x + step);
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++)
                    if ((1 << tile_input) & tile_input_config[tile_output])
                        add_pip(loc, w.tile_outputs_west.at(tile_output), dst);
            }
        }

        // Tile outputs
        for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++) {
            for (int z = 0; z < 2; z++) {
                WireId src = w.slice_outputs.at(z);
                // O output
                if (y == 0)
                    add_pip(loc, src, w.tile_outputs_north.at(tile_output));
                if (x == 0)
                    add_pip(loc, src, w.tile_outputs_east.at(tile_output));
                if (y == Y - 1)
                    add_pip(loc, src, w.tile_outputs_south.at(tile_output));
                if (x == X - 1)
                    add_pip(loc, src, w.tile_outputs_west.at(tile_output));
            }
        }

        // Pad inputs
        for (const auto &src : w.tile_inputs_north) {
            for (int z = 0; z < 2; z++) {
                // I input
                add_pip(loc, src, w.slice_inputs.at(z * K + 0));
                // EN input
                add_pip(loc, src, w.slice_inputs.at(z * K + 1));
            }
        }
        for (const auto &src : w.tile_inputs_east) {
            for (int z = 0; z < 2; z++) {
                // I input
                add_pip(loc, src, w.slice_inputs.at(z * K + 0));
                // EN input
                add_pip(loc, src, w.slice_inputs.at(z * K + 1));
            }
        }
        for (const auto &src : w.tile_inputs_south) {
            for (int z = 0; z < 2; z++) {
                // I input
                add_pip(loc, src, w.slice_inputs.at(z * K + 0));
                // EN input
                add_pip(loc, src, w.slice_inputs.at(z * K + 1));
            }
        }
        for (const auto &src : w.tile_inputs_west) {
            for (int z = 0; z < 2; z++) {
                // I input
                add_pip(loc, src, w.slice_inputs.at(z * K + 0));
                // EN input
                add_pip(loc, src, w.slice_inputs.at(z * K + 1));
            }
        }
    }
    void add_slice_pips(int x, int y)
    {
        auto &w = wires_by_tile.at(y).at(x);
        Loc loc(x, y, 0);

        const uint16_t tile_input_config[8] = {0b1010'1010'1010'1010, 0b0101'0101'0101'0101, 0b0110'0110'0110'0110,
                                               0b1001'1001'1001'1001, 0b0011'0011'0011'0011, 0b1100'1100'1100'1100,
                                               0b1111'0000'1111'0000, 0b0000'1111'0000'1111};

        // Slice input selector
        for (int lut = 0; lut < N; lut++) {
            for (int lut_input = 0; lut_input < K; lut_input++) {
                for (const auto &tile_input : w.tile_inputs_north) // Tile input bus
                    add_pip(loc, tile_input, w.slice_inputs.at(lut * K + lut_input));
                for (const auto &tile_input : w.tile_inputs_east) // Tile input bus
                    add_pip(loc, tile_input, w.slice_inputs.at(lut * K + lut_input));
                for (const auto &tile_input : w.tile_inputs_south) // Tile input bus
                    add_pip(loc, tile_input, w.slice_inputs.at(lut * K + lut_input));
                for (const auto &tile_input : w.tile_inputs_west) // Tile input bus
                    add_pip(loc, tile_input, w.slice_inputs.at(lut * K + lut_input));
                for (const auto &slice_output : w.slice_outputs) // Slice output bus
                    add_pip(loc, slice_output, w.slice_inputs.at(lut * K + lut_input));
            }
            for (const auto &tile_input : w.tile_inputs_north) // Clock selector
                add_pip(loc, tile_input, w.clk.at(lut));
            for (const auto &tile_input : w.tile_inputs_east) // Clock selector
                add_pip(loc, tile_input, w.clk.at(lut));
            for (const auto &tile_input : w.tile_inputs_south) // Clock selector
                add_pip(loc, tile_input, w.clk.at(lut));
            for (const auto &tile_input : w.tile_inputs_west) // Clock selector
                add_pip(loc, tile_input, w.clk.at(lut));
        }

        // Slice output selector
        for (int slice_output = 0; slice_output < N; slice_output++) {
            add_pip(loc, w.f.at(slice_output), w.slice_outputs.at(slice_output)); // LUT output
            add_pip(loc, w.q.at(slice_output), w.slice_outputs.at(slice_output)); // DFF output
        }

        // Tile input selector
        for (int step = 1; step <= 4; step++) {
            if (y + step < Y) // South
                for (size_t tile_input_index = 0; tile_input_index < w.tile_inputs_north.size(); tile_input_index++)
                    for (size_t tile_output_index = 0;
                         tile_output_index < wires_by_tile.at(y + step).at(x).tile_outputs_south.size();
                         tile_output_index++)
                        if ((1 << tile_input_index) & tile_input_config[tile_output_index])
                            add_pip(loc, wires_by_tile.at(y + step).at(x).tile_outputs_south.at(tile_output_index),
                                    w.tile_inputs_north.at(tile_input_index));

            if (x + step < X) // West
                for (size_t tile_input_index = 0; tile_input_index < w.tile_inputs_east.size(); tile_input_index++)
                    for (size_t tile_output_index = 0;
                         tile_output_index < wires_by_tile.at(y).at(x + step).tile_outputs_west.size();
                         tile_output_index++)
                        if ((1 << tile_input_index) & tile_input_config[tile_output_index])
                            add_pip(loc, wires_by_tile.at(y).at(x + step).tile_outputs_west.at(tile_output_index),
                                    w.tile_inputs_east.at(tile_input_index));

            if (y - step >= 0) // North
                for (size_t tile_input_index = 0; tile_input_index < w.tile_inputs_south.size(); tile_input_index++)
                    for (size_t tile_output_index = 0;
                         tile_output_index < wires_by_tile.at(y - step).at(x).tile_outputs_north.size();
                         tile_output_index++)
                        if ((1 << tile_input_index) & tile_input_config[tile_output_index])
                            add_pip(loc, wires_by_tile.at(y - step).at(x).tile_outputs_north.at(tile_output_index),
                                    w.tile_inputs_south.at(tile_input_index));

            if (x - step >= 0) // East
                for (size_t tile_input_index = 0; tile_input_index < w.tile_inputs_west.size(); tile_input_index++)
                    for (size_t tile_output_index = 0;
                         tile_output_index < wires_by_tile.at(y).at(x - step).tile_outputs_east.size();
                         tile_output_index++)
                        if ((1 << tile_input_index) & tile_input_config[tile_output_index])
                            add_pip(loc, wires_by_tile.at(y).at(x - step).tile_outputs_east.at(tile_output_index),
                                    w.tile_inputs_west.at(tile_input_index));
        }

        // Tile output selector
        for (const auto &slice_output : w.slice_outputs) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, slice_output, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, slice_output, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, slice_output, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, slice_output, tile_output);
        }

        for (const auto &tile_input : w.tile_inputs_north) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, tile_input, tile_output);
        }
        for (const auto &tile_input : w.tile_inputs_east) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, tile_input, tile_output);
        }
        for (const auto &tile_input : w.tile_inputs_south) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, tile_input, tile_output);
        }
        for (const auto &tile_input : w.tile_inputs_west) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, tile_input, tile_output);
        }
    }
    void init_pips()
    {
        log_info("Creating pips...\n");
        for (int y = 0; y < Y; y++)
            for (int x = 0; x < X; x++) {
                if (is_io(x, y)) {
                    add_io_pips(x, y);
                } else {
                    add_slice_pips(x, y);
                }
            }
    }
    // Validity checking
    struct OkamiCellInfo
    {
        const NetInfo *lut_f = nullptr, *ff_d = nullptr;
        bool lut_i3_used = false;
    };
    std::vector<OkamiCellInfo> fast_cell_info;
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
        if (ff_data.ff_d == lut_data.lut_f && lut_data.lut_f->users.entries() == 1)
            return true;
        // Can't route FF and LUT output separately
        return false;
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

struct OkamiArch : ViaductArch
{
    OkamiArch() : ViaductArch("okami"){};
    std::unique_ptr<ViaductAPI> create(const dict<std::string, std::string> &args)
    {
        return std::make_unique<OkamiImpl>();
    }
} exampleArch;
} // namespace

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-22  gatecat <gatecat@ds0.me>
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

#include "fabric_parsing.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "viaduct_api.h"
#include "viaduct_helpers.h"

#include <fstream>

#define GEN_INIT_CONSTIDS
#define VIADUCT_CONSTIDS "viaduct/fabulous/constids.inc"
#include "viaduct_constids.h"

#include "fab_cfg.h"
#include "fab_defs.h"
#include "fasm.h"
#include "pack.h"
#include "validity_check.h"

#include <boost/filesystem.hpp>

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct FabulousImpl : ViaductAPI
{
    FabulousImpl(const dict<std::string, std::string> &args)
    {
        for (auto a : args) {
            if (a.first == "fasm")
                fasm_file = a.second;
            else
                log_error("unrecognised fabulous option '%s'\n", a.first.c_str());
        }
    }
    ~FabulousImpl(){};
    void init(Context *ctx) override
    {
        init_uarch_constids(ctx);
        ViaductAPI::init(ctx);
        h.init(ctx);
        fab_root = get_env_var("FAB_ROOT", ", set it to the fabulous build output or project path");
        if (boost::filesystem::exists(fab_root + "/.FABulous"))
            is_new_fab = true;
        else
            is_new_fab = false;
        log_info("Detected FABulous %s format project.\n", is_new_fab ? "2.0" : "1.0");
        // To consider: a faster serialised form of the device data (like bba that other arches use) so we don't have to
        // go through the whole csv parsing malarkey each time
        blk_trk = std::make_unique<BlockTracker>(ctx, cfg);
        is_new_fab ? init_bels_v2() : init_bels_v1();
        init_pips();
        ctx->setDelayScaling(0.25, 0.5);
    }

    void pack() override { fabulous_pack(ctx, cfg); }

    void postRoute() override
    {
        if (!fasm_file.empty())
            fabulous_write_fasm(ctx, cfg, fasm_file);
    }

    void prePlace() override { assign_cell_info(); }
    bool isBelLocationValid(BelId bel, bool explain_invalid) const override
    {
        return blk_trk->check_validity(bel, cfg, cell_tags);
    }

  private:
    FabricConfig cfg; // TODO: non-default config
    ViaductHelpers h;

    WireId global_clk_wire;

    std::string fasm_file;

    std::unique_ptr<BlockTracker> blk_trk;

    std::string get_env_var(const std::string &name, const std::string &prompt = "")
    {
        const char *var = getenv(name.c_str());
        if (var == nullptr)
            log_error("environment variable '%s' is not set%s\n", name.c_str(), prompt.c_str());
        return std::string(var);
    }

    std::ifstream open_data_rel(const std::string &postfix)
    {
        const std::string filename(fab_root + postfix);
        std::ifstream in(filename);
        if (!in)
            log_error("failed to open data file '%s' (is FAB_ROOT set correctly?)\n", filename.c_str());
        return in;
    }

    std::string fab_root;
    bool is_new_fab;

    pool<IdString> warned_beltypes;

    void add_pseudo_pip(WireId src, WireId dst, IdString pip_type)
    {
        const auto &src_data = ctx->wire_info(src);
        IdStringList pip_name = IdStringList::concat(ctx->getWireName(src), ctx->getWireName(dst));
        ctx->addPip(pip_name, pip_type, src, dst, ctx->getDelayFromNS(0.05), Loc(src_data.x, src_data.y, 0));
    }

    void handle_bel_ports(BelId bel, IdString tile, IdString bel_type, const std::vector<parser_view> &ports)
    {
        // TODO: improve the scalability here as we support more bel types
        IdString idx = ctx->getBelName(bel)[1];
        Loc loc = ctx->getBelLocation(bel);
        if (bel_type == id_IO_1_bidirectional_frame_config_pass) {
            for (parser_view p : ports) {
                IdString port_id = p.to_id(ctx);
                WireId port_wire = get_wire(tile, port_id, ctx->idf("W_IO_%s", port_id.c_str(ctx)));
                IdString pin = p.back(1).to_id(ctx);
                ctx->addBelPin(bel, pin, port_wire, pin.in(id_I, id_T) ? PORT_IN : PORT_OUT);
            }
        } else if (bel_type.in(id_InPass4_frame_config, id_OutPass4_frame_config)) {
            for (parser_view p : ports) {
                IdString port_id = p.to_id(ctx);
                WireId port_wire = get_wire(tile, port_id, port_id);
                IdString pin = p.back(2).to_id(ctx);
                ctx->addBelPin(bel, pin, port_wire, bel_type == id_OutPass4_frame_config ? PORT_IN : PORT_OUT);
            }
        } else if (bel_type == id_RegFile_32x4) {
            WireId clk_wire = get_wire(tile, id_CLK, id_REG_CLK);
            ctx->addBelInput(bel, id_CLK, clk_wire);
            add_pseudo_pip(global_clk_wire, clk_wire, id_global_clock);
            for (parser_view p : ports) {
                IdString port_id = p.to_id(ctx);
                // TODO: nicer way of determining port type?
                if (p[0] == 'D') {
                    ctx->addBelInput(bel, port_id, get_wire(tile, port_id, id_WRITE_DATA));
                } else if (p[0] == 'W') {
                    ctx->addBelInput(bel, port_id, get_wire(tile, port_id, id_WRITE_ADDRESS));
                } else if (p[1] == 'D') {
                    ctx->addBelOutput(bel, port_id, get_wire(tile, port_id, id_READ_DATA));
                } else {
                    ctx->addBelInput(bel, port_id, get_wire(tile, port_id, id_READ_ADDRESS));
                }
            }
        } else if (bel_type == id_MULADD) {
            // TODO: do DSPs need a clock too like regfiles?
            for (parser_view p : ports) {
                IdString port_id = p.to_id(ctx);
                if (p[0] == 'Q') {
                    ctx->addBelOutput(bel, port_id, get_wire(tile, port_id, id_DSP_DATA_OUT));
                } else if (port_id == id_clr) {
                    ctx->addBelInput(bel, port_id, get_wire(tile, port_id, id_DSP_CLR));
                } else {
                    ctx->addBelInput(bel, port_id, get_wire(tile, port_id, id_DSP_DATA_IN));
                }
            }
        } else if (bel_type == id_MUX8LUT_frame_config) {
            for (parser_view p : ports) {
                IdString port_id = p.to_id(ctx);
                ctx->addBelPin(bel, port_id, get_wire(tile, port_id, ctx->idf("LUTMUX_%s", port_id.c_str(ctx))),
                               p[0] == 'M' ? PORT_OUT : PORT_IN);
            }
        } else if (bel_type == id_FABULOUS_LC) {
            // TODO: split LC mode, LUT permutation pseudo-switchbox, LUT thru pseudo-pips
            WireId clk_wire = get_wire(tile, ctx->idf("L%s_CLK", idx.c_str(ctx)), id_LUT_CLK);
            ctx->addBelInput(bel, id_CLK, clk_wire);
            add_pseudo_pip(global_clk_wire, clk_wire, id_global_clock);
            blk_trk->set_bel_type(bel, BelFlags::BLOCK_CLB, BelFlags::FUNC_LC_COMB, loc.z);
            for (parser_view p : ports) {
                IdString port_id = p.to_id(ctx);
                WireId port_wire = get_wire(tile, port_id, ctx->idf("LUT_%s", port_id.c_str(ctx)));
                // TODO: more robust port name handling
                if (p[3] == 'S' || p[3] == 'E' || p[3] == 'I') { // set/reset, enable, LUT input
                    ctx->addBelInput(bel, p.substr(3).to_id(ctx), port_wire);
                } else if (p[3] == 'O') { // LUT otuput
                    ctx->addBelOutput(bel, p.substr(3, 1).to_id(ctx), port_wire);
                } else if (p[3] == 'C') { // carry chain
                    if (p[4] == 'i') {
                        ctx->addBelInput(bel, id_Ci, port_wire);
                    } else {
                        NPNR_ASSERT(p[4] == 'o');
                        ctx->addBelOutput(bel, id_Co, port_wire);
                    }
                } else {
                    log_error("don't know what to do with LC port '%s'\n", port_id.c_str(ctx));
                }
            }
        } else {
            // ...
            if (!warned_beltypes.count(bel_type) && !ports.empty()) {
                log_warning("don't know how to handle ports for bel type '%s'\n", bel_type.c_str(ctx));
                warned_beltypes.insert(bel_type);
            }
        }
    }

    void init_global_clock()
    {
        // TODO: how do we extend this to more complex clocking topologies?
        BelId global_clk_bel =
                ctx->addBel(IdStringList::concat(ctx->id("X0Y0"), id_CLK), id_Global_Clock, Loc(0, 0, 0), true, false);
        global_clk_wire = ctx->addWire(IdStringList::concat(ctx->id("X0Y0"), id_CLK), id_CLK, 0, 0);
        ctx->addBelOutput(global_clk_bel, id_CLK, global_clk_wire);
    }

    // TODO: this is for legacy fabulous only, the new code path can be a lot simpler
    void init_bels_v1()
    {
        std::ifstream in = open_data_rel("/npnroutput/bel.txt");
        CsvParser csv(in);
        init_global_clock();
        while (csv.fetch_next_line()) {
            IdString tile = csv.next_field().to_id(ctx);
            int bel_x = csv.next_field().substr(1).to_int();
            int bel_y = csv.next_field().substr(1).to_int();
            auto bel_idx = csv.next_field();
            IdString bel_type = csv.next_field().to_id(ctx);
            NPNR_ASSERT(bel_idx.size() == 1);
            int bel_z = bel_idx[0] - 'A';
            NPNR_ASSERT(bel_z >= 0 && bel_z < 26);
            /*
            In the future we will need to handle optionally splitting SLICEs into separate LUT/COMB and FF bels
            This is the preferred approach in nextpnr for arches where the LUT and FF can be used separately of
            each other (e.g. there is a way of routing the LUT and FF outputs individually, and some extra
            optional FF input).
            While this isn't yet the standard fabulous SLICE, it should be considered as a future option in fabulous.
            */
            Loc loc(bel_x, bel_y, bel_z);
            BelId bel = ctx->addBel(IdStringList::concat(tile, bel_idx.to_id(ctx)), bel_type, loc, false, false);
            std::vector<parser_view> ports;
            parser_view port;
            while (!(port = csv.next_field()).empty()) {
                ports.push_back(port);
            }
            handle_bel_ports(bel, tile, bel_type, ports);
        }
        postprocess_bels();
    }

    void init_bels_v2()
    {
        std::ifstream in = open_data_rel("/.FABulous/bel.v2.txt");
        CsvParser csv(in);
        init_global_clock();
        BelId curr_bel;
        while (csv.fetch_next_line()) {
            IdString cmd = csv.next_field().to_id(ctx);
            if (cmd == id_BelBegin) {
                IdString tile = csv.next_field().to_id(ctx);
                auto bel_idx = csv.next_field();
                IdString bel_type = csv.next_field().to_id(ctx);
                NPNR_ASSERT(bel_idx.size() == 1);
                int bel_z = bel_idx[0] - 'A';
                NPNR_ASSERT(bel_z >= 0 && bel_z < 26);
                Loc loc = tile_loc(tile);
                curr_bel = ctx->addBel(IdStringList::concat(tile, bel_idx.to_id(ctx)), bel_type,
                                       Loc(loc.x, loc.y, bel_z), false, false);
            } else if (cmd.in(id_I, id_O)) {
                IdString port = csv.next_field().to_id(ctx);
                auto wire_name = csv.next_field().split('.');
                WireId wire =
                        get_wire(wire_name.first.to_id(ctx), wire_name.second.to_id(ctx), wire_name.second.to_id(ctx));
                ctx->addBelPin(curr_bel, port, wire, cmd == id_O ? PORT_OUT : PORT_IN);
            } else if (cmd == id_GlobalClk) {
                IdStringList bel_name = ctx->getBelName(curr_bel);
                WireId clk_wire = get_wire(bel_name[0], ctx->idf("%s_CLK", bel_name[1].c_str(ctx)), id_REG_CLK);
                ctx->addBelInput(curr_bel, id_CLK, clk_wire);
                add_pseudo_pip(global_clk_wire, clk_wire, id_global_clock);
            } else if (cmd == id_CFG) {
                // TODO...
            } else if (cmd == id_BelEnd) {
                curr_bel = BelId();
            } else if (cmd != IdString()) {
                log_error("unsupported command %s in definition of bel %s\n", cmd.c_str(ctx),
                          curr_bel == BelId() ? "<none>" : ctx->nameOfBel(curr_bel));
            }
        }
        postprocess_bels();
    }

    void generate_split_mux8(BelId bel)
    {
        // _don't_ take a reference here because it might be invalidated by adding bels
        auto data = ctx->bel_info(bel);
        const std::array<IdString, 4> mux_outs{id_M_AB, id_M_AD, id_M_EF, id_M_AH};
        for (unsigned k = 1; k <= 3; k++) {
            // create MUX2 through 8
            unsigned m = (1U << k);
            for (unsigned i = 0; i < 8; i += m) {
                // mux indexing scheme
                //  - MUX2s are at (z % 2) == 0
                //  - MUX4s are at (z % 4) == 1
                //  - MUX8s are at (z % 8) == 7
                int idx = (m == 2) ? i : (m == 4) ? (i + 1) : (i + 7);
                BelId mux =
                        ctx->addBel(IdStringList::concat(data.name[0], ctx->idf("MUX%d_%d", m, i)),
                                    ctx->idf("FABULOUS_MUX%d", m), Loc(data.x, data.y, data.z + 1 + idx), false, false);
                blk_trk->set_bel_type(mux, BelFlags::BLOCK_CLB, BelFlags::FUNC_MUX, idx);
                // M data inputs
                for (unsigned j = 0; j < m; j++) {
                    ctx->addBelInput(mux, ctx->idf("I%d", j), data.pins.at(ctx->idf("%c", char('A' + i + j))).wire);
                }
                // K select inputs
                for (unsigned j = 0; j < k; j++) {
                    ctx->addBelInput(mux, ctx->idf("S%d", j),
                                     data.pins.at(ctx->idf("S%d", (m == 8 && j == 2) ? 3 : ((i / m) * k + j))).wire);
                }
                // Output
                IdString output = (m == 2)   ? mux_outs.at(i / m)
                                  : (m == 4) ? mux_outs.at((i / m) * k + 1)
                                             : mux_outs.at(3);
                ctx->addBelOutput(mux, id_O, data.pins.at(output).wire);
            }
        }
    }

    void postprocess_bels()
    {
        // This does some post-processing on bels to make them useful for nextpnr place-and-route regardless of the code
        // path that creates them. In the future, splitting muxes and creating split LCs would be done here, too
        for (auto bel : ctx->getBels()) {
            // _don't_ take a reference here because it might be invalidated by adding bels
            auto data = ctx->bel_info(bel);
            if (data.type == id_FABULOUS_LC) {
                if (!data.pins.count(id_Q)) {
                    // Add a Q pseudo-pin and pseudo-pip from Q to O
                    WireId o_wire = ctx->getBelPinWire(bel, id_O);
                    IdString q_name = ctx->idf("%s_Q", data.name[1].c_str(ctx));
                    WireId q_wire = get_wire(data.name[0], q_name, q_name);
                    ctx->addBelOutput(bel, id_Q, q_wire);
                    // Pseudo-pip for FF mode
                    add_pseudo_pip(q_wire, o_wire, id_O2Q);
                }
            } else if (data.type.in(id_MUX8LUT_frame_config, id_MUX8LUT_frame_config_mux)) {
                generate_split_mux8(bel);
                ctx->bel_info(bel).hidden = true;
            } else if (data.type == id_IO_1_bidirectional_frame_config_pass) {
                if (!data.pins.count(id_PAD)) {
                    // Add a PAD pseudo-pin for the top level
                    ctx->addBelInout(bel, id_PAD,
                                     get_wire(data.name[0], ctx->idf("PAD_%s", data.name[1].c_str(ctx)), id_PAD));
                }
            }
        }
    }

    void init_pips()
    {
        std::ifstream in = open_data_rel(is_new_fab ? "/.FABulous/pips.txt" : "/npnroutput/pips.txt");
        CsvParser csv(in);
        while (csv.fetch_next_line()) {
            IdString src_tile = csv.next_field().to_id(ctx);
            IdString src_port = csv.next_field().to_id(ctx);
            IdString dst_tile = csv.next_field().to_id(ctx);
            IdString dst_port = csv.next_field().to_id(ctx);
            int delay = csv.next_field().to_int();
            IdString pip_name = csv.next_field().to_id(ctx);
            WireId src_wire = get_wire(src_tile, src_port, src_port);
            WireId dst_wire = get_wire(dst_tile, dst_port, dst_port);
            ctx->addPip(IdStringList::concat(src_tile, pip_name), pip_name, src_wire, dst_wire,
                        ctx->getDelayFromNS(0.01 * delay), tile_loc(src_tile));
        }
    }

    // Fast lookup of tile names to XY pairs
    dict<IdString, Loc> tile2loc;
    Loc tile_loc(IdString tile)
    {
        if (!tile2loc.count(tile)) {
            std::string tile_name = tile.str(ctx);
            parser_view view(tile_name);
            NPNR_ASSERT(view[0] == 'X');
            size_t ypos = view.find('Y');
            NPNR_ASSERT(ypos != parser_view::npos);
            int x = view.substr(1, ypos - 1).to_int();
            int y = view.substr(ypos + 1).to_int();
            tile2loc[tile] = Loc(x, y, 0);
        }
        return tile2loc.at(tile);
    }

    // Create a wire if it doesn't exist, otherwise just return it
    WireId get_wire(IdString tile, IdString wire, IdString type)
    {
        // Create a wire name by using the built-in IdStringList mechanism to store a (tile, wire) pair
        // this way we don't store a full string in memory of every concatenated wire name, reducing the memory
        // footprint and start time significantly beyond the ~1k LUT scale
        auto wire_name = IdStringList::concat(tile, wire);
        auto found = ctx->wire_by_name.find(wire_name);
        if (found != ctx->wire_by_name.end())
            return found->second;
        // doesn't exist
        Loc loc = tile_loc(tile);
        return ctx->addWire(wire_name, type, loc.x, loc.y);
    }

    CellTagger cell_tags;
    void assign_cell_info()
    {
        for (auto &cell : ctx->cells) {
            cell_tags.assign_for(ctx, cfg, cell.second.get());
        }
    }
    void notifyBelChange(BelId bel, CellInfo *cell)
    {
        CellInfo *old = ctx->getBoundBelCell(bel);
        blk_trk->update_bel(bel, old, cell);
    }
};

struct FabulousArch : ViaductArch
{
    FabulousArch() : ViaductArch("fabulous"){};
    std::unique_ptr<ViaductAPI> create(const dict<std::string, std::string> &args)
    {
        return std::make_unique<FabulousImpl>(args);
    }
} fabulousArch;
} // namespace

NEXTPNR_NAMESPACE_END

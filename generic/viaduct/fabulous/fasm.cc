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

#include "fasm.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include <algorithm>
#include <boost/range/adaptor/reversed.hpp>
#include <fstream>

#define VIADUCT_CONSTIDS "viaduct/fabulous/constids.inc"
#include "viaduct_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct FabFasmWriter
{
    FabFasmWriter(const Context *ctx, const FabricConfig &cfg, const std::vector<PseudoPipTags> &pip_tags,
                  const std::string &filename)
            : ctx(ctx), cfg(cfg), pip_tags(pip_tags), out(filename)
    {
        if (!out)
            log_error("failed to open fasm file '%s' for writing\n", filename.c_str());
    }
    std::string format_name(IdStringList name)
    {
        std::string result;
        for (IdString entry : name) {
            if (!result.empty())
                result += ".";
            result += entry.str(ctx);
        }
        return result;
    }
    void write_pip(PipId pip)
    {
        auto &data = ctx->pip_info(pip);
        if (pip.index < int(pip_tags.size()) && pip_tags.at(pip.index).type != PseudoPipTags::NONE) {
            // pseudo PIP
            const auto &tag = pip_tags.at(pip.index);
            if (tag.type == PseudoPipTags::LUT_CONST) {
                NPNR_ASSERT(ctx->checkBelAvail(tag.bel));
                prefix = format_name(ctx->getBelName(tag.bel)) + ".";
                write_int_vector(stringf("INIT[%d:0]", (1U << cfg.clb.lut_k) - 1), 0, 1U << cfg.clb.lut_k,
                                 (tag.data & 0x1));
                prefix = "";
            }
            return;
        }
        if (data.type.in(id_global_clock, id_O2Q) || data.type.c_str(ctx)[0] == '$')
            return; // pseudo-pips with no underlying bitstream bits
        // write pip name but with '.' instead of '/' for separator
        out << format_name(data.name) << std::endl;
    }
    void write_routing(const NetInfo *net)
    {
        std::vector<PipId> sorted_pips;
        for (auto &w : net->wires)
            if (w.second.pip != PipId())
                sorted_pips.push_back(w.second.pip);
        std::sort(sorted_pips.begin(), sorted_pips.end());
        out << stringf("# routing for net '%s'\n", ctx->nameOf(net)) << std::endl;
        for (auto pip : sorted_pips)
            write_pip(pip);
        out << std::endl;
    }

    std::string prefix;

    // Write a FASM bitvector; optionally inverting the values in the process
    void write_vector(const std::string &name, const std::vector<bool> &value, bool invert = false)
    {
        out << prefix << name << " = " << int(value.size()) << "'b";
        for (auto bit : boost::adaptors::reverse(value))
            out << ((bit ^ invert) ? '1' : '0');
        out << std::endl;
    }
    // Write a FASM bitvector given an integer value
    void write_int_vector(const std::string &name, uint64_t value, int width, bool invert = false)
    {
        std::vector<bool> bits(width, false);
        for (int i = 0; i < width; i++)
            bits[i] = (value & (1ULL << unsigned(i))) != 0;
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

    void write_bool(const CellInfo *cell, const std::string &name)
    {
        if (bool_or_default(cell->params, ctx->id(name))) {
            out << prefix << name << std::endl;
        }
    }

    void add_feature(const std::string &name) { out << prefix << name << std::endl; }

    uint64_t depermute_lut(const CellInfo *lut)
    {
        uint64_t orig_init = 0;
        if (lut->params.count(id_INIT))
            orig_init = lut->params.at(id_INIT).as_int64();
        std::vector<std::vector<unsigned>> phys_to_log;
        phys_to_log.resize(cfg.clb.lut_k);
        for (unsigned i = 0; i < cfg.clb.lut_k; i++) {
            WireId pin_wire = ctx->getBelPinWire(lut->bel, ctx->idf("I%d", i));
            for (PipId pip : ctx->getPipsUphill(pin_wire)) {
                if (!ctx->getBoundPipNet(pip))
                    continue;
                unsigned pip_data = pip_tags.at(pip.index).data;
                unsigned from_pin = (pip_data >> 4) & 0xF;
                unsigned to_pin = (pip_data)&0xF;
                NPNR_ASSERT(to_pin == i);
                phys_to_log[from_pin].push_back(i);
            }
        }
        if (bool_or_default(lut->params, id_I0MUX, false)) // internal, hardcoded
            phys_to_log[0].push_back(0);
        for (unsigned i = 0; i < cfg.clb.lut_k; i++) {
            for (auto j : phys_to_log.at(i))
                out << stringf(" # permute phys %d log %d\n", i, j);
        }
        uint64_t permuted_init = 0;
        for (unsigned i = 0; i < (1U << cfg.clb.lut_k); i++) {
            unsigned log_idx = 0;
            for (unsigned j = 0; j < cfg.clb.lut_k; j++) {
                if ((i >> j) & 0x1) {
                    for (auto log_pin : phys_to_log[j])
                        log_idx |= (1 << log_pin);
                }
            }
            if ((orig_init >> log_idx) & 0x1)
                permuted_init |= (uint64_t(1U) << i);
        }
        return permuted_init;
    }

    void write_logic(const CellInfo *lc)
    {
        prefix = format_name(ctx->getBelName(lc->bel)) + ".";
        if (lc->type.in(id_FABULOUS_LC, id_FABULOUS_COMB)) {
            uint64_t init = depermute_lut(lc);
            unsigned width = 1U << cfg.clb.lut_k;
            write_int_vector(stringf("INIT[%d:0]", width - 1), init, width); // todo lut depermute and thru
            if (bool_or_default(lc->params, id_I0MUX, false))
                add_feature("IOmux"); // typo in FABulous?
        }
        if (lc->type == id_FABULOUS_LC) {
            write_bool(lc, "FF");
        }
        if (lc->type.in(id_FABULOUS_LC, id_FABULOUS_FF)) {
            write_bool(lc, "SET_NORESET");
            write_bool(lc, "NEG_CLK");
            write_bool(lc, "NEG_EN");
            write_bool(lc, "NEG_SR");
            write_bool(lc, "ASYNC_SR");
        }
        if (lc->type.in(id_FABULOUS_MUX4, id_FABULOUS_MUX8)) {
            // TODO: don't hardcode prefix
            out << prefix << "I.c0" << std::endl;
        }
        if (lc->type == id_FABULOUS_MUX8) {
            // TODO: don't hardcode prefix
            out << prefix << "I.c1" << std::endl;
        }
    }

    void write_io(const CellInfo *io)
    {
#if 0
        prefix = format_name(ctx->getBelName(io->bel)) + ".";
        write_bool(io, "INPUT_USED");
        write_bool(io, "OUTPUT_USED");
        write_bool(io, "ENABLE_USED");
#endif
    }

    void write_generic_cell(const CellInfo *ci)
    {
        prefix = format_name(ctx->getBelName(ci->bel)) + ".";
        for (auto &param : ci->params) {
            // TODO: better parameter type auto-detection
            if (param.second.is_string) {
                // enum type parameter
                out << prefix << param.first.c_str(ctx) << "." << param.second.str << std::endl;
            } else if (param.second.str.size() == 1) {
                // boolean type parameter
                if (param.second.intval != 0)
                    out << prefix << param.first.c_str(ctx) << std::endl;
            } else {
                // vector type parameter
                int msb = int(param.second.str.size()) - 1;
                out << prefix << param.first.c_str(ctx) << "[" << msb << ":0] = ";
                for (auto bit : boost::adaptors::reverse(param.second.str))
                    out << bit;
                out << std::endl;
            }
        }
    }

    void write_iopass(const CellInfo *ci)
    {
        Loc loc = ctx->getBelLocation(ci->bel);
        // We use 'nice' names based on function for the IOPass bels inside nextpnr
        // but in the bitstream we need to use character names
        NPNR_ASSERT(loc.z <= 26);
        prefix = stringf("X%dY%d.%c.", loc.x, loc.y, 'A' + loc.z);
        if (ci->params.count(id_I_reg)) {
            uint64_t regval = int_or_default(ci->params, id_I_reg);
            for (unsigned i = 0; i < 4; i++) {
                if (regval & (1 << i))
                    out << prefix << stringf("I%d_reg", i) << std::endl;
            }
        }
        prefix = "";
    }

    void write_cell(const CellInfo *ci)
    {
        out << stringf("# config for cell '%s'\n", ctx->nameOf(ci)) << std::endl;
        if (ci->type.in(id_FABULOUS_COMB, id_FABULOUS_FF, id_FABULOUS_LC, id_FABULOUS_MUX2, id_FABULOUS_MUX4,
                        id_FABULOUS_MUX8))
            write_logic(ci);
        else if (ci->type == id_IO_1_bidirectional_frame_config_pass)
            write_io(ci);
        else if (ci->type.in(id_InPass4_frame_config, id_OutPass4_frame_config))
            write_iopass(ci);
        else
            write_generic_cell(ci);
        // TODO: other cell types
        out << std::endl;
    }

    void write_fasm()
    {
        for (const auto &net : ctx->nets)
            write_routing(net.second.get());
        for (const auto &cell : ctx->cells)
            write_cell(cell.second.get());
    }

    const Context *ctx;
    const FabricConfig &cfg;
    const std::vector<PseudoPipTags> &pip_tags;
    std::ofstream out;
};
} // namespace

void fabulous_write_fasm(const Context *ctx, const FabricConfig &cfg, const std::vector<PseudoPipTags> &pip_tags,
                         const std::string &filename)
{
    FabFasmWriter wr(ctx, cfg, pip_tags, filename);
    wr.write_fasm();
}

NEXTPNR_NAMESPACE_END

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

#include "pack.h"
#include "log.h"
#include "util.h"

#define VIADUCT_CONSTIDS "viaduct/fabulous/constids.inc"
#include "viaduct_constids.h"
#include "viaduct_helpers.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct FabulousPacker
{
    Context *ctx;
    const FabricConfig &cfg;
    ViaductHelpers h;

    dict<IdString, unsigned> lut_types;
    std::vector<IdString> lut_inputs;

    FabulousPacker(Context *ctx, const FabricConfig &cfg) : ctx(ctx), cfg(cfg)
    {
        // Set up some structures for faster lookups
        for (unsigned i = 0; i < cfg.clb.lut_k; i++) {
            lut_types[ctx->idf("LUT%d", i + 1)] = i + 1;
            lut_inputs.push_back(ctx->idf("I%d", i));
        }
        h.init(ctx);
    }

    void pack_luts()
    {
        // Pack LUTs into FABULOUS_COMB (split-LUTFF mode) or FABULOUS_LC (packed-LUTFF mode)
        // TODO: fracturable LUT handling
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            auto fnd_lut = lut_types.find(ci->type);
            if (fnd_lut == lut_types.end())
                continue;
            unsigned lut_n = fnd_lut->second;
            // convert to the necessary type
            ci->type = cfg.clb.split_lc ? id_FABULOUS_COMB : id_FABULOUS_LC;
            // add disconnected unused inputs
            for (unsigned i = 0; i < cfg.clb.lut_k; i++)
                if (!ci->ports.count(lut_inputs.at(i)))
                    ci->addInput(lut_inputs.at(i));
            // replicate the INIT value so the unused MSBs become don't-cares
            auto inst_init = get_or_default(ci->params, id_INIT, Property(0));
            unsigned orig_init_len = 1U << lut_n, prim_len = 1U << cfg.clb.lut_k;
            Property new_init(0, prim_len);
            for (unsigned i = 0; i < prim_len; i += orig_init_len) {
                auto chunk = inst_init.extract(0, orig_init_len);
                for (unsigned j = 0; j < orig_init_len; j++)
                    new_init.str.at(i + j) = chunk.str.at(j);
            }
            new_init.update_intval();
            ci->params[id_INIT] = new_init;
        }
    }

    // Two-stage flipflop packing. First convert all the random primitives into a much easier-to-handle FABULOUS_FF
    // Then for split-LC mode, cluster it to connected LUTs; separate-LC mode, pack it into a connected or new LC

    void prepare_ffs()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            const std::string &type_str = ci->type.str(ctx);
            if (type_str.size() < 5 || type_str.substr(0, 5) != "LUTFF")
                continue;
            ci->type = id_FABULOUS_FF;
            // parse config string and unify
            size_t idx = 5;
            if (idx < type_str.size() && type_str.at(idx) == '_')
                ++idx;
            // clock inversion
            if (idx < type_str.size() && type_str.at(idx) == 'N') {
                ci->params[id_NEG_CLK] = 1;
                ++idx;
            } else {
                ci->params[id_NEG_CLK] = 0;
            }
            // clock enable
            if (idx < type_str.size() && type_str.at(idx) == 'E')
                ++idx;
            if (ci->ports.count(id_E))
                ci->renamePort(id_E, id_EN);
            else
                ci->addInput(id_EN); // autocreate emtpy enable port if enable missing or unused
            // sr presence and type
            std::string srt = type_str.substr(idx);
            if (srt == "S") {
                ci->params[id_SET_NORESET] = 1;
                ci->params[id_ASYNC_SR] = 1;
            } else if (srt == "R") {
                ci->params[id_SET_NORESET] = 0;
                ci->params[id_ASYNC_SR] = 1;
            } else if (srt == "SS") {
                ci->params[id_SET_NORESET] = 1;
                ci->params[id_ASYNC_SR] = 0;
            } else if (srt == "SR" || srt == "") {
                ci->params[id_SET_NORESET] = 0;
                ci->params[id_ASYNC_SR] = 0;
            } else {
                NPNR_ASSERT_FALSE("unhandled FF type");
            }
            if (ci->ports.count(id_S))
                ci->renamePort(id_S, id_SR);
            else if (ci->ports.count(id_R))
                ci->renamePort(id_R, id_SR);
            if (!ci->ports.count(id_SR))
                ci->addInput(id_SR); // autocreate emtpy enable port if enable missing or unused
        }
    }

    void pack_muxes()
    {
        // TODO: don't hardcode z-offset -- we should come up with our own constraint structure
        int lut_muxes_dz = 9;
        int lut_lut_dz = 1;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            unsigned k = 0;
            if (ci->type == id_FABULOUS_MUX2)
                k = 1;
            else if (ci->type == id_FABULOUS_MUX4)
                k = 2;
            else if (ci->type == id_FABULOUS_MUX8)
                k = 3;
            else
                continue;
            unsigned m = (1U << k);
            std::vector<CellInfo *> luts;
            for (unsigned i = 0; i < m; i++) {
                NetInfo *ii = ci->getPort(ctx->idf("I%d", i));
                if (!ii || !ii->driver.cell || !ii->driver.cell->type.in(id_FABULOUS_LC, id_FABULOUS_COMB) ||
                    ii->driver.port != id_O)
                    log_error("mux %s input I%d net %s is not driven by a LUT!\n", ctx->nameOf(ci), i, ctx->nameOf(ii));
                CellInfo *lut = ii->driver.cell;
                NPNR_ASSERT(lut->cluster == ClusterId());
                luts.push_back(lut);
            }
            luts.at(0)->cluster = luts.at(0)->name;
            for (unsigned i = 0; i < m; i++) {
                luts.at(i)->cluster = luts.at(0)->name;
                luts.at(i)->constr_x = 0;
                luts.at(i)->constr_y = 0;
                luts.at(i)->constr_z = i * lut_lut_dz;
                luts.at(i)->constr_abs_z = false;
                if (i > 0)
                    luts.at(0)->constr_children.push_back(luts.at(i));
            }
            int extra_mux_dz = (m == 8) ? 7 : (m == 4) ? 1 : 0;
            ci->cluster = luts.at(0)->name;
            ci->constr_x = 0;
            ci->constr_y = 0;
            ci->constr_z = lut_muxes_dz + extra_mux_dz;
            ci->constr_abs_z = false;
            luts.at(0)->constr_children.push_back(ci);
        }
    }

    void pack_ffs()
    {
        pool<IdString> to_delete;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_FABULOUS_FF)
                continue;
            NetInfo *d = ci->getPort(id_D);
            if (!d || !d->driver.cell)
                continue;
            CellInfo *drv = d->driver.cell;
            if (drv->type != (cfg.clb.split_lc ? id_FABULOUS_COMB : id_FABULOUS_LC) || d->driver.port != id_O)
                continue;
            if (!cfg.clb.split_lc && d->users.entries() > 1)
                continue; // TODO: could also resolve by duplicating LUT
            if (drv->cluster != ClusterId()) {
                // TODO: actually we can pack these often, we just have to be more careful to check control sets
                continue;
            }
            // we can pack them together
            if (cfg.clb.split_lc) {
                // create/modify cluster and add constraints. copy from an arch where we do this already...
                NPNR_ASSERT_FALSE("unimplemented");
            } else {
                to_delete.insert(ci->name);
                // this connection is packed inside the LC
                ci->disconnectPort(id_D);
                drv->disconnectPort(id_O);
                // move other ports/params
                ci->movePortTo(id_CLK, drv, id_CLK);
                ci->movePortTo(id_SR, drv, id_SR);
                ci->movePortTo(id_EN, drv, id_EN);
                ci->movePortTo(id_O, drv, id_Q);
                drv->params[id_NEG_CLK] = ci->params[id_NEG_CLK];
                drv->params[id_ASYNC_SR] = ci->params[id_ASYNC_SR];
                drv->params[id_SET_NORESET] = ci->params[id_SET_NORESET];
                drv->params[id_FF] = 1;
                for (auto &attr : ci->attrs)
                    drv->attrs[attr.first] = attr.second;
            }
        }
        for (auto del : to_delete)
            ctx->cells.erase(del);
        if (!cfg.clb.split_lc) {
            // convert remaining ffs to their own lc
            for (auto &cell : ctx->cells) {
                CellInfo *ci = cell.second.get();
                if (ci->type != id_FABULOUS_FF)
                    continue;
                ci->type = id_FABULOUS_LC;
                ci->renamePort(id_D, lut_inputs.at(0));
                ci->renamePort(id_O, id_Q);
                // configure LUT as a thru
                Property init(1U << cfg.clb.lut_k);
                for (unsigned i = 0; i < (1U << cfg.clb.lut_k); i += 2) {
                    init.str[i] = Property::S0;
                    init.str[i + 1] = Property::S1;
                }
                init.update_intval();
                ci->params[id_INIT] = init;
                ci->params[id_FF] = 1;
            }
        }
    }

    void update_bel_attrs()
    {
        // This new arch uses the new IdStringList system with a / separator
        // old fabulous arches used a dot separator in bel names
        // update old attributes for maximum cross-compat
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (!ci->attrs.count(id_BEL))
                continue;
            std::string &bel = ci->attrs.at(id_BEL).str;
            if (bel.find('/') != std::string::npos) // new style
                continue;
            size_t dot_pos = bel.find('.');
            if (dot_pos != std::string::npos)
                bel[dot_pos] = '/';
        }
    }

    void handle_constants()
    {
        const dict<IdString, Property> vcc_params = {{id_INIT, Property(0x3, 2)}};
        const dict<IdString, Property> gnd_params = {{id_INIT, Property(0x0, 2)}};
        h.replace_constants(CellTypePort(id_LUT1, id_O), CellTypePort(id_LUT1, id_O), vcc_params, gnd_params);
    }

    void handle_io()
    {
        // As per the preferred approach for new nextpnr flows, we require IO to be inserted by Yosys
        // pre-place-and-route, or just manually instantiated
        const pool<CellTypePort> top_ports{
                CellTypePort(id_IO_1_bidirectional_frame_config_pass, id_PAD),
        };
        h.remove_nextpnr_iobs(top_ports);
    }

    void run()
    {
        update_bel_attrs();
        handle_constants();
        handle_io();
        pack_luts();
        pack_muxes();
        prepare_ffs();
        pack_ffs();
    }
};
} // namespace

void fabulous_pack(Context *ctx, const FabricConfig &cfg)
{
    FabulousPacker packer(ctx, cfg);
    packer.run();
}

NEXTPNR_NAMESPACE_END

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

#include "validity_check.h"
#include "log.h"
#include "util.h"

#define VIADUCT_CONSTIDS "viaduct/fabulous/constids.inc"
#include "viaduct_constids.h"

NEXTPNR_NAMESPACE_BEGIN

CLBState::CLBState(const LogicConfig &cfg)
{
    // TODO: more than one per LC if in split-SLICE mode with fracturable LUTs
    lc_comb = std::make_unique<CellInfo *[]>(cfg.lc_per_clb);
    if (cfg.split_lc) {
        ff = std::make_unique<CellInfo *[]>(cfg.lc_per_clb * cfg.ff_per_lc);
    }
    mux = std::make_unique<CellInfo *[]>(cfg.lc_per_clb);
    // TODO: mux
}

void CellTagger::assign_for(const Context *ctx, const FabricConfig &cfg, const CellInfo *ci)
{
    if (int(data.size()) <= ci->flat_index)
        data.resize(ci->flat_index + 1);
    auto &t = data.at(ci->flat_index);
    // Use the same logic to handle both packed and split LC modes
    if (ci->type.in(id_FABULOUS_COMB, id_FABULOUS_LC)) {
        unsigned lut_input_count = 0;
        for (unsigned i = 0; i < cfg.clb.lut_k; i++)
            if (ci->getPort(ctx->idf("I%d", i)))
                lut_input_count = i + 1;
        t.comb.lut_inputs = SSOArray<IdString, MAX_LUTK>(lut_input_count, IdString());
        for (unsigned i = 0; i < lut_input_count; i++) {
            const NetInfo *sig = ci->getPort(ctx->idf("I%d", i));
            t.comb.lut_inputs[i] = sig ? sig->name : IdString();
        }
        t.comb.carry_used = ci->getPort(id_Ci) || ci->getPort(id_Co); // TODO
        t.comb.lut_out = ci->getPort(id_O);
    }
    if (ci->type.in(id_FABULOUS_FF, id_FABULOUS_LC)) {
        if (ci->type == id_FABULOUS_FF || bool_or_default(ci->params, id_FF)) {
            t.ff.ff_used = true;
            auto get_ctrlsig = [&](IdString name) {
                const NetInfo *sig = ci->getPort(name);
                bool invert = sig && bool_or_default(ci->params, ctx->idf("NEG_%s", name.c_str(ctx)));
                if (sig && sig->driver.cell && sig->driver.cell->type.in(id__CONST0_DRV, id__CONST1_DRV)) {
                    return ControlSig(((sig->driver.cell->type == id__CONST1_DRV) ^ invert) ? id__CONST1 : id__CONST0,
                                      false);
                }
                return ControlSig(sig ? sig->name : id___disconnected, invert);
            };
            t.ff.clk = get_ctrlsig(id_CLK);
            t.ff.sr = get_ctrlsig(id_SR);
            t.ff.en = get_ctrlsig(id_EN);
            t.ff.async = bool_or_default(ci->params, id_ASYNC_SR);
            t.ff.latch = bool_or_default(ci->params, id_LATCH_NOFF);
            t.ff.d = ci->getPort(id_D);
            t.ff.q = ci->getPort(id_Q);
        } else {
            t.ff.ff_used = false;
        }
    }
}

void BlockTracker::set_bel_type(BelId bel, BelFlags::BlockType block, BelFlags::FuncType func, uint8_t index)
{
    Loc loc = ctx->getBelLocation(bel);
    if (int(tiles.size()) <= loc.y)
        tiles.resize(loc.y + 1);
    auto &row = tiles.at(loc.y);
    if (int(row.size()) <= loc.x)
        row.resize(loc.x + 1);
    auto &tile = row.at(loc.x);
    if (block == BelFlags::BLOCK_CLB) {
        if (!tile.clb)
            tile.clb = std::make_unique<CLBState>(cfg.clb);
    }
    if (int(bel_data.size()) <= bel.index)
        bel_data.resize(bel.index + 1);
    auto &flags = bel_data.at(bel.index);
    flags.block = block;
    flags.func = func;
    flags.index = index;
}

void BlockTracker::update_bel(BelId bel, CellInfo *old_cell, CellInfo *new_cell)
{
    if (bel.index >= int(bel_data.size()))
        return; // some kind of bel not being tracked
    auto flags = bel_data.at(bel.index);
    if (flags.block == BelFlags::BLOCK_OTHER)
        return; // no structures to update
    Loc loc = ctx->getBelLocation(bel);
    if (loc.y >= int(tiles.size()))
        return; // some kind of bel not being tracked
    const auto &row = tiles.at(loc.y);
    if (loc.x >= int(row.size()))
        return; // some kind of bel not being tracked
    const auto &entry = row.at(loc.x);
    if (flags.block == BelFlags::BLOCK_CLB) {
        NPNR_ASSERT(entry.clb);
        // TODO: incremental validity check updates might care about this in the future, hence keeping it in the
        // interface for now
        NPNR_UNUSED(old_cell);
        if (flags.func == BelFlags::FUNC_LC_COMB)
            entry.clb->lc_comb[flags.index] = new_cell;
        else if (flags.func == BelFlags::FUNC_FF)
            entry.clb->ff[flags.index] = new_cell;
        else if (flags.func == BelFlags::FUNC_MUX)
            entry.clb->mux[flags.index] = new_cell;
    }
}

bool CLBState::check_validity(const LogicConfig &cfg, const CellTagger &cell_data)
{
    SSOArray<ControlSig, 2> used_clk(cfg.clk.routing.size()), used_sr(cfg.sr.routing.size()),
            used_en(cfg.en.routing.size());
    auto check_ctrlsig = [&](unsigned idx, ControlSig actual, const ControlSetConfig &ctrl,
                             SSOArray<ControlSig, 2> &used) {
        if (ctrl.can_mask != -1) {
            // Using the per-entry control signal masking
            if (actual.net == id___disconnected || (actual.net == id__CONST0 && ctrl.can_mask == 0) ||
                (actual.net == id__CONST1 && ctrl.can_mask == 0)) {
                return true;
            }
        }
        // see if we have an already-matching signal available
        for (unsigned i = 0; i < ctrl.routing.size(); i++) {
            // doesn't route to this pin
            if (((ctrl.routing.at(i) >> unsigned(idx)) & 0x1U) == 0)
                continue;
            if (used[i] == actual)
                return true;
        }
        // see if we have a free slot available
        for (unsigned i = 0; i < ctrl.routing.size(); i++) {
            // doesn't route to this pin
            if (((ctrl.routing.at(i) >> unsigned(idx)) & 0x1U) == 0)
                continue;
            if (used[i] != ControlSig())
                continue;
            used[i] = actual;
            return true;
        }
        // no option available
        return false;
    };
    for (unsigned z = 0; z < cfg.lc_per_clb; z++) {
        // flipflop control set checking
        if (cfg.split_lc) {
            NPNR_ASSERT_FALSE("unimplemented!"); // TODO
        } else {
            NPNR_ASSERT(cfg.ff_per_lc == 1); // split-slice mode must be used for more
            const CellInfo *lc = lc_comb[z];
            if (!lc)
                continue;
            auto &lct = cell_data.get(lc);
            if (lct.ff.ff_used) {
                // check shared control signals
                if (!check_ctrlsig(z, lct.ff.clk, cfg.clk, used_clk))
                    return false;
                if (cfg.en.have_signal && !check_ctrlsig(z, lct.ff.en, cfg.en, used_en))
                    return false;
                if (cfg.sr.have_signal && !check_ctrlsig(z, lct.ff.sr, cfg.sr, used_sr))
                    return false;
            }
        }
    }
    // don't allow mixed MUX types in the classic fabulous arch where ctrl sigs are shared
    int tile_mux_type = 0;
    for (unsigned z = 0; z < cfg.lc_per_clb; z++) {
        const CellInfo *m = mux[z];
        if (!m)
            continue;
        int this_mux = 0;
        if (m->type == id_FABULOUS_MUX2)
            this_mux = 2;
        else if (m->type == id_FABULOUS_MUX4)
            this_mux = 4;
        else if (m->type == id_FABULOUS_MUX8)
            this_mux = 8;
        else
            NPNR_ASSERT_FALSE("unknown mux type");
        if (tile_mux_type == 0)
            tile_mux_type = this_mux;
        else if (tile_mux_type != this_mux)
            return false;
    }
    // TODO: other checks...
    return true;
}

bool BlockTracker::check_validity(BelId bel, const FabricConfig &cfg, const CellTagger &cell_data)
{
    if (bel.index >= int(bel_data.size()))
        return true; // some kind of bel not being tracked
    auto flags = bel_data.at(bel.index);
    if (flags.block == BelFlags::BLOCK_OTHER)
        return true; // no structures to update
    Loc loc = ctx->getBelLocation(bel);
    if (loc.y >= int(tiles.size()))
        return true; // some kind of bel not being tracked
    const auto &row = tiles.at(loc.y);
    if (loc.x >= int(row.size()))
        return true; // some kind of bel not being tracked
    const auto &entry = row.at(loc.x);
    if (flags.block == BelFlags::BLOCK_CLB) {
        return entry.clb->check_validity(cfg.clb, cell_data);
    } else {
        return true;
    }
}

NEXTPNR_NAMESPACE_END

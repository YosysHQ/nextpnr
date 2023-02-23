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

#include "detail_place_core.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

DetailPlaceCfg::DetailPlaceCfg(Context *ctx)
{
    timing_driven = ctx->setting<bool>("timing_driven");

    hpwl_scale_x = 1;
    hpwl_scale_y = 1;
}

PlacePartition::PlacePartition(Context *ctx)
{
    x0 = ctx->getGridDimX();
    y0 = ctx->getGridDimY();
    x1 = 0;
    y1 = 0;
    for (auto &cell : ctx->cells) {
        if (cell.second->isPseudo())
            continue;
        Loc l = ctx->getBelLocation(cell.second->bel);
        x0 = std::min(x0, l.x);
        x1 = std::max(x1, l.x);
        y0 = std::min(y0, l.y);
        y1 = std::max(y1, l.y);
        cells.push_back(cell.second.get());
    }
}

void PlacePartition::split(Context *ctx, bool yaxis, float pivot, PlacePartition &l, PlacePartition &r)
{
    std::sort(cells.begin(), cells.end(), [&](CellInfo *a, CellInfo *b) {
        Loc l0 = ctx->getBelLocation(a->bel), l1 = ctx->getBelLocation(b->bel);
        return yaxis ? (l0.y < l1.y) : (l0.x < l1.x);
    });
    size_t pivot_point = size_t(cells.size() * pivot);
    l.cells.clear();
    r.cells.clear();
    l.cells.reserve(pivot_point);
    r.cells.reserve(cells.size() - pivot_point);
    int pivot_coord = (pivot_point == 0) ? (yaxis ? y1 : x1)
                                         : (yaxis ? ctx->getBelLocation(cells.at(pivot_point - 1)->bel).y
                                                  : ctx->getBelLocation(cells.at(pivot_point - 1)->bel).x);
    for (size_t i = 0; i < cells.size(); i++) {
        Loc loc = ctx->getBelLocation(cells.at(i)->bel);
        ((yaxis ? loc.y : loc.x) <= pivot_coord ? l.cells : r.cells).push_back(cells.at(i));
    }
    if (yaxis) {
        l.x0 = r.x0 = x0;
        l.x1 = r.x1 = x1;
        l.y0 = y0;
        l.y1 = pivot_coord;
        r.y0 = (pivot_coord == y1) ? y1 : (pivot_coord + 1);
        r.y1 = y1;
    } else {
        l.y0 = r.y0 = y0;
        l.y1 = r.y1 = y1;
        l.x0 = x0;
        l.x1 = pivot_coord;
        r.x0 = (pivot_coord == x1) ? x1 : (pivot_coord + 1);
        r.x1 = x1;
    }
}

void DetailPlacerState::update_global_costs()
{
    last_bounds.resize(flat_nets.size());
    last_tmg_costs.resize(flat_nets.size());
    total_wirelen = 0;
    total_timing_cost = 0;
    for (size_t i = 0; i < flat_nets.size(); i++) {
        NetInfo *ni = flat_nets.at(i);
        if (skip_net(ni))
            continue;
        last_bounds.at(i) = NetBB::compute(ctx, ni);
        total_wirelen += last_bounds.at(i).hpwl(base_cfg);
        if (!timing_skip_net(ni)) {
            auto &tc = last_tmg_costs.at(i);
            tc.resize(ni->users.capacity());
            for (auto usr : ni->users.enumerate()) {
                tc.at(usr.index.idx()) = get_timing_cost(ni, usr.index);
                total_timing_cost += tc.at(usr.index.idx());
            }
        }
    }
}

NetBB NetBB::compute(const Context *ctx, const NetInfo *net, const dict<IdString, BelId> *cell2bel)
{
    NetBB result{};
    if (!net->driver.cell)
        return result;
    auto bel_loc = [&](const CellInfo *cell) {
        if (cell->isPseudo())
            return cell->getLocation();
        BelId bel = cell2bel ? cell2bel->at(cell->name) : cell->bel;
        return ctx->getBelLocation(bel);
    };
    result.nx0 = result.nx1 = result.ny0 = result.ny1 = 1;
    Loc drv_loc = bel_loc(net->driver.cell);
    result.x0 = result.x1 = drv_loc.x;
    result.y0 = result.y1 = drv_loc.y;
    for (auto &usr : net->users) {
        Loc l = bel_loc(usr.cell);
        if (l.x == result.x0)
            ++result.nx0; // on the edge
        else if (l.x < result.x0) {
            result.x0 = l.x; // extends the edge
            result.nx0 = 1;
        }
        if (l.x == result.x1)
            ++result.nx1; // on the edge
        else if (l.x > result.x1) {
            result.x1 = l.x; // extends the edge
            result.nx1 = 1;
        }
        if (l.y == result.y0)
            ++result.ny0; // on the edge
        else if (l.y < result.y0) {
            result.y0 = l.y; // extends the edge
            result.ny0 = 1;
        }
        if (l.y == result.y1)
            ++result.ny1; // on the edge
        else if (l.y > result.y1) {
            result.y1 = l.y; // extends the edge
            result.ny1 = 1;
        }
    }
    return result;
}

void DetailPlacerThreadState::set_partition(const PlacePartition &part)
{
    p = part;
    thread_nets.clear();
    thread_net_idx.resize(g.flat_nets.size());
    std::fill(thread_net_idx.begin(), thread_net_idx.end(), -1);
    // Determine the set of nets that are within the thread; and therefore we care about
    for (auto thread_cell : part.cells) {
        for (auto &port : thread_cell->ports) {
            if (!port.second.net)
                continue;
            int global_idx = port.second.net->udata;
            auto &thread_idx = thread_net_idx.at(global_idx);
            // Already added to the set
            if (thread_idx != -1)
                continue;
            thread_idx = thread_nets.size();
            thread_nets.push_back(port.second.net);
        }
    }
    tmg_ignored_nets.clear();
    ignored_nets.clear();
    for (auto tn : thread_nets) {
        ignored_nets.push_back(g.skip_net(tn));
        tmg_ignored_nets.push_back(g.timing_skip_net(tn));
    }
    // Set up the original cell-bel map for all nets inside the thread
    local_cell2bel.clear();
    for (NetInfo *net : thread_nets) {
        if (net->driver.cell && !net->driver.cell->isPseudo())
            local_cell2bel[net->driver.cell->name] = net->driver.cell->bel;
        for (auto &usr : net->users) {
            if (!usr.cell->isPseudo())
                local_cell2bel[usr.cell->name] = usr.cell->bel;
        }
    }
}

void DetailPlacerThreadState::setup_initial_state()
{
    // Setup initial net bounding boxes and timing costs
    net_bounds.clear();
    arc_tmg_cost.clear();
    for (auto tn : thread_nets) {
        net_bounds.push_back(g.last_bounds.at(tn->udata));
        arc_tmg_cost.push_back(g.last_tmg_costs.at(tn->udata));
    }
    new_net_bounds = net_bounds;
    for (int j = 0; j < 2; j++) {
        auto &a = axes.at(j);
        a.already_bounds_changed.resize(net_bounds.size());
    }
    already_timing_changed.clear();
    already_timing_changed.resize(net_bounds.size());
    for (size_t i = 0; i < thread_nets.size(); i++)
        already_timing_changed.at(i) = std::vector<bool>(thread_nets.at(i)->users.capacity());
}

bool DetailPlacerThreadState::bounds_check(BelId bel)
{
    Loc l = ctx->getBelLocation(bel);
    if (l.x < p.x0 || l.x > p.x1 || l.y < p.y0 || l.y > p.y1)
        return false;
    return true;
}

bool DetailPlacerThreadState::bind_move()
{
#if !defined(NPNR_DISABLE_THREADS)
    std::unique_lock<std::shared_timed_mutex> l(g.archapi_mutex);
#endif
    for (auto &entry : moved_cells) {
        ctx->unbindBel(entry.second.first);
    }
    bool success = true;
    for (auto &entry : moved_cells) {
        // Make sure targets are available before we bind them
        if (!ctx->checkBelAvail(entry.second.second)) {
            success = false;
            break;
        }
        ctx->bindBel(entry.second.second, ctx->cells.at(entry.first).get(), STRENGTH_WEAK);
    }
    arch_state_dirty = true;
    return success;
}

bool DetailPlacerThreadState::check_validity()
{
#if !defined(NPNR_DISABLE_THREADS)
    std::shared_lock<std::shared_timed_mutex> l(g.archapi_mutex);
#endif
    bool result = true;
    for (auto e : moved_cells) {
        if (!ctx->isBelLocationValid(e.second.first)) {
            // Have to check old; too; as unbinding a bel could make a placement illegal by virtue of no longer
            // enabling dedicated routes to be used
            result = false;
            break;
        }
        if (!ctx->isBelLocationValid(e.second.second)) {
            result = false;
            break;
        }
    }
    return result;
}

void DetailPlacerThreadState::revert_move()
{
    if (arch_state_dirty) {
        // If changes to the arch state were made, revert them by restoring original cell bindings
#if !defined(NPNR_DISABLE_THREADS)
        std::unique_lock<std::shared_timed_mutex> l(g.archapi_mutex);
#endif
        for (auto &entry : moved_cells) {
            BelId curr_bound = ctx->cells.at(entry.first)->bel;
            if (curr_bound != BelId())
                ctx->unbindBel(curr_bound);
        }
        for (auto &entry : moved_cells) {
            ctx->bindBel(entry.second.first, ctx->cells.at(entry.first).get(), STRENGTH_WEAK);
        }
        arch_state_dirty = false;
    }
    for (auto &entry : moved_cells)
        local_cell2bel[entry.first] = entry.second.first;
}

void DetailPlacerThreadState::commit_move()
{
    arch_state_dirty = false;
    for (auto &axis : axes) {
        for (auto bc : axis.bounds_changed_nets) {
            // Commit updated net bounds
            net_bounds.at(bc) = new_net_bounds.at(bc);
        }
    }
    if (g.base_cfg.timing_driven) {
        NPNR_ASSERT(timing_changed_arcs.size() == new_timing_costs.size());
        for (size_t i = 0; i < timing_changed_arcs.size(); i++) {
            auto arc = timing_changed_arcs.at(i);
            arc_tmg_cost.at(arc.first).at(arc.second.idx()) = new_timing_costs.at(i);
        }
    }
}

void DetailPlacerThreadState::compute_changes_for_cell(CellInfo *cell, BelId old_bel, BelId new_bel)
{
    Loc new_loc = ctx->getBelLocation(new_bel);
    Loc old_loc = ctx->getBelLocation(old_bel);
    for (const auto &port : cell->ports) {
        NetInfo *pn = port.second.net;
        if (!pn)
            continue;
        int idx = thread_net_idx.at(pn->udata);
        if (ignored_nets.at(idx))
            continue;
        NetBB &new_bounds = new_net_bounds.at(idx);
        // For the x-axis (i=0) and y-axis (i=1)
        for (int i = 0; i < 2; i++) {
            auto &axis = axes.at(i);
            // New and old on this axis
            int new_pos = i ? new_loc.y : new_loc.x, old_pos = i ? old_loc.y : old_loc.x;
            // References to updated bounding box entries
            auto &b0 = i ? new_bounds.y0 : new_bounds.x0;
            auto &n0 = i ? new_bounds.ny0 : new_bounds.nx0;
            auto &b1 = i ? new_bounds.y1 : new_bounds.x1;
            auto &n1 = i ? new_bounds.ny1 : new_bounds.nx1;
            auto &change = axis.already_bounds_changed.at(idx);
            // Lower bound
            if (new_pos < b0) {
                // Further out than current lower bound
                b0 = new_pos;
                n0 = 1;
                if (change == NO_CHANGE) {
                    change = CELL_MOVED_OUTWARDS;
                    axis.bounds_changed_nets.push_back(idx);
                }
            } else if (new_pos == b0 && old_pos > b0) {
                // Moved from inside into current bound
                ++n0;
                if (change == NO_CHANGE) {
                    change = CELL_MOVED_OUTWARDS;
                    axis.bounds_changed_nets.push_back(idx);
                }
            } else if (old_pos == b0 && new_pos > b0) {
                // Moved from current bound to inside
                if (change == NO_CHANGE)
                    axis.bounds_changed_nets.push_back(idx);
                if (n0 == 1) {
                    // Was the last cell on the bound; have to do a full recompute
                    change = FULL_RECOMPUTE;
                } else {
                    --n0;
                    if (change == NO_CHANGE)
                        change = CELL_MOVED_INWARDS;
                }
            }
            // Upper bound
            if (new_pos > b1) {
                // Further out than current upper bound
                b1 = new_pos;
                n1 = 1;
                if (change == NO_CHANGE) {
                    change = CELL_MOVED_OUTWARDS;
                    axis.bounds_changed_nets.push_back(idx);
                }
            } else if (new_pos == b1 && old_pos < b1) {
                // Moved onto current bound
                ++n1;
                if (change == NO_CHANGE) {
                    change = CELL_MOVED_OUTWARDS;
                    axis.bounds_changed_nets.push_back(idx);
                }
            } else if (old_pos == b1 && new_pos < b1) {
                // Moved from current bound to inside
                if (change == NO_CHANGE)
                    axis.bounds_changed_nets.push_back(idx);
                if (n1 == 1) {
                    // Was the last cell on the bound; have to do a full recompute
                    change = FULL_RECOMPUTE;
                } else {
                    --n1;
                    if (change == NO_CHANGE)
                        change = CELL_MOVED_INWARDS;
                }
            }
        }
        // Timing updates if timing driven
        if (g.base_cfg.timing_driven && !tmg_ignored_nets.at(idx)) {
            if (port.second.type == PORT_OUT) {
                int cc;
                TimingPortClass cls = ctx->getPortTimingClass(cell, port.first, cc);
                if (cls != TMG_IGNORE) {
                    for (auto usr : pn->users.enumerate())
                        if (!already_timing_changed.at(idx).at(usr.index.idx())) {
                            timing_changed_arcs.emplace_back(std::make_pair(idx, usr.index));
                            already_timing_changed.at(idx).at(usr.index.idx()) = true;
                        }
                }
            } else {
                auto usr = port.second.user_idx;
                if (!already_timing_changed.at(idx).at(usr.idx())) {
                    timing_changed_arcs.emplace_back(std::make_pair(idx, usr));
                    already_timing_changed.at(idx).at(usr.idx()) = true;
                }
            }
        }
    }
}

void DetailPlacerThreadState::compute_total_change()
{
    auto &xa = axes.at(0), &ya = axes.at(1);
    for (auto &bc : xa.bounds_changed_nets)
        if (xa.already_bounds_changed.at(bc) == FULL_RECOMPUTE)
            new_net_bounds.at(bc) = NetBB::compute(ctx, thread_nets.at(bc), &local_cell2bel);
    for (auto &bc : ya.bounds_changed_nets)
        if (xa.already_bounds_changed.at(bc) != FULL_RECOMPUTE && ya.already_bounds_changed.at(bc) == FULL_RECOMPUTE)
            new_net_bounds.at(bc) = NetBB::compute(ctx, thread_nets.at(bc), &local_cell2bel);
    for (auto &bc : xa.bounds_changed_nets)
        wirelen_delta += (new_net_bounds.at(bc).hpwl(g.base_cfg) - net_bounds.at(bc).hpwl(g.base_cfg));
    for (auto &bc : ya.bounds_changed_nets)
        if (xa.already_bounds_changed.at(bc) == NO_CHANGE)
            wirelen_delta += (new_net_bounds.at(bc).hpwl(g.base_cfg) - net_bounds.at(bc).hpwl(g.base_cfg));
    if (g.base_cfg.timing_driven) {
        NPNR_ASSERT(new_timing_costs.empty());
        for (auto arc : timing_changed_arcs) {
            double new_cost = g.get_timing_cost(thread_nets.at(arc.first), arc.second, &local_cell2bel);
            timing_delta += (new_cost - arc_tmg_cost.at(arc.first).at(arc.second.idx()));
            new_timing_costs.push_back(new_cost);
        }
    }
}

void DetailPlacerThreadState::reset_move_state()
{
    moved_cells.clear();
    cell_rel.clear();
    for (auto &axis : axes) {
        for (auto bc : axis.bounds_changed_nets) {
            new_net_bounds.at(bc) = net_bounds.at(bc);
            axis.already_bounds_changed[bc] = NO_CHANGE;
        }
        axis.bounds_changed_nets.clear();
    }
    for (auto &arc : timing_changed_arcs) {
        already_timing_changed.at(arc.first).at(arc.second.idx()) = false;
    }
    timing_changed_arcs.clear();
    new_timing_costs.clear();
    wirelen_delta = 0;
    timing_delta = 0;
}

bool DetailPlacerThreadState::add_to_move(CellInfo *cell, BelId old_bel, BelId new_bel)
{
    if (!bounds_check(old_bel) || !bounds_check(new_bel))
        return false;
    if (!ctx->isValidBelForCellType(cell->type, new_bel))
        return false;
    NPNR_ASSERT(!moved_cells.count(cell->name));
    moved_cells[cell->name] = std::make_pair(old_bel, new_bel);
    local_cell2bel[cell->name] = new_bel;
    compute_changes_for_cell(cell, old_bel, new_bel);
    return true;
}

NEXTPNR_NAMESPACE_END

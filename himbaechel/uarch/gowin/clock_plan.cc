/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2026  scullymi
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

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <map>
#include <numeric>
#include <queue>

#include "log.h"
#include "nextpnr.h"

#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_helpers.h"

#include "clock_plan.h"
#include "gowin.h"
#include "gowin_utils.h"

NEXTPNR_NAMESPACE_BEGIN

GowinClockPlanner::GowinClockPlanner(Context *ctx, GowinUtils &gwu, std::function<bool(PipId, WireId)> pip_filter)
        : ctx(ctx), gwu(gwu), pip_filter(pip_filter)
{
}

bool GowinClockPlanner::allows(PipId pip, const NetInfo *net) const
{
    WireId dst = ctx->getPipDstWire(pip);
    if (!shared_wires.count(dst)) {
        return true;
    }
    auto plan = net_plans.find(net->name);
    if (plan != net_plans.end()) {
        return plan->second.count(dst);
    }
    auto res = reserved_wires.find(dst);
    return res == reserved_wires.end() || res->second == net->name;
}

bool GowinClockPlanner::is_planned(const NetInfo *net) const { return net_plans.count(net->name); }

// The router names the spines by the prefix of their wire name
bool GowinClockPlanner::is_spine(WireId wire) const
{
    return strncmp(ctx->getWireName(wire)[1].c_str(ctx), "SPINE", 5) == 0;
}

int GowinClockPlanner::spine_number(WireId spine) const
{
    const char *name = ctx->getWireName(spine)[1].c_str(ctx);
    while (*name != 0 && !isdigit(*name)) {
        ++name;
    }
    return atoi(name);
}

// What a gate drives, by a search downhill that stays in the clock network and stops at the
// spines and DCS inputs. Behind a spine the distribution to the sinks begins.
GowinClockPlanner::GateReach GowinClockPlanner::gate_reach(WireId gate) const
{
    GateReach result;
    dict<WireId, PipId> backtrace;
    std::queue<WireId> visit;
    backtrace[gate] = PipId();
    visit.push(gate);
    while (!visit.empty()) {
        WireId cursor = visit.front();
        visit.pop();
        for (PipId pip : ctx->getPipsDownhill(cursor)) {
            WireId dst = ctx->getPipDstWire(pip);
            if (backtrace.count(dst) || !pip_filter(pip, gate)) {
                continue;
            }
            backtrace[dst] = pip;
            if (is_spine(dst) || gwu.is_dcs_input(dst)) {
                std::vector<WireId> &path = result[dst];
                for (WireId w = dst; w != gate; w = ctx->getPipSrcWire(backtrace.at(w))) {
                    path.push_back(w);
                }
            } else if (ctx->getWireType(dst) == id_GLOBAL_CLK) {
                visit.push(dst);
            }
        }
    }
    return result;
}

// The clock network uphill of a sink: every wire on a way from a spine to it, with the resources
// above it. The filter of the router keeps the search inside the clock network. A wire that more
// than one resource reaches is a mux: a tile clock wire, an HCLK or PLL input. The taps and
// branches of a column carry one spine each and need no plan of their own.
const GowinClockPlanner::SinkReach &GowinClockPlanner::sink_reach(WireId sink)
{
    auto cached = reach_cache.find(sink);
    if (cached != reach_cache.end()) {
        return cached->second;
    }
    SinkReach &sr = reach_cache[sink];
    std::vector<WireId> order;
    std::queue<WireId> visit;
    sr.parents[sink];
    visit.push(sink);
    while (!visit.empty()) {
        WireId cursor = visit.front();
        visit.pop();
        order.push_back(cursor);
        if (cursor != sink && is_spine(cursor)) {
            continue;
        }
        for (PipId pip : ctx->getPipsUphill(cursor)) {
            WireId src = ctx->getPipSrcWire(pip);
            // no wire is the source of this search, so the general rule of the filter applies
            if (!pip_filter(pip, WireId())) {
                continue;
            }
            sr.parents[cursor].insert(src);
            if (!sr.parents.count(src)) {
                sr.parents[src];
                visit.push(src);
            }
        }
    }
    // the resources above a wire, as a fixed point because the paths may rejoin
    for (WireId w : order) {
        pool<int> &above = sr.above[w];
        auto res = resource_of_spine.find(w);
        if (res != resource_of_spine.end()) {
            above.insert(res->second);
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            pool<int> &mine = sr.above.at(*it);
            for (WireId p : sr.parents.at(*it)) {
                for (int r : sr.above.at(p)) {
                    changed |= mine.insert(r).second;
                }
            }
        }
    }
    for (WireId w : order) {
        if (!resource_of_spine.count(w) && sr.above.at(w).size() > 1) {
            sr.muxes.insert(w);
        }
    }
    return sr;
}

// The sinks of net that the clock network reaches, with the distinct regions they lie in. A DCS
// output comes from hardware DCS bels, every other net from components.
void GowinClockPlanner::add_sinks(Item &item, NetInfo *net, bool dcs, int &sinks)
{
    int n_comp = component_rank.size();
    for (auto &usr : net->users) {
        if (usr.cell->isPseudo()) {
            continue;
        }
        WireId sink = ctx->getNetinfoSinkWire(net, usr, 0);
        if (sink == WireId()) {
            continue;
        }
        const SinkReach &sr = sink_reach(sink);
        pool<int> region;
        for (int r : sr.above.at(sink)) {
            if ((r >= n_comp) == dcs) {
                region.insert(r);
            }
        }
        if (region.empty()) {
            continue;
        }
        ++sinks;
        item.sink_wires.push_back(sink);
        for (WireId mux : sr.muxes) {
            mux_nets[mux].insert(item.net->name);
        }
        if (std::find(item.regions.begin(), item.regions.end(), region) != item.regions.end()) {
            continue;
        }
        pool<WireId> spines;
        for (auto &w : sr.above) {
            auto res = resource_of_spine.find(w.first);
            if (res != resource_of_spine.end() && region.count(res->second)) {
                spines.insert(w.first);
            }
        }
        item.regions.push_back(region);
        item.region_spines.push_back(spines);
    }
}

// The pseudo cells on net: a DQCE output is an item of its own from the same gate, a DHCEN output
// shares the spines of net, a DCS gets a demand of its own
void GowinClockPlanner::add_pseudo_users(Buffer &b, NetInfo *net)
{
    for (auto &usr : net->users) {
        if (!usr.cell->isPseudo()) {
            continue;
        }
        NetInfo *out = usr.cell->getPort(id_CLKOUT);
        if (out == nullptr) {
            continue;
        }
        if (usr.cell->type == id_DQCE) {
            b.items.push_back(Item{out, {}, {}, {}, {}, {}, {}, false});
            add_sinks(b.items.back(), out, false, b.sinks);
        } else if (usr.cell->type == id_DHCEN) {
            b.aliases.push_back(out->name);
            add_sinks(b.items.front(), out, false, b.sinks);
        } else if (usr.cell->type == id_DCS) {
            add_dcs_demands(usr.cell);
        }
    }
}

// A DCS needs, in every quadrant its output has sinks in, a hardware DCS that every BUFG on its
// clock inputs drives at that input. Its regions are the hardware DCS bels that reach its sinks.
void GowinClockPlanner::add_dcs_demands(CellInfo *dcs)
{
    for (auto &d : dcs_demands) {
        if (d.dcs == dcs) {
            return;
        }
    }
    NetInfo *out = dcs->getPort(id_CLKOUT);
    if (out == nullptr) {
        return;
    }
    DcsDemand demand;
    demand.dcs = dcs;
    demand.item.net = out;
    demand.first_input = -1;
    // a DCS has the four clock inputs CLK0 to CLK3
    for (int i = 0; i < 4; ++i) {
        NetInfo *in = dcs->getPort(ctx->idf("%s%d", gwu.get_dcs_prefix().c_str(ctx), i));
        if (in == nullptr) {
            continue;
        }
        if (demand.first_input < 0 && (gwu.driver_is_buf(in->driver) || gwu.driver_is_clksrc(in->driver))) {
            demand.first_input = i;
        }
        if (gwu.driver_is_buf(in->driver)) {
            demand.buffered_inputs.emplace_back(i, in->driver.cell);
        } else if (gwu.driver_is_clksrc(in->driver)) {
            demand.source_inputs.emplace_back(i, in);
        }
    }
    if (demand.first_input < 0) {
        return;
    }
    int sinks = 0;
    add_sinks(demand.item, out, true, sinks);
    if (!demand.item.regions.empty()) {
        dcs_demands.push_back(demand);
    }
}

// A mux that the sinks of two nets can reach belongs to one of them, like a spine
void GowinClockPlanner::find_contested()
{
    for (auto &mn : mux_nets) {
        if (mn.second.size() > 1) {
            contested_wires.insert(mn.first);
            shared_wires.insert(mn.first);
            contested_by_index.push_back(mn.first);
        }
    }
    std::sort(contested_by_index.begin(), contested_by_index.end());
    for (size_t i = 0; i < contested_by_index.size(); ++i) {
        wire_index[contested_by_index[i]] = i;
    }
}

// The ways into a sink behind contested muxes: for every resource that reaches it, the contested
// wires on each simple path. Entries of one class with the same wires share one, and an entry
// that needs more wires than another one of its class that comes from the same resources is
// dropped. A sink that reaches every one of its classes without a contested wire needs no entry.
void GowinClockPlanner::add_entries(Item &item, WireId sink, bool dcs)
{
    const SinkReach &sr = sink_reach(sink);
    bool any = false;
    for (WireId mux : sr.muxes) {
        any |= contested_wires.count(mux);
    }
    if (!any) {
        return;
    }
    int n_comp = component_rank.size();
    // (resource, contested wires) of every simple path from a spine down to the sink
    std::vector<std::pair<int, std::vector<WireId>>> paths;
    std::vector<WireId> stack{sink};
    pool<WireId> on_stack{sink};
    const size_t path_limit = 1024;
    std::function<void(WireId)> walk = [&](WireId w) {
        if (paths.size() >= path_limit) {
            return;
        }
        for (WireId p : sr.parents.at(w)) {
            if (on_stack.count(p)) {
                continue;
            }
            auto res = resource_of_spine.find(p);
            if (res != resource_of_spine.end()) {
                if ((res->second >= n_comp) == dcs) {
                    std::vector<WireId> wires;
                    for (WireId s : stack) {
                        if (contested_wires.count(s)) {
                            wires.push_back(s);
                        }
                    }
                    std::sort(wires.begin(), wires.end());
                    paths.emplace_back(res->second, wires);
                }
                continue;
            }
            bool leads_on = false;
            for (int r : sr.above.at(p)) {
                leads_on |= (r >= n_comp) == dcs;
            }
            if (!leads_on) {
                continue;
            }
            stack.push_back(p);
            on_stack.insert(p);
            walk(p);
            stack.pop_back();
            on_stack.erase(p);
        }
    };
    walk(sink);
    if (paths.size() >= path_limit) {
        log_warning("The clock network above %s has more than %d paths, only the first ones are planned.\n",
                    ctx->nameOfWire(sink), int(path_limit));
    }
    // entries: one per class and wire set
    std::vector<Entry> entries;
    for (auto &p : paths) {
        int cls = resource_class[p.first];
        bool merged = false;
        for (Entry &e : entries) {
            if (e.cls == cls && e.wires == p.second) {
                if (std::find(e.resources.begin(), e.resources.end(), p.first) == e.resources.end()) {
                    e.resources.push_back(p.first);
                }
                merged = true;
                break;
            }
        }
        if (!merged) {
            entries.push_back(Entry{cls, {p.first}, p.second, WireId()});
        }
    }
    for (Entry &e : entries) {
        std::sort(e.resources.begin(), e.resources.end());
    }
    std::vector<Entry> kept;
    for (size_t i = 0; i < entries.size(); ++i) {
        bool dominated = false;
        for (size_t j = 0; j < entries.size() && !dominated; ++j) {
            const Entry &a = entries[i], &b = entries[j];
            if (i == j || a.cls != b.cls) {
                continue;
            }
            bool wires_sub = std::includes(a.wires.begin(), a.wires.end(), b.wires.begin(), b.wires.end());
            bool res_super =
                    std::includes(b.resources.begin(), b.resources.end(), a.resources.begin(), a.resources.end());
            // b needs no more wires and comes from no fewer resources; among equals the first stays
            dominated = wires_sub && res_super && (a.wires != b.wires || j < i);
        }
        if (!dominated) {
            kept.push_back(entries[i]);
        }
    }
    bool needs_wires = false;
    for (Entry &e : kept) {
        needs_wires |= !e.wires.empty();
        // the wire that most nets compete for stands for the entry in the matching
        size_t most = 0;
        for (WireId w : e.wires) {
            size_t nets = mux_nets.at(w).size();
            if (e.key == WireId() || nets > most) {
                e.key = w;
                most = nets;
            }
        }
    }
    if (!needs_wires) {
        return;
    }
    // classes the net needs for its other sinks anyway first, then entries without contested wires
    auto own = [&](const Entry &e) {
        return std::find(item.own_classes.begin(), item.own_classes.end(), e.cls) != item.own_classes.end();
    };
    std::stable_sort(kept.begin(), kept.end(), [&](const Entry &a, const Entry &b) {
        if (own(a) != own(b)) {
            return own(a);
        }
        if (a.wires.empty() != b.wires.empty()) {
            return a.wires.empty();
        }
        if (class_rank[a.cls] != class_rank[b.cls]) {
            return class_rank[a.cls] < class_rank[b.cls];
        }
        if (a.wires.size() != b.wires.size()) {
            return a.wires.size() < b.wires.size();
        }
        return a.resources.size() > b.resources.size();
    });
    item.contested.push_back(ContestedSink{sink, kept, -1});
}

// Resources that lie in the same regions form a class, so that one of them serves every sink of a
// net whose region contains the class
void GowinClockPlanner::make_classes()
{
    int n_res = component_rank.size() + dcs_bels.size();
    std::vector<pool<int>> all_regions;
    auto collect = [&](const Item &item) {
        for (auto &region : item.regions) {
            if (std::find(all_regions.begin(), all_regions.end(), region) == all_regions.end()) {
                all_regions.push_back(region);
            }
        }
    };
    for (auto &b : buffers) {
        for (auto &item : b.items) {
            collect(item);
        }
    }
    for (auto &dd : dcs_demands) {
        collect(dd.item);
    }
    auto rank = [&](int r) { return r < int(component_rank.size()) ? component_rank[r] : r; };
    std::vector<int> by_rank(n_res);
    for (int r = 0; r < n_res; ++r) {
        by_rank[r] = r;
    }
    std::sort(by_rank.begin(), by_rank.end(), [&](int x, int y) { return rank(x) < rank(y); });
    resource_class.assign(n_res, -1);
    std::map<std::vector<bool>, int> class_of_membership;
    for (int r : by_rank) {
        std::vector<bool> membership(all_regions.size(), false);
        bool any = false;
        for (size_t i = 0; i < all_regions.size(); ++i) {
            membership[i] = all_regions[i].count(r);
            any |= membership[i];
        }
        if (!any) {
            continue;
        }
        auto found = class_of_membership.find(membership);
        if (found == class_of_membership.end()) {
            found = class_of_membership.emplace(membership, class_resources.size()).first;
            class_resources.emplace_back();
            class_rank.push_back(rank(r));
        }
        resource_class[r] = found->second;
        class_resources[found->second].push_back(r);
    }
}

WireId GowinClockPlanner::dcs_input_wire(BelId dcs_bel, int input) const
{
    return ctx->getBelPinWire(dcs_bel, ctx->idf("%s%d", gwu.get_dcs_prefix().c_str(ctx), input));
}

// The gates, their classes, and the resources they compete for
void GowinClockPlanner::gather_resources(std::vector<BelId> &bufg_bels, const std::vector<IdString> &clk_nets)
{
    dict<WireId, int> gates_per_wire;
    auto count_wires = [&](const GateReach &r) {
        pool<WireId> wires;
        for (auto &target : r) {
            for (WireId w : target.second) {
                wires.insert(w);
            }
        }
        for (WireId w : wires) {
            ++gates_per_wire[w];
        }
    };
    for (BelId bel : ctx->getBels()) {
        if (ctx->getBelType(bel) == id_DCS) {
            dcs_bel_index[bel] = dcs_bels.size();
            dcs_bels.push_back(bel);
        }
        if (ctx->getBelType(bel) != id_BUFG) {
            continue;
        }
        bufg_bels.push_back(bel);
        GateReach r = gate_reach(ctx->getBelPinWire(bel, id_I));
        count_wires(r);
        reach[bel] = r;
    }
    std::sort(bufg_bels.begin(), bufg_bels.end());
    // a clock source without BUFG enters the network from its driver pin and competes for the same
    // spines, so its paths count as well
    for (IdString name : clk_nets) {
        NetInfo *ni = ctx->nets.at(name).get();
        WireId src = ctx->getNetinfoSourceWire(ni);
        if (ni->driver.cell->bel == BelId() || src == WireId()) {
            continue;
        }
        GateReach r = gate_reach(src);
        if (!r.empty()) {
            count_wires(r);
            source_reach[name] = r;
        }
    }
    // bels whose gates drive the same targets are one class, numbered in bel order
    std::vector<pool<WireId>> class_targets;
    for (BelId bel : bufg_bels) {
        pool<WireId> targets;
        for (auto &target : reach.at(bel)) {
            targets.insert(target.first);
        }
        auto found = std::find(class_targets.begin(), class_targets.end(), targets);
        if (found == class_targets.end()) {
            class_targets.push_back(targets);
            found = class_targets.end() - 1;
        }
        bel_class[bel] = found - class_targets.begin();
    }
    // a spine, a DCS input, or a wire on the way there that more than one gate drives
    for (auto &gw : gates_per_wire) {
        if (gw.second > 1 || is_spine(gw.first) || gwu.is_dcs_input(gw.first)) {
            shared_wires.insert(gw.first);
        }
    }
    // the wires of one path to a spine form a component, a DCS input is a component of its own
    dict<WireId, WireId> parent;
    auto root = [&](WireId w) {
        while (parent.at(w) != w) {
            w = parent.at(w);
        }
        return w;
    };
    for (WireId w : shared_wires) {
        parent[w] = w;
    }
    auto join_paths = [&](const GateReach &r) {
        for (auto &target : r) {
            if (gwu.is_dcs_input(target.first)) {
                continue;
            }
            for (WireId w : target.second) {
                if (shared_wires.count(w)) {
                    parent[root(w)] = root(target.first);
                }
            }
        }
    };
    for (BelId bel : bufg_bels) {
        join_paths(reach.at(bel));
    }
    for (auto &sr : source_reach) {
        join_paths(sr.second);
    }
    std::vector<WireId> wires(shared_wires.begin(), shared_wires.end());
    std::sort(wires.begin(), wires.end());
    dict<WireId, int> component_of_root;
    std::vector<int> lowest_spine;
    for (WireId w : wires) {
        WireId r = root(w);
        if (!component_of_root.count(r)) {
            component_of_root[r] = lowest_spine.size();
            lowest_spine.push_back(INT_MAX);
        }
        component[w] = component_of_root.at(r);
        if (is_spine(w)) {
            lowest_spine[component[w]] = std::min(lowest_spine[component[w]], spine_number(w));
            component_spines[component[w]].push_back(w);
        }
    }
    std::vector<int> by_spine(lowest_spine.size());
    for (size_t i = 0; i < by_spine.size(); ++i) {
        by_spine[i] = i;
    }
    std::stable_sort(by_spine.begin(), by_spine.end(), [&](int x, int y) { return lowest_spine[x] < lowest_spine[y]; });
    component_rank.assign(by_spine.size(), 0);
    for (size_t i = 0; i < by_spine.size(); ++i) {
        component_rank[by_spine[i]] = i;
    }
    for (BelId bel : bufg_bels) {
        pool<int> comps;
        for (auto &target : reach.at(bel)) {
            if (is_spine(target.first)) {
                comps.insert(component.at(target.first));
            }
        }
        bel_components[bel] = std::vector<int>(comps.begin(), comps.end());
    }
    // the resources behind a spine: its component, or the hardware DCS whose output it is
    for (auto &cs : component_spines) {
        for (WireId w : cs.second) {
            resource_of_spine[w] = cs.first;
        }
    }
    for (BelId bel : dcs_bels) {
        resource_of_spine[ctx->getBelPinWire(bel, id_CLKOUT)] = component_rank.size() + dcs_bel_index.at(bel);
    }
}

// The BUFGs and the clock sources with their sinks, DQCE and DHCEN nets and DCS roles, the
// contested muxes and the classes
void GowinClockPlanner::gather_demands(const std::vector<IdString> &buf_nets, const std::vector<IdString> &clk_nets)
{
    for (IdString name : buf_nets) {
        NetInfo *ni = ctx->nets.at(name).get();
        Buffer b;
        b.buf = ni->driver.cell;
        NPNR_ASSERT(b.buf->bel != BelId());
        b.strength = b.buf->belStrength;
        b.fixed = b.strength >= STRENGTH_FIXED;
        b.items.push_back(Item{ni, {}, {}, {}, {}, {}, {}, false});
        add_sinks(b.items.front(), ni, false, b.sinks);
        add_pseudo_users(b, ni);
        buffers.push_back(b);
    }
    for (IdString name : clk_nets) {
        auto found = source_reach.find(name);
        if (found == source_reach.end()) {
            continue;
        }
        NetInfo *ni = ctx->nets.at(name).get();
        Buffer b;
        b.buf = ni->driver.cell;
        b.strength = b.buf->belStrength;
        b.fixed = true;
        b.source = true;
        b.own_reach = found->second;
        pool<int> comps;
        for (auto &target : b.own_reach) {
            if (is_spine(target.first)) {
                comps.insert(component.at(target.first));
            }
        }
        b.own_components = std::vector<int>(comps.begin(), comps.end());
        b.items.push_back(Item{ni, {}, {}, {}, {}, {}, {}, false});
        add_sinks(b.items.front(), ni, false, b.sinks);
        add_pseudo_users(b, ni);
        buffers.push_back(b);
    }
    find_contested();
    make_classes();
    // the classes every minimal hitting set of an item needs, for the order of its entries
    auto own_classes = [&](Item &item) {
        std::vector<std::vector<int>> sets;
        if (!hitting_sets(item, [](int) { return true; }, {}, sets)) {
            return;
        }
        for (int cls : sets.front()) {
            bool forced = true;
            for (auto &set : sets) {
                forced &= std::find(set.begin(), set.end(), cls) != set.end();
            }
            if (forced) {
                item.own_classes.push_back(cls);
            }
        }
    };
    for (auto &b : buffers) {
        for (auto &item : b.items) {
            own_classes(item);
            for (WireId sink : item.sink_wires) {
                add_entries(item, sink, false);
            }
        }
    }
    for (auto &dd : dcs_demands) {
        own_classes(dd.item);
        for (WireId sink : dd.item.sink_wires) {
            add_entries(dd.item, sink, true);
        }
    }
    for (auto &b : buffers) {
        b.active = false;
        for (auto &item : b.items) {
            b.active |= !item.regions.empty();
        }
        for (auto &dd : dcs_demands) {
            for (auto &in : dd.buffered_inputs) {
                if (in.second == b.buf) {
                    b.active = true;
                    b.dcs_roles.emplace_back(dd.dcs, in.first);
                }
            }
            for (auto &in : dd.source_inputs) {
                if (b.source && in.second == b.items.front().net) {
                    b.active = true;
                    b.dcs_roles.emplace_back(dd.dcs, in.first);
                }
            }
        }
        std::sort(b.dcs_roles.begin(), b.dcs_roles.end());
    }
}

// The bels a BUFG may take, as one list per gate: its placed bel's gate first, then the gates by
// the distance of their nearest bel. Within a gate the placed bel comes first, then the free
// bels by distance, then the bels of other BUFGs that may move as well. Bels of one gate are
// interchangeable, so the search takes the first one of a list that is not taken.
void GowinClockPlanner::gather_choices(Buffer &b, const std::vector<BelId> &bufg_bels)
{
    b.choices.push_back({b.buf->bel});
    // the driver of a clock source is no BUFG bel and has no class, it never has a twin
    b.choice_class.push_back(b.source ? -1 : bel_class.at(b.buf->bel));
    if (b.fixed) {
        return;
    }
    auto open = [&](BelId bel) {
        CellInfo *holder = ctx->getBoundBelCell(bel);
        if (holder == nullptr) {
            return true;
        }
        for (auto &other : buffers) {
            if (other.buf == holder) {
                return other.active && !other.fixed;
            }
        }
        return false;
    };
    Loc here = ctx->getBelLocation(b.buf->bel);
    auto distance = [&](BelId bel) {
        Loc loc = ctx->getBelLocation(bel);
        return std::abs(loc.x - here.x) + std::abs(loc.y - here.y);
    };
    std::vector<BelId> others;
    for (BelId bel : bufg_bels) {
        if (bel != b.buf->bel && !reach.at(bel).empty() && open(bel) && ctx->isValidBelForCellType(b.buf->type, bel) &&
            (b.buf->region == nullptr || !b.buf->region->constr_bels || b.buf->region->bels.count(bel))) {
            others.push_back(bel);
        }
    }
    // free bels before held ones, nearer before farther
    std::stable_sort(others.begin(), others.end(), [&](BelId x, BelId y) {
        bool fx = ctx->getBoundBelCell(x) == nullptr, fy = ctx->getBoundBelCell(y) == nullptr;
        if (fx != fy) {
            return fx;
        }
        return distance(x) < distance(y);
    });
    for (BelId bel : others) {
        int cls = bel_class.at(bel);
        auto found = std::find(b.choice_class.begin(), b.choice_class.end(), cls);
        if (found == b.choice_class.end()) {
            b.choices.push_back({bel});
            b.choice_class.push_back(cls);
        } else {
            b.choices[found - b.choice_class.begin()].push_back(bel);
        }
    }
    // gates in the order of their nearest bel, the gate of the placed bel first
    std::vector<size_t> order(b.choices.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin() + 1, order.end(), [&](size_t x, size_t y) {
        return distance(b.choices[x].front()) < distance(b.choices[y].front());
    });
    std::vector<std::vector<BelId>> choices;
    std::vector<int> classes;
    for (size_t i : order) {
        choices.push_back(b.choices[i]);
        classes.push_back(b.choice_class[i]);
    }
    b.choices = choices;
    b.choice_class = classes;
}

// BUFGs with the same regions, the same entries, the same DCS roles and the same bels to choose
// from are interchangeable. They take the positions of their set in the order of the gates of
// their placed bels, so that staying put obeys the rule, and the later one only takes a gate of a
// class at least as high as the earlier one's.
void GowinClockPlanner::find_twins(std::vector<int> &active)
{
    auto same_entries = [&](const std::vector<ContestedSink> &x, const std::vector<ContestedSink> &y) {
        if (x.size() != y.size()) {
            return false;
        }
        for (size_t i = 0; i < x.size(); ++i) {
            if (x[i].entries.size() != y[i].entries.size()) {
                return false;
            }
            for (size_t k = 0; k < x[i].entries.size(); ++k) {
                const Entry &a = x[i].entries[k], &b = y[i].entries[k];
                if (a.cls != b.cls || a.resources != b.resources || a.wires != b.wires) {
                    return false;
                }
            }
        }
        return true;
    };
    auto same_choice = [&](const Buffer &x, const Buffer &y) {
        if (x.fixed || y.fixed || x.items.size() != y.items.size() || x.dcs_roles != y.dcs_roles) {
            return false;
        }
        for (size_t i = 0; i < x.items.size(); ++i) {
            if (x.items[i].regions != y.items[i].regions || !same_entries(x.items[i].contested, y.items[i].contested)) {
                return false;
            }
        }
        pool<BelId> xb, yb;
        for (auto &list : x.choices) {
            xb.insert(list.begin(), list.end());
        }
        for (auto &list : y.choices) {
            yb.insert(list.begin(), list.end());
        }
        return xb == yb;
    };
    for (auto &b : buffers) {
        b.twin = -1;
    }
    std::vector<bool> grouped(active.size(), false);
    for (size_t i = 0; i < active.size(); ++i) {
        if (grouped[i]) {
            continue;
        }
        std::vector<size_t> positions{i};
        for (size_t j = i + 1; j < active.size(); ++j) {
            if (!grouped[j] && same_choice(buffers[active[i]], buffers[active[j]])) {
                positions.push_back(j);
                grouped[j] = true;
            }
        }
        std::vector<int> members;
        for (size_t pos : positions) {
            members.push_back(active[pos]);
        }
        std::sort(members.begin(), members.end(), [&](int x, int y) {
            int cx = bel_class.at(buffers[x].buf->bel), cy = bel_class.at(buffers[y].buf->bel);
            return cx != cy ? cx < cy : buffers[x].buf->bel < buffers[y].buf->bel;
        });
        for (size_t k = 0; k < positions.size(); ++k) {
            active[positions[k]] = members[k];
            buffers[members[k]].twin = k == 0 ? -1 : members[k - 1];
        }
    }
}

// Whether another BUFG holds the bel: one in the search by its assignment, one that stays where it
// is by its placement. A BUFG that may move but is not in the search yet joins it later and is
// assigned anew then, so its bel is open.
bool GowinClockPlanner::bel_taken(const std::vector<int> &active, int self, BelId bel) const
{
    for (size_t other = 0; other < buffers.size(); ++other) {
        if (int(other) == self) {
            continue;
        }
        const Buffer &o = buffers[other];
        // a clock source holds no BUFG bel, and two outputs of one PLL share their driver
        if (o.source) {
            continue;
        }
        bool searched = std::find(active.begin(), active.end(), int(other)) != active.end();
        if (searched ? o.bel == bel : (o.fixed || !o.active) && o.buf->bel == bel) {
            return true;
        }
    }
    return false;
}

// The bel a BUFG has in the current assignment, or where it stays when it is not planned
BelId GowinClockPlanner::buffer_bel(CellInfo *buf) const
{
    for (auto &b : buffers) {
        if (b.buf == buf) {
            return b.choices.empty() ? buf->bel : b.bel;
        }
    }
    return buf->bel;
}

// What the net of b drives from bel: the gate of a BUFG bel, or the driver pin of a clock source
const GowinClockPlanner::GateReach &GowinClockPlanner::reach_of(const Buffer &b, BelId bel) const
{
    return b.source ? b.own_reach : reach.at(bel);
}

// The components that the buffer drives from at, or from any of its open candidates when it has
// no bel yet
pool<int> GowinClockPlanner::drives(const std::vector<int> &active, int bi, BelId at) const
{
    const Buffer &b = buffers[bi];
    pool<int> result;
    if (b.source) {
        result.insert(b.own_components.begin(), b.own_components.end());
    } else if (at != BelId()) {
        result.insert(bel_components.at(at).begin(), bel_components.at(at).end());
    } else {
        for (auto &list : b.choices) {
            for (BelId cand : list) {
                if (!bel_taken(active, bi, cand)) {
                    result.insert(bel_components.at(cand).begin(), bel_components.at(cand).end());
                }
            }
        }
    }
    return result;
}

// The resources of a class among the driven ones, in order of preference
std::vector<int> GowinClockPlanner::options_in(const pool<int> &driven, int cls) const
{
    std::vector<int> options;
    for (int r : class_resources[cls]) {
        if (driven.count(r)) {
            options.push_back(r);
        }
    }
    return options;
}

// The hardware DCS bels of a class whose inputs every placed BUFG on them drives, the ones that the
// clock sources on them reach through the clock network first. A BUFG without a bel yet may still
// end up anywhere. A clock source that does not reach the input of a hardware DCS feeds it through
// general routing, as the router does without a plan, so its reach only orders the options.
std::vector<int> GowinClockPlanner::dcs_options(const DcsDemand &dd, int cls, int pinned, BelId pin) const
{
    std::vector<int> reached, unreached;
    for (int r : class_resources[cls]) {
        BelId dcs_bel = dcs_bels[r - component_rank.size()];
        bool ok = true;
        for (auto &in : dd.buffered_inputs) {
            BelId bel = pinned >= 0 && buffers[pinned].buf == in.second ? pin : buffer_bel(in.second);
            ok &= bel == BelId() || reach.at(bel).count(dcs_input_wire(dcs_bel, in.first));
        }
        if (!ok) {
            continue;
        }
        bool by_sources = true;
        for (auto &in : dd.source_inputs) {
            auto found = source_reach.find(in.second->name);
            by_sources &= found != source_reach.end() && found->second.count(dcs_input_wire(dcs_bel, in.first));
        }
        (by_sources ? reached : unreached).push_back(r);
    }
    reached.insert(reached.end(), unreached.begin(), unreached.end());
    return reached;
}

// Whether a BUFG or a clock source of the search feeds the DCS
bool GowinClockPlanner::dcs_involved(const DcsDemand &dd, const std::vector<int> &active) const
{
    for (int bi : active) {
        const Buffer &b = buffers[bi];
        if (b.source) {
            for (auto &in : dd.source_inputs) {
                if (in.second == b.items.front().net) {
                    return true;
                }
            }
        } else {
            for (auto &in : dd.buffered_inputs) {
                if (in.second == b.buf) {
                    return true;
                }
            }
        }
    }
    return false;
}

// The minimal sets of usable classes that hit every region of item not hit already, in order of
// preference: fewer classes first, then the better ranked. False when a region has no usable
// class.
bool GowinClockPlanner::hitting_sets(const Item &item, const std::function<bool(int)> &usable, const pool<int> &hit,
                                     std::vector<std::vector<int>> &sets) const
{
    sets.clear();
    std::vector<std::vector<int>> remaining;
    for (auto &region : item.regions) {
        std::vector<int> classes;
        bool is_hit = false;
        for (int r : region) {
            int cls = resource_class[r];
            is_hit |= hit.count(cls);
            if (usable(cls) && std::find(classes.begin(), classes.end(), cls) == classes.end()) {
                classes.push_back(cls);
            }
        }
        if (is_hit) {
            continue;
        }
        if (classes.empty()) {
            return false;
        }
        std::sort(classes.begin(), classes.end(), [&](int x, int y) { return class_rank[x] < class_rank[y]; });
        remaining.push_back(classes);
    }
    if (remaining.empty()) {
        sets.push_back({});
        return true;
    }
    // every way to hit the first region that the set misses so far, so that every minimal set
    // turns up; a set that contains another one is dropped below
    std::vector<std::vector<int>> found;
    std::vector<int> set;
    std::function<void()> extend = [&]() {
        for (auto &classes : remaining) {
            bool is_hit = false;
            for (int cls : classes) {
                is_hit |= std::find(set.begin(), set.end(), cls) != set.end();
            }
            if (is_hit) {
                continue;
            }
            for (int cls : classes) {
                set.push_back(cls);
                extend();
                set.pop_back();
            }
            return;
        }
        std::vector<int> sorted = set;
        std::sort(sorted.begin(), sorted.end());
        found.push_back(sorted);
    };
    extend();
    auto rank_sum = [&](const std::vector<int> &x) {
        int sum = 0;
        for (int cls : x) {
            sum += class_rank[cls];
        }
        return sum;
    };
    std::stable_sort(found.begin(), found.end(), [&](const std::vector<int> &x, const std::vector<int> &y) {
        if (x.size() != y.size()) {
            return x.size() < y.size();
        }
        return rank_sum(x) < rank_sum(y);
    });
    for (auto &candidate : found) {
        bool minimal = true;
        for (auto &kept : sets) {
            minimal &= !std::includes(candidate.begin(), candidate.end(), kept.begin(), kept.end());
        }
        if (minimal) {
            sets.push_back(candidate);
        }
    }
    return true;
}

// The demands of one item: one per class of its cover, or, while it is not decided, per class
// every hitting set needs, together with the classes its chosen entries come from. An entry
// restricts its class to the resources that lead to its sink. False when a class has no option,
// with the reason in why when it is asked for.
bool GowinClockPlanner::item_demands(const Item &item, const std::function<std::vector<int>(int)> &options, int buffer,
                                     int ii, int dcs, std::vector<Demand> &demands, std::string *why)
{
    pool<int> hit;
    dict<int, std::vector<int>> restrict;
    for (auto &cs : item.contested) {
        if (cs.choice < 0) {
            continue;
        }
        const Entry &e = cs.entries[cs.choice];
        hit.insert(e.cls);
        auto found = restrict.find(e.cls);
        if (found == restrict.end()) {
            restrict[e.cls] = e.resources;
        } else {
            std::vector<int> both;
            std::set_intersection(found->second.begin(), found->second.end(), e.resources.begin(), e.resources.end(),
                                  std::back_inserter(both));
            found->second = both;
        }
    }
    std::vector<int> needed(hit.begin(), hit.end());
    if (item.decided) {
        for (int cls : item.cover) {
            if (!hit.count(cls)) {
                needed.push_back(cls);
            }
        }
    } else {
        std::vector<std::vector<int>> sets;
        if (!hitting_sets(item, [&](int cls) { return !options(cls).empty(); }, hit, sets)) {
            if (why != nullptr) {
                *why = stringf("no gate left for '%s' reaches the clock network above one of its users",
                               ctx->nameOf(item.net));
            }
            return false;
        }
        for (int cls : sets.front()) {
            bool forced = true;
            for (auto &set : sets) {
                forced &= std::find(set.begin(), set.end(), cls) != set.end();
            }
            if (forced) {
                needed.push_back(cls);
            }
        }
    }
    std::sort(needed.begin(), needed.end(), [&](int x, int y) { return class_rank[x] < class_rank[y]; });
    for (int cls : needed) {
        Demand d;
        d.buffer = buffer;
        d.item = ii;
        d.dcs = dcs;
        d.cls = cls;
        d.options = options(cls);
        auto found = restrict.find(cls);
        if (found != restrict.end()) {
            std::vector<int> both;
            for (int r : d.options) {
                if (std::find(found->second.begin(), found->second.end(), r) != found->second.end()) {
                    both.push_back(r);
                }
            }
            d.options = both;
        }
        if (d.options.empty()) {
            if (why != nullptr) {
                *why = stringf("no gate left for '%s' drives a clock spine of %s", ctx->nameOf(item.net),
                               class_name(cls).c_str());
            }
            return false;
        }
        demands.push_back(d);
    }
    return true;
}

// The demands of one BUFG with the gate at, or with everything its remaining candidate gates drive
// when it has no bel yet. False when a demand has no option.
bool GowinClockPlanner::buffer_demands(const std::vector<int> &active, int bi, BelId at, std::vector<Demand> &demands,
                                       std::string *why)
{
    Buffer &b = buffers[bi];
    pool<int> driven = drives(active, bi, at);
    for (size_t ii = 0; ii < b.items.size(); ++ii) {
        if (!item_demands(
                    b.items[ii], [&](int cls) { return options_in(driven, cls); }, bi, int(ii), -1, demands, why)) {
            return false;
        }
    }
    return true;
}

// The demands of the DCS that a BUFG or clock source of the search feeds, with the BUFG pinned to
// pin checked at that gate. False when a DCS quadrant has no option.
bool GowinClockPlanner::dcs_demands_of(const std::vector<int> &active, int pinned, BelId pin,
                                       std::vector<Demand> &demands, std::string *why)
{
    for (size_t di = 0; di < dcs_demands.size(); ++di) {
        const DcsDemand &dd = dcs_demands[di];
        if (!dcs_involved(dd, active)) {
            continue;
        }
        if (!item_demands(
                    dd.item, [&](int cls) { return dcs_options(dd, cls, pinned, pin); }, -1, -1, int(di), demands,
                    why)) {
            return false;
        }
    }
    return true;
}

GowinClockPlanner::Item &GowinClockPlanner::step_item(const Step &s)
{
    return s.dcs >= 0 ? dcs_demands[s.dcs].item : buffers[s.buffer].items[s.item];
}

// The resources of cls that the net of the step can get, with the BUFG pinned to pin at that gate
std::vector<int> GowinClockPlanner::step_options(const std::vector<int> &active, const Step &s, int cls, int pinned,
                                                 BelId pin)
{
    if (s.dcs >= 0) {
        return dcs_options(dcs_demands[s.dcs], cls, pinned, pin);
    }
    const Buffer &b = buffers[s.buffer];
    return options_in(drives(active, s.buffer, s.buffer == pinned ? pin : b.bel), cls);
}

// Whether the entry is still open: no other net holds one of its wires, and one of its resources
// can be had
bool GowinClockPlanner::entry_viable(const std::vector<int> &active, const Step &s, const Entry &e, int pinned,
                                     BelId pin)
{
    IdString net = step_item(s).net->name;
    for (WireId w : e.wires) {
        auto found = claimed.find(w);
        if (found != claimed.end() && found->second.first != net) {
            return false;
        }
    }
    std::vector<int> opts = step_options(active, s, e.cls, pinned, pin);
    for (int r : e.resources) {
        if (std::find(opts.begin(), opts.end(), r) != opts.end()) {
            return true;
        }
    }
    return false;
}

// What the undecided contested sinks need: every one of them an entry that is still open, and one
// sink per net, the one with the fewest open entries, a demand for the matching. An entry from a
// class the net asks for anyway costs the wire that stands for it, an entry from another class
// costs a resource of that class, and an entry without wires from a class the net asks for costs
// nothing. Two sinks of one net may share a wire, so only one of them asks.
bool GowinClockPlanner::entry_demands(const std::vector<int> &active, const std::vector<Demand> &demands, int pinned,
                                      BelId pin, std::vector<Demand> &entries, std::string *why)
{
    int n_res = component_rank.size() + dcs_bels.size();
    std::map<std::pair<int, int>, pool<int>> asked;
    for (auto &d : demands) {
        asked[{d.buffer, d.dcs >= 0 ? d.dcs : d.item}].insert(d.cls);
    }
    std::map<std::pair<int, int>, std::pair<int, std::vector<const Entry *>>> representative;
    for (size_t k = 0; k < steps.size(); ++k) {
        const Step &s = steps[k];
        Item &item = step_item(s);
        const ContestedSink &cs = item.contested[s.sink];
        if (cs.choice >= 0) {
            continue;
        }
        std::vector<const Entry *> open;
        for (const Entry &e : cs.entries) {
            if (entry_viable(active, s, e, pinned, pin)) {
                open.push_back(&e);
            }
        }
        if (open.empty()) {
            if (why != nullptr) {
                *why = stringf("'%s' has no free entry into the clock network at %s", ctx->nameOf(item.net),
                               ctx->nameOfWire(cs.sink));
            }
            return false;
        }
        auto key = std::make_pair(s.buffer, s.dcs >= 0 ? s.dcs : s.item);
        auto found = representative.find(key);
        if (found == representative.end() || open.size() < found->second.second.size()) {
            representative[key] = {int(k), open};
        }
    }
    for (auto &rep : representative) {
        const Step &s = steps[rep.second.first];
        const pool<int> &classes = asked[rep.first];
        Demand d;
        d.buffer = s.buffer;
        d.item = s.item;
        d.dcs = s.dcs;
        bool free = false;
        for (const Entry *e : rep.second.second) {
            if (classes.count(e->cls)) {
                if (e->wires.empty()) {
                    free = true;
                    break;
                }
                d.options.push_back(n_res + wire_index.at(e->key));
            } else {
                std::vector<int> opts = step_options(active, s, e->cls, pinned, pin);
                for (int r : e->resources) {
                    if (std::find(opts.begin(), opts.end(), r) != opts.end()) {
                        d.options.push_back(r);
                    }
                }
            }
        }
        if (!free) {
            entries.push_back(d);
        }
    }
    return true;
}

// Bipartite matching of the demands to the resources and contested wires by augmenting paths.
// When it fails and why is asked for, the demands reached by alternating paths from the one
// without a partner outnumber the resources they can take, and both are named.
bool GowinClockPlanner::match_demands(const std::vector<Demand> &demands, std::vector<int> &match, std::string *why)
{
    int right = component_rank.size() + dcs_bels.size() + contested_by_index.size();
    std::vector<int> match_right(right, -1);
    match.assign(demands.size(), -1);
    std::function<bool(int, std::vector<bool> &)> augment = [&](int left, std::vector<bool> &seen) {
        for (int r : demands[left].options) {
            if (seen[r]) {
                continue;
            }
            seen[r] = true;
            if (match_right[r] < 0 || augment(match_right[r], seen)) {
                match_right[r] = left;
                match[left] = r;
                return true;
            }
        }
        return false;
    };
    for (size_t left = 0; left < demands.size(); ++left) {
        std::vector<bool> seen(right, false);
        if (augment(left, seen)) {
            continue;
        }
        if (why != nullptr) {
            std::vector<int> lefts{int(left)};
            std::vector<WireId> spines, wires;
            int n_rights = 0;
            for (int r = 0; r < right; ++r) {
                if (!seen[r]) {
                    continue;
                }
                lefts.push_back(match_right[r]);
                ++n_rights;
                if (r < int(component_rank.size())) {
                    spines.insert(spines.end(), component_spines.at(r).begin(), component_spines.at(r).end());
                } else if (r < int(component_rank.size() + dcs_bels.size())) {
                    spines.push_back(ctx->getBelPinWire(dcs_bels[r - component_rank.size()], id_CLKOUT));
                } else {
                    wires.push_back(contested_by_index[r - component_rank.size() - dcs_bels.size()]);
                }
            }
            std::sort(spines.begin(), spines.end(),
                      [&](WireId x, WireId y) { return spine_number(x) < spine_number(y); });
            std::sort(wires.begin(), wires.end());
            std::string rights;
            for (WireId w : spines) {
                rights += " " + ctx->getWireName(w)[1].str(ctx);
            }
            for (WireId w : wires) {
                rights += std::string(" ") + ctx->nameOfWire(w);
            }
            *why = stringf("the clocks %s need %d clock spines or entries into the clock network, and only %d can "
                           "serve them:%s",
                           net_list(demands, lefts).c_str(), int(lefts.size()), n_rights, rights.c_str());
        }
        return false;
    }
    return true;
}

// Whether the BUFGs and clock sources of the search and the DCS they feed can all get the
// resources they need. Four necessary conditions, each a relaxation of the complete assignment:
// the matching with every BUFG without a bel allowed everything its candidates drive, every net
// not decided yet asking only for what every hitting set needs and one undecided sink per net
// asking for an entry, the flow over the bels, and for every BUFG without a bel a matching with it
// pinned to one of its gates. The last one catches a BUFG whose regions need two gates, which the
// first two let split: the matching gives its classes different gates, and the flow gives it two
// bels. When everything is decided the matching is exact. demands and match hold the matching of
// the first condition afterwards. With why asked for, a failure names its reason.
bool GowinClockPlanner::feasible(const std::vector<int> &active, std::vector<Demand> &demands, std::vector<int> &match,
                                 std::string *why)
{
    ++checks;
    // the demands of every buffer on its own, so that the pinned checks rebuild only one of them
    std::vector<std::vector<Demand>> per_buffer(active.size());
    demands.clear();
    for (size_t i = 0; i < active.size(); ++i) {
        if (!buffer_demands(active, active[i], buffers[active[i]].bel, per_buffer[i], why)) {
            return false;
        }
        demands.insert(demands.end(), per_buffer[i].begin(), per_buffer[i].end());
    }
    if (!dcs_demands_of(active, -1, BelId(), demands, why)) {
        return false;
    }
    std::vector<Demand> all = demands;
    if (!entry_demands(active, demands, -1, BelId(), all, why)) {
        return false;
    }
    std::vector<int> all_match;
    if (!match_demands(all, all_match, why)) {
        return false;
    }
    match.assign(all_match.begin(), all_match.begin() + demands.size());
    if (!flow_feasible(active, demands, why)) {
        return false;
    }
    std::vector<Demand> pinned_demands, pinned_all;
    std::vector<int> pinned_match;
    for (size_t i = 0; i < active.size(); ++i) {
        int bi = active[i];
        Buffer &b = buffers[bi];
        if (b.bel != BelId() || b.source) {
            continue;
        }
        bool any = false;
        for (size_t k = 0; k < b.choices.size() && !any; ++k) {
            BelId cand;
            for (BelId option : b.choices[k]) {
                if (!bel_taken(active, bi, option)) {
                    cand = option;
                    break;
                }
            }
            if (cand == BelId()) {
                continue;
            }
            pinned_demands.clear();
            for (size_t j = 0; j < active.size(); ++j) {
                if (j != i) {
                    pinned_demands.insert(pinned_demands.end(), per_buffer[j].begin(), per_buffer[j].end());
                }
            }
            if (!buffer_demands(active, bi, cand, pinned_demands, nullptr) ||
                !dcs_demands_of(active, bi, cand, pinned_demands, nullptr)) {
                continue;
            }
            pinned_all = pinned_demands;
            any = entry_demands(active, pinned_demands, bi, cand, pinned_all, nullptr) &&
                  match_demands(pinned_all, pinned_match, nullptr);
        }
        if (!any) {
            if (why != nullptr) {
                *why = stringf("no single BUFG gate drives a free clock spine for every quadrant that the users of "
                               "'%s' are in, next to the other clocks",
                               ctx->nameOf(b.items.front().net));
            }
            return false;
        }
    }
    return true;
}

// The matching lets a BUFG without a bel take resources of different gates for different classes.
// This flow adds the bels: every demand of a BUFG goes through one of its bels, a bel carries at
// most as many demands as one BUFG has, and a component takes one. It rejects, for example, more
// BUFGs on gates with a single index than that index has quadrants.
bool GowinClockPlanner::flow_feasible(const std::vector<int> &active, const std::vector<Demand> &demands,
                                      std::string *why)
{
    std::vector<int> demand_count(buffers.size(), 0);
    int total = 0, max_count = 0;
    for (auto &d : demands) {
        // a clock source has one gate and no bel to choose, the matching covers it exactly
        if (d.buffer >= 0 && !buffers[d.buffer].source) {
            ++demand_count[d.buffer];
            ++total;
        }
    }
    for (int bi : active) {
        max_count = std::max(max_count, demand_count[bi]);
    }
    if (total == 0) {
        return true;
    }
    // nodes: source, one per active BUFG, two per bel, one per component, sink
    dict<BelId, int> bel_node;
    std::vector<BelId> bels;
    for (int bi : active) {
        Buffer &b = buffers[bi];
        if (b.source) {
            continue;
        }
        for (auto &list : b.choices) {
            for (BelId cand : list) {
                if ((b.bel == BelId() && !bel_taken(active, bi, cand)) || cand == b.bel) {
                    if (!bel_node.count(cand)) {
                        bel_node[cand] = bels.size();
                        bels.push_back(cand);
                    }
                }
            }
        }
    }
    int n_active = active.size(), n_bels = bels.size(), n_comp = component_rank.size();
    int source = 0, sink = 1 + n_active + 2 * n_bels + n_comp;
    auto buffer_node = [&](int i) { return 1 + i; };
    auto bel_in = [&](int i) { return 1 + n_active + 2 * i; };
    auto bel_out = [&](int i) { return 1 + n_active + 2 * i + 1; };
    auto comp_node = [&](int c) { return 1 + n_active + 2 * n_bels + c; };
    struct Edge
    {
        int to, cap, rev;
    };
    std::vector<std::vector<Edge>> adj(sink + 1);
    auto add_edge = [&](int from, int to, int cap) {
        adj[from].push_back({to, cap, int(adj[to].size())});
        adj[to].push_back({from, 0, int(adj[from].size()) - 1});
    };
    for (int i = 0; i < n_active; ++i) {
        Buffer &b = buffers[active[i]];
        int count = demand_count[active[i]];
        if (count == 0) {
            continue;
        }
        add_edge(source, buffer_node(i), count);
        for (auto &list : b.choices) {
            for (BelId cand : list) {
                if ((b.bel == BelId() && !bel_taken(active, active[i], cand)) || cand == b.bel) {
                    add_edge(buffer_node(i), bel_in(bel_node.at(cand)), count);
                }
            }
        }
    }
    for (int i = 0; i < n_bels; ++i) {
        add_edge(bel_in(i), bel_out(i), max_count);
        for (int c : bel_components.at(bels[i])) {
            add_edge(bel_out(i), comp_node(c), 1);
        }
    }
    for (int c = 0; c < n_comp; ++c) {
        add_edge(comp_node(c), sink, 1);
    }
    // maximum flow by shortest augmenting paths
    int flow = 0;
    while (true) {
        std::vector<int> prev_node(sink + 1, -1), prev_edge(sink + 1, -1);
        std::queue<int> visit;
        visit.push(source);
        prev_node[source] = source;
        while (!visit.empty() && prev_node[sink] < 0) {
            int u = visit.front();
            visit.pop();
            for (size_t e = 0; e < adj[u].size(); ++e) {
                if (adj[u][e].cap > 0 && prev_node[adj[u][e].to] < 0) {
                    prev_node[adj[u][e].to] = u;
                    prev_edge[adj[u][e].to] = e;
                    visit.push(adj[u][e].to);
                }
            }
        }
        if (prev_node[sink] < 0) {
            break;
        }
        int push = INT_MAX;
        for (int v = sink; v != source; v = prev_node[v]) {
            push = std::min(push, adj[prev_node[v]][prev_edge[v]].cap);
        }
        for (int v = sink; v != source; v = prev_node[v]) {
            Edge &e = adj[prev_node[v]][prev_edge[v]];
            e.cap -= push;
            adj[v][e.rev].cap += push;
        }
        flow += push;
    }
    if (flow < total && why != nullptr) {
        std::vector<int> lefts;
        for (size_t i = 0; i < demands.size(); ++i) {
            if (demands[i].buffer >= 0 && !buffers[demands[i].buffer].source) {
                lefts.push_back(i);
            }
        }
        *why = stringf("the clocks %s need %d clock spines, and the BUFG gates left for them drive only %d",
                       net_list(demands, lefts).c_str(), total, flow);
    }
    return flow == total;
}

// The contested sinks of the search, sinks that share a wire one after the other, within a group
// the sink with the fewest entries first
void GowinClockPlanner::gather_steps(const std::vector<int> &active)
{
    steps.clear();
    for (int bi : active) {
        for (size_t ii = 0; ii < buffers[bi].items.size(); ++ii) {
            for (size_t si = 0; si < buffers[bi].items[ii].contested.size(); ++si) {
                steps.push_back(Step{bi, int(ii), -1, int(si)});
            }
        }
    }
    for (size_t di = 0; di < dcs_demands.size(); ++di) {
        if (!dcs_involved(dcs_demands[di], active)) {
            continue;
        }
        for (size_t si = 0; si < dcs_demands[di].item.contested.size(); ++si) {
            steps.push_back(Step{-1, -1, int(di), int(si)});
        }
    }
    std::vector<int> parent(steps.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto root = [&](int k) {
        while (parent[k] != k) {
            k = parent[k];
        }
        return k;
    };
    auto entries = [&](int k) -> const std::vector<Entry> & {
        return step_item(steps[k]).contested[steps[k].sink].entries;
    };
    dict<WireId, int> first_with;
    for (size_t k = 0; k < steps.size(); ++k) {
        for (const Entry &e : entries(k)) {
            for (WireId w : e.wires) {
                auto found = first_with.find(w);
                if (found == first_with.end()) {
                    first_with[w] = k;
                } else {
                    parent[root(k)] = root(found->second);
                }
            }
        }
    }
    std::vector<int> group(steps.size());
    for (size_t k = 0; k < steps.size(); ++k) {
        group[k] = root(k);
    }
    std::vector<int> group_first(steps.size(), INT_MAX);
    for (size_t k = 0; k < steps.size(); ++k) {
        group_first[group[k]] = std::min(group_first[group[k]], int(k));
    }
    std::vector<int> order(steps.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
        if (group_first[group[x]] != group_first[group[y]]) {
            return group_first[group[x]] < group_first[group[y]];
        }
        return entries(x).size() < entries(y).size();
    });
    std::vector<Step> sorted;
    for (int k : order) {
        sorted.push_back(steps[k]);
    }
    steps = sorted;
}

// A contested wire belongs to the net that chose it first. True when net may have all wires of e.
bool GowinClockPlanner::claim(const Entry &e, IdString net)
{
    for (WireId w : e.wires) {
        auto found = claimed.find(w);
        if (found != claimed.end() && found->second.first != net) {
            return false;
        }
    }
    for (WireId w : e.wires) {
        auto &owner = claimed[w];
        owner.first = net;
        ++owner.second;
    }
    return true;
}

void GowinClockPlanner::release(const Entry &e, IdString net)
{
    for (WireId w : e.wires) {
        auto &owner = claimed.at(w);
        NPNR_ASSERT(owner.first == net);
        if (--owner.second == 0) {
            claimed.erase(w);
        }
    }
}

// Decides the entries of the contested sinks from step k on, then the bels and covers
bool GowinClockPlanner::search_entries(const std::vector<int> &active, size_t k, std::vector<Demand> &demands,
                                       std::vector<int> &match)
{
    if (k == steps.size()) {
        return search(active, 0, demands, match);
    }
    const Step &s = steps[k];
    Item &item = step_item(s);
    ContestedSink &cs = item.contested[s.sink];
    for (size_t i = 0; i < cs.entries.size(); ++i) {
        const Entry &e = cs.entries[i];
        if (!entry_viable(active, s, e, -1, BelId()) || !claim(e, item.net->name)) {
            continue;
        }
        cs.choice = i;
        if (feasible(active, demands, match) && search_entries(active, k + 1, demands, match)) {
            return true;
        }
        cs.choice = -1;
        release(e, item.net->name);
    }
    return false;
}

// Assigns bels to the active BUFGs from position idx on, the preferred gate first, and decides
// their covers. After the last one the DCS they feed are decided.
bool GowinClockPlanner::search(const std::vector<int> &active, size_t idx, std::vector<Demand> &demands,
                               std::vector<int> &match)
{
    if (idx == active.size()) {
        return decide_dcs(active, 0, demands, match);
    }
    Buffer &b = buffers[active[idx]];
    if (b.fixed) {
        b.bel = b.buf->bel;
        return decide_cover(active, idx, 0, demands, match);
    }
    for (size_t k = 0; k < b.choices.size(); ++k) {
        if (b.twin >= 0 && buffers[b.twin].bel != BelId() && b.choice_class[k] < bel_class.at(buffers[b.twin].bel)) {
            continue;
        }
        BelId cand;
        for (BelId option : b.choices[k]) {
            if (!bel_taken(active, active[idx], option)) {
                cand = option;
                break;
            }
        }
        if (cand == BelId()) {
            continue;
        }
        b.bel = cand;
        if (feasible(active, demands, match) && decide_cover(active, idx, 0, demands, match)) {
            return true;
        }
    }
    b.bel = BelId();
    return false;
}

// Decides the cover of item ii of the buffer at idx, then the next item or the next buffer
bool GowinClockPlanner::decide_cover(const std::vector<int> &active, size_t idx, size_t ii,
                                     std::vector<Demand> &demands, std::vector<int> &match)
{
    Buffer &b = buffers[active[idx]];
    if (ii == b.items.size()) {
        return search(active, idx + 1, demands, match);
    }
    Item &item = b.items[ii];
    pool<int> driven = drives(active, active[idx], b.bel);
    pool<int> hit;
    for (auto &cs : item.contested) {
        hit.insert(cs.entries[cs.choice].cls);
    }
    std::vector<std::vector<int>> sets;
    if (!hitting_sets(item, [&](int cls) { return !options_in(driven, cls).empty(); }, hit, sets)) {
        return false;
    }
    for (auto &set : sets) {
        item.cover = set;
        item.decided = true;
        // a single hitting set was the demand of the check before, it needs no new one
        if ((sets.size() == 1 || feasible(active, demands, match)) &&
            decide_cover(active, idx, ii + 1, demands, match)) {
            return true;
        }
        item.decided = false;
    }
    return false;
}

// Decides the covers of the DCS outputs that a BUFG or clock source of the search feeds, then
// checks the complete assignment
bool GowinClockPlanner::decide_dcs(const std::vector<int> &active, size_t di, std::vector<Demand> &demands,
                                   std::vector<int> &match)
{
    while (di < dcs_demands.size() && !dcs_involved(dcs_demands[di], active)) {
        ++di;
    }
    if (di == dcs_demands.size()) {
        return feasible(active, demands, match);
    }
    DcsDemand &dd = dcs_demands[di];
    Item &item = dd.item;
    pool<int> hit;
    for (auto &cs : item.contested) {
        hit.insert(cs.entries[cs.choice].cls);
    }
    std::vector<std::vector<int>> sets;
    if (!hitting_sets(item, [&](int cls) { return !dcs_options(dd, cls, -1, BelId()).empty(); }, hit, sets)) {
        return false;
    }
    for (auto &set : sets) {
        item.cover = set;
        item.decided = true;
        if ((sets.size() == 1 || feasible(active, demands, match)) && decide_dcs(active, di + 1, demands, match)) {
            return true;
        }
        item.decided = false;
    }
    return false;
}

// The shared wires of a path belong to net from now on
void GowinClockPlanner::reserve(IdString net, const std::vector<WireId> &wires)
{
    for (WireId w : wires) {
        if (shared_wires.count(w)) {
            reserved_wires[w] = net;
            net_plans[net].insert(w);
        }
    }
}

// Moves the planned BUFGs and reserves the wires of the matching and of the chosen entries
void GowinClockPlanner::commit(const std::vector<int> &planned, const std::vector<Demand> &demands,
                               const std::vector<int> &match)
{
    for (int bi : planned) {
        Buffer &b = buffers[bi];
        if (b.bel != b.buf->bel) {
            log_info("    '%s' moves from %s to %s, whose gate drives a free clock spine in every quadrant of its "
                     "users.\n",
                     ctx->nameOf(b.buf), ctx->nameOfBel(b.buf->bel), ctx->nameOfBel(b.bel));
            ctx->unbindBel(b.buf->bel);
        }
    }
    for (int bi : planned) {
        Buffer &b = buffers[bi];
        if (b.buf->bel == BelId()) {
            ctx->bindBel(b.bel, b.buf, b.strength);
        }
    }
    // a source without a plan owns no wire, so that allows() leaves it all the free ones
    pool<IdString> planned_sources;
    for (int bi : planned) {
        if (buffers[bi].source) {
            planned_sources.insert(buffers[bi].items.front().net->name);
        }
    }
    pool<int> dcs_done;
    for (size_t di = 0; di < demands.size(); ++di) {
        const Demand &d = demands[di];
        if (d.dcs >= 0) {
            const DcsDemand &dd = dcs_demands[d.dcs];
            BelId dcs_bel = dcs_bels[match[di] - component_rank.size()];
            reserve(dd.item.net->name, {dcs_input_wire(dcs_bel, dd.first_input)});
            for (auto &in : dd.buffered_inputs) {
                reserve(in.second->getPort(id_O)->name, {dcs_input_wire(dcs_bel, in.first)});
            }
            for (auto &in : dd.source_inputs) {
                if (planned_sources.count(in.second->name)) {
                    reserve(in.second->name, {dcs_input_wire(dcs_bel, in.first)});
                }
            }
            if (dcs_done.insert(d.dcs).second) {
                for (auto &cs : dd.item.contested) {
                    reserve(dd.item.net->name, cs.entries[cs.choice].wires);
                }
            }
            continue;
        }
        const Buffer &b = buffers[d.buffer];
        const Item &item = b.items[d.item];
        const GateReach &r = reach_of(b, b.bel);
        for (size_t ri = 0; ri < item.regions.size(); ++ri) {
            if (!item.regions[ri].count(match[di])) {
                continue;
            }
            // the lowest spine of the resource that reaches the region
            WireId pick;
            for (WireId spine : item.region_spines[ri]) {
                if (r.count(spine) && resource_of_spine.at(spine) == match[di] &&
                    (pick == WireId() || spine_number(spine) < spine_number(pick))) {
                    pick = spine;
                }
            }
            NPNR_ASSERT(pick != WireId());
            reserve(item.net->name, r.at(pick));
        }
    }
    for (int bi : planned) {
        Buffer &b = buffers[bi];
        for (auto &item : b.items) {
            for (auto &cs : item.contested) {
                reserve(item.net->name, cs.entries[cs.choice].wires);
            }
        }
        IdString own = b.items.front().net->name;
        if (net_plans.count(own)) {
            pool<WireId> own_wires = net_plans.at(own);
            for (IdString alias : b.aliases) {
                net_plans[alias] = own_wires;
            }
        }
        if (ctx->verbose) {
            for (auto &item : b.items) {
                if (!net_plans.count(item.net->name)) {
                    continue;
                }
                std::vector<WireId> wires(net_plans.at(item.net->name).begin(), net_plans.at(item.net->name).end());
                std::sort(wires.begin(), wires.end());
                std::string names, entries;
                for (WireId w : wires) {
                    if (is_spine(w) || gwu.is_dcs_input(w)) {
                        names += " " + ctx->getWireName(w)[1].str(ctx);
                    } else if (contested_wires.count(w)) {
                        entries += " " + std::string(ctx->nameOfWire(w));
                    }
                }
                log_info("    '%s' from %s takes%s%s%s\n", ctx->nameOf(item.net), ctx->nameOfBel(b.bel), names.c_str(),
                         entries.empty() ? "" : ", enters through", entries.c_str());
            }
        }
    }
}

// The spines of a class, for the messages
std::string GowinClockPlanner::class_name(int cls) const
{
    std::vector<WireId> spines;
    for (int r : class_resources[cls]) {
        if (r < int(component_rank.size())) {
            spines.insert(spines.end(), component_spines.at(r).begin(), component_spines.at(r).end());
        } else {
            spines.push_back(ctx->getBelPinWire(dcs_bels[r - component_rank.size()], id_CLKOUT));
        }
    }
    std::sort(spines.begin(), spines.end(), [&](WireId x, WireId y) { return spine_number(x) < spine_number(y); });
    std::string name = "{";
    for (WireId w : spines) {
        name += " " + ctx->getWireName(w)[1].str(ctx);
    }
    return name + " }";
}

// The nets of the demands at the positions in which, each once, as "'a', 'b' and 'c'"
std::string GowinClockPlanner::net_list(const std::vector<Demand> &demands, const std::vector<int> &which) const
{
    std::vector<std::string> names;
    for (int i : which) {
        const Demand &d = demands[i];
        NetInfo *net = d.dcs >= 0 ? dcs_demands[d.dcs].item.net : buffers[d.buffer].items[d.item].net;
        std::string name = stringf("'%s'", ctx->nameOf(net));
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    std::string list;
    for (size_t i = 0; i < names.size(); ++i) {
        list += names[i];
        if (i + 2 < names.size()) {
            list += ", ";
        } else if (i + 2 == names.size()) {
            list += " and ";
        }
    }
    return list;
}

// Takes back every decision of the search for the active set
void GowinClockPlanner::reset(const std::vector<int> &active)
{
    for (int bi : active) {
        Buffer &b = buffers[bi];
        b.bel = b.fixed ? b.buf->bel : BelId();
        for (auto &item : b.items) {
            item.decided = false;
            for (auto &cs : item.contested) {
                cs.choice = -1;
            }
        }
    }
    for (auto &dd : dcs_demands) {
        dd.item.decided = false;
        for (auto &cs : dd.item.contested) {
            cs.choice = -1;
        }
    }
    claimed.clear();
}

// Why no assignment serves the active set: the reason the relaxation at the root of the search
// gives, or, when the root passes, that only the search found out
std::string GowinClockPlanner::explain(const std::vector<int> &active)
{
    reset(active);
    std::string why;
    std::vector<Demand> demands;
    std::vector<int> match;
    if (feasible(active, demands, match, &why)) {
        why = "no assignment of BUFG bels, clock spines and entries into the clock network serves all of them";
    }
    return why;
}

void GowinClockPlanner::plan(const std::vector<IdString> &buf_nets, const std::vector<IdString> &clk_nets)
{
    if (buf_nets.empty()) {
        return;
    }
    std::vector<BelId> bufg_bels;
    gather_resources(bufg_bels, clk_nets);
    gather_demands(buf_nets, clk_nets);
    std::vector<int> active;
    for (size_t bi = 0; bi < buffers.size(); ++bi) {
        if (buffers[bi].active) {
            gather_choices(buffers[bi], bufg_bels);
            active.push_back(bi);
        }
    }
    if (active.empty()) {
        return;
    }
    // fixed BUFGs first, then the BUFGs with the most sinks, then the clock sources: they take
    // what the BUFGs leave, or they route as they are
    auto rank = [&](const Buffer &b) { return b.source ? 2 : b.fixed ? 0 : 1; };
    std::stable_sort(active.begin(), active.end(), [&](int x, int y) {
        if (rank(buffers[x]) != rank(buffers[y])) {
            return rank(buffers[x]) < rank(buffers[y]);
        }
        return buffers[x].sinks > buffers[y].sinks;
    });
    find_twins(active);
    if (ctx->debug) {
        for (int bi : active) {
            std::string gates;
            for (auto &list : buffers[bi].choices) {
                gates += std::string(" ") + ctx->nameOfBel(list.front());
                if (list.size() > 1) {
                    gates += stringf("(+%d)", int(list.size()) - 1);
                }
            }
            log_info("    '%s' with %d sink regions may take%s%s%s\n", ctx->nameOf(buffers[bi].buf),
                     int(buffers[bi].items.front().regions.size()), gates.c_str(),
                     buffers[bi].twin >= 0 ? ", after " : "",
                     buffers[bi].twin >= 0 ? ctx->nameOf(buffers[buffers[bi].twin].buf) : "");
            for (auto &item : buffers[bi].items) {
                std::string regions;
                for (auto &spines : item.region_spines) {
                    std::vector<WireId> sorted(spines.begin(), spines.end());
                    std::sort(sorted.begin(), sorted.end(),
                              [&](WireId x, WireId y) { return spine_number(x) < spine_number(y); });
                    regions += " {";
                    for (WireId w : sorted) {
                        regions += " " + ctx->getWireName(w)[1].str(ctx);
                    }
                    regions += " }";
                }
                log_info("      regions of '%s':%s\n", ctx->nameOf(item.net), regions.c_str());
                for (auto &cs : item.contested) {
                    for (auto &e : cs.entries) {
                        std::string wires;
                        for (WireId w : e.wires) {
                            wires += " " + std::string(ctx->nameOfWire(w));
                        }
                        log_info("      entry of '%s' into %s: from %s through%s\n", ctx->nameOf(item.net),
                                 ctx->nameOfWire(cs.sink), class_name(e.cls).c_str(),
                                 wires.empty() ? " nothing contested" : wires.c_str());
                    }
                }
            }
        }
    }
    // The clocks join the plan one by one in that order, and every time all BUFGs that may move
    // are assigned anew. The first BUFG without room fails: without a plan its clock would reach
    // part of its sinks through a second clock gate. A clock source without room is left out.
    std::vector<Demand> demands;
    std::vector<int> match;
    std::vector<int> planned;
    size_t fixed_count = 0;
    auto attempt = [&]() {
        reset(planned);
        gather_steps(planned);
        return search_entries(planned, 0, demands, match);
    };
    for (int bi : active) {
        planned.push_back(bi);
        if (!attempt()) {
            Buffer &b = buffers[bi];
            NetInfo *net = b.items.front().net;
            std::string why = explain(planned);
            if (b.source) {
                // a source without a plan is routed with whatever the planned nets leave, and the
                // router reports the sinks it cannot reach. The search of the others is restored.
                if (ctx->verbose) {
                    log_info("    '%s' is not planned and routes as it is: %s\n", ctx->nameOf(net), why.c_str());
                }
                planned.pop_back();
                bool restored = attempt();
                NPNR_ASSERT(restored);
                continue;
            }
            // a BUFG asks for the clock network: reaching part of its users outside it adds clock skew
            // that can break the design on the device, so a BUFG without a plan is an error
            if (b.fixed) {
                log_error("'%s' is fixed to %s by a BEL constraint, but its gate does not drive a free clock spine for "
                          "every quadrant that its users are in%s: %s. Use a BUFG bel with another gate.\n",
                          ctx->nameOf(b.buf), ctx->nameOfBel(b.buf->bel),
                          fixed_count > 0 ? ", together with the BUFGs fixed before it" : "", why.c_str());
            }
            log_error("Cannot plan the clock '%s' together with the %d clocks planned before it: %s. Routing part of "
                      "its users outside the clock network would add clock skew. Fewer clocks per quadrant or BEL "
                      "constraints on the BUFGs are needed.\n",
                      ctx->nameOf(net), int(planned.size()) - 1, why.c_str());
        }
        if (buffers[bi].fixed) {
            ++fixed_count;
        }
    }
    if (ctx->verbose) {
        log_info("planned the clock spines of %d clock nets\n", int(planned.size()));
    }
    if (ctx->debug) {
        log_info("    %d feasibility checks\n", checks);
    }
    commit(planned, demands, match);
}

NEXTPNR_NAMESPACE_END

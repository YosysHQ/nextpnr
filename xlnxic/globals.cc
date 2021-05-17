/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include <queue>

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct GlobalPnR
{
    GlobalPnR(Context *ctx) : ctx(ctx){};
    Context *ctx;
    ClockRegion find_centroid(const NetInfo *net)
    {
        int count = 0;
        double cx = 0, cy = 0;
        for (auto &usr : net->users) {
            BelId bel = usr.cell->bel;
            ClockRegion usr_cr = ctx->get_clock_region(bel.tile);
            if (usr_cr == ClockRegion())
                continue;
            ++count;
            cx += usr_cr.x;
            cy += usr_cr.y;
        }
        if (count == 0)
            return ClockRegion();
        else
            return ClockRegion(int((cx / count) + 0.5), int((cy / count) + 0.5));
    }
    struct RouteQueueEntry
    {
        WireId wire;
        // To route UltraScale+ globals in the correct, balanced, fashion, we implement a state machine of sort that
        // only allows certain transitions between intent types
        uint32_t flags;
        static constexpr uint32_t FLAG_HIT_HDISTR = 0x01;
        static constexpr uint32_t FLAG_HIT_VDISTR = 0x02;
        static constexpr uint32_t FLAG_HIT_HROUTE = 0x04;
        static constexpr uint32_t FLAG_HIT_VROUTE = 0x08;
        static constexpr uint32_t FLAG_ILLEGAL = 0xFFFFFFFF;
        // TODO: do we want a hops/togo estimate, or is simple backwards BFS always fine
        RouteQueueEntry(WireId wire) : wire(wire), flags(0){};
        RouteQueueEntry(WireId wire, uint32_t flags) : wire(wire), flags(flags){};
    };
    // Our route 'state machine' for UltraScale+ globals. Returns the next flags if permissible or FLAG_ILLEGAL if not
    uint32_t fsm_ultrascale_plus(uint32_t curr_flags, PipId pip, ClockRegion centroid)
    {
        uint32_t next_flags = curr_flags;
        WireId uphill = ctx->getPipSrcWire(pip);
        IdString intent = ctx->getWireType(uphill);
        if (intent == id_NODE_GLOBAL_HDISTR) {
            if ((curr_flags & RouteQueueEntry::FLAG_HIT_VDISTR) || (curr_flags & RouteQueueEntry::FLAG_HIT_HROUTE) ||
                (curr_flags & RouteQueueEntry::FLAG_HIT_VROUTE)) {
                // Musn't go backwards from VDISTR to HDISTR
                return RouteQueueEntry::FLAG_ILLEGAL;
            }
            next_flags |= RouteQueueEntry::FLAG_HIT_HDISTR;
        } else if (intent == id_NODE_GLOBAL_VDISTR) {
            if ((curr_flags & RouteQueueEntry::FLAG_HIT_HROUTE) || (curr_flags & RouteQueueEntry::FLAG_HIT_VROUTE)) {
                // Musn't go backwards from ROUTE to VDISTR
                return RouteQueueEntry::FLAG_ILLEGAL;
            }
            next_flags |= RouteQueueEntry::FLAG_HIT_VDISTR;
        }

        // If we are leaving DISTR for the first time, we must be in the centroid clock region
        if (intent == id_NODE_GLOBAL_HROUTE || intent == id_NODE_GLOBAL_VROUTE || intent == id_NODE_GLOBAL_BUFG) {
            if ((!(curr_flags & RouteQueueEntry::FLAG_HIT_HROUTE) &&
                 !(curr_flags & RouteQueueEntry::FLAG_HIT_VROUTE)) &&
                ctx->get_clock_region(pip.tile) != centroid) {
                return RouteQueueEntry::FLAG_ILLEGAL;
            }
        }

        if (intent == id_NODE_GLOBAL_HROUTE)
            next_flags |= RouteQueueEntry::FLAG_HIT_HROUTE;
        if (intent == id_NODE_GLOBAL_VROUTE)
            next_flags |= RouteQueueEntry::FLAG_HIT_VROUTE;
        return next_flags;
    }
    enum RouteType
    {
        TYPE_GENERAL,   // general global routing
        TYPE_USP_CLOCK, // UltraScale+ clock
        // ...
    };

    ClockRegion curr_centroid{};
    RouteType curr_mode = TYPE_GENERAL;
    bool is_illegal_routethru(NetInfo *net, const PortRef &user, PipId pip)
    {
        const auto &data = chip_pip_info(ctx->chip_info, pip);
        if (!(data.flags & PipDataPOD::FLAG_PSEUDO))
            return false;
        for (auto pin : data.pseudo_pip) {
            BelId bel(pip.tile, pin.bel_index);
            CellInfo *bound = ctx->getBoundBelCell(bel);
            if (bound && bound != net->driver.cell && bound != user.cell)
                return true;
        }
        return false;
    }
    int route_arc(NetInfo *net, store_index<PortRef> user, size_t phys_pin, int iter_limit = 10000000, bool bind = true)
    {
        std::queue<RouteQueueEntry> queue;
        dict<WireId, PipId> pip_downhill; // TODO: what if same wire visited with different flags?
        int iter = 0;

        WireId src_wire = ctx->getNetinfoSourceWire(net);
        WireId dst_wire = ctx->getNetinfoSinkWire(net, net->users.at(user), phys_pin);
        queue.emplace(dst_wire);

        WireId found_startpoint = WireId();
        // Backwards BFS loop
        while (!queue.empty() && (iter++ < iter_limit)) {
            RouteQueueEntry entry = queue.front();
            WireId cursor = entry.wire;
            queue.pop();
            if (cursor == src_wire || ctx->getBoundWireNet(cursor) == net) {
                found_startpoint = cursor;
                break;
            }
            for (PipId uh : ctx->getPipsUphill(cursor)) {
                if (!ctx->checkPipAvailForNet(uh, net))
                    continue;
                WireId pip_src = ctx->getPipSrcWire(uh);
                if (!ctx->checkWireAvail(pip_src) && ctx->getBoundWireNet(pip_src) != net)
                    continue;
                // Don't route-through bound bels - globals won't be ripped up later...
                if (is_illegal_routethru(net, net->users.at(user), uh))
                    continue;
                // Never use general inter-tile wires for global routes
                if (ctx->is_general_routing(pip_src))
                    continue;
                // Don't revisit wires
                if (pip_downhill.count(pip_src))
                    continue;
                // Check the task-specific requirements
                uint32_t next_flags = entry.flags;
                if (curr_mode == TYPE_USP_CLOCK)
                    next_flags = fsm_ultrascale_plus(next_flags, uh, curr_centroid);
                if (next_flags == RouteQueueEntry::FLAG_ILLEGAL)
                    continue;
                pip_downhill[pip_src] = uh;
                queue.emplace(pip_src, next_flags);
            }
        }

        if (found_startpoint == WireId())
            return -1;

        int hops = 0;
        // Compute the hop count; and bind the routing if we need to
        WireId dh_cursor = found_startpoint;
        if (bind && net->wires.empty()) {
            // Need to bind source
            ctx->bindWire(src_wire, net, STRENGTH_LOCKED);
        }
        while (pip_downhill.count(dh_cursor)) {
            PipId pip = pip_downhill.at(dh_cursor);
            if (bind) {
#if 0
                if (!net->wires.count(ctx->getPipDstWire(pip)))
                    log_info("bind %s %s\n", ctx->nameOfWire(ctx->getPipDstWire(pip)), ctx->nameOfPip(pip));
#endif
                ctx->bindPip(pip, net, STRENGTH_LOCKED);
            }
            ++hops;
            dh_cursor = ctx->getPipDstWire(pip);
        }

        return hops;
    }
    void route_net(NetInfo *net, RouteType mode)
    {
        log_info("    routing global net %s\n", ctx->nameOf(net));
        curr_mode = mode;
        if (mode == TYPE_USP_CLOCK)
            curr_centroid = find_centroid(net);
        int total_arcs = 0, global_arcs = 0;
        for (auto usr : net->users.enumerate()) {
            for (size_t pin = 0; pin < ctx->getNetinfoSinkWireCount(net, usr.value); pin++) {
                ++total_arcs;
                int result = route_arc(net, usr.index, pin, 10000000, true);
                if (result != -1)
                    ++global_arcs;
            }
        }
        log_info("        %d/%d arcs used dedicated resources.\n", global_arcs, total_arcs);
    }
    const dict<IdString, pool<IdString>> clock_inputs = {
            {id_BUFGCE, {id_I}},
            {id_BUFGCE_DIV, {id_I}},
            {id_BUFGCTRL, {id_I0}},
            {id_BUFGCTRL, {id_I1}},
            {id_BUFG_PS, {id_I}},
            {id_BUFG_GT, {id_I}},
            {id_MMCME2_ADV, {id_CLKIN1, id_CLKIN2}},
            {id_MMCME3_ADV, {id_CLKIN1, id_CLKIN2}},
            {id_MMCME4_ADV, {id_CLKIN1, id_CLKIN2}},
            {id_MMCME2_BASE, {id_CLKIN1}},
            {id_MMCME3_BASE, {id_CLKIN1}},
            {id_MMCME4_BASE, {id_CLKIN1}},
    };
    int get_route_hops(NetInfo *net, WireId dst_wire, const pool<PipId> &blocked,
                       std::vector<PipId> *to_block = nullptr, int max_hops = 12, int max_iters = 5000)
    {
        WireId src_wire = ctx->getNetinfoSourceWire(net);
        if (src_wire == WireId() || dst_wire == WireId())
            return -1;
        dict<WireId, std::pair<PipId, int>> visited;
        std::queue<WireId> to_visit;
        to_visit.push(dst_wire);
        visited[dst_wire] = std::make_pair(PipId(), 0);
        bool max_hops_hit = false;
        // backwards bfs to determine hops
        int iters = 0;
        while (!to_visit.empty() && iters++ < max_iters) {
            WireId cursor = to_visit.front();
            to_visit.pop();
            int curr_hops = visited.at(cursor).second;
            if (cursor == src_wire) {
                if (to_block) {
                    to_block->clear();
                    // Update the set of blocked pips with this route
                    while (visited.count(cursor)) {
                        PipId pip = visited.at(cursor).first;
                        if (pip == PipId())
                            break;
                        to_block->push_back(pip);
                        cursor = ctx->getPipDstWire(pip);
                    }
                }
                return curr_hops;
            }
            for (auto pip : ctx->getPipsUphill(cursor)) {
                WireId next = ctx->getPipSrcWire(pip);
                if (visited.count(next))
                    continue;
                if (ctx->is_general_routing(next))
                    continue;
                if (blocked.count(pip))
                    continue;
                if (curr_hops == max_hops) {
                    max_hops_hit = true;
                } else {
                    visited[next] = std::make_pair(pip, curr_hops + 1);
                    to_visit.push(next);
                }
            }
        }
        if (to_visit.empty() && !max_hops_hit) {
            // Assume unrouteable
            return -1;
        } else {
            // Assume routeable, but hard
            // Max hops or iter limit exceeded: assume it is routeable, just hard
            return max_hops + 2;
        }
    };
};
struct CellCompare
{
    bool operator()(const CellInfo *a, const CellInfo *b) const { return a->name < b->name; }
};
} // namespace

void Arch::preplace_globals()
{
    // TopoSort clock tree so we can place in order
    GlobalPnR globals(getCtx());
    TopoSort<CellInfo *, CellCompare> topo;
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (!globals.clock_inputs.count(ci->type))
            continue;
        topo.node(ci);
        for (auto inp : globals.clock_inputs.at(ci->type)) {
            NetInfo *inp_net = ci->getPort(inp);
            if (inp_net && inp_net->driver.cell && globals.clock_inputs.count(inp_net->driver.cell->type))
                topo.edge(inp_net->driver.cell, ci);
        }
    }
    bool no_loops = topo.sort();
    if (!no_loops)
        log_warning("Clock placer found loops in the clock tree!\n");
    // Find useful bels for clock resources
    dict<IdString, pool<BelId>> candidate_bels;
    for (auto to_place : topo.sorted) {
        candidate_bels[to_place->type];
    }
    for (auto bel : getBels()) {
        if (!checkBelAvail(bel))
            continue;
        for (auto &typ : candidate_bels) {
            if (isValidBelForCellType(typ.first, bel))
                typ.second.insert(bel);
        }
    }
    // Do placement
    pool<PipId> blocked_pips;
    for (auto to_place : topo.sorted) {
        BelId best_bel = BelId();
        std::vector<PipId> best_to_block;
        // Place based on dedicated input routing
        // Attempt crudely to avoid conflicts
        const int max_hops = 12;
        int best_hops = max_hops + 5;
        bool dedi_routing_found = true;
        for (auto inp : globals.clock_inputs.at(to_place->type)) {
            NetInfo *inp_net = to_place->getPort(inp);
            if (!inp_net || !inp_net->driver.cell || inp_net->driver.cell->bel == BelId())
                continue;
            for (auto tgt : candidate_bels.at(to_place->type)) {
                WireId dst = WireId();
                for (auto phys_pin : getBelPinsForCellPin(to_place, inp)) {
                    dst = getBelPinWire(tgt, phys_pin);
                    if (dst != WireId())
                        break;
                }
                if (dst == WireId())
                    continue;
                // Compute hops using this bel. Pick the lowest hops for the closest routed path...
                int hops = globals.get_route_hops(inp_net, dst, blocked_pips, nullptr, max_hops);
                if (hops != -1 && hops < best_hops) {
                    // Rerun, updating the set of newly blocked pips
                    globals.get_route_hops(inp_net, dst, blocked_pips, &best_to_block, max_hops);
                    best_hops = hops;
                    best_bel = tgt;
                }
            }
        }
        // Meh, just place randomly for now
        if (best_bel == BelId()) {
            dedi_routing_found = false;
            for (auto tgt : candidate_bels.at(to_place->type)) {
                best_bel = tgt;
                break;
            }
        }
        if (best_bel == BelId()) {
            log_error("Failed to find a bel for clock cell '%s' of type '%s'.\n", nameOf(to_place),
                      to_place->type.c_str(this));
        }
        log_info("Binding clock cell '%s' to bel '%s'%s.\n", nameOf(to_place), getCtx()->nameOfBel(best_bel),
                 dedi_routing_found ? " based on dedicated routing" : "");
        bindBel(best_bel, to_place, STRENGTH_LOCKED);
        // Remove from the pool of available bels
        for (auto &cand : candidate_bels)
            cand.second.erase(best_bel);
        for (PipId to_block : best_to_block)
            blocked_pips.insert(to_block);
    }
}
void Arch::route_globals()
{
    log_info("Routing globals...\n");
    GlobalPnR globals(getCtx());
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_BUFGCE || ci->type == id_BUFGCTRL || ci->type == id_BUFGCE_DIV) {
            NetInfo *o = ci->getPort(id_O);
            if (o)
                globals.route_net(o, family == ArchFamily::XCUP ? GlobalPnR::TYPE_USP_CLOCK : GlobalPnR::TYPE_GENERAL);
        }
    }
}

NEXTPNR_NAMESPACE_END

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

#include <queue>

NEXTPNR_NAMESPACE_BEGIN

namespace {

struct GlobalVist
{
    PipId downhill = PipId();
    int total_hops = 0;
    int global_hops = 0;
    bool operator<(const GlobalVist &other) const
    {
        return (total_hops < other.total_hops) ||
               ((total_hops == other.total_hops) && (global_hops > other.global_hops));
    }
};

// This is our main global routing implementation. It is used both to actually route globals; and also to discover if
// global buffers have available short routes from their source for auto-placement
static int route_global_arc(Context *ctx, NetInfo *net, store_index<PortRef> usr_idx, size_t phys_port_idx,
                            int max_hops, bool dry_run)
{
    auto &usr = net->users.at(usr_idx);
    WireId src = ctx->getNetinfoSourceWire(net);
    WireId dest = ctx->getNetinfoSinkWire(net, usr, phys_port_idx);
    if (dest == WireId()) {
        if (dry_run)
            return -1;
        else
            log_error("Arc %d.%d (%s.%s) of net %s has no sink wire!\n", usr_idx.idx(), int(phys_port_idx),
                      ctx->nameOf(usr.cell), ctx->nameOf(usr.port), ctx->nameOf(net));
    }
    // Consider any existing routing put in place by the site router, etc
    int start_hops = 0;
    while (net->wires.count(dest) && dest != src) {
        dest = ctx->getPipSrcWire(net->wires.at(dest).pip);
        ++start_hops;
    }
    // The main BFS implementation
    // Currently this is a backwards-BFS from sink to source (or pre-existing routing) that avoids general routing. It
    // currently aims for minimum hops as a primary goal and maximum global resource usage as a secondary goal. More
    // advanced heuristics will likely be needed for more complex situation
    WireId startpoint;
    GlobalVist best_visit;
    std::queue<WireId> visit_queue;
    dict<WireId, GlobalVist> visits;

    visit_queue.push(dest);
    visits[dest].downhill = PipId();
    visits[dest].total_hops = start_hops;

    while (!visit_queue.empty()) {
        WireId cursor = visit_queue.front();
        visit_queue.pop();
        auto curr_visit = visits.at(cursor);
        // We're now at least one layer deeper than a valid visit, any further exploration is futile
        if (startpoint != WireId() && curr_visit.total_hops > best_visit.total_hops)
            break;
        // Valid end of routing
        if ((cursor == src) || (ctx->getBoundWireNet(cursor) == net)) {
            if (startpoint == WireId() || curr_visit < best_visit) {
                startpoint = cursor;
                best_visit = curr_visit;
            }
        }
        // Explore uphill
        for (auto pip : ctx->getPipsUphill(cursor)) {
            if (!dry_run && !ctx->checkPipAvailForNet(pip, net))
                continue;
            WireId pip_src = ctx->getPipSrcWire(pip);
            if (!dry_run && !ctx->checkWireAvail(pip_src) && ctx->getBoundWireNet(pip_src) != net)
                continue;
            auto cat = ctx->get_wire_category(pip_src);
            if (!ctx->is_site_wire(pip_src) && cat == WIRE_CAT_GENERAL)
                continue; // never allow general routing
            GlobalVist next_visit;
            next_visit.downhill = pip;
            next_visit.total_hops = curr_visit.total_hops + 1;
            if (max_hops != -1 && next_visit.total_hops > max_hops)
                continue;
            next_visit.global_hops = curr_visit.global_hops + ((cat == WIRE_CAT_GLOBAL) ? 1 : 0);
            auto fnd_src = visits.find(pip_src);
            if (fnd_src == visits.end() || next_visit < fnd_src->second) {
                visit_queue.push(pip_src);
                visits[pip_src] = next_visit;
            }
        }
    }

    if (startpoint == WireId())
        return -1;
    if (!dry_run) {
        if (ctx->getBoundWireNet(startpoint) == nullptr)
            ctx->bindWire(startpoint, net, STRENGTH_LOCKED);

        WireId cursor = startpoint;
        std::vector<PipId> pips;
        // Create a list of pips on the routed path
        while (true) {
            PipId pip = visits.at(cursor).downhill;
            if (pip == PipId())
                break;
            pips.push_back(pip);
            cursor = ctx->getPipDstWire(pip);
        }
        // Reverse that list
        std::reverse(pips.begin(), pips.end());
        // Bind pips until we hit already-bound routing
        for (PipId pip : pips) {
            WireId dst = ctx->getPipDstWire(pip);
            if (ctx->getBoundWireNet(dst) == net)
                break;
            ctx->bindPip(pip, net, STRENGTH_LOCKED);
        }
    }
    return visits.at(startpoint).total_hops;
}
}; // namespace

const GlobalCellPOD *Arch::global_cell_info(IdString cell_type) const
{
    for (const auto &glb_cell : chip_info->global_cells)
        if (IdString(glb_cell.cell_type) == cell_type)
            return &glb_cell;

    return nullptr;
}

void Arch::place_globals()
{
    log_info("Placing globals...\n");

    Context *ctx = getCtx();
    IdString gnd_net_name(chip_info->constants->gnd_net_name);
    IdString vcc_net_name(chip_info->constants->vcc_net_name);

    // TODO: for more complex PLL type setups, we might want a toposort or iterative loop as the PLL must be placed
    // before the GBs it drives
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        const GlobalCellPOD *glb_cell = global_cell_info(ci->type);
        if (glb_cell == nullptr)
            continue;
        // Ignore if already placed
        if (ci->bel != BelId())
            continue;

        for (const auto &pin : glb_cell->pins) {
            if (!pin.guide_placement)
                continue;

            IdString pin_name(pin.name);
            if (!ci->ports.count(pin_name))
                continue;
            auto &port = ci->ports.at(pin_name);

            // only input ports currently used for placement guidance
            if (port.type != PORT_IN)
                continue;

            NetInfo *net = port.net;
            if (net == nullptr || net->name == gnd_net_name || net->name == vcc_net_name)
                continue;
            // Ignore if there is no driver; or the driver is not placed
            if (net->driver.cell == nullptr || net->driver.cell->bel == BelId())
                continue;

            // TODO: substantial performance improvements are probably possible, although of questionable benefit given
            // the low number of globals in a typical device...
            BelId best_bel;
            int shortest_distance = std::numeric_limits<int>::max();

            for (auto bel : getBels()) {
                int distance;
                if (!isValidBelForCellType(ci->type, bel))
                    continue;
                if (!checkBelAvail(bel))
                    continue;
                // Provisionally place
                bindBel(bel, ci, STRENGTH_WEAK);
                if (!isBelLocationValid(bel))
                    goto fail;
                // Check distance
                distance = route_global_arc(ctx, net, port.user_idx, 0, pin.max_hops, true);
                if (distance != -1 && distance < shortest_distance) {
                    best_bel = bel;
                    shortest_distance = distance;
                }
            fail:
                unbindBel(bel);
            }

            if (best_bel != BelId()) {
                bindBel(best_bel, ci, STRENGTH_LOCKED);
                log_info("    placed %s:%s at %s\n", ctx->nameOf(ci), ctx->nameOf(ci->type), ctx->nameOfBel(best_bel));
                break;
            }
        }
    }
}

void Arch::route_globals()
{
    log_info("Routing globals...\n");

    Context *ctx = getCtx();
    IdString gnd_net_name(chip_info->constants->gnd_net_name);
    IdString vcc_net_name(chip_info->constants->vcc_net_name);

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        const GlobalCellPOD *glb_cell = global_cell_info(ci->type);
        if (glb_cell == nullptr)
            continue;
        for (const auto &pin : glb_cell->pins) {
            IdString pin_name(pin.name);
            if (!ci->ports.count(pin_name))
                continue;
            auto &port = ci->ports.at(pin_name);

            // TOOD: routing of input ports, too
            // output ports are generally the first priority though
            if (port.type != PORT_OUT)
                continue;

            NetInfo *net = port.net;
            if (net == nullptr || net->name == gnd_net_name || net->name == vcc_net_name)
                continue;

            int total_sinks = 0;
            int global_sinks = 0;

            for (auto usr : net->users.enumerate()) {
                for (size_t j = 0; j < ctx->getNetinfoSinkWireCount(net, usr.value); j++) {
                    int result = route_global_arc(ctx, net, usr.index, j, pin.max_hops, false);
                    ++total_sinks;
                    if (result != -1)
                        ++global_sinks;
                    if ((result == -1) && pin.force_routing)
                        log_error("Failed to route arc %d.%d (%s.%s) of net %s using dedicated global routing!\n",
                                  usr.index.idx(), int(j), ctx->nameOf(usr.value.cell), ctx->nameOf(usr.value.port),
                                  ctx->nameOf(net));
                }
            }

            log_info("    routed %d/%d sinks of net %s using dedicated routing.\n", global_sinks, total_sinks,
                     ctx->nameOf(net));
        }
    }
}

NEXTPNR_NAMESPACE_END

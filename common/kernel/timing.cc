/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2018  Eddie Hung <eddieh@ece.ubc.ca>
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

#include "timing.h"
#include <algorithm>
#include <boost/range/adaptor/reversed.hpp>
#include <deque>
#include <map>
#include <utility>
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
const char *edge_name(ClockEdge edge) { return (edge == FALLING_EDGE) ? "negedge" : "posedge"; }
} // namespace

void TimingAnalyser::setup()
{
    init_ports();
    get_cell_delays();
    topo_sort();
    setup_port_domains();
    identify_related_domains();
    run();
}

void TimingAnalyser::run(bool update_route_delays)
{
    reset_times();
    if (update_route_delays)
        get_route_delays();
    walk_forward();
    walk_backward();
    compute_slack();
    compute_criticality();
}

void TimingAnalyser::init_ports()
{
    // Per cell port structures
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        for (auto &port : ci->ports) {
            auto &data = ports[CellPortKey(ci->name, port.first)];
            data.type = port.second.type;
            data.cell_port = CellPortKey(ci->name, port.first);
        }
    }
}

void TimingAnalyser::get_cell_delays()
{
    for (auto &port : ports) {
        CellInfo *ci = cell_info(port.first);
        auto &pi = port_info(port.first);
        auto &pd = port.second;

        IdString name = port.first.port;
        // Ignore dangling ports altogether for timing purposes
        if (!pi.net)
            continue;
        pd.cell_arcs.clear();
        int clkInfoCount = 0;
        TimingPortClass cls = ctx->getPortTimingClass(ci, name, clkInfoCount);
        if (cls == TMG_STARTPOINT || cls == TMG_ENDPOINT || cls == TMG_CLOCK_INPUT || cls == TMG_GEN_CLOCK ||
            cls == TMG_IGNORE)
            continue;
        if (pi.type == PORT_IN) {
            // Input ports might have setup/hold relationships
            if (cls == TMG_REGISTER_INPUT) {
                for (int i = 0; i < clkInfoCount; i++) {
                    auto info = ctx->getPortClockingInfo(ci, name, i);
                    if (!ci->ports.count(info.clock_port) || ci->ports.at(info.clock_port).net == nullptr)
                        continue;
                    pd.cell_arcs.emplace_back(CellArc::SETUP, info.clock_port, DelayQuad(info.setup, info.setup),
                                              info.edge);
                    pd.cell_arcs.emplace_back(CellArc::HOLD, info.clock_port, DelayQuad(info.hold, info.hold),
                                              info.edge);
                }
            }
            // Combinational delays through cell
            for (auto &other_port : ci->ports) {
                auto &op = other_port.second;
                // ignore dangling ports and non-outputs
                if (op.net == nullptr || op.type != PORT_OUT)
                    continue;
                DelayQuad delay;
                bool is_path = ctx->getCellDelay(ci, name, other_port.first, delay);
                if (is_path)
                    pd.cell_arcs.emplace_back(CellArc::COMBINATIONAL, other_port.first, delay);
            }
        } else if (pi.type == PORT_OUT) {
            // Output ports might have clk-to-q relationships
            if (cls == TMG_REGISTER_OUTPUT) {
                for (int i = 0; i < clkInfoCount; i++) {
                    auto info = ctx->getPortClockingInfo(ci, name, i);
                    if (!ci->ports.count(info.clock_port) || ci->ports.at(info.clock_port).net == nullptr)
                        continue;
                    pd.cell_arcs.emplace_back(CellArc::CLK_TO_Q, info.clock_port, info.clockToQ, info.edge);
                }
            }
            // Combinational delays through cell
            for (auto &other_port : ci->ports) {
                auto &op = other_port.second;
                // ignore dangling ports and non-inputs
                if (op.net == nullptr || op.type != PORT_IN)
                    continue;
                DelayQuad delay;
                bool is_path = ctx->getCellDelay(ci, other_port.first, name, delay);
                if (is_path)
                    pd.cell_arcs.emplace_back(CellArc::COMBINATIONAL, other_port.first, delay);
            }
        }
    }
}

void TimingAnalyser::get_route_delays()
{
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        if (ni->driver.cell == nullptr || ni->driver.cell->bel == BelId())
            continue;
        for (auto &usr : ni->users) {
            if (usr.cell->bel == BelId())
                continue;
            ports.at(CellPortKey(usr)).route_delay = DelayPair(ctx->getNetinfoRouteDelay(ni, usr));
        }
    }
}

void TimingAnalyser::set_route_delay(CellPortKey port, DelayPair value) { ports.at(port).route_delay = value; }

void TimingAnalyser::topo_sort()
{
    TopoSort<CellPortKey> topo;
    for (auto &port : ports) {
        auto &pd = port.second;
        // All ports are nodes
        topo.node(port.first);
        if (pd.type == PORT_IN) {
            // inputs: combinational arcs through the cell are edges
            for (auto &arc : pd.cell_arcs) {
                if (arc.type != CellArc::COMBINATIONAL)
                    continue;
                topo.edge(port.first, CellPortKey(port.first.cell, arc.other_port));
            }
        } else if (pd.type == PORT_OUT) {
            // output: routing arcs are edges
            const NetInfo *pn = port_info(port.first).net;
            if (pn != nullptr) {
                for (auto &usr : pn->users)
                    topo.edge(port.first, CellPortKey(usr));
            }
        }
    }
    bool no_loops = topo.sort();
    if (!no_loops && verbose_mode) {
        log_info("Found %d combinational loops:\n", int(topo.loops.size()));
        int i = 0;
        for (auto &loop : topo.loops) {
            log_info("    loop %d:\n", ++i);
            for (auto &port : loop) {
                log_info("        %s.%s (%s)\n", ctx->nameOf(port.cell), ctx->nameOf(port.port),
                         ctx->nameOf(port_info(port).net));
            }
        }
    }
    have_loops = !no_loops;
    std::swap(topological_order, topo.sorted);
}

void TimingAnalyser::setup_port_domains()
{
    for (auto &d : domains) {
        d.startpoints.clear();
        d.endpoints.clear();
    }
    // Go forward through the topological order (domains from the PoV of arrival time)
    bool first_iter = true;
    do {
        updated_domains = false;
        for (auto port : topological_order) {
            auto &pd = ports.at(port);
            auto &pi = port_info(port);
            if (pi.type == PORT_OUT) {
                if (first_iter) {
                    for (auto &fanin : pd.cell_arcs) {
                        if (fanin.type != CellArc::CLK_TO_Q)
                            continue;
                        // registered outputs are startpoints
                        auto dom = domain_id(port.cell, fanin.other_port, fanin.edge);
                        // create per-domain data
                        pd.arrival[dom];
                        domains.at(dom).startpoints.emplace_back(port, fanin.other_port);
                    }
                }
                // copy domains across routing
                if (pi.net != nullptr)
                    for (auto &usr : pi.net->users)
                        copy_domains(port, CellPortKey(usr), false);
            } else {
                // copy domains from input to output
                for (auto &fanout : pd.cell_arcs) {
                    if (fanout.type != CellArc::COMBINATIONAL)
                        continue;
                    copy_domains(port, CellPortKey(port.cell, fanout.other_port), false);
                }
            }
        }
        // Go backward through the topological order (domains from the PoV of required time)
        for (auto port : reversed_range(topological_order)) {
            auto &pd = ports.at(port);
            auto &pi = port_info(port);
            if (pi.type == PORT_OUT) {
                // copy domains from output to input
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type != CellArc::COMBINATIONAL)
                        continue;
                    copy_domains(port, CellPortKey(port.cell, fanin.other_port), true);
                }
            } else {
                if (first_iter) {
                    for (auto &fanout : pd.cell_arcs) {
                        if (fanout.type != CellArc::SETUP)
                            continue;
                        // registered inputs are endpoints
                        auto dom = domain_id(port.cell, fanout.other_port, fanout.edge);
                        // create per-domain data
                        pd.required[dom];
                        domains.at(dom).endpoints.emplace_back(port, fanout.other_port);
                    }
                }
                // copy port to driver
                if (pi.net != nullptr && pi.net->driver.cell != nullptr)
                    copy_domains(port, CellPortKey(pi.net->driver), true);
            }
        }
        // Iterate over ports and find domain paris
        for (auto port : topological_order) {
            auto &pd = ports.at(port);
            for (auto &arr : pd.arrival)
                for (auto &req : pd.required) {
                    pd.domain_pairs[domain_pair_id(arr.first, req.first)];
                }
        }
        first_iter = false;
        // If there are loops, repeat the process until a fixed point is reached, as there might be unusual ways to
        // visit points, which would result in a missing domain key and therefore crash later on
    } while (have_loops && updated_domains);
    for (auto &dp : domain_pairs) {
        auto &launch_data = domains.at(dp.key.launch);
        auto &capture_data = domains.at(dp.key.capture);
        if (launch_data.key.clock != capture_data.key.clock)
            continue;
        IdString clk = launch_data.key.clock;
        delay_t period = ctx->getDelayFromNS(1.0e9 / ctx->setting<float>("target_freq"));
        if (ctx->nets.count(clk)) {
            NetInfo *clk_net = ctx->nets.at(clk).get();
            if (clk_net->clkconstr) {
                period = clk_net->clkconstr->period.minDelay();
            }
        }
        if (launch_data.key.edge != capture_data.key.edge)
            period /= 2;
        dp.period = DelayPair(period);
    }
}

void TimingAnalyser::identify_related_domains()
{

    // Identify clock nets
    pool<IdString> clock_nets;
    for (const auto &domain : domains) {
        clock_nets.insert(domain.key.clock);
    }

    // For each clock net identify all nets that can possibly drive it. Compute
    // cumulative delays to each of them.
    std::function<void(const NetInfo *, dict<IdString, delay_t> &, delay_t)> find_net_drivers =
            [&](const NetInfo *ni, dict<IdString, delay_t> &drivers, delay_t delay_acc) {
                // Get driving cell and port
                if (ni == nullptr)
                    return;
                const CellInfo *cell = ni->driver.cell;
                if (cell == nullptr)
                    return;

                const IdString port = ni->driver.port;

                bool didGoUpstream = false;

                // The cell has only one port
                if (cell->ports.size() == 1) {
                    drivers[ni->name] = delay_acc;
                    return;
                }

                // Get the driver timing class
                int info_count = 0;
                auto timing_class = ctx->getPortTimingClass(cell, port, info_count);

                // The driver must be a combinational output
                if (timing_class != TMG_COMB_OUTPUT) {
                    drivers[ni->name] = delay_acc;
                    return;
                }

                // Recurse upstream through all input ports that have combinational
                // paths to this driver
                for (const auto &it : cell->ports) {
                    const auto &pi = it.second;

                    // Only connected inputs
                    if (pi.type != PORT_IN) {
                        continue;
                    }
                    if (pi.net == nullptr) {
                        continue;
                    }

                    // The input must be a combinational input
                    timing_class = ctx->getPortTimingClass(cell, pi.name, info_count);
                    if (timing_class != TMG_COMB_INPUT) {
                        continue;
                    }
                    // There must be a combinational arc
                    DelayQuad delay;
                    if (!ctx->getCellDelay(cell, pi.name, port, delay)) {
                        continue;
                    }

                    // Recurse
                    find_net_drivers(pi.net, drivers, delay_acc + delay.maxDelay());
                    didGoUpstream = true;
                }

                // Did not propagate upstream through the cell, mark the net as driver
                if (!didGoUpstream) {
                    drivers[ni->name] = delay_acc;
                }
            };

    // Identify possible drivers for each clock domain
    dict<IdString, dict<IdString, delay_t>> clock_drivers;
    for (const auto &domain : domains) {

        const NetInfo *ni = ctx->nets.at(domain.key.clock).get();
        if (ni == nullptr)
            continue;
        if (ni->driver.cell == nullptr)
            continue;

        dict<IdString, delay_t> drivers;
        find_net_drivers(ni, drivers, 0);

        clock_drivers[domain.key.clock] = drivers;

        if (ctx->debug) {
            log("Clock '%s' can be driven by:\n", domain.key.clock.str(ctx).c_str());
            for (const auto &it : drivers) {
                const NetInfo *net = ctx->nets.at(it.first).get();
                log(" %s.%s delay %.3fns\n", net->driver.cell->name.str(ctx).c_str(), net->driver.port.str(ctx).c_str(),
                    ctx->getDelayNS(it.second));
            }
        }
    }

    // Identify related clocks. For simplicity do it both for A->B and B->A
    // cases.
    for (const auto &c1 : clock_drivers) {
        for (const auto &c2 : clock_drivers) {

            if (c1 == c2) {
                continue;
            }

            // Make an intersection of the two drivers sets
            pool<IdString> common_drivers;
            for (const auto &it : c1.second) {
                common_drivers.insert(it.first);
            }
            for (const auto &it : c2.second) {
                common_drivers.insert(it.first);
            }

            for (auto it = common_drivers.begin(); it != common_drivers.end();) {
                if (!c1.second.count(*it) || !c2.second.count(*it)) {
                    it = common_drivers.erase(it);
                } else {
                    ++it;
                }
            }

            if (ctx->debug) {

                log("Possible common driver(s) for clocks '%s' and '%s'\n", c1.first.str(ctx).c_str(),
                    c2.first.str(ctx).c_str());

                for (const auto &it : common_drivers) {

                    const NetInfo *ni = ctx->nets.at(it).get();
                    const CellInfo *cell = ni->driver.cell;
                    const IdString port = ni->driver.port;

                    log(" net '%s', cell %s (%s), port %s\n", it.str(ctx).c_str(), cell->name.str(ctx).c_str(),
                        cell->type.str(ctx).c_str(), port.str(ctx).c_str());
                }
            }

            // If there is no single driver then consider the two clocks
            // unrelated.
            if (common_drivers.size() != 1) {
                continue;
            }

            // Compute delay from c1 to c2 and store it
            auto driver = *common_drivers.begin();
            auto delay = c2.second.at(driver) - c1.second.at(driver);
            clock_delays[std::make_pair(c1.first, c2.first)] = delay;
        }
    }
}

void TimingAnalyser::reset_times()
{
    for (auto &port : ports) {
        auto do_reset = [&](dict<domain_id_t, ArrivReqTime> &times) {
            for (auto &t : times) {
                t.second.value = init_delay;
                t.second.path_length = 0;
                t.second.bwd_min = CellPortKey();
                t.second.bwd_max = CellPortKey();
            }
        };
        do_reset(port.second.arrival);
        do_reset(port.second.required);
        for (auto &dp : port.second.domain_pairs) {
            dp.second.setup_slack = std::numeric_limits<delay_t>::max();
            dp.second.hold_slack = std::numeric_limits<delay_t>::max();
            dp.second.max_path_length = 0;
            dp.second.criticality = 0;
            dp.second.budget = 0;
        }
        port.second.worst_crit = 0;
        port.second.worst_setup_slack = std::numeric_limits<delay_t>::max();
        port.second.worst_hold_slack = std::numeric_limits<delay_t>::max();
    }
}

void TimingAnalyser::set_arrival_time(CellPortKey target, domain_id_t domain, DelayPair arrival, int path_length,
                                      CellPortKey prev)
{
    auto &arr = ports.at(target).arrival.at(domain);
    if (arrival.max_delay > arr.value.max_delay) {
        arr.value.max_delay = arrival.max_delay;
        arr.bwd_max = prev;
    }
    if (!setup_only && (arrival.min_delay < arr.value.min_delay)) {
        arr.value.min_delay = arrival.min_delay;
        arr.bwd_min = prev;
    }
    arr.path_length = std::max(arr.path_length, path_length);
}

void TimingAnalyser::set_required_time(CellPortKey target, domain_id_t domain, DelayPair required, int path_length,
                                       CellPortKey prev)
{
    auto &req = ports.at(target).required.at(domain);
    if (required.min_delay < req.value.min_delay) {
        req.value.min_delay = required.min_delay;
        req.bwd_min = prev;
    }
    if (!setup_only && (required.max_delay > req.value.max_delay)) {
        req.value.max_delay = required.max_delay;
        req.bwd_max = prev;
    }
    req.path_length = std::max(req.path_length, path_length);
}

void TimingAnalyser::walk_forward()
{
    // Assign initial arrival time to domain startpoints
    for (domain_id_t dom_id = 0; dom_id < domain_id_t(domains.size()); ++dom_id) {
        auto &dom = domains.at(dom_id);
        for (auto &sp : dom.startpoints) {
            auto &pd = ports.at(sp.first);
            DelayPair init_arrival(0);
            CellPortKey clock_key;
            // TODO: clock routing delay, if analysis of that is enabled
            if (sp.second != IdString()) {
                // clocked startpoints have a clock-to-out time
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type == CellArc::CLK_TO_Q && fanin.other_port == sp.second) {
                        init_arrival = init_arrival + fanin.value.delayPair();
                        break;
                    }
                }
                clock_key = CellPortKey(sp.first.cell, sp.second);
            }
            set_arrival_time(sp.first, dom_id, init_arrival, 1, clock_key);
        }
    }
    // Walk forward in topological order
    for (auto p : topological_order) {
        auto &pd = ports.at(p);
        for (auto &arr : pd.arrival) {
            if (pd.type == PORT_OUT) {
                // Output port: propagate delay through net, adding route delay
                NetInfo *net = port_info(p).net;
                if (net != nullptr)
                    for (auto &usr : net->users) {
                        CellPortKey usr_key(usr);
                        auto &usr_pd = ports.at(usr_key);
                        set_arrival_time(usr_key, arr.first, arr.second.value + usr_pd.route_delay,
                                         arr.second.path_length, p);
                    }
            } else if (pd.type == PORT_IN) {
                // Input port; propagate delay through cell, adding combinational delay
                for (auto &fanout : pd.cell_arcs) {
                    if (fanout.type != CellArc::COMBINATIONAL)
                        continue;
                    set_arrival_time(CellPortKey(p.cell, fanout.other_port), arr.first,
                                     arr.second.value + fanout.value.delayPair(), arr.second.path_length + 1, p);
                }
            }
        }
    }
}

void TimingAnalyser::walk_backward()
{
    // Assign initial required time to domain endpoints
    // Note that clock frequency will be considered later in the analysis for, for now all required times are normalised
    // to 0ns
    for (domain_id_t dom_id = 0; dom_id < domain_id_t(domains.size()); ++dom_id) {
        auto &dom = domains.at(dom_id);
        for (auto &ep : dom.endpoints) {
            auto &pd = ports.at(ep.first);
            DelayPair init_setuphold(0);
            CellPortKey clock_key;
            // TODO: clock routing delay, if analysis of that is enabled
            if (ep.second != IdString()) {
                // Add setup/hold time, if this endpoint is clocked
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type == CellArc::SETUP && fanin.other_port == ep.second)
                        init_setuphold.min_delay -= fanin.value.maxDelay();
                    if (fanin.type == CellArc::HOLD && fanin.other_port == ep.second)
                        init_setuphold.max_delay -= fanin.value.maxDelay();
                }
                clock_key = CellPortKey(ep.first.cell, ep.second);
            }
            set_required_time(ep.first, dom_id, init_setuphold, 1, clock_key);
        }
    }
    // Walk backwards in topological order
    for (auto p : reversed_range(topological_order)) {
        auto &pd = ports.at(p);
        for (auto &req : pd.required) {
            if (pd.type == PORT_IN) {
                // Input port: propagate delay back through net, subtracting route delay
                NetInfo *net = port_info(p).net;
                if (net != nullptr && net->driver.cell != nullptr)
                    set_required_time(CellPortKey(net->driver), req.first,
                                      req.second.value - DelayPair(pd.route_delay.maxDelay()), req.second.path_length,
                                      p);
            } else if (pd.type == PORT_OUT) {
                // Output port : propagate delay back through cell, subtracting combinational delay
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type != CellArc::COMBINATIONAL)
                        continue;
                    set_required_time(CellPortKey(p.cell, fanin.other_port), req.first,
                                      req.second.value - DelayPair(fanin.value.maxDelay()), req.second.path_length + 1,
                                      p);
                }
            }
        }
    }
}

void TimingAnalyser::print_fmax()
{
    // Temporary testing code for comparison only
    dict<int, double> domain_fmax;
    for (auto p : topological_order) {
        auto &pd = ports.at(p);
        for (auto &req : pd.required) {
            if (pd.arrival.count(req.first)) {
                auto &arr = pd.arrival.at(req.first);
                double fmax = 1000.0 / ctx->getDelayNS(arr.value.maxDelay() - req.second.value.minDelay());
                if (!domain_fmax.count(req.first) || domain_fmax.at(req.first) > fmax)
                    domain_fmax[req.first] = fmax;
            }
        }
    }
    for (auto &fm : domain_fmax) {
        log_info("Domain %s Worst Fmax %.02f\n", ctx->nameOf(domains.at(fm.first).key.clock), fm.second);
    }
}

void TimingAnalyser::compute_slack()
{
    for (auto &dp : domain_pairs) {
        dp.worst_setup_slack = std::numeric_limits<delay_t>::max();
        dp.worst_hold_slack = std::numeric_limits<delay_t>::max();
    }
    for (auto p : topological_order) {
        auto &pd = ports.at(p);
        for (auto &pdp : pd.domain_pairs) {
            auto &dp = domain_pairs.at(pdp.first);

            // Get clock names
            const auto &launch_clock = domains.at(dp.key.launch).key.clock;
            const auto &capture_clock = domains.at(dp.key.capture).key.clock;

            // Get clock-to-clock delay if any
            delay_t clock_to_clock = 0;
            auto clocks = std::make_pair(launch_clock, capture_clock);
            if (clock_delays.count(clocks)) {
                clock_to_clock = clock_delays.at(clocks);
            }

            auto &arr = pd.arrival.at(dp.key.launch);
            auto &req = pd.required.at(dp.key.capture);
            pdp.second.setup_slack = 0 - (arr.value.maxDelay() - req.value.minDelay() + clock_to_clock);
            if (!setup_only)
                pdp.second.hold_slack = arr.value.minDelay() - req.value.maxDelay() + clock_to_clock;
            pdp.second.max_path_length = arr.path_length + req.path_length;
            if (dp.key.launch == dp.key.capture)
                pd.worst_setup_slack = std::min(pd.worst_setup_slack, dp.period.minDelay() + pdp.second.setup_slack);
            dp.worst_setup_slack = std::min(dp.worst_setup_slack, pdp.second.setup_slack);
            if (!setup_only) {
                pd.worst_hold_slack = std::min(pd.worst_hold_slack, pdp.second.hold_slack);
                dp.worst_hold_slack = std::min(dp.worst_hold_slack, pdp.second.hold_slack);
            }
        }
    }
}

void TimingAnalyser::compute_criticality()
{
    for (auto p : topological_order) {
        auto &pd = ports.at(p);
        for (auto &pdp : pd.domain_pairs) {
            auto &dp = domain_pairs.at(pdp.first);
            float crit =
                    1.0f - (float(pdp.second.setup_slack) - float(dp.worst_setup_slack)) / float(-dp.worst_setup_slack);
            crit = std::min(crit, 1.0f);
            crit = std::max(crit, 0.0f);
            pdp.second.criticality = crit;
            pd.worst_crit = std::max(pd.worst_crit, crit);
        }
    }
}

std::vector<CellPortKey> TimingAnalyser::get_failing_eps(domain_id_t domain_pair, int count)
{
    std::vector<CellPortKey> failing_eps;
    delay_t last_slack = std::numeric_limits<delay_t>::min();
    auto &dp = domain_pairs.at(domain_pair);
    auto &cap_d = domains.at(dp.key.capture);
    while (int(failing_eps.size()) < count) {
        CellPortKey next;
        delay_t next_slack = std::numeric_limits<delay_t>::max();
        for (auto ep : cap_d.endpoints) {
            auto &pd = ports.at(ep.first);
            if (!pd.domain_pairs.count(domain_pair))
                continue;
            delay_t ep_slack = pd.domain_pairs.at(domain_pair).setup_slack;
            if (ep_slack < next_slack && ep_slack > last_slack) {
                next = ep.first;
                next_slack = ep_slack;
            }
        }
        if (next == CellPortKey())
            break;
        failing_eps.push_back(next);
        last_slack = next_slack;
    }
    return failing_eps;
}

void TimingAnalyser::print_critical_path(CellPortKey endpoint, domain_id_t domain_pair)
{
    CellPortKey cursor = endpoint;
    auto &dp = domain_pairs.at(domain_pair);
    log("    endpoint %s.%s (slack %.02fns):\n", ctx->nameOf(cursor.cell), ctx->nameOf(cursor.port),
        ctx->getDelayNS(ports.at(cursor).domain_pairs.at(domain_pair).setup_slack));
    while (cursor != CellPortKey()) {
        log("        %s.%s (net %s)\n", ctx->nameOf(cursor.cell), ctx->nameOf(cursor.port),
            ctx->nameOf(ctx->cells.at(cursor.cell)->getPort(cursor.port)));
        if (!ports.at(cursor).arrival.count(dp.key.launch))
            break;
        cursor = ports.at(cursor).arrival.at(dp.key.launch).bwd_max;
    }
}

void TimingAnalyser::print_report()
{
    for (int i = 0; i < int(domain_pairs.size()); i++) {
        auto &dp = domain_pairs.at(i);
        auto &launch = domains.at(dp.key.launch);
        auto &capture = domains.at(dp.key.capture);
        log("Worst endpoints for %s %s -> %s %s\n", edge_name(launch.key.edge), ctx->nameOf(launch.key.clock),
            edge_name(capture.key.edge), ctx->nameOf(capture.key.clock));
        auto failing_eps = get_failing_eps(i, 5);
        for (auto &ep : failing_eps)
            print_critical_path(ep, i);
        log_break();
    }

    print_fmax();

    for (const auto &it : clock_delays) {
        log_info("Clock-to-clock %s -> %s: %0.02f ns\n", it.first.first.str(ctx).c_str(),
                 it.first.second.str(ctx).c_str(), ctx->getDelayNS(it.second));
    }
}

domain_id_t TimingAnalyser::domain_id(IdString cell, IdString clock_port, ClockEdge edge)
{
    return domain_id(ctx->cells.at(cell)->ports.at(clock_port).net, edge);
}
domain_id_t TimingAnalyser::domain_id(const NetInfo *net, ClockEdge edge)
{
    NPNR_ASSERT(net != nullptr);
    ClockDomainKey key{net->name, edge};
    auto inserted = domain_to_id.emplace(key, domains.size());
    if (inserted.second) {
        domains.emplace_back(key);
    }
    return inserted.first->second;
}
domain_id_t TimingAnalyser::domain_pair_id(domain_id_t launch, domain_id_t capture)
{
    ClockDomainPairKey key{launch, capture};
    auto inserted = pair_to_id.emplace(key, domain_pairs.size());
    if (inserted.second) {
        domain_pairs.emplace_back(key);
    }
    return inserted.first->second;
}

void TimingAnalyser::copy_domains(const CellPortKey &from, const CellPortKey &to, bool backward)
{
    auto &f = ports.at(from), &t = ports.at(to);
    for (auto &dom : (backward ? f.required : f.arrival)) {
        updated_domains |= (backward ? t.required : t.arrival).emplace(dom.first, ArrivReqTime{}).second;
    }
}

CellInfo *TimingAnalyser::cell_info(const CellPortKey &key) { return ctx->cells.at(key.cell).get(); }

PortInfo &TimingAnalyser::port_info(const CellPortKey &key) { return ctx->cells.at(key.cell)->ports.at(key.port); }

/** LEGACY CODE BEGIN **/

typedef std::vector<const PortRef *> PortRefVector;
typedef std::map<int, unsigned> DelayFrequency;

struct CriticalPathData
{
    PortRefVector ports;
    delay_t path_delay;
    delay_t path_period;
};

typedef dict<ClockPair, CriticalPathData> CriticalPathDataMap;

typedef dict<IdString, std::vector<NetSinkTiming>> DetailedNetTimings;

struct Timing
{
    Context *ctx;
    bool net_delays;
    bool update;
    delay_t min_slack;
    CriticalPathDataMap *crit_path;
    DelayFrequency *slack_histogram;
    DetailedNetTimings *detailed_net_timings;
    IdString async_clock;

    struct TimingData
    {
        TimingData() : max_arrival(), max_path_length(), min_remaining_budget() {}
        TimingData(delay_t max_arrival) : max_arrival(max_arrival), max_path_length(), min_remaining_budget() {}
        delay_t max_arrival;
        unsigned max_path_length = 0;
        delay_t min_remaining_budget;
        bool false_startpoint = false;
        std::vector<delay_t> min_required;
        dict<ClockEvent, delay_t> arrival_time;
    };

    Timing(Context *ctx, bool net_delays, bool update, CriticalPathDataMap *crit_path = nullptr,
           DelayFrequency *slack_histogram = nullptr, DetailedNetTimings *detailed_net_timings = nullptr)
            : ctx(ctx), net_delays(net_delays), update(update), min_slack(1.0e12 / ctx->setting<float>("target_freq")),
              crit_path(crit_path), slack_histogram(slack_histogram), detailed_net_timings(detailed_net_timings),
              async_clock(ctx->id("$async$"))
    {
    }

    delay_t walk_paths()
    {
        const auto clk_period = ctx->getDelayFromNS(1.0e9 / ctx->setting<float>("target_freq"));

        // First, compute the topological order of nets to walk through the circuit, assuming it is a _acyclic_ graph
        // TODO(eddieh): Handle the case where it is cyclic, e.g. combinatorial loops
        std::vector<NetInfo *> topological_order;
        dict<const NetInfo *, dict<ClockEvent, TimingData>, hash_ptr_ops> net_data;
        // In lieu of deleting edges from the graph, simply count the number of fanins to each output port
        dict<const PortInfo *, unsigned, hash_ptr_ops> port_fanin;

        std::vector<IdString> input_ports;
        std::vector<const PortInfo *> output_ports;

        pool<IdString> ooc_port_nets;

        // In out-of-context mode, top-level inputs look floating but aren't
        if (bool_or_default(ctx->settings, ctx->id("arch.ooc"))) {
            for (auto &p : ctx->ports) {
                if (p.second.type != PORT_IN || p.second.net == nullptr)
                    continue;
                ooc_port_nets.insert(p.second.net->name);
            }
        }

        for (auto &cell : ctx->cells) {
            input_ports.clear();
            output_ports.clear();
            for (auto &port : cell.second->ports) {
                if (!port.second.net)
                    continue;
                if (port.second.type == PORT_OUT)
                    output_ports.push_back(&port.second);
                else
                    input_ports.push_back(port.first);
            }

            for (auto o : output_ports) {
                int clocks = 0;
                TimingPortClass portClass = ctx->getPortTimingClass(cell.second.get(), o->name, clocks);
                // If output port is influenced by a clock (e.g. FF output) then add it to the ordering as a timing
                // start-point
                if (portClass == TMG_REGISTER_OUTPUT) {
                    topological_order.emplace_back(o->net);
                    for (int i = 0; i < clocks; i++) {
                        TimingClockingInfo clkInfo = ctx->getPortClockingInfo(cell.second.get(), o->name, i);
                        const NetInfo *clknet = cell.second->getPort(clkInfo.clock_port);
                        IdString clksig = clknet ? clknet->name : async_clock;
                        net_data[o->net][ClockEvent{clksig, clknet ? clkInfo.edge : RISING_EDGE}] =
                                TimingData{clkInfo.clockToQ.maxDelay()};
                    }

                } else {
                    if (portClass == TMG_STARTPOINT || portClass == TMG_GEN_CLOCK || portClass == TMG_IGNORE) {
                        topological_order.emplace_back(o->net);
                        TimingData td;
                        td.false_startpoint = (portClass == TMG_GEN_CLOCK || portClass == TMG_IGNORE);
                        td.max_arrival = 0;
                        net_data[o->net][ClockEvent{async_clock, RISING_EDGE}] = td;
                    }

                    // Don't analyse paths from a clock input to other pins - they will be considered by the
                    // special-case handling register input/output class ports
                    if (portClass == TMG_CLOCK_INPUT)
                        continue;

                    // Otherwise, for all driven input ports on this cell, if a timing arc exists between the input and
                    // the current output port, increment fanin counter
                    for (auto i : input_ports) {
                        DelayQuad comb_delay;
                        NetInfo *i_net = cell.second->ports[i].net;
                        if (i_net->driver.cell == nullptr && !ooc_port_nets.count(i_net->name))
                            continue;
                        bool is_path = ctx->getCellDelay(cell.second.get(), i, o->name, comb_delay);
                        if (is_path)
                            port_fanin[o]++;
                    }
                    // If there is no fanin, add the port as a false startpoint
                    if (!port_fanin.count(o) && !net_data.count(o->net)) {
                        topological_order.emplace_back(o->net);
                        TimingData td;
                        td.false_startpoint = true;
                        td.max_arrival = 0;
                        net_data[o->net][ClockEvent{async_clock, RISING_EDGE}] = td;
                    }
                }
            }
        }

        // In out-of-context mode, handle top-level ports correctly
        if (bool_or_default(ctx->settings, ctx->id("arch.ooc"))) {
            for (auto &p : ctx->ports) {
                if (p.second.type != PORT_IN || p.second.net == nullptr)
                    continue;
                topological_order.emplace_back(p.second.net);
            }
        }

        std::deque<NetInfo *> queue(topological_order.begin(), topological_order.end());
        // Now walk the design, from the start points identified previously, building up a topological order
        while (!queue.empty()) {
            const auto net = queue.front();
            queue.pop_front();

            for (auto &usr : net->users) {
                int user_clocks;
                TimingPortClass usrClass = ctx->getPortTimingClass(usr.cell, usr.port, user_clocks);
                if (usrClass == TMG_IGNORE || usrClass == TMG_CLOCK_INPUT)
                    continue;
                for (auto &port : usr.cell->ports) {
                    if (port.second.type != PORT_OUT || !port.second.net)
                        continue;
                    int port_clocks;
                    TimingPortClass portClass = ctx->getPortTimingClass(usr.cell, port.first, port_clocks);

                    // Skip if this is a clocked output (but allow non-clocked ones)
                    if (portClass == TMG_REGISTER_OUTPUT || portClass == TMG_STARTPOINT || portClass == TMG_IGNORE ||
                        portClass == TMG_GEN_CLOCK)
                        continue;
                    DelayQuad comb_delay;
                    bool is_path = ctx->getCellDelay(usr.cell, usr.port, port.first, comb_delay);
                    if (!is_path)
                        continue;
                    // Decrement the fanin count, and only add to topological order if all its fanins have already
                    // been visited
                    auto it = port_fanin.find(&port.second);
                    if (it == port_fanin.end())
                        log_error("Timing counted negative fanin count for port %s.%s (net %s), please report this "
                                  "error.\n",
                                  ctx->nameOf(usr.cell), ctx->nameOf(port.first), ctx->nameOf(port.second.net));
                    if (--it->second == 0) {
                        topological_order.emplace_back(port.second.net);
                        queue.emplace_back(port.second.net);
                        port_fanin.erase(it);
                    }
                }
            }
        }

        // Sanity check to ensure that all ports where fanins were recorded were indeed visited
        if (!port_fanin.empty() && !bool_or_default(ctx->settings, ctx->id("timing/ignoreLoops"), false)) {
            for (auto fanin : port_fanin) {
                NetInfo *net = fanin.first->net;
                if (net != nullptr) {
                    log_info("   remaining fanin includes %s (net %s)\n", fanin.first->name.c_str(ctx),
                             net->name.c_str(ctx));
                    if (net->driver.cell != nullptr)
                        log_info("        driver = %s.%s\n", net->driver.cell->name.c_str(ctx),
                                 net->driver.port.c_str(ctx));
                    for (auto net_user : net->users)
                        log_info("        user: %s.%s\n", net_user.cell->name.c_str(ctx), net_user.port.c_str(ctx));
                } else {
                    log_info("   remaining fanin includes %s (no net)\n", fanin.first->name.c_str(ctx));
                }
            }
            if (ctx->force)
                log_warning("timing analysis failed due to presence of combinatorial loops, incomplete specification "
                            "of timing ports, etc.\n");
            else
                log_error("timing analysis failed due to presence of combinatorial loops, incomplete specification of "
                          "timing ports, etc.\n");
        }

        // Go forwards topologically to find the maximum arrival time and max path length for each net
        std::vector<ClockEvent> startdomains;
        for (auto net : topological_order) {
            if (!net_data.count(net))
                continue;
            // Updates later on might invalidate a reference taken here to net_data, so iterate over a list of domains
            // instead
            startdomains.clear();
            {
                auto &nd_map = net_data.at(net);
                for (auto &startdomain : nd_map)
                    startdomains.push_back(startdomain.first);
            }
            for (auto &start_clk : startdomains) {
                auto &nd = net_data.at(net).at(start_clk);
                if (nd.false_startpoint)
                    continue;
                const auto net_arrival = nd.max_arrival;
                const auto net_length_plus_one = nd.max_path_length + 1;
                nd.min_remaining_budget = clk_period;
                for (auto &usr : net->users) {
                    int port_clocks;
                    TimingPortClass portClass = ctx->getPortTimingClass(usr.cell, usr.port, port_clocks);
                    auto net_delay = net_delays ? ctx->getNetinfoRouteDelay(net, usr) : delay_t();
                    auto usr_arrival = net_arrival + net_delay;

                    if (portClass == TMG_ENDPOINT || portClass == TMG_IGNORE || portClass == TMG_CLOCK_INPUT) {
                        // Skip
                    } else {
                        auto budget_override = ctx->getBudgetOverride(net, usr, net_delay);
                        // Iterate over all output ports on the same cell as the sink
                        for (auto port : usr.cell->ports) {
                            if (port.second.type != PORT_OUT || !port.second.net)
                                continue;
                            DelayQuad comb_delay;
                            // Look up delay through this path
                            bool is_path = ctx->getCellDelay(usr.cell, usr.port, port.first, comb_delay);
                            if (!is_path)
                                continue;
                            auto &data = net_data[port.second.net][start_clk];
                            auto &arrival = data.max_arrival;
                            arrival = std::max(arrival, usr_arrival + comb_delay.maxDelay());
                            if (!budget_override) { // Do not increment path length if budget overridden since it
                                                    // doesn't
                                // require a share of the slack
                                auto &path_length = data.max_path_length;
                                path_length = std::max(path_length, net_length_plus_one);
                            }
                        }
                    }
                }
            }
        }

        dict<ClockPair, std::pair<delay_t, NetInfo *>> crit_nets;

        // Now go backwards topologically to determine the minimum path slack, and to distribute all path slack evenly
        // between all nets on the path
        for (auto net : boost::adaptors::reverse(topological_order)) {
            if (!net_data.count(net))
                continue;
            auto &nd_map = net_data.at(net);
            for (auto &startdomain : nd_map) {
                auto &nd = startdomain.second;
                // Ignore false startpoints
                if (nd.false_startpoint)
                    continue;
                const delay_t net_length_plus_one = nd.max_path_length + 1;
                auto &net_min_remaining_budget = nd.min_remaining_budget;
                for (auto &usr : net->users) {
                    auto net_delay = net_delays ? ctx->getNetinfoRouteDelay(net, usr) : delay_t();
                    auto budget_override = ctx->getBudgetOverride(net, usr, net_delay);
                    int port_clocks;
                    TimingPortClass portClass = ctx->getPortTimingClass(usr.cell, usr.port, port_clocks);
                    if (portClass == TMG_REGISTER_INPUT || portClass == TMG_ENDPOINT) {
                        auto process_endpoint = [&](IdString clksig, ClockEdge edge, delay_t setup) {
                            const auto net_arrival = nd.max_arrival;
                            const auto endpoint_arrival = net_arrival + net_delay + setup;
                            delay_t period;
                            // Set default period
                            if (edge == startdomain.first.edge) {
                                period = clk_period;
                            } else {
                                period = clk_period / 2;
                            }
                            if (clksig != async_clock) {
                                if (ctx->nets.at(clksig)->clkconstr) {
                                    if (edge == startdomain.first.edge) {
                                        // same edge
                                        period = ctx->nets.at(clksig)->clkconstr->period.minDelay();
                                    } else if (edge == RISING_EDGE) {
                                        // falling -> rising
                                        period = ctx->nets.at(clksig)->clkconstr->low.minDelay();
                                    } else if (edge == FALLING_EDGE) {
                                        // rising -> falling
                                        period = ctx->nets.at(clksig)->clkconstr->high.minDelay();
                                    }
                                }
                            }
                            auto path_budget = period - endpoint_arrival;

                            if (update) {
                                auto budget_share = budget_override ? 0 : path_budget / net_length_plus_one;
                                usr.budget = std::min(usr.budget, net_delay + budget_share);
                                net_min_remaining_budget =
                                        std::min(net_min_remaining_budget, path_budget - budget_share);
                            }

                            if (path_budget < min_slack)
                                min_slack = path_budget;

                            if (slack_histogram) {
                                int slack_ps = ctx->getDelayNS(path_budget) * 1000;
                                (*slack_histogram)[slack_ps]++;
                            }
                            ClockEvent dest_ev{clksig, edge};
                            ClockPair clockPair{startdomain.first, dest_ev};
                            nd.arrival_time[dest_ev] = std::max(nd.arrival_time[dest_ev], endpoint_arrival);

                            // Store the detailed timing for each net and user (a.k.a. sink)
                            if (detailed_net_timings) {
                                NetSinkTiming sink_timing;
                                sink_timing.clock_pair = clockPair;
                                sink_timing.cell_port = std::make_pair(usr.cell->name, usr.port);
                                sink_timing.delay = endpoint_arrival;
                                sink_timing.budget = period;

                                (*detailed_net_timings)[net->name].push_back(sink_timing);
                            }

                            if (crit_path) {
                                if (!crit_nets.count(clockPair) || crit_nets.at(clockPair).first < endpoint_arrival) {
                                    crit_nets[clockPair] = std::make_pair(endpoint_arrival, net);
                                    (*crit_path)[clockPair].path_delay = endpoint_arrival;
                                    (*crit_path)[clockPair].path_period = period;
                                    (*crit_path)[clockPair].ports.clear();
                                    (*crit_path)[clockPair].ports.push_back(&usr);
                                }
                            }
                        };
                        if (portClass == TMG_REGISTER_INPUT) {
                            for (int i = 0; i < port_clocks; i++) {
                                TimingClockingInfo clkInfo = ctx->getPortClockingInfo(usr.cell, usr.port, i);
                                const NetInfo *clknet = usr.cell->getPort(clkInfo.clock_port);
                                IdString clksig = clknet ? clknet->name : async_clock;
                                process_endpoint(clksig, clknet ? clkInfo.edge : RISING_EDGE, clkInfo.setup.maxDelay());
                            }
                        } else {
                            process_endpoint(async_clock, RISING_EDGE, 0);
                        }

                    } else if (update) {

                        // Iterate over all output ports on the same cell as the sink
                        for (const auto &port : usr.cell->ports) {
                            if (port.second.type != PORT_OUT || !port.second.net)
                                continue;
                            DelayQuad comb_delay;
                            bool is_path = ctx->getCellDelay(usr.cell, usr.port, port.first, comb_delay);
                            if (!is_path)
                                continue;
                            if (net_data.count(port.second.net) &&
                                net_data.at(port.second.net).count(startdomain.first)) {
                                auto path_budget =
                                        net_data.at(port.second.net).at(startdomain.first).min_remaining_budget;
                                auto budget_share = budget_override ? 0 : path_budget / net_length_plus_one;
                                usr.budget = std::min(usr.budget, net_delay + budget_share);
                                net_min_remaining_budget =
                                        std::min(net_min_remaining_budget, path_budget - budget_share);
                            }
                        }
                    }
                }
            }
        }

        if (crit_path) {
            // Walk backwards from the most critical net
            for (auto crit_pair : crit_nets) {
                NetInfo *crit_net = crit_pair.second.second;
                auto &cp_ports = (*crit_path)[crit_pair.first].ports;
                while (crit_net) {
                    const PortInfo *crit_ipin = nullptr;
                    delay_t max_arrival = std::numeric_limits<delay_t>::min();
                    // Look at all input ports on its driving cell
                    for (const auto &port : crit_net->driver.cell->ports) {
                        if (port.second.type != PORT_IN || !port.second.net)
                            continue;
                        DelayQuad comb_delay;
                        bool is_path =
                                ctx->getCellDelay(crit_net->driver.cell, port.first, crit_net->driver.port, comb_delay);
                        if (!is_path)
                            continue;
                        // If input port is influenced by a clock, skip
                        int port_clocks;
                        TimingPortClass portClass =
                                ctx->getPortTimingClass(crit_net->driver.cell, port.first, port_clocks);
                        if (portClass == TMG_CLOCK_INPUT || portClass == TMG_ENDPOINT || portClass == TMG_IGNORE)
                            continue;
                        // And find the fanin net with the latest arrival time
                        if (net_data.count(port.second.net) &&
                            net_data.at(port.second.net).count(crit_pair.first.start)) {
                            auto net_arrival = net_data.at(port.second.net).at(crit_pair.first.start).max_arrival;
                            if (net_delays) {
                                for (auto &user : port.second.net->users)
                                    if (user.port == port.first && user.cell == crit_net->driver.cell) {
                                        net_arrival += ctx->getNetinfoRouteDelay(port.second.net, user);
                                        break;
                                    }
                            }
                            net_arrival += comb_delay.maxDelay();
                            if (net_arrival > max_arrival) {
                                max_arrival = net_arrival;
                                crit_ipin = &port.second;
                            }
                        }
                    }

                    if (!crit_ipin)
                        break;
                    // Now convert PortInfo* into a PortRef*
                    for (auto &usr : crit_ipin->net->users) {
                        if (usr.cell->name == crit_net->driver.cell->name && usr.port == crit_ipin->name) {
                            cp_ports.push_back(&usr);
                            break;
                        }
                    }
                    crit_net = crit_ipin->net;
                }
                std::reverse(cp_ports.begin(), cp_ports.end());
            }
        }
        return min_slack;
    }

    void assign_budget()
    {
        // Clear delays to a very high value first
        for (auto &net : ctx->nets) {
            for (auto &usr : net.second->users) {
                usr.budget = std::numeric_limits<delay_t>::max();
            }
        }

        walk_paths();
    }
};

void assign_budget(Context *ctx, bool quiet)
{
    if (!quiet) {
        log_break();
        log_info("Annotating ports with timing budgets for target frequency %.2f MHz\n",
                 ctx->setting<float>("target_freq") / 1e6);
    }

    Timing timing(ctx, ctx->setting<int>("slack_redist_iter") > 0 /* net_delays */, true /* update */);
    timing.assign_budget();

    if (!quiet || ctx->verbose) {
        for (auto &net : ctx->nets) {
            for (auto &user : net.second->users) {
                // Post-update check
                if (!ctx->setting<bool>("auto_freq") && user.budget < 0)
                    log_info("port %s.%s, connected to net '%s', has negative "
                             "timing budget of %fns\n",
                             user.cell->name.c_str(ctx), user.port.c_str(ctx), net.first.c_str(ctx),
                             ctx->getDelayNS(user.budget));
                else if (ctx->debug)
                    log_info("port %s.%s, connected to net '%s', has "
                             "timing budget of %fns\n",
                             user.cell->name.c_str(ctx), user.port.c_str(ctx), net.first.c_str(ctx),
                             ctx->getDelayNS(user.budget));
            }
        }
    }

    // For slack redistribution, if user has not specified a frequency dynamically adjust the target frequency to be the
    // currently achieved maximum
    if (ctx->setting<bool>("auto_freq") && ctx->setting<int>("slack_redist_iter") > 0) {
        delay_t default_slack = delay_t((1.0e9 / ctx->getDelayNS(1)) / ctx->setting<float>("target_freq"));
        ctx->settings[ctx->id("target_freq")] =
                std::to_string(1.0e9 / ctx->getDelayNS(default_slack - timing.min_slack));
        if (ctx->verbose)
            log_info("minimum slack for this assign = %.2f ns, target Fmax for next "
                     "update = %.2f MHz\n",
                     ctx->getDelayNS(timing.min_slack), ctx->setting<float>("target_freq") / 1e6);
    }

    if (!quiet)
        log_info("Checksum: 0x%08x\n", ctx->checksum());
}

CriticalPath build_critical_path_report(Context *ctx, ClockPair &clocks, const PortRefVector &crit_path)
{

    CriticalPath report;
    report.clock_pair = clocks;

    auto &front = crit_path.front();
    auto &front_port = front->cell->ports.at(front->port);
    auto &front_driver = front_port.net->driver;

    int port_clocks;
    auto portClass = ctx->getPortTimingClass(front_driver.cell, front_driver.port, port_clocks);

    const CellInfo *last_cell = front->cell;
    IdString last_port = front_driver.port;

    int clock_start = -1;
    if (portClass == TMG_REGISTER_OUTPUT) {
        for (int i = 0; i < port_clocks; i++) {
            TimingClockingInfo clockInfo = ctx->getPortClockingInfo(front_driver.cell, front_driver.port, i);
            const NetInfo *clknet = front_driver.cell->getPort(clockInfo.clock_port);
            if (clknet != nullptr && clknet->name == clocks.start.clock && clockInfo.edge == clocks.start.edge) {
                last_port = clockInfo.clock_port;
                clock_start = i;
                break;
            }
        }
    }

    for (auto sink : crit_path) {
        auto sink_cell = sink->cell;
        auto &port = sink_cell->ports.at(sink->port);
        auto net = port.net;
        auto &driver = net->driver;
        auto driver_cell = driver.cell;

        CriticalPath::Segment seg_logic;

        DelayQuad comb_delay;
        if (clock_start != -1) {
            auto clockInfo = ctx->getPortClockingInfo(driver_cell, driver.port, clock_start);
            comb_delay = clockInfo.clockToQ;
            clock_start = -1;
            seg_logic.type = CriticalPath::Segment::Type::CLK_TO_Q;
        } else if (last_port == driver.port) {
            // Case where we start with a STARTPOINT etc
            comb_delay = DelayQuad(0);
            seg_logic.type = CriticalPath::Segment::Type::SOURCE;
        } else {
            ctx->getCellDelay(driver_cell, last_port, driver.port, comb_delay);
            seg_logic.type = CriticalPath::Segment::Type::LOGIC;
        }

        seg_logic.delay = comb_delay.maxDelay();
        seg_logic.budget = 0;
        seg_logic.from = std::make_pair(last_cell->name, last_port);
        seg_logic.to = std::make_pair(driver_cell->name, driver.port);
        seg_logic.net = IdString();
        report.segments.push_back(seg_logic);

        auto net_delay = ctx->getNetinfoRouteDelay(net, *sink);

        CriticalPath::Segment seg_route;
        seg_route.type = CriticalPath::Segment::Type::ROUTING;
        seg_route.delay = net_delay;
        seg_route.budget = sink->budget;
        seg_route.from = std::make_pair(driver_cell->name, driver.port);
        seg_route.to = std::make_pair(sink_cell->name, sink->port);
        seg_route.net = net->name;
        report.segments.push_back(seg_route);

        last_cell = sink_cell;
        last_port = sink->port;
    }

    int clockCount = 0;
    auto sinkClass = ctx->getPortTimingClass(crit_path.back()->cell, crit_path.back()->port, clockCount);
    if (sinkClass == TMG_REGISTER_INPUT && clockCount > 0) {
        auto sinkClockInfo = ctx->getPortClockingInfo(crit_path.back()->cell, crit_path.back()->port, 0);
        delay_t setup = sinkClockInfo.setup.maxDelay();

        CriticalPath::Segment seg_logic;
        seg_logic.type = CriticalPath::Segment::Type::SETUP;
        seg_logic.delay = setup;
        seg_logic.budget = 0;
        seg_logic.from = std::make_pair(last_cell->name, last_port);
        seg_logic.to = seg_logic.from;
        seg_logic.net = IdString();
        report.segments.push_back(seg_logic);
    }

    return report;
}

void timing_analysis(Context *ctx, bool print_histogram, bool print_fmax, bool print_path, bool warn_on_failure,
                     bool update_results)
{
    auto format_event = [ctx](const ClockEvent &e, int field_width = 0) {
        std::string value;
        if (e.clock == ctx->id("$async$"))
            value = std::string("<async>");
        else
            value = (e.edge == FALLING_EDGE ? std::string("negedge ") : std::string("posedge ")) + e.clock.str(ctx);
        if (int(value.length()) < field_width)
            value.insert(value.length(), field_width - int(value.length()), ' ');
        return value;
    };

    CriticalPathDataMap crit_paths;
    DelayFrequency slack_histogram;
    DetailedNetTimings detailed_net_timings;

    Timing timing(ctx, true /* net_delays */, false /* update */, (print_path || print_fmax) ? &crit_paths : nullptr,
                  print_histogram ? &slack_histogram : nullptr,
                  (update_results && ctx->detailed_timing_report) ? &detailed_net_timings : nullptr);
    timing.walk_paths();

    // Use TimingAnalyser to determine clock-to-clock relations
    TimingAnalyser timingAnalyser(ctx);
    timingAnalyser.setup();

    bool report_critical_paths = print_path || print_fmax || update_results;

    dict<IdString, CriticalPath> clock_reports;
    std::vector<CriticalPath> xclock_reports;
    dict<IdString, ClockFmax> clock_fmax;
    std::set<IdString> empty_clocks; // set of clocks with no interior paths

    if (report_critical_paths) {

        for (auto path : crit_paths) {
            const ClockEvent &a = path.first.start;
            const ClockEvent &b = path.first.end;
            empty_clocks.insert(a.clock);
            empty_clocks.insert(b.clock);
        }
        for (auto path : crit_paths) {
            const ClockEvent &a = path.first.start;
            const ClockEvent &b = path.first.end;
            if (a.clock != b.clock || a.clock == ctx->id("$async$"))
                continue;
            double Fmax;
            empty_clocks.erase(a.clock);
            if (a.edge == b.edge)
                Fmax = 1000 / ctx->getDelayNS(path.second.path_delay);
            else
                Fmax = 500 / ctx->getDelayNS(path.second.path_delay);
            if (!clock_fmax.count(a.clock) || Fmax < clock_fmax.at(a.clock).achieved) {
                clock_fmax[a.clock].achieved = Fmax;
                clock_fmax[a.clock].constraint = 0.0f; // Will be filled later
                clock_reports[a.clock] = build_critical_path_report(ctx, path.first, path.second.ports);
                clock_reports[a.clock].period = path.second.path_period;
            }
        }

        for (auto &path : crit_paths) {
            const ClockEvent &a = path.first.start;
            const ClockEvent &b = path.first.end;
            if (a.clock == b.clock && a.clock != ctx->id("$async$"))
                continue;

            auto &crit_path = crit_paths.at(path.first).ports;
            xclock_reports.push_back(build_critical_path_report(ctx, path.first, crit_path));
            xclock_reports.back().period = path.second.path_period;
        }

        if (clock_reports.empty() && xclock_reports.empty()) {
            log_info("No Fmax available; no interior timing paths found in design.\n");
        }

        std::sort(xclock_reports.begin(), xclock_reports.end(), [ctx](const CriticalPath &ra, const CriticalPath &rb) {
            const auto &a = ra.clock_pair;
            const auto &b = rb.clock_pair;

            if (a.start.clock.str(ctx) < b.start.clock.str(ctx))
                return true;
            if (a.start.clock.str(ctx) > b.start.clock.str(ctx))
                return false;
            if (a.start.edge < b.start.edge)
                return true;
            if (a.start.edge > b.start.edge)
                return false;
            if (a.end.clock.str(ctx) < b.end.clock.str(ctx))
                return true;
            if (a.end.clock.str(ctx) > b.end.clock.str(ctx))
                return false;
            if (a.end.edge < b.end.edge)
                return true;
            return false;
        });

        for (auto &clock : clock_reports) {
            float target = ctx->setting<float>("target_freq") / 1e6;
            if (ctx->nets.at(clock.first)->clkconstr)
                target = 1000 / ctx->getDelayNS(ctx->nets.at(clock.first)->clkconstr->period.minDelay());
            clock_fmax[clock.first].constraint = target;
        }
    }

    // Print critical paths
    if (print_path) {

        static auto print_net_source = [ctx](const NetInfo *net) {
            // Check if this net is annotated with a source list
            auto sources = net->attrs.find(ctx->id("src"));
            if (sources == net->attrs.end()) {
                // No sources for this net, can't print anything
                return;
            }

            // Sources are separated by pipe characters.
            // There is no guaranteed ordering on sources, so we just print all
            auto sourcelist = sources->second.as_string();
            std::vector<std::string> source_entries;
            size_t current = 0, prev = 0;
            while ((current = sourcelist.find("|", prev)) != std::string::npos) {
                source_entries.emplace_back(sourcelist.substr(prev, current - prev));
                prev = current + 1;
            }
            // Ensure we emplace the final entry
            source_entries.emplace_back(sourcelist.substr(prev, current - prev));

            // Iterate and print our source list at the correct indentation level
            log_info("               Defined in:\n");
            for (auto entry : source_entries) {
                log_info("                 %s\n", entry.c_str());
            }
        };

        // A helper function for reporting one critical path
        auto print_path_report = [ctx](const CriticalPath &path) {
            delay_t total = 0, logic_total = 0, route_total = 0;

            log_info("curr total\n");
            for (const auto &segment : path.segments) {

                total += segment.delay;

                if (segment.type == CriticalPath::Segment::Type::CLK_TO_Q ||
                    segment.type == CriticalPath::Segment::Type::SOURCE ||
                    segment.type == CriticalPath::Segment::Type::LOGIC ||
                    segment.type == CriticalPath::Segment::Type::SETUP) {
                    logic_total += segment.delay;

                    const std::string type_name =
                            (segment.type == CriticalPath::Segment::Type::SETUP) ? "Setup" : "Source";

                    log_info("%4.1f %4.1f  %s %s.%s\n", ctx->getDelayNS(segment.delay), ctx->getDelayNS(total),
                             type_name.c_str(), segment.to.first.c_str(ctx), segment.to.second.c_str(ctx));
                } else if (segment.type == CriticalPath::Segment::Type::ROUTING) {
                    route_total += segment.delay;

                    const auto &driver = ctx->cells.at(segment.from.first);
                    const auto &sink = ctx->cells.at(segment.to.first);

                    auto driver_loc = ctx->getBelLocation(driver->bel);
                    auto sink_loc = ctx->getBelLocation(sink->bel);

                    log_info("%4.1f %4.1f    Net %s budget %f ns (%d,%d) -> (%d,%d)\n", ctx->getDelayNS(segment.delay),
                             ctx->getDelayNS(total), segment.net.c_str(ctx), ctx->getDelayNS(segment.budget),
                             driver_loc.x, driver_loc.y, sink_loc.x, sink_loc.y);
                    log_info("               Sink %s.%s\n", segment.to.first.c_str(ctx), segment.to.second.c_str(ctx));

                    const NetInfo *net = ctx->nets.at(segment.net).get();

                    if (ctx->verbose) {

                        PortRef sink_ref;
                        sink_ref.cell = sink.get();
                        sink_ref.port = segment.to.second;
                        sink_ref.budget = segment.budget;

                        auto driver_wire = ctx->getNetinfoSourceWire(net);
                        auto sink_wire = ctx->getNetinfoSinkWire(net, sink_ref, 0);
                        log_info("                 prediction: %f ns estimate: %f ns\n",
                                 ctx->getDelayNS(ctx->predictArcDelay(net, sink_ref)),
                                 ctx->getDelayNS(ctx->estimateDelay(driver_wire, sink_wire)));
                        auto cursor = sink_wire;
                        delay_t delay;
                        while (driver_wire != cursor) {
#ifdef ARCH_ECP5
                            if (net->is_global)
                                break;
#endif
                            auto it = net->wires.find(cursor);
                            assert(it != net->wires.end());
                            auto pip = it->second.pip;
                            NPNR_ASSERT(pip != PipId());
                            delay = ctx->getPipDelay(pip).maxDelay();
                            log_info("                 %1.3f %s\n", ctx->getDelayNS(delay), ctx->nameOfPip(pip));
                            cursor = ctx->getPipSrcWire(pip);
                        }
                    }

                    if (!ctx->disable_critical_path_source_print) {
                        print_net_source(net);
                    }
                }
            }
            log_info("%.1f ns logic, %.1f ns routing\n", ctx->getDelayNS(logic_total), ctx->getDelayNS(route_total));
        };

        // Single domain paths
        for (auto &clock : clock_reports) {
            log_break();
            std::string start = clock.second.clock_pair.start.edge == FALLING_EDGE ? std::string("negedge")
                                                                                   : std::string("posedge");
            std::string end =
                    clock.second.clock_pair.end.edge == FALLING_EDGE ? std::string("negedge") : std::string("posedge");
            log_info("Critical path report for clock '%s' (%s -> %s):\n", clock.first.c_str(ctx), start.c_str(),
                     end.c_str());
            auto &report = clock.second;
            print_path_report(report);
        }

        // Cross-domain paths
        for (auto &report : xclock_reports) {
            log_break();
            std::string start = format_event(report.clock_pair.start);
            std::string end = format_event(report.clock_pair.end);
            log_info("Critical path report for cross-domain path '%s' -> '%s':\n", start.c_str(), end.c_str());
            print_path_report(report);
        }
    }

    if (print_fmax) {
        log_break();

        unsigned max_width = 0;
        for (auto &clock : clock_reports)
            max_width = std::max<unsigned>(max_width, clock.first.str(ctx).size());

        for (auto &clock : clock_reports) {
            const auto &clock_name = clock.first.str(ctx);
            const int width = max_width - clock_name.size();

            float fmax = clock_fmax[clock.first].achieved;
            float target = clock_fmax[clock.first].constraint;
            bool passed = target < fmax;

            if (!warn_on_failure || passed)
                log_info("Max frequency for clock %*s'%s': %.02f MHz (%s at %.02f MHz)\n", width, "",
                         clock_name.c_str(), fmax, passed ? "PASS" : "FAIL", target);
            else if (bool_or_default(ctx->settings, ctx->id("timing/allowFail"), false))
                log_warning("Max frequency for clock %*s'%s': %.02f MHz (%s at %.02f MHz)\n", width, "",
                            clock_name.c_str(), fmax, passed ? "PASS" : "FAIL", target);
            else
                log_nonfatal_error("Max frequency for clock %*s'%s': %.02f MHz (%s at %.02f MHz)\n", width, "",
                                   clock_name.c_str(), fmax, passed ? "PASS" : "FAIL", target);
        }
        log_break();

        // All clock to clock delays
        const auto &clock_delays = timingAnalyser.get_clock_delays();

        // Clock to clock delays for xpaths
        dict<ClockPair, delay_t> xclock_delays;
        for (auto &report : xclock_reports) {
            const auto &clock1_name = report.clock_pair.start.clock;
            const auto &clock2_name = report.clock_pair.end.clock;

            const auto key = std::make_pair(clock1_name, clock2_name);
            if (clock_delays.count(key)) {
                xclock_delays[report.clock_pair] = clock_delays.at(key);
            }
        }

        unsigned max_width_xca = 0;
        unsigned max_width_xcb = 0;
        for (auto &report : xclock_reports) {
            max_width_xca = std::max((unsigned)format_event(report.clock_pair.start).length(), max_width_xca);
            max_width_xcb = std::max((unsigned)format_event(report.clock_pair.end).length(), max_width_xcb);
        }

        // Check and report xpath delays for related clocks
        if (!xclock_reports.empty()) {
            for (auto &report : xclock_reports) {
                const auto &clock_a = report.clock_pair.start.clock;
                const auto &clock_b = report.clock_pair.end.clock;

                const auto key = std::make_pair(clock_a, clock_b);
                if (!clock_delays.count(key)) {
                    continue;
                }

                delay_t path_delay = 0;
                for (const auto &segment : report.segments) {
                    path_delay += segment.delay;
                }

                // Compensate path delay for clock-to-clock delay. If the
                // result is negative then only the latter matters. Otherwise
                // the compensated path delay is taken.
                auto clock_delay = clock_delays.at(key);
                path_delay -= clock_delay;

                float fmax = std::numeric_limits<float>::infinity();
                if (path_delay < 0) {
                    fmax = 1e3f / ctx->getDelayNS(clock_delay);
                } else if (path_delay > 0) {
                    fmax = 1e3f / ctx->getDelayNS(path_delay);
                }

                // Both clocks are related so they should have the same
                // frequency. However, they may get different constraints from
                // user input. In case of only one constraint preset take it,
                // otherwise get the worst case (min.)
                float target;
                if (clock_fmax.count(clock_a) && !clock_fmax.count(clock_b)) {
                    target = clock_fmax.at(clock_a).constraint;
                } else if (!clock_fmax.count(clock_a) && clock_fmax.count(clock_b)) {
                    target = clock_fmax.at(clock_b).constraint;
                } else {
                    target = std::min(clock_fmax.at(clock_a).constraint, clock_fmax.at(clock_b).constraint);
                }

                bool passed = target < fmax;

                auto ev_a = format_event(report.clock_pair.start, max_width_xca);
                auto ev_b = format_event(report.clock_pair.end, max_width_xcb);

                if (!warn_on_failure || passed)
                    log_info("Max frequency for %s -> %s: %.02f MHz (%s at %.02f MHz)\n", ev_a.c_str(), ev_b.c_str(),
                             fmax, passed ? "PASS" : "FAIL", target);
                else if (bool_or_default(ctx->settings, ctx->id("timing/allowFail"), false) ||
                         bool_or_default(ctx->settings, ctx->id("timing/ignoreRelClk"), false))
                    log_warning("Max frequency for  %s -> %s: %.02f MHz (%s at %.02f MHz)\n", ev_a.c_str(),
                                ev_b.c_str(), fmax, passed ? "PASS" : "FAIL", target);
                else
                    log_nonfatal_error("Max frequency for %s -> %s: %.02f MHz (%s at %.02f MHz)\n", ev_a.c_str(),
                                       ev_b.c_str(), fmax, passed ? "PASS" : "FAIL", target);
            }
            log_break();
        }

        // Report clock delays for xpaths
        if (!clock_delays.empty()) {
            for (auto &pair : xclock_delays) {
                auto ev_a = format_event(pair.first.start, max_width_xca);
                auto ev_b = format_event(pair.first.end, max_width_xcb);

                delay_t delay = pair.second;
                if (pair.first.start.edge != pair.first.end.edge) {
                    delay /= 2;
                }

                log_info("Clock to clock delay %s -> %s: %0.02f ns\n", ev_a.c_str(), ev_b.c_str(),
                         ctx->getDelayNS(delay));
            }

            log_break();
        }

        for (auto &eclock : empty_clocks) {
            if (eclock != ctx->id("$async$"))
                log_info("Clock '%s' has no interior paths\n", eclock.c_str(ctx));
        }
        log_break();

        int start_field_width = 0, end_field_width = 0;
        for (auto &report : xclock_reports) {
            start_field_width = std::max((int)format_event(report.clock_pair.start).length(), start_field_width);
            end_field_width = std::max((int)format_event(report.clock_pair.end).length(), end_field_width);
        }

        for (auto &report : xclock_reports) {
            const ClockEvent &a = report.clock_pair.start;
            const ClockEvent &b = report.clock_pair.end;
            delay_t path_delay = 0;
            for (const auto &segment : report.segments) {
                path_delay += segment.delay;
            }
            auto ev_a = format_event(a, start_field_width), ev_b = format_event(b, end_field_width);
            log_info("Max delay %s -> %s: %0.02f ns\n", ev_a.c_str(), ev_b.c_str(), ctx->getDelayNS(path_delay));
        }
        log_break();
    }

    if (print_histogram && slack_histogram.size() > 0) {
        unsigned num_bins = 20;
        unsigned bar_width = 60;
        auto min_slack = slack_histogram.begin()->first;
        auto max_slack = slack_histogram.rbegin()->first;
        auto bin_size = std::max<unsigned>(1, ceil((max_slack - min_slack + 1) / float(num_bins)));
        std::vector<unsigned> bins(num_bins);
        unsigned max_freq = 0;
        for (const auto &i : slack_histogram) {
            int bin_idx = int((i.first - min_slack) / bin_size);
            if (bin_idx < 0)
                bin_idx = 0;
            else if (bin_idx >= int(num_bins))
                bin_idx = num_bins - 1;
            auto &bin = bins.at(bin_idx);
            bin += i.second;
            max_freq = std::max(max_freq, bin);
        }
        bar_width = std::min(bar_width, max_freq);

        log_break();
        log_info("Slack histogram:\n");
        log_info(" legend: * represents %d endpoint(s)\n", max_freq / bar_width);
        log_info("         + represents [1,%d) endpoint(s)\n", max_freq / bar_width);
        for (unsigned i = 0; i < num_bins; ++i)
            log_info("[%6d, %6d) |%s%c\n", min_slack + bin_size * i, min_slack + bin_size * (i + 1),
                     std::string(bins[i] * bar_width / max_freq, '*').c_str(),
                     (bins[i] * bar_width) % max_freq > 0 ? '+' : ' ');
    }

    // Update timing results in the context
    if (update_results) {
        auto &results = ctx->timing_result;

        results.clock_fmax = std::move(clock_fmax);
        results.clock_paths = std::move(clock_reports);
        results.xclock_paths = std::move(xclock_reports);

        results.detailed_net_timings = std::move(detailed_net_timings);
    }
}

NEXTPNR_NAMESPACE_END

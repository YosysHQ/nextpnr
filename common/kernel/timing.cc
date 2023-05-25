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
std::string clock_event_name(const Context *ctx, ClockDomainKey &dom)
{
    std::string value;
    if (dom.is_async())
        value = "<async>";
    else
        value = (dom.edge == FALLING_EDGE ? "negedge " : "posedge ") + dom.clock.str(ctx);

    return value;
}
} // namespace

TimingAnalyser::TimingAnalyser(Context *ctx) : ctx(ctx)
{
    ClockDomainKey key{IdString(), ClockEdge::RISING_EDGE};
    domain_to_id.emplace(key, 0);
    domains.emplace_back(key);
    async_clock_id = 0;
};

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
    auto async_clk_key = domains.at(async_clock_id);

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
        if (cls == TMG_CLOCK_INPUT || cls == TMG_GEN_CLOCK || cls == TMG_IGNORE)
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
            // asynchronous endpoint
            else if (cls == TMG_ENDPOINT) {
                pd.cell_arcs.emplace_back(CellArc::ENDPOINT, async_clk_key.key.clock, DelayQuad{});
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
            // Asynchronous startpoint
            else if (cls == TMG_STARTPOINT) {
                pd.cell_arcs.emplace_back(CellArc::STARTPOINT, async_clk_key.key.clock, DelayQuad{});
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
    bool first_iter = true;
    do {
        // Go forward through the topological order (domains from the PoV of arrival time)
        updated_domains = false;
        for (auto port : topological_order) {
            auto &pd = ports.at(port);
            auto &pi = port_info(port);
            if (pi.type == PORT_OUT) {
                if (first_iter) {
                    for (auto &fanin : pd.cell_arcs) {
                        domain_id_t dom;
                        // registered outputs are startpoints
                        if (fanin.type == CellArc::CLK_TO_Q)
                            dom = domain_id(port.cell, fanin.other_port, fanin.edge);
                        else if (fanin.type == CellArc::STARTPOINT)
                            dom = async_clock_id;
                        else
                            continue;
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
                        domain_id_t dom;
                        // registered inputs are endpoints
                        if (fanout.type == CellArc::SETUP)
                            dom = domain_id(port.cell, fanout.other_port, fanout.edge);
                        else if (fanout.type == CellArc::ENDPOINT)
                            dom = async_clock_id;
                        else
                            continue;
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
        // Iterate over ports and find domain pairs
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
    std::function<void(const NetInfo *, pool<IdString> &, dict<IdString, delay_t> &, delay_t)> find_net_drivers =
            [&](const NetInfo *ni, pool<IdString> &net_trace, dict<IdString, delay_t> &drivers, delay_t delay_acc) {
                // Get driving cell and port
                if (ni == nullptr)
                    return;
                const CellInfo *cell = ni->driver.cell;
                if (cell == nullptr)
                    return;

                const IdString port = ni->driver.port;

                bool didGoUpstream = false;

                // Ring oscillator driving the net
                if (net_trace.find(ni->name) != net_trace.end()) {
                    drivers[ni->name] = delay_acc;
                    return;
                }
                net_trace.insert(ni->name);

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
                    find_net_drivers(pi.net, net_trace, drivers, delay_acc + delay.maxDelay());
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
        if (domain.key.is_async())
            continue;

        const NetInfo *ni = ctx->nets.at(domain.key.clock).get();
        if (ni == nullptr)
            continue;
        if (ni->driver.cell == nullptr)
            continue;

        dict<IdString, delay_t> drivers;
        pool<IdString> net_trace;
        find_net_drivers(ni, net_trace, drivers, 0);

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

dict<domain_id_t, delay_t> TimingAnalyser::max_delay_by_domain()
{
    dict<domain_id_t, delay_t> domain_delay;
    for (auto p : topological_order) {
        auto &pd = ports.at(p);
        for (auto &req : pd.required) {
            if (pd.arrival.count(req.first)) {
                if (domains.at(req.first).key.is_async())
                    continue;
                auto &arr = pd.arrival.at(req.first);
                delay_t delay = arr.value.maxDelay() - req.second.value.minDelay();
                if (!domain_delay.count(req.first) || domain_delay.at(req.first) < delay)
                    domain_delay[req.first] = delay;
            }
        }
    }

    return domain_delay;
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
            // Do not set criticality for asynchronous paths
            if (domains.at(dp.key.launch).key.is_async() || domains.at(dp.key.capture).key.is_async())
                continue;

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

std::string tgp_to_string2(TimingPortClass c) {
    switch (c) {
        case TMG_CLOCK_INPUT:
            return "TMG_CLOCK_INPUT";
        case TMG_GEN_CLOCK:
            return "TMG_GEN_CLOCK";
        case TMG_REGISTER_INPUT:
            return "TMG_REGISTER_INPUT";
        case TMG_REGISTER_OUTPUT:
            return "TMG_REGISTER_OUTPUT";
        case TMG_COMB_INPUT:
            return "TMG_COMB_INPUT";
        case TMG_COMB_OUTPUT:
            return "TMG_COMB_OUTPUT";
        case TMG_STARTPOINT:
            return "TMG_STARTPOINT";
        case TMG_ENDPOINT:
            return "TMG_ENDPOINT";
        case TMG_IGNORE:
            return "TMG_IGNORE";
    }

    return "UNKNOWN";
}

CriticalPath TimingAnalyser::build_critical_path_report(domain_id_t domain_pair, CellPortKey endpoint)
{
    CriticalPath report;

    auto &dp = domain_pairs.at(domain_pair);
    auto &launch = domains.at(dp.key.launch).key;
    auto &capture = domains.at(dp.key.capture).key;

    report.clock_pair = ClockPair {
        .start = ClockEvent {
            .clock = launch.clock,
            .edge = launch.edge
        },
        .end = ClockEvent {
            .clock = capture.clock,
            .edge = capture.edge
        }
    };

    report.period = ctx->getDelayFromNS(1.0e9 / ctx->setting<float>("target_freq"));
    if (launch.edge != capture.edge) {
        report.period = report.period / 2;
    }

    if (!launch.is_async() && ctx->nets.at(launch.clock)->clkconstr) {
        if (launch.edge == capture.edge) {
            report.period = ctx->nets.at(launch.clock)->clkconstr->period.minDelay();
        } else if (capture.edge == RISING_EDGE) {
            report.period = ctx->nets.at(launch.clock)->clkconstr->low.minDelay();
        } else if (capture.edge == FALLING_EDGE) {
            report.period = ctx->nets.at(launch.clock)->clkconstr->high.minDelay();
        }
    }

    std::vector<PortRef> crit_path_rev;
    auto cursor = endpoint;
    while (cursor != CellPortKey()) {
        auto cell = cell_info(cursor);
        auto& port = port_info(cursor);

        int port_clocks;
        auto portClass = ctx->getPortTimingClass(cell, port.name, port_clocks);

        if (portClass != TMG_CLOCK_INPUT &&
            portClass != TMG_ENDPOINT &&
            portClass != TMG_IGNORE &&
            port.type == PortType::PORT_IN)
        {
            crit_path_rev.emplace_back(PortRef { cell, port.name });        
        }
        if (!ports.at(cursor).arrival.count(dp.key.launch))
            break;

        cursor = ports.at(cursor).arrival.at(dp.key.launch).bwd_max;
    }

    auto crit_path = boost::adaptors::reverse(crit_path_rev);

    auto &front = crit_path.front();
    auto &front_port = front.cell->ports.at(front.port);
    auto &front_driver = front_port.net->driver;

    int port_clocks;
    auto portClass = ctx->getPortTimingClass(front_driver.cell, front_driver.port, port_clocks);

    const CellInfo *last_cell = front.cell;
    IdString last_port = front_driver.port;

    int clock_start = -1;
    if (portClass == TMG_REGISTER_OUTPUT) {
        for (int i = 0; i < port_clocks; i++) {
            TimingClockingInfo clockInfo = ctx->getPortClockingInfo(front_driver.cell, front_driver.port, i);
            const NetInfo *clknet = front_driver.cell->getPort(clockInfo.clock_port);
            if (clknet != nullptr && clknet->name == launch.clock && clockInfo.edge == launch.edge) {
                last_port = clockInfo.clock_port;
                clock_start = i;
                break;
            }
        }
    }

    log_info("building critical path report for clocks: %s -> %s\n", launch.clock.c_str(ctx), capture.clock.c_str(ctx));

    for (auto sink : crit_path) {
        auto sink_cell = sink.cell;
        auto &port = sink_cell->ports.at(sink.port);
        auto net = port.net;
        auto &driver = net->driver;
        auto driver_cell = driver.cell;

        auto clockInfo0 = ctx->getPortTimingClass(driver_cell, driver.port, clock_start);

        log_info("\t%s.%s, tmgPortClass: %s\n", sink_cell->name.c_str(ctx), port.name.c_str(ctx), tgp_to_string2(clockInfo0).c_str());

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
        seg_logic.from = std::make_pair(last_cell->name, last_port);
        seg_logic.to = std::make_pair(driver_cell->name, driver.port);
        seg_logic.net = IdString();
        report.segments.push_back(seg_logic);

        auto net_delay = ctx->getNetinfoRouteDelay(net, sink);

        CriticalPath::Segment seg_route;
        seg_route.type = CriticalPath::Segment::Type::ROUTING;
        seg_route.delay = net_delay;
        seg_route.from = std::make_pair(driver_cell->name, driver.port);
        seg_route.to = std::make_pair(sink_cell->name, sink.port);
        seg_route.net = net->name;
        report.segments.push_back(seg_route);

        last_cell = sink_cell;
        last_port = sink.port;
    }

    int clockCount = 0;
    auto sinkClass = ctx->getPortTimingClass(crit_path.back().cell, crit_path.back().port, clockCount);
    if (sinkClass == TMG_REGISTER_INPUT && clockCount > 0) {
        auto sinkClockInfo = ctx->getPortClockingInfo(crit_path.back().cell, crit_path.back().port, 0);
        delay_t setup = sinkClockInfo.setup.maxDelay();

        CriticalPath::Segment seg_logic;
        seg_logic.type = CriticalPath::Segment::Type::SETUP;
        seg_logic.delay = setup;
        seg_logic.from = std::make_pair(last_cell->name, last_port);
        seg_logic.to = seg_logic.from;
        seg_logic.net = IdString();
        report.segments.push_back(seg_logic);
    }

    return report;
}

void TimingAnalyser::print_report()
{
    dict<IdString, CriticalPath> clock_reports;
    std::vector<CriticalPath> xclock_reports;
    dict<IdString, ClockFmax> clock_fmax;
    std::set<IdString> empty_clocks;

    auto delay_by_domain = max_delay_by_domain();

    for (int i = 0; i < int(domain_pairs.size()); i++) {
        auto &dp = domain_pairs.at(i);
        auto &launch = domains.at(dp.key.launch).key;
        auto &capture = domains.at(dp.key.capture).key;
        
        empty_clocks.insert(launch.clock);
        empty_clocks.insert(capture.clock);
    }

    for (int i = 0; i < int(domain_pairs.size()); i++) {
        auto &dp = domain_pairs.at(i);
        auto &launch = domains.at(dp.key.launch).key;
        auto &capture = domains.at(dp.key.capture).key;

        if (launch.clock != capture.clock || launch.clock == ctx->id("$async$"))
            continue;

        auto path_delay = delay_by_domain.at(dp.key.launch);

        double Fmax;
        empty_clocks.erase(launch.clock);

        if (launch.edge == capture.edge)
            Fmax = 1000 / ctx->getDelayNS(path_delay);
        else
            Fmax = 500 / ctx->getDelayNS(path_delay);
        
        if (!clock_fmax.count(launch.clock) || Fmax < clock_fmax.at(launch.clock).achieved) {
            clock_fmax[launch.clock].achieved = Fmax;
            clock_fmax[launch.clock].constraint = 0.0f; // Will be filled later
            auto worst_endpoint = get_failing_eps(i, 1).at(0);
            clock_reports[launch.clock] = build_critical_path_report(i, worst_endpoint);
        }
    }

    for (int i = 0; i < int(domain_pairs.size()); i++) {
        auto &dp = domain_pairs.at(i);
        auto &launch = domains.at(dp.key.launch).key;
        auto &capture = domains.at(dp.key.capture).key;

        if (launch.clock == capture.clock && launch.clock == ctx->id("$async$"))
            continue;

        auto worst_endpoint = get_failing_eps(i, 1).at(0);
        xclock_reports.emplace_back(build_critical_path_report(i, worst_endpoint));
    }

    if (clock_reports.empty() && xclock_reports.empty()) {
        log_info("No Fmax available; no interior timing paths found in design.\n");
    }

    std::sort(xclock_reports.begin(), xclock_reports.end(), [&](const CriticalPath &ra, const CriticalPath &rb) {
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


    auto print_path = true;
    auto print_fmax =true;

    auto format_event = [&](const ClockEvent &e, int field_width = 0) {
        std::string value;
        if (e.clock == ctx->id("$async$"))
            value = std::string("<async>");
        else
            value = (e.edge == FALLING_EDGE ? std::string("negedge ") : std::string("posedge ")) + e.clock.str(ctx);
        if (int(value.length()) < field_width)
            value.insert(value.length(), field_width - int(value.length()), ' ');
        return value;
    };

        // Print critical paths
    if (print_path) {
        if (ctx->idstring_str_to_idx == nullptr) {
            log_info("global: ctx->idstring_str_to_idx is nullptr\n");
        } else {
            for (const auto& kv : *ctx->idstring_str_to_idx) {
                log_info("global: %s -> %d\n", kv.first.c_str(), kv.second);
            }
        }

        static auto print_net_source = [](Context* ctx, const NetInfo *net) {
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
        auto print_path_report = [](Context* ctx, const CriticalPath &path) {
            delay_t total = 0, logic_total = 0, route_total = 0;

            log_info("curr total\n");

            for (const auto &segment : path.segments) {

                // log_info("processing segment %s\n", segment.net.c_str(ctx));

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

                    log_info("%4.1f %4.1f    Net %s (%d,%d) -> (%d,%d)\n", ctx->getDelayNS(segment.delay),
                             ctx->getDelayNS(total), segment.net.c_str(ctx),
                             driver_loc.x, driver_loc.y, sink_loc.x, sink_loc.y);
                    log_info("               Sink %s.%s\n", segment.to.first.c_str(ctx), segment.to.second.c_str(ctx));

                    const NetInfo *net = ctx->nets.at(segment.net).get();

                    if (ctx->verbose) {
                        PortRef sink_ref;
                        sink_ref.cell = sink.get();
                        sink_ref.port = segment.to.second;

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
                        print_net_source(ctx, net);
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
            print_path_report(ctx, report);
        }

        // Cross-domain paths
        for (auto &report : xclock_reports) {
            log_break();
            std::string start = format_event(report.clock_pair.start);
            std::string end = format_event(report.clock_pair.end);
            log_info("Critical path report for cross-domain path '%s' -> '%s':\n", start.c_str(), end.c_str());
            print_path_report(ctx, report);
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

            if (passed)
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
        const auto &clock_delays = get_clock_delays();

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

                if (passed)
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

NEXTPNR_NAMESPACE_END

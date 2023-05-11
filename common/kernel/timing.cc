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

void TimingAnalyser::print_fmax()
{
    // Temporary testing code for comparison only
    dict<int, double> domain_fmax;
    for (auto p : topological_order) {
        auto &pd = ports.at(p);
        for (auto &req : pd.required) {
            if (pd.arrival.count(req.first)) {
                if (domains.at(req.first).key.is_async())
                    continue;
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

        log("Worst endpoints for %s -> %s\n", clock_event_name(ctx, launch.key).c_str(),
            clock_event_name(ctx, capture.key).c_str());
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

NEXTPNR_NAMESPACE_END

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

void TimingAnalyser::setup()
{
    init_ports();
    get_cell_delays();
    topo_sort();
    setup_port_domains();
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
    // Cell port to net port mapping
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        if (ni->driver.cell != nullptr)
            ports[CellPortKey(ni->driver)].net_port = NetPortKey(ni->name);
        for (size_t i = 0; i < ni->users.size(); i++)
            ports[CellPortKey(ni->users.at(i))].net_port = NetPortKey(ni->name, i);
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
        if (pd.net_port.net == IdString())
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
        if (!ctx->nets.count(clk))
            continue;
        NetInfo *clk_net = ctx->nets.at(clk).get();
        if (!clk_net->clkconstr)
            continue;
        delay_t period = clk_net->clkconstr->period.minDelay();
        if (launch_data.key.edge != capture_data.key.edge)
            period /= 2;
        dp.period = DelayPair(period);
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
                    set_required_time(CellPortKey(net->driver), req.first, req.second.value - pd.route_delay,
                                      req.second.path_length, p);
            } else if (pd.type == PORT_OUT) {
                // Output port : propagate delay back through cell, subtracting combinational delay
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type != CellArc::COMBINATIONAL)
                        continue;
                    set_required_time(CellPortKey(p.cell, fanin.other_port), req.first,
                                      req.second.value - fanin.value.delayPair(), req.second.path_length + 1, p);
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
            auto &arr = pd.arrival.at(dp.key.launch);
            auto &req = pd.required.at(dp.key.capture);
            pdp.second.setup_slack = 0 - (arr.value.maxDelay() - req.value.minDelay());
            if (!setup_only)
                pdp.second.hold_slack = arr.value.minDelay() - req.value.maxDelay();
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
            ctx->nameOf(get_net_or_empty(ctx->cells.at(cursor.cell).get(), cursor.port)));
        if (!ports.at(cursor).arrival.count(dp.key.launch))
            break;
        cursor = ports.at(cursor).arrival.at(dp.key.launch).bwd_max;
    }
}

namespace {
const char *edge_name(ClockEdge edge) { return (edge == FALLING_EDGE) ? "negedge" : "posedge"; }
} // namespace

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

namespace {
struct ClockEvent
{
    IdString clock;
    ClockEdge edge;

    bool operator==(const ClockEvent &other) const { return clock == other.clock && edge == other.edge; }
    unsigned int hash() const { return mkhash(clock.hash(), int(edge)); }
};

struct ClockPair
{
    ClockEvent start, end;

    bool operator==(const ClockPair &other) const { return start == other.start && end == other.end; }
    unsigned int hash() const { return mkhash(start.hash(), end.hash()); }
};
} // namespace

typedef std::vector<const PortRef *> PortRefVector;
typedef std::map<int, unsigned> DelayFrequency;

struct CriticalPath
{
    PortRefVector ports;
    delay_t path_delay;
    delay_t path_period;
};

typedef dict<ClockPair, CriticalPath> CriticalPathMap;

struct Timing
{
    Context *ctx;
    bool net_delays;
    bool update;
    delay_t min_slack;
    CriticalPathMap *crit_path;
    DelayFrequency *slack_histogram;
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

    Timing(Context *ctx, bool net_delays, bool update, CriticalPathMap *crit_path = nullptr,
           DelayFrequency *slack_histogram = nullptr)
            : ctx(ctx), net_delays(net_delays), update(update), min_slack(1.0e12 / ctx->setting<float>("target_freq")),
              crit_path(crit_path), slack_histogram(slack_histogram), async_clock(ctx->id("$async$"))
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
                        const NetInfo *clknet = get_net_or_empty(cell.second.get(), clkInfo.clock_port);
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
                                const NetInfo *clknet = get_net_or_empty(usr.cell, clkInfo.clock_port);
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

void timing_analysis(Context *ctx, bool print_histogram, bool print_fmax, bool print_path, bool warn_on_failure)
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

    CriticalPathMap crit_paths;
    DelayFrequency slack_histogram;

    Timing timing(ctx, true /* net_delays */, false /* update */, (print_path || print_fmax) ? &crit_paths : nullptr,
                  print_histogram ? &slack_histogram : nullptr);
    timing.walk_paths();
    std::map<IdString, std::pair<ClockPair, CriticalPath>> clock_reports;
    std::map<IdString, double> clock_fmax;
    std::vector<ClockPair> xclock_paths;
    std::set<IdString> empty_clocks; // set of clocks with no interior paths
    if (print_path || print_fmax) {
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
            if (!clock_fmax.count(a.clock) || Fmax < clock_fmax.at(a.clock)) {
                clock_reports[a.clock] = path;
                clock_fmax[a.clock] = Fmax;
            }
        }

        for (auto &path : crit_paths) {
            const ClockEvent &a = path.first.start;
            const ClockEvent &b = path.first.end;
            if (a.clock == b.clock && a.clock != ctx->id("$async$"))
                continue;
            xclock_paths.push_back(path.first);
        }

        if (clock_reports.empty()) {
            log_info("No Fmax available; no interior timing paths found in design.\n");
        }

        std::sort(xclock_paths.begin(), xclock_paths.end(), [ctx](const ClockPair &a, const ClockPair &b) {
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
    }

    if (print_path) {
        static auto print_net_source = [](Context *ctx, NetInfo *net) {
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

        auto print_path_report = [ctx](ClockPair &clocks, PortRefVector &crit_path) {
            delay_t total = 0, logic_total = 0, route_total = 0;
            auto &front = crit_path.front();
            auto &front_port = front->cell->ports.at(front->port);
            auto &front_driver = front_port.net->driver;

            int port_clocks;
            auto portClass = ctx->getPortTimingClass(front_driver.cell, front_driver.port, port_clocks);
            IdString last_port = front_driver.port;
            int clock_start = -1;
            if (portClass == TMG_REGISTER_OUTPUT) {
                for (int i = 0; i < port_clocks; i++) {
                    TimingClockingInfo clockInfo = ctx->getPortClockingInfo(front_driver.cell, front_driver.port, i);
                    const NetInfo *clknet = get_net_or_empty(front_driver.cell, clockInfo.clock_port);
                    if (clknet != nullptr && clknet->name == clocks.start.clock &&
                        clockInfo.edge == clocks.start.edge) {
                        last_port = clockInfo.clock_port;
                        clock_start = i;
                        break;
                    }
                }
            }

            log_info("curr total\n");
            for (auto sink : crit_path) {
                auto sink_cell = sink->cell;
                auto &port = sink_cell->ports.at(sink->port);
                auto net = port.net;
                auto &driver = net->driver;
                auto driver_cell = driver.cell;
                DelayQuad comb_delay;
                if (clock_start != -1) {
                    auto clockInfo = ctx->getPortClockingInfo(driver_cell, driver.port, clock_start);
                    comb_delay = clockInfo.clockToQ;
                    clock_start = -1;
                } else if (last_port == driver.port) {
                    // Case where we start with a STARTPOINT etc
                    comb_delay = DelayQuad(0);
                } else {
                    ctx->getCellDelay(driver_cell, last_port, driver.port, comb_delay);
                }
                total += comb_delay.maxDelay();
                logic_total += comb_delay.maxDelay();
                log_info("%4.1f %4.1f  Source %s.%s\n", ctx->getDelayNS(comb_delay.maxDelay()), ctx->getDelayNS(total),
                         driver_cell->name.c_str(ctx), driver.port.c_str(ctx));
                auto net_delay = ctx->getNetinfoRouteDelay(net, *sink);
                total += net_delay;
                route_total += net_delay;
                auto driver_loc = ctx->getBelLocation(driver_cell->bel);
                auto sink_loc = ctx->getBelLocation(sink_cell->bel);
                log_info("%4.1f %4.1f    Net %s budget %f ns (%d,%d) -> (%d,%d)\n", ctx->getDelayNS(net_delay),
                         ctx->getDelayNS(total), net->name.c_str(ctx), ctx->getDelayNS(sink->budget), driver_loc.x,
                         driver_loc.y, sink_loc.x, sink_loc.y);
                log_info("               Sink %s.%s\n", sink_cell->name.c_str(ctx), sink->port.c_str(ctx));
                if (ctx->verbose) {
                    auto driver_wire = ctx->getNetinfoSourceWire(net);
                    auto sink_wire = ctx->getNetinfoSinkWire(net, *sink, 0);
                    log_info("                 prediction: %f ns estimate: %f ns\n",
                             ctx->getDelayNS(ctx->predictDelay(net, *sink)),
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
                last_port = sink->port;
            }
            int clockCount = 0;
            auto sinkClass = ctx->getPortTimingClass(crit_path.back()->cell, crit_path.back()->port, clockCount);
            if (sinkClass == TMG_REGISTER_INPUT && clockCount > 0) {
                auto sinkClockInfo = ctx->getPortClockingInfo(crit_path.back()->cell, crit_path.back()->port, 0);
                delay_t setup = sinkClockInfo.setup.maxDelay();
                total += setup;
                logic_total += setup;
                log_info("%4.1f %4.1f  Setup %s.%s\n", ctx->getDelayNS(setup), ctx->getDelayNS(total),
                         crit_path.back()->cell->name.c_str(ctx), crit_path.back()->port.c_str(ctx));
            }
            log_info("%.1f ns logic, %.1f ns routing\n", ctx->getDelayNS(logic_total), ctx->getDelayNS(route_total));
        };

        for (auto &clock : clock_reports) {
            log_break();
            std::string start =
                    clock.second.first.start.edge == FALLING_EDGE ? std::string("negedge") : std::string("posedge");
            std::string end =
                    clock.second.first.end.edge == FALLING_EDGE ? std::string("negedge") : std::string("posedge");
            log_info("Critical path report for clock '%s' (%s -> %s):\n", clock.first.c_str(ctx), start.c_str(),
                     end.c_str());
            auto &crit_path = clock.second.second.ports;
            print_path_report(clock.second.first, crit_path);
        }

        for (auto &xclock : xclock_paths) {
            log_break();
            std::string start = format_event(xclock.start);
            std::string end = format_event(xclock.end);
            log_info("Critical path report for cross-domain path '%s' -> '%s':\n", start.c_str(), end.c_str());
            auto &crit_path = crit_paths.at(xclock).ports;
            print_path_report(xclock, crit_path);
        }
    }
    if (print_fmax) {
        log_break();
        unsigned max_width = 0;
        auto &result = ctx->timing_result;
        result.clock_fmax.clear();
        for (auto &clock : clock_reports)
            max_width = std::max<unsigned>(max_width, clock.first.str(ctx).size());
        for (auto &clock : clock_reports) {
            const auto &clock_name = clock.first.str(ctx);
            const int width = max_width - clock_name.size();
            float target = ctx->setting<float>("target_freq") / 1e6;
            if (ctx->nets.at(clock.first)->clkconstr)
                target = 1000 / ctx->getDelayNS(ctx->nets.at(clock.first)->clkconstr->period.minDelay());

            result.clock_fmax[clock.first].achieved = clock_fmax[clock.first];
            result.clock_fmax[clock.first].constraint = target;

            bool passed = target < clock_fmax[clock.first];
            if (!warn_on_failure || passed)
                log_info("Max frequency for clock %*s'%s': %.02f MHz (%s at %.02f MHz)\n", width, "",
                         clock_name.c_str(), clock_fmax[clock.first], passed ? "PASS" : "FAIL", target);
            else if (bool_or_default(ctx->settings, ctx->id("timing/allowFail"), false))
                log_warning("Max frequency for clock %*s'%s': %.02f MHz (%s at %.02f MHz)\n", width, "",
                            clock_name.c_str(), clock_fmax[clock.first], passed ? "PASS" : "FAIL", target);
            else
                log_nonfatal_error("Max frequency for clock %*s'%s': %.02f MHz (%s at %.02f MHz)\n", width, "",
                                   clock_name.c_str(), clock_fmax[clock.first], passed ? "PASS" : "FAIL", target);
        }
        for (auto &eclock : empty_clocks) {
            if (eclock != ctx->id("$async$"))
                log_info("Clock '%s' has no interior paths\n", eclock.c_str(ctx));
        }
        log_break();

        int start_field_width = 0, end_field_width = 0;
        for (auto &xclock : xclock_paths) {
            start_field_width = std::max((int)format_event(xclock.start).length(), start_field_width);
            end_field_width = std::max((int)format_event(xclock.end).length(), end_field_width);
        }

        for (auto &xclock : xclock_paths) {
            const ClockEvent &a = xclock.start;
            const ClockEvent &b = xclock.end;
            auto &path = crit_paths.at(xclock);
            auto ev_a = format_event(a, start_field_width), ev_b = format_event(b, end_field_width);
            log_info("Max delay %s -> %s: %0.02f ns\n", ev_a.c_str(), ev_b.c_str(), ctx->getDelayNS(path.path_delay));
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
}

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018-19  David Shah <david@symbioticeda.com>
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
#include <unordered_map>
#include <utility>
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

struct TimingAnalyser
{
    Context *ctx;
    TimingData *td;

    void label_ports()
    {
        // Clear out all existing data, and re-label all parts
        td->domains.clear();
        td->domainTagIds.clear();
        td->domainPairs.clear();
        td->domainPairIds.clear();
        td->ports.clear();
        td->portInfos_by_uid.clear();
        td->ports_by_uid.clear();

        // First, clear UID of all ports to -1
        for (auto cell : sorted(ctx->cells)) {
            for (auto &port : cell.second->ports)
                port.second.uid = -1;
        }

        int max_uid = 0;
        // Handle ports connected to nets
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            if (ni->driver.cell != nullptr) {
                ni->driver.uid = max_uid;
                ni->driver.cell->ports.at(ni->driver.port).uid = max_uid;
                td->ports_by_uid.push_back(&ni->driver);
                td->portInfos_by_uid.push_back(&ni->driver.cell->ports.at(ni->driver.port));
                td->ports.emplace_back();
                td->ports.back().cell = ni->driver.cell;
                ++max_uid;
            }
            for (auto &usr : ni->users) {
                usr.uid = max_uid;
                usr.cell->ports.at(usr.port).uid = max_uid;
                td->ports_by_uid.push_back(&usr);
                td->portInfos_by_uid.push_back(&usr.cell->ports.at(usr.port));
                td->ports.emplace_back();
                td->ports.back().cell = usr.cell;
                ++max_uid;
            }
        }
        // Handle any ports not connected to nets
        for (auto cell : sorted(ctx->cells)) {
            for (auto &port : cell.second->ports) {
                if (port.second.uid != -1)
                    continue;
                NPNR_ASSERT(port.second.net == nullptr);
                port.second.uid = max_uid;
                td->portInfos_by_uid.push_back(&port.second);
                td->ports_by_uid.push_back(nullptr);
                td->ports.emplace_back();
                td->ports.back().cell = cell.second;
                ++max_uid;
            }
        }
    }

    void get_cell_arcs()
    {
        for (auto cell : sorted(ctx->cells)) {
            for (auto &port : cell.second->ports) {
                auto &p = port.second;
                auto &pd = td->ports.at(p.uid);
                pd.cell_arcs.clear();

                int clkInfoCount = 0;
                TimingPortClass cls = ctx->getPortTimingClass(cell.second, port.first, clkInfoCount);
                if (cls == TMG_STARTPOINT || cls == TMG_ENDPOINT || cls == TMG_CLOCK_INPUT || cls == TMG_GEN_CLOCK ||
                    cls == TMG_IGNORE)
                    continue;
                if (p.type == PORT_IN) {
                    // Input ports might have setup/hold relationships
                    if (cls == TMG_REGISTER_INPUT) {
                        for (int i = 0; i < clkInfoCount; i++) {
                            auto info = ctx->getPortClockingInfo(cell.second, port.first, i);
                            pd.cell_arcs.push_back(
                                    TimingCellArc{TimingCellArc::SETUP, info.clock_port, info.setup, info.edge});
                            pd.cell_arcs.push_back(
                                    TimingCellArc{TimingCellArc::HOLD, info.clock_port, info.hold, info.edge});
                        }
                    }
                    // Obtain arcs out of an input port through the cell
                    for (auto &other_port : cell.second->ports) {
                        auto &op = other_port.second;
                        if (other_port.first == port.first)
                            continue;
                        if (op.type != PORT_OUT)
                            continue;
                        DelayInfo delay;
                        bool is_path = ctx->getCellDelay(cell.second, port.first, other_port.first, delay);
                        if (is_path)
                            pd.cell_arcs.push_back(
                                    TimingCellArc{TimingCellArc::COMBINATIONAL, other_port.first, delay});
                    }
                } else if (p.type == PORT_OUT) {
                    // Output ports might have clk-to-q relationships
                    if (cls == TMG_REGISTER_OUTPUT) {
                        for (int i = 0; i < clkInfoCount; i++) {
                            auto info = ctx->getPortClockingInfo(cell.second, port.first, i);
                            pd.cell_arcs.push_back(
                                    TimingCellArc{TimingCellArc::CLK_TO_Q, info.clock_port, info.clockToQ, info.edge});
                        }
                    }
                    // Obtain arcs from an input port into this output port through the cell
                    for (auto &other_port : cell.second->ports) {
                        auto &op = other_port.second;
                        if (other_port.first == port.first)
                            continue;
                        if (op.type != PORT_IN)
                            continue;
                        DelayInfo delay;
                        bool is_path = ctx->getCellDelay(cell.second, other_port.first, port.first, delay);
                        if (is_path)
                            pd.cell_arcs.push_back(
                                    TimingCellArc{TimingCellArc::COMBINATIONAL, other_port.first, delay});
                    }
                }
            }
        }
    }

    int domain_tag_id(const TimingDomainTag &dt)
    {
        auto fnd = td->domainTagIds.find(dt);
        if (fnd == td->domainTagIds.end()) {
            int id = int(td->domains.size());
            TimingDomainData dd;
            dd.tag = dt;
            td->domains.push_back(dd);
            td->domainTagIds[dt] = id;
            return id;
        } else {
            return fnd->second;
        }
    }

    void topo_sort()
    {
        // Build up a topological ordering of ports
        std::vector<int> port_fanin(td->ports.size());
        std::unordered_set<port_uid_t> nonzero_fanin;

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            for (auto &port : ci->ports) {
                auto &p = port.second;
                if (p.type != PORT_OUT)
                    continue;
                int clkCount;
                auto cls = ctx->getPortTimingClass(ci, port.first, clkCount);
                auto &pd = td->ports.at(p.uid);
                for (auto &arcin : pd.cell_arcs) {
                    // Look at all input ports that fan into this port
                    if (arcin.type != TimingCellArc::COMBINATIONAL)
                        continue;
                    // Ignore floating inputs, as these will not be visited
                    if (ci->ports.at(arcin.other_port).net == nullptr)
                        continue;
                    if (port_fanin[p.uid] == 0)
                        nonzero_fanin.insert(p.uid);
                    port_fanin[p.uid]++;
                }

                if ((cls == TMG_REGISTER_OUTPUT || cls == TMG_STARTPOINT || cls == TMG_GEN_CLOCK ||
                     cls == TMG_IGNORE) &&
                    port_fanin[p.uid] == 0) {
                    // Startpoints - note only clocked outputs/startpoints without combinational paths into them are
                    // added (it is possible that a cell containing e.g. a register followed by a mux would have a port
                    // of class TMG_REGISTER_OUTPUT that also had a combinational arc onto it)
                    td->topological_order.push_back(p.uid);
                }
            }
        }

        std::deque<port_uid_t> queue(td->topological_order.begin(), td->topological_order.end());
        // Ports that were left with a non-zero fanin at the end of topological ordering
        std::vector<port_uid_t> bad_ports;

        while (!queue.empty()) {
            int next_uid = queue.front();
            queue.pop_front();
            auto &p = td->portInfos_by_uid.at(next_uid);
            if (p->type == PORT_OUT) {
                // Sanity check that we are visiting things in the correct order
                NPNR_ASSERT(port_fanin[p->uid] == 0);
                // Output ports - immediately add all driven inputs to topological order
                if (p->net != nullptr)
                    for (auto &usr : p->net->users) {
                        td->topological_order.push_back(usr.uid);
                        queue.push_back(usr.uid);
                    }
            } else {
                // Input port
                auto &pd = td->ports.at(next_uid);
                // Look for all outputs with combinational arcs from this input
                for (auto &arcout : pd.cell_arcs) {
                    if (arcout.type != TimingCellArc::COMBINATIONAL)
                        continue;
                    port_uid_t other_port_id = td->ports_by_uid.at(next_uid)->cell->ports.at(arcout.other_port).uid;
                    // If the output already has a zero fanout, then we must have visited it too early or
                    // forgotten to add some fanin to port_fanin
                    NPNR_ASSERT(port_fanin.at(other_port_id) > 0);
                    // Decrement the output's fanin now we have visited one fanin, and add it to the topological order
                    // if and only if it's fanin is now zero
                    if (--port_fanin.at(other_port_id) == 0) {
                        td->topological_order.push_back(other_port_id);
                        queue.push_back(other_port_id);
                        nonzero_fanin.erase(other_port_id);
                    }
                }
            }

            // If the queue is now empty, check we haven't missed any ports due to combinational loops, etc
            // If we find any, add them to the order and queue for "best-effort" handling of loops, and allow a
            // error/warning to be shown to the user
            if (queue.empty() && !nonzero_fanin.empty()) {
                port_uid_t nz_port = *(nonzero_fanin.begin());
                nonzero_fanin.erase(nz_port);
                // FIXME: trace source of non-zero fanin to find and report loops
                bad_ports.push_back(nz_port);
                td->topological_order.push_back(nz_port);
                queue.push_back(nz_port);
            }
        }
        if (!bad_ports.empty() && !bool_or_default(ctx->settings, ctx->id("timing/ignoreLoops"), false)) {
            for (auto bp : bad_ports) {
                PortInfo *pi = td->portInfos_by_uid.at(bp);
                NetInfo *net = pi->net;
                if (net != nullptr) {
                    log_info("   remaining fanin includes %s (net %s)\n", pi->name.c_str(ctx), net->name.c_str(ctx));
                    if (net->driver.cell != nullptr)
                        log_info("        driver = %s.%s\n", net->driver.cell->name.c_str(ctx),
                                 net->driver.port.c_str(ctx));
                    for (auto net_user : net->users)
                        log_info("        user: %s.%s\n", net_user.cell->name.c_str(ctx), net_user.port.c_str(ctx));
                } else {
                    log_info("   remaining fanin includes %s (no net)\n", pi->name.c_str(ctx));
                }
            }
            if (ctx->force)
                log_warning("timing analysis failed due to presence of combinatorial loops, incomplete specification "
                            "of timing ports, etc.\n");
            else
                log_error("timing analysis failed due to presence of combinatorial loops, incomplete specification of "
                          "timing ports, etc.\n");
        }
    }

    void get_net_delays()
    {
        for (auto &n : sorted(ctx->nets)) {
            NetInfo *ni = n.second;
            for (auto &usr : ni->users) {
                td->ports.at(usr.uid).net_delay = ctx->getNetinfoRouteDelay(ni, usr);
            }
        }
    }

    void setup_domains()
    {
        // Clear the list of startpoints and endpoints
        for (auto &d : td->domains) {
            d.startpoints.clear();
            d.endpoints.clear();
        }

        // Go forward through the topological order (domains from the PoV of arrival time)
        for (auto p_uid : td->topological_order) {
            auto &p = td->ports_by_uid.at(p_uid);
            auto &pi = td->portInfos_by_uid.at(p_uid);
            auto &pd = td->ports.at(p_uid);
            if (pi->type == PORT_OUT) {
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type == TimingCellArc::CLK_TO_Q) {
                        // Create domain for clocked port
                        NetInfo *clknet = pd.cell->ports.at(fanin.other_port).net;
                        if (clknet == nullptr)
                            continue;
                        TimingDomainTag tdt;
                        tdt.clock = clknet->name;
                        tdt.edge = fanin.edge;
                        auto tdid = domain_tag_id(tdt);
                        pd.arrival[tdid];
                        td->domains[tdid].startpoints.emplace_back(p_uid, pd.cell->ports.at(fanin.other_port).uid);
                    }
                }

                // Copy domains onto directly connected input ports
                NetInfo *n = pi->net;
                if (n == nullptr)
                    continue;
                for (auto &usr : n->users) {
                    auto &usr_pd = td->ports.at(usr.uid);
                    for (auto &domain : pd.arrival)
                        usr_pd.arrival[domain.first];
                }
            } else {
                // Input pin - copy domains - TODO: duplicating domains when seeing constraints
                for (auto &fanout : pd.cell_arcs) {
                    if (fanout.type == TimingCellArc::COMBINATIONAL) {
                        // Copy domains for combinational fanout
                        for (auto &dt : td->ports.at(pd.cell->ports.at(fanout.other_port).uid).arrival) {
                            pd.arrival[dt.first];
                        }
                    }
                }
            }
        }

        // Go backward through the topological order (domains from the PoV of required time)
        for (auto p_uid : boost::adaptors::reverse(td->topological_order)) {
            auto &p = td->ports_by_uid.at(p_uid);
            auto &pi = td->portInfos_by_uid.at(p_uid);
            auto &pd = td->ports.at(p_uid);
            if (pi->type == PORT_OUT) {
                // Output pin - copy domains - TODO: duplicating domains when seeing constraints
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type == TimingCellArc::COMBINATIONAL) {
                        // Copy domains for combinational fanout
                        for (auto &dt : td->ports.at(pd.cell->ports.at(fanin.other_port).uid).required) {
                            pd.required[dt.first];
                        }
                    }
                }
            } else if (pi->type == PORT_IN) {
                for (auto &fanout : pd.cell_arcs) {
                    if (fanout.type == TimingCellArc::SETUP) {
                        // Create domain for clocked port
                        if (p == nullptr)
                            continue;
                        NetInfo *clknet = pd.cell->ports.at(fanout.other_port).net;
                        if (clknet == nullptr)
                            continue;
                        TimingDomainTag tdt;
                        tdt.clock = clknet->name;
                        tdt.edge = fanout.edge;
                        auto tdid = domain_tag_id(tdt);
                        pd.required[tdid];
                        td->domains[tdid].endpoints.emplace_back(p_uid, pd.cell->ports.at(fanout.other_port).uid);
                    }
                }
                // Copy domains to connected net driver
                NetInfo *n = pi->net;
                if (n == nullptr || n->driver.cell == nullptr)
                    continue;
                for (auto &dt : pd.required)
                    td->ports[n->driver.uid].required[dt.first];
            }

            // Iterate through all ports finding domain pairs
            for (size_t p_uid = 0; p_uid < td->ports.size(); p_uid++) {
                auto &pd = td->ports.at(p_uid);
                pd.times.clear();
                for (auto &at : pd.arrival) {
                    for (auto &rt : pd.required) {
                        auto &ad = td->domains.at(at.first), &rd = td->domains.at(rt.first);
                        if (ad.tag.clock == rd.tag.clock) {
                            // FIXME: cross clock path analysis
                            if (td->domainPairIds.count(at.first) && td->domainPairIds.at(at.first).count(rt.first)) {
                                // Domain pair already created
                                int dp_uid = td->domainPairIds[at.first][rt.first];
                                pd.times[dp_uid];
                                td->domainPairs.at(dp_uid).ports.push_back(p_uid);
                            } else {
                                // Need to discover period and create domain pair
                                DelayInfo period = ctx->getDelayFromNS(1000.0 / ctx->target_freq);
                                NetInfo *clknet = ctx->nets.at(ad.tag.clock).get();
                                if (clknet->clkconstr)
                                    period = clknet->clkconstr->period;
                                // FIXME: duty cycle
                                if (ad.tag.edge != rd.tag.edge)
                                    period = clknet->clkconstr->high;
                                td->domainPairIds[at.first][rt.first] = int(td->domainPairs.size());
                                td->domainPairs.emplace_back();
                                td->domainPairs.back().start_domain = at.first;
                                td->domainPairs.back().end_domain = rt.first;
                                td->domainPairs.back().period.min = period.minDelay();
                                td->domainPairs.back().period.max = period.minDelay();
                                td->domainPairs.back().ports.push_back(p_uid);
                            }
                        }
                    }
                }
            }
        }
    }
    enum
    {
        SETUP_ONLY = 1,
        IGNORE_CLOCK_ROUTING = 2,
    } sta_flags;

    void add_arrival_time(port_uid_t target, int domain, delay_t max_arr, delay_t min_arr, int path_length,
                          port_uid_t prev = -1)
    {
        auto &t = td->ports[target].arrival[domain];
        if (max_arr > t.value.max) {
            t.value.max = max_arr;
            t.bwd_max = prev;
        }
        if (path_length > t.path_length)
            t.path_length = path_length;
        if (!(sta_flags & SETUP_ONLY) && min_arr < t.value.min) {
            t.value.min = min_arr;
            t.bwd_min = prev;
        }
    }

    void add_required_time(port_uid_t target, int domain, delay_t max_req, delay_t min_req, int path_length,
                           port_uid_t prev = -1)
    {
        auto &t = td->ports[target].required[domain];
        if (min_req < t.value.min) {
            t.value.min = min_req;
            t.bwd_min = prev;
        }
        if (path_length > t.path_length)
            t.path_length = path_length;
        if (!(sta_flags & SETUP_ONLY) && max_req > t.value.max) {
            t.value.max = max_req;
            t.bwd_max = prev;
        }
    }

    void reset_times()
    {
        for (auto &p : td->ports) {
            for (auto &t : p.arrival) {
                t.second.value = MinMaxDelay();
                t.second.path_length = 0;
                t.second.bwd_max = -1;
                t.second.bwd_min = -1;
            }
            for (auto &t : p.required) {
                t.second.value = MinMaxDelay();
                t.second.path_length = 0;
                t.second.bwd_max = -1;
                t.second.bwd_min = -1;
            }
            for (auto &t : p.times) {
                t.second.setup_slack = std::numeric_limits<delay_t>::max();
                t.second.hold_slack = std::numeric_limits<delay_t>::max();

                t.second.max_path_length = 0;
                t.second.criticality = 0;
                t.second.budget = 0;
            }
        }
        for (auto &dp : td->domainPairs) {
            dp.crit_delay = MinMaxDelay();
            dp.crit_hold_ep = -1;
            dp.crit_setup_ep = -1;
            dp.worst_setup_slack = std::numeric_limits<delay_t>::max();
            dp.worst_hold_slack = std::numeric_limits<delay_t>::max();
        }
    }

    delay_t add_safe(delay_t a, delay_t b)
    {
        return ((b < 0 || a < std::numeric_limits<delay_t>::max()) &&
                (b > 0 || a > std::numeric_limits<delay_t>::lowest()))
                       ? a + b
                       : a;
    }

    void walk_forward(int domain)
    {
        auto &dm = td->domains.at(domain);
        // Assign initial arrival time to domain startpoints
        for (auto &sp : dm.startpoints) {
            auto &pd = td->ports.at(sp.first);
            delay_t min_dly = 0, max_dly = 0;
            // Add clock routing delay, if we need to consider it
            if (!(sta_flags & IGNORE_CLOCK_ROUTING) && sp.second != -1) {
                min_dly = td->ports.at(sp.second).net_delay;
                max_dly = td->ports.at(sp.second).net_delay;
            }
            // Add clock to out time, if this startpoint is clocked
            if (sp.second != -1) {
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type == TimingCellArc::CLK_TO_Q &&
                        fanin.other_port == td->portInfos_by_uid.at(sp.second)->name) {
                        min_dly += fanin.value.minDelay();
                        max_dly += fanin.value.maxDelay();
                        break;
                    }
                }
            }
            add_arrival_time(sp.first, domain, max_dly, min_dly, 1, sp.second);
        }
        // Walk forward in topological order
        for (auto p_uid : td->topological_order) {
            auto &pd = td->ports.at(p_uid);
            auto &pi = td->portInfos_by_uid.at(p_uid);
            auto &p = td->ports_by_uid.at(p_uid);
            if (!pd.arrival.count(domain))
                continue;
            auto &pt = pd.arrival.at(domain);
            if (pi->type == PORT_OUT) {
                // Output port: propagate delay through net, adding route delay
                if (pi->net == nullptr)
                    continue;
                for (auto &usr : pi->net->users)
                    add_arrival_time(usr.uid, domain, add_safe(pt.value.max, td->ports.at(usr.uid).net_delay),
                                     add_safe(pt.value.min, td->ports.at(usr.uid).net_delay), pt.path_length, p_uid);
            } else if (pi->type == PORT_IN) {
                // Input port : propagate delay through cell, adding combinational delay
                for (auto &fanout : pd.cell_arcs) {
                    if (fanout.type != TimingCellArc::COMBINATIONAL)
                        continue;
                    add_arrival_time(pd.cell->ports.at(fanout.other_port).uid, domain,
                                     add_safe(pt.value.max, fanout.value.maxDelay()),
                                     add_safe(pt.value.min, fanout.value.minDelay()), pt.path_length + 1, p_uid);
                }
            }
        }
    }

    void walk_backward(int domain)
    {
        auto &dm = td->domains.at(domain);
        // Assign initial required time to domain endpoints
        // Note that clock frequency will be considered later, for now
        // all required times are normalised to 0-setup/hold
        for (auto &sp : dm.endpoints) {
            auto &pd = td->ports.at(sp.first);
            delay_t setup = 0, hold = 0;
            // Add clock routing delay, if we need to consider it
            if (!(sta_flags & IGNORE_CLOCK_ROUTING) && sp.second != -1) {
                setup += td->ports.at(sp.second).net_delay;
                hold += td->ports.at(sp.second).net_delay;
            }
            // Add setup/hold time, if this endpoint is clocked
            if (sp.second != -1) {
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type == TimingCellArc::SETUP &&
                        fanin.other_port == td->portInfos_by_uid.at(sp.second)->name) {
                        setup -= fanin.value.maxDelay();
                    }
                    if (fanin.type == TimingCellArc::HOLD &&
                        fanin.other_port == td->portInfos_by_uid.at(sp.second)->name) {
                        hold -= fanin.value.maxDelay();
                    }
                }
            }

            add_required_time(sp.first, domain, hold, setup, 1, sp.second);
        }

        // Walk backwards in topological order
        for (auto p_uid : boost::adaptors::reverse(td->topological_order)) {
            auto &pd = td->ports.at(p_uid);
            auto &pi = td->portInfos_by_uid.at(p_uid);
            auto &p = td->ports_by_uid.at(p_uid);
            if (!pd.required.count(domain))
                continue;
            auto &pt = pd.required.at(domain);
            if (pi->type == PORT_IN) {
                // Input port: propagate delay back through net, subtracting route delay
                if (pi->net == nullptr)
                    continue;
                add_required_time(pi->net->driver.uid, domain, add_safe(pt.value.max, -td->ports.at(p_uid).net_delay),
                                  add_safe(pt.value.min, -td->ports.at(p_uid).net_delay), pt.path_length, p_uid);
            } else if (pi->type == PORT_OUT) {
                // Output port : propagate delay back through cell, subtracting combinational delay
                for (auto &fanin : pd.cell_arcs) {
                    if (fanin.type != TimingCellArc::COMBINATIONAL)
                        continue;
                    add_required_time(pd.cell->ports.at(fanin.other_port).uid, domain,
                                      add_safe(pt.value.max, -fanin.value.maxDelay()),
                                      add_safe(pt.value.min, -fanin.value.minDelay()), pt.path_length + 1, p_uid);
                }
            }
        }
    }

    void compute_slack()
    {
        for (auto &p : td->ports) {
            for (auto &t : p.times) {
                auto &dp = td->domainPairs.at(t.first);
                t.second.setup_slack = dp.period.min + p.arrival.at(dp.start_domain).value.max -
                                       p.required.at(dp.end_domain).value.min;
                t.second.hold_slack = p.arrival.at(dp.start_domain).value.min - p.required.at(dp.end_domain).value.max;
                t.second.max_path_length =
                        p.arrival.at(dp.start_domain).path_length + p.required.at(dp.end_domain).path_length;
                t.second.budget = t.second.setup_slack / t.second.max_path_length;
                p.min_slack = std::min(p.min_slack, t.second.setup_slack);
                p.min_budget = std::min(p.min_budget, t.second.budget);
            }
        }
        for (size_t dp_uid = 0; dp_uid < td->domainPairs.size(); dp_uid++) {
            auto &dp = td->domainPairs.at(dp_uid);
            dp.worst_setup_slack = std::numeric_limits<delay_t>::max();
            dp.worst_hold_slack = std::numeric_limits<delay_t>::max();
            for (auto p_uid : dp.ports) {
                auto &pd = td->ports.at(p_uid);
                dp.worst_setup_slack = std::min(dp.worst_setup_slack, pd.times.at(dp_uid).setup_slack);
                dp.worst_hold_slack = std::min(dp.worst_hold_slack, pd.times.at(dp_uid).hold_slack);
            }
        }
    }

    void assign_criticality()
    {
        for (auto &p : td->ports) {
            for (auto &t : p.times) {
                delay_t path_delay =
                        td->domainPairs.at(t.first).period.min + td->domainPairs.at(t.first).worst_setup_slack;
                t.second.criticality =
                        1.0f - ((float(t.second.setup_slack) - float(td->domainPairs.at(t.first).worst_setup_slack)) /
                                path_delay);
                p.max_crit = std::max(p.max_crit, t.second.criticality);
            }
        }
    }

    TimingAnalyser(Context *ctx, TimingData *td) : ctx(ctx), td(td){};

    void reset_all()
    {
        label_ports();
        get_cell_arcs();
        get_net_delays();
        topo_sort();
        setup_domains();
    }

    void calculate_times()
    {
        reset_times();
        for (size_t i = 0; i < td->domains.size(); i++) {
            walk_forward(i);
            walk_backward(i);
        }
    }

    void print_times()
    {
        std::unordered_map<int, double> domain_fmax;

        for (size_t i = 0; i < td->ports.size(); i++) {
            auto pi = td->portInfos_by_uid.at(i);
            auto pr = td->ports_by_uid.at(i);
            auto &pd = td->ports.at(i);
            if (pr == nullptr)
                continue;
            log_info("Port %s.%s: \n", pr->cell->name.c_str(ctx), pr->port.c_str(ctx));
            for (auto &t : pd.arrival) {
                log_info("    Domain %s arrival [%.02f, %.02f]\n", td->domains.at(t.first).tag.clock.c_str(ctx),
                         ctx->getDelayNS(t.second.value.min), ctx->getDelayNS(t.second.value.max));
            }
            for (auto &t : pd.required) {
                log_info("    Domain %s required [%.02f, %.02f]\n", td->domains.at(t.first).tag.clock.c_str(ctx),
                         ctx->getDelayNS(t.second.value.min), ctx->getDelayNS(t.second.value.max));
                auto at = pd.arrival.find(t.first);
                if (at != pd.arrival.end()) {
                    double fmax = 1000.0 / ctx->getDelayNS(at->second.value.max - t.second.value.min);
                    log_info("    Domain %s Fmax %.02f\n", td->domains.at(t.first).tag.clock.c_str(ctx), fmax);
                    if (!domain_fmax.count(t.first) || domain_fmax.at(t.first) > fmax)
                        domain_fmax[t.first] = fmax;
                }
            }
        }
        for (auto &fm : domain_fmax) {
            log_info("Domain %s Worst Fmax %.02f\n", td->domains.at(fm.first).tag.clock.c_str(ctx), fm.second);
        }
    }
};

struct ClockEvent
{
    IdString clock;
    ClockEdge edge;

    bool operator==(const ClockEvent &other) const { return clock == other.clock && edge == other.edge; }
};

struct ClockPair
{
    ClockEvent start, end;

    bool operator==(const ClockPair &other) const { return start == other.start && end == other.end; }
};
} // namespace

NEXTPNR_NAMESPACE_END
namespace std {

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX ClockEvent>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX ClockEvent &obj) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(obj.clock));
        boost::hash_combine(seed, hash<int>()(int(obj.edge)));
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX ClockPair>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX ClockPair &obj) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<NEXTPNR_NAMESPACE_PREFIX ClockEvent>()(obj.start));
        boost::hash_combine(seed, hash<NEXTPNR_NAMESPACE_PREFIX ClockEvent>()(obj.start));
        return seed;
    }
};

} // namespace std
NEXTPNR_NAMESPACE_BEGIN

typedef std::vector<const PortRef *> PortRefVector;
typedef std::map<int, unsigned> DelayFrequency;

struct CriticalPath
{
    PortRefVector ports;
    delay_t path_delay;
    delay_t path_period;
};

typedef std::unordered_map<ClockPair, CriticalPath> CriticalPathMap;
typedef std::unordered_map<IdString, NetCriticalityInfo> NetCriticalityMap;

struct Timing
{
    Context *ctx;
    bool net_delays;
    bool update;
    delay_t min_slack;
    CriticalPathMap *crit_path;
    DelayFrequency *slack_histogram;
    NetCriticalityMap *net_crit;
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
        std::unordered_map<ClockEvent, delay_t> arrival_time;
    };

    Timing(Context *ctx, bool net_delays, bool update, CriticalPathMap *crit_path = nullptr,
           DelayFrequency *slack_histogram = nullptr, NetCriticalityMap *net_crit = nullptr)
            : ctx(ctx), net_delays(net_delays), update(update), min_slack(1.0e12 / ctx->setting<float>("target_freq")),
              crit_path(crit_path), slack_histogram(slack_histogram), net_crit(net_crit),
              async_clock(ctx->id("$async$"))
    {
    }

    delay_t walk_paths()
    {
        const auto clk_period = ctx->getDelayFromNS(1.0e9 / ctx->setting<float>("target_freq")).maxDelay();

        // First, compute the topographical order of nets to walk through the circuit, assuming it is a _acyclic_ graph
        // TODO(eddieh): Handle the case where it is cyclic, e.g. combinatorial loops
        std::vector<NetInfo *> topographical_order;
        std::unordered_map<const NetInfo *, std::unordered_map<ClockEvent, TimingData>> net_data;
        // In lieu of deleting edges from the graph, simply count the number of fanins to each output port
        std::unordered_map<const PortInfo *, unsigned> port_fanin;

        std::vector<IdString> input_ports;
        std::vector<const PortInfo *> output_ports;
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
                    topographical_order.emplace_back(o->net);
                    for (int i = 0; i < clocks; i++) {
                        TimingClockingInfo clkInfo = ctx->getPortClockingInfo(cell.second.get(), o->name, i);
                        const NetInfo *clknet = get_net_or_empty(cell.second.get(), clkInfo.clock_port);
                        IdString clksig = clknet ? clknet->name : async_clock;
                        net_data[o->net][ClockEvent{clksig, clknet ? clkInfo.edge : RISING_EDGE}] =
                                TimingData{clkInfo.clockToQ.maxDelay()};
                    }

                } else {
                    if (portClass == TMG_STARTPOINT || portClass == TMG_GEN_CLOCK || portClass == TMG_IGNORE) {
                        topographical_order.emplace_back(o->net);
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
                        DelayInfo comb_delay;
                        bool is_path = ctx->getCellDelay(cell.second.get(), i, o->name, comb_delay);
                        if (is_path)
                            port_fanin[o]++;
                    }
                }
            }
        }

        // In out-of-context mode, handle top-level ports correctly
        if (bool_or_default(ctx->settings, ctx->id("arch.ooc"))) {
            for (auto &p : ctx->ports) {
                if (p.second.type != PORT_IN || p.second.net == nullptr)
                    continue;
                topographical_order.emplace_back(p.second.net);
            }
        }

        std::deque<NetInfo *> queue(topographical_order.begin(), topographical_order.end());
        // Now walk the design, from the start points identified previously, building up a topographical order
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
                    DelayInfo comb_delay;
                    bool is_path = ctx->getCellDelay(usr.cell, usr.port, port.first, comb_delay);
                    if (!is_path)
                        continue;
                    // Decrement the fanin count, and only add to topographical order if all its fanins have already
                    // been visited
                    auto it = port_fanin.find(&port.second);
                    NPNR_ASSERT(it != port_fanin.end());
                    if (--it->second == 0) {
                        topographical_order.emplace_back(port.second.net);
                        queue.emplace_back(port.second.net);
                        port_fanin.erase(it);
                    }
                }
            }
        }

        // Sanity check to ensure that all ports whereThat isn't to say that bounded model checks are bad. Although they
        // cannot prove that a design has no bugs, they can still find bugs. A classic example here would be the
        // riscv-formal project to formally verify RISC-V CPUs. In order to be generic to all RISC-V CPUs independent of
        // their architecture, the project does bounded model checks only. fanins were recorded were indeed visited
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

        // Go forwards topographically to find the maximum arrival time and max path length for each net
        for (auto net : topographical_order) {
            if (!net_data.count(net))
                continue;
            auto &nd_map = net_data.at(net);
            for (auto &startdomain : nd_map) {
                ClockEvent start_clk = startdomain.first;
                auto &nd = startdomain.second;
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
                            DelayInfo comb_delay;
                            // Look up delay through this path
                            bool is_path = ctx->getCellDelay(usr.cell, usr.port, port.first, comb_delay);
                            if (!is_path)
                                continue;
                            auto &data = net_data[port.second.net][start_clk];
                            auto &arrival = data.max_arrival;
                            arrival = std::max(arrival, usr_arrival + comb_delay.maxDelay());
                            if (!budget_override) { // Do not increment path length if budget overriden since it doesn't
                                // require a share of the slack
                                auto &path_length = data.max_path_length;
                                path_length = std::max(path_length, net_length_plus_one);
                            }
                        }
                    }
                }
            }
        }

        std::unordered_map<ClockPair, std::pair<delay_t, NetInfo *>> crit_nets;

        // Now go backwards topographically to determine the minimum path slack, and to distribute all path slack evenly
        // between all nets on the path
        for (auto net : boost::adaptors::reverse(topographical_order)) {
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
                            DelayInfo comb_delay;
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
                        DelayInfo comb_delay;
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

        if (net_crit) {
            NPNR_ASSERT(crit_path);
            // Go through in reverse topographical order to set required times
            for (auto net : boost::adaptors::reverse(topographical_order)) {
                if (!net_data.count(net))
                    continue;
                auto &nd_map = net_data.at(net);
                for (auto &startdomain : nd_map) {
                    auto &nd = startdomain.second;
                    if (nd.false_startpoint)
                        continue;
                    if (startdomain.first.clock == async_clock)
                        continue;
                    if (nd.min_required.empty())
                        nd.min_required.resize(net->users.size(), std::numeric_limits<delay_t>::max());
                    delay_t net_min_required = std::numeric_limits<delay_t>::max();
                    for (size_t i = 0; i < net->users.size(); i++) {
                        auto &usr = net->users.at(i);
                        auto net_delay = ctx->getNetinfoRouteDelay(net, usr);
                        int port_clocks;
                        TimingPortClass portClass = ctx->getPortTimingClass(usr.cell, usr.port, port_clocks);
                        if (portClass == TMG_REGISTER_INPUT || portClass == TMG_ENDPOINT) {
                            auto process_endpoint = [&](IdString clksig, ClockEdge edge, delay_t setup) {
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
                                nd.min_required.at(i) = std::min(period - setup, nd.min_required.at(i));
                            };
                            if (portClass == TMG_REGISTER_INPUT) {
                                for (int j = 0; j < port_clocks; j++) {
                                    TimingClockingInfo clkInfo = ctx->getPortClockingInfo(usr.cell, usr.port, j);
                                    const NetInfo *clknet = get_net_or_empty(usr.cell, clkInfo.clock_port);
                                    IdString clksig = clknet ? clknet->name : async_clock;
                                    process_endpoint(clksig, clknet ? clkInfo.edge : RISING_EDGE,
                                                     clkInfo.setup.maxDelay());
                                }
                            } else {
                                process_endpoint(async_clock, RISING_EDGE, 0);
                            }
                        }
                        net_min_required = std::min(net_min_required, nd.min_required.at(i) - net_delay);
                    }
                    PortRef &drv = net->driver;
                    if (drv.cell == nullptr)
                        continue;
                    for (const auto &port : drv.cell->ports) {
                        if (port.second.type != PORT_IN || !port.second.net)
                            continue;
                        DelayInfo comb_delay;
                        bool is_path = ctx->getCellDelay(drv.cell, port.first, drv.port, comb_delay);
                        if (!is_path)
                            continue;
                        int cc;
                        auto pclass = ctx->getPortTimingClass(drv.cell, port.first, cc);
                        if (pclass != TMG_COMB_INPUT)
                            continue;
                        NetInfo *sink_net = port.second.net;
                        if (net_data.count(sink_net) && net_data.at(sink_net).count(startdomain.first)) {
                            auto &sink_nd = net_data.at(sink_net).at(startdomain.first);
                            if (sink_nd.min_required.empty())
                                sink_nd.min_required.resize(sink_net->users.size(),
                                                            std::numeric_limits<delay_t>::max());
                            for (size_t i = 0; i < sink_net->users.size(); i++) {
                                auto &user = sink_net->users.at(i);
                                if (user.cell == drv.cell && user.port == port.first) {
                                    sink_nd.min_required.at(i) = std::min(sink_nd.min_required.at(i),
                                                                          net_min_required - comb_delay.maxDelay());
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            std::unordered_map<ClockEvent, delay_t> worst_slack;

            // Assign slack values
            for (auto &net_entry : net_data) {
                const NetInfo *net = net_entry.first;
                for (auto &startdomain : net_entry.second) {
                    auto &nd = startdomain.second;
                    if (startdomain.first.clock == async_clock)
                        continue;
                    if (nd.min_required.empty())
                        continue;
                    auto &nc = (*net_crit)[net->name];
                    if (nc.slack.empty())
                        nc.slack.resize(net->users.size(), std::numeric_limits<delay_t>::max());
#if 0
                    if (ctx->debug)
                        log_info("Net %s cd %s\n", net->name.c_str(ctx), startdomain.first.clock.c_str(ctx));
#endif
                    for (size_t i = 0; i < net->users.size(); i++) {
                        delay_t slack = nd.min_required.at(i) -
                                        (nd.max_arrival + ctx->getNetinfoRouteDelay(net, net->users.at(i)));
#if 0
                        if (ctx->debug)
                            log_info("    user %s.%s required %.02fns arrival %.02f route %.02f slack %.02f\n",
                                    net->users.at(i).cell->name.c_str(ctx), net->users.at(i).port.c_str(ctx),
                                    ctx->getDelayNS(nd.min_required.at(i)), ctx->getDelayNS(nd.max_arrival),
                                    ctx->getDelayNS(ctx->getNetinfoRouteDelay(net, net->users.at(i))), ctx->getDelayNS(slack));
#endif
                        if (worst_slack.count(startdomain.first))
                            worst_slack.at(startdomain.first) = std::min(worst_slack.at(startdomain.first), slack);
                        else
                            worst_slack[startdomain.first] = slack;
                        nc.slack.at(i) = slack;
                    }
                    if (ctx->debug)
                        log_break();
                }
            }
            // Assign criticality values
            for (auto &net_entry : net_data) {
                const NetInfo *net = net_entry.first;
                for (auto &startdomain : net_entry.second) {
                    if (startdomain.first.clock == async_clock)
                        continue;
                    auto &nd = startdomain.second;
                    if (nd.min_required.empty())
                        continue;
                    auto &nc = (*net_crit)[net->name];
                    if (nc.slack.empty())
                        continue;
                    if (nc.criticality.empty())
                        nc.criticality.resize(net->users.size(), 0);
                    // Only consider intra-clock paths for criticality
                    if (!crit_path->count(ClockPair{startdomain.first, startdomain.first}))
                        continue;
                    delay_t dmax = crit_path->at(ClockPair{startdomain.first, startdomain.first}).path_delay;
                    for (size_t i = 0; i < net->users.size(); i++) {
                        float criticality =
                                1.0f - ((float(nc.slack.at(i)) - float(worst_slack.at(startdomain.first))) / dmax);
                        nc.criticality.at(i) = std::min<double>(1.0, std::max<double>(0.0, criticality));
                    }
                    nc.max_path_length = nd.max_path_length;
                    nc.cd_worst_slack = worst_slack.at(startdomain.first);
                }
            }
#if 0
            if (ctx->debug) {
                for (auto &nc : *net_crit) {
                    NetInfo *net = ctx->nets.at(nc.first).get();
                    log_info("Net %s maxlen %d worst_slack %.02fns: \n", nc.first.c_str(ctx), nc.second.max_path_length,
                             ctx->getDelayNS(nc.second.cd_worst_slack));
                    if (!nc.second.criticality.empty() && !nc.second.slack.empty()) {
                        for (size_t i = 0; i < net->users.size(); i++) {
                            log_info("   user %s.%s slack %.02fns crit %.03f\n", net->users.at(i).cell->name.c_str(ctx),
                                     net->users.at(i).port.c_str(ctx), ctx->getDelayNS(nc.second.slack.at(i)),
                                     nc.second.criticality.at(i));
                        }
                    }
                    log_break();
                }
            }
#endif
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
            log_warning("No clocks found in design\n");
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
                DelayInfo comb_delay;
                if (clock_start != -1) {
                    auto clockInfo = ctx->getPortClockingInfo(driver_cell, driver.port, clock_start);
                    comb_delay = clockInfo.clockToQ;
                    clock_start = -1;
                } else if (last_port == driver.port) {
                    // Case where we start with a STARTPOINT etc
                    comb_delay = ctx->getDelayFromNS(0);
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
                    auto sink_wire = ctx->getNetinfoSinkWire(net, *sink);
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
                        log_info("                 %1.3f %s\n", ctx->getDelayNS(delay),
                                 ctx->getPipName(pip).c_str(ctx));
                        cursor = ctx->getPipSrcWire(pip);
                    }
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
        for (auto &clock : clock_reports)
            max_width = std::max<unsigned>(max_width, clock.first.str(ctx).size());
        for (auto &clock : clock_reports) {
            const auto &clock_name = clock.first.str(ctx);
            const int width = max_width - clock_name.size();
            float target = ctx->setting<float>("target_freq") / 1e6;
            if (ctx->nets.at(clock.first)->clkconstr)
                target = 1000 / ctx->getDelayNS(ctx->nets.at(clock.first)->clkconstr->period.minDelay());

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
            auto &bin = bins[(i.first - min_slack) / bin_size];
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

    // Test new timing analysis - WIP
    TimingData td;
    TimingAnalyser ta(ctx, &td);
    ta.reset_all();
    ta.get_net_delays();
    ta.reset_times();
    ta.calculate_times();
    ta.print_times();
}

void get_criticalities(Context *ctx, NetCriticalityMap *net_crit)
{
    CriticalPathMap crit_paths;
    net_crit->clear();
    Timing timing(ctx, true, true, &crit_paths, nullptr, net_crit);
    timing.walk_paths();
}

NEXTPNR_NAMESPACE_END

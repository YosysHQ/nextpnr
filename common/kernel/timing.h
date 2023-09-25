/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2023  rowanG077 <goemansrowan@gmail.com>
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

#ifndef TIMING_H
#define TIMING_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

struct CellPortKey
{
    CellPortKey(){};
    CellPortKey(IdString cell, IdString port) : cell(cell), port(port){};
    explicit CellPortKey(const PortRef &pr)
    {
        NPNR_ASSERT(pr.cell != nullptr);
        cell = pr.cell->name;
        port = pr.port;
    }
    IdString cell, port;
    unsigned int hash() const { return mkhash(cell.hash(), port.hash()); }
    inline bool operator==(const CellPortKey &other) const { return (cell == other.cell) && (port == other.port); }
    inline bool operator!=(const CellPortKey &other) const { return (cell != other.cell) || (port != other.port); }
    inline bool operator<(const CellPortKey &other) const
    {
        return cell == other.cell ? port < other.port : cell < other.cell;
    }
};

struct ClockDomainKey
{
    IdString clock;
    ClockEdge edge;
    ClockDomainKey(IdString clock_net, ClockEdge edge) : clock(clock_net), edge(edge){};
    // probably also need something here to deal with constraints
    inline bool is_async() const { return clock == IdString(); }

    unsigned int hash() const { return mkhash(clock.hash(), int(edge)); }

    inline bool operator==(const ClockDomainKey &other) const { return (clock == other.clock) && (edge == other.edge); }
};

typedef int domain_id_t;

struct ClockDomainPairKey
{
    domain_id_t launch, capture;
    ClockDomainPairKey(domain_id_t launch, domain_id_t capture) : launch(launch), capture(capture){};
    inline bool operator==(const ClockDomainPairKey &other) const
    {
        return (launch == other.launch) && (capture == other.capture);
    }
    unsigned int hash() const { return mkhash(launch, capture); }
};

struct TimingAnalyser
{
  public:
    TimingAnalyser(Context *ctx);

    void setup(bool update_net_timings = false, bool update_histogram = false, bool update_crit_paths = false);
    void run(bool update_route_delays = true, bool update_net_timings = false, bool update_histogram = false,
             bool update_crit_paths = false);

    // This is used when routers etc are not actually binding detailed routing (due to congestion or an abstracted
    // model), but want to re-run STA with their own calculated delays
    void set_route_delay(CellPortKey port, DelayPair value);

    float get_criticality(CellPortKey port) const { return ports.at(port).worst_crit; }
    float get_setup_slack(CellPortKey port) const { return ports.at(port).worst_setup_slack; }
    float get_domain_setup_slack(CellPortKey port) const
    {
        delay_t slack = std::numeric_limits<delay_t>::max();
        for (const auto &dp : ports.at(port).domain_pairs)
            slack = std::min(slack, domain_pairs.at(dp.first).worst_setup_slack);
        return slack;
    }

    dict<std::pair<IdString, IdString>, delay_t> get_clock_delays() const { return clock_delays; }

    TimingResult &get_timing_result() { return result; }

    bool setup_only = false;
    bool have_loops = false;
    bool updated_domains = false;

  private:
    void init_ports();
    void get_cell_delays();
    void get_route_delays();
    void topo_sort();
    void setup_port_domains();
    void identify_related_domains();

    void reset_times();

    void walk_forward();
    void walk_backward();

    void compute_slack();
    void compute_criticality();

    void build_detailed_net_timing_report();
    CriticalPath build_critical_path_report(domain_id_t domain_pair, CellPortKey endpoint);
    void build_crit_path_reports();
    void build_slack_histogram_report();

    dict<domain_id_t, delay_t> max_delay_by_domain_pairs();

    // get the N worst endpoints for a given domain pair
    std::vector<CellPortKey> get_worst_eps(domain_id_t domain_pair, int count);

    const DelayPair init_delay{std::numeric_limits<delay_t>::max(), std::numeric_limits<delay_t>::lowest()};

    // Set arrival/required times if more/less than the current value
    void set_arrival_time(CellPortKey target, domain_id_t domain, DelayPair arrival, int path_length,
                          CellPortKey prev = CellPortKey());
    void set_required_time(CellPortKey target, domain_id_t domain, DelayPair required, int path_length,
                           CellPortKey prev = CellPortKey());

    // To avoid storing the domain tag structure (which could get large when considering more complex constrained tag
    // cases), assign each domain an ID and use that instead
    // An arrival or required time entry. Stores both the min/max delays; and the traversal to reach them for critical
    // path reporting
    struct ArrivReqTime
    {
        DelayPair value;
        CellPortKey bwd_min, bwd_max;
        int path_length;
    };
    // Data per port-domain tuple
    struct PortDomainPairData
    {
        delay_t setup_slack = std::numeric_limits<delay_t>::max(), hold_slack = std::numeric_limits<delay_t>::max();
        int max_path_length = 0;
        float criticality = 0;
    };

    // A cell timing arc, used to cache cell timings and reduce the number of potentially-expensive Arch API calls
    struct CellArc
    {

        enum ArcType
        {
            COMBINATIONAL,
            SETUP,
            HOLD,
            CLK_TO_Q,
            STARTPOINT,
            ENDPOINT,
        } type;

        IdString other_port;
        DelayQuad value;
        // Clock polarity, not used for combinational arcs
        ClockEdge edge;

        CellArc(ArcType type, IdString other_port, DelayQuad value)
                : type(type), other_port(other_port), value(value), edge(RISING_EDGE){};
        CellArc(ArcType type, IdString other_port, DelayQuad value, ClockEdge edge)
                : type(type), other_port(other_port), value(value), edge(edge){};
    };

    // Timing data for every cell port
    struct PerPort
    {
        CellPortKey cell_port;
        PortType type;
        // per domain timings
        dict<domain_id_t, ArrivReqTime> arrival;
        dict<domain_id_t, ArrivReqTime> required;
        dict<domain_id_t, PortDomainPairData> domain_pairs;
        // cell timing arcs to (outputs)/from (inputs)  from this port
        std::vector<CellArc> cell_arcs;
        // routing delay into this port (input ports only)
        DelayPair route_delay{0};
        // worst criticality and slack across domain pairs
        float worst_crit = 0;
        delay_t worst_setup_slack = std::numeric_limits<delay_t>::max(),
                worst_hold_slack = std::numeric_limits<delay_t>::max();
    };

    struct PerDomain
    {
        PerDomain(ClockDomainKey key) : key(key){};
        ClockDomainKey key;
        // these are pairs (signal port; clock port)
        std::vector<std::pair<CellPortKey, IdString>> startpoints, endpoints;
    };

    struct PerDomainPair
    {
        PerDomainPair(ClockDomainPairKey key) : key(key){};
        ClockDomainPairKey key;
        DelayPair period{0};
        delay_t worst_setup_slack, worst_hold_slack;
    };

    CellInfo *cell_info(const CellPortKey &key);
    PortInfo &port_info(const CellPortKey &key);

    domain_id_t domain_id(IdString cell, IdString clock_port, ClockEdge edge);
    domain_id_t domain_id(const NetInfo *net, ClockEdge edge);
    domain_id_t domain_pair_id(domain_id_t launch, domain_id_t capture);

    void copy_domains(const CellPortKey &from, const CellPortKey &to, bool backwards);

    dict<CellPortKey, PerPort> ports;
    dict<ClockDomainKey, domain_id_t> domain_to_id;
    dict<ClockDomainPairKey, domain_id_t> pair_to_id;
    std::vector<PerDomain> domains;
    std::vector<PerDomainPair> domain_pairs;
    dict<std::pair<IdString, IdString>, delay_t> clock_delays;

    std::vector<CellPortKey> topological_order;

    domain_id_t async_clock_id;

    Context *ctx;

    TimingResult result;
};

// Perform timing analysis and optionaly print out slack histogram, fmax and critical paths
void timing_analysis(Context *ctx, bool slack_histogram = true, bool print_fmax = true, bool print_path = false,
                     bool warn_on_failure = false, bool update_results = false);

NEXTPNR_NAMESPACE_END

#endif

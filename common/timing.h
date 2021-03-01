/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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
    struct Hash
    {
        inline std::size_t operator()(const CellPortKey &arg) const noexcept
        {
            std::size_t seed = std::hash<IdString>()(arg.cell);
            seed ^= std::hash<IdString>()(arg.port) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
    inline bool operator==(const CellPortKey &other) const { return (cell == other.cell) && (port == other.port); }
    inline bool operator<(const CellPortKey &other) const
    {
        return cell == other.cell ? port < other.port : cell < other.cell;
    }
};

struct NetPortKey
{
    IdString net;
    size_t idx;
    NetPortKey(){};
    explicit NetPortKey(IdString net) : net(net), idx(DRIVER_IDX){};        // driver
    explicit NetPortKey(IdString net, size_t user) : net(net), idx(user){}; // user

    static const size_t DRIVER_IDX = std::numeric_limits<size_t>::max();

    inline bool is_driver() const { return (idx == DRIVER_IDX); }
    inline size_t user_idx() const
    {
        NPNR_ASSERT(idx != DRIVER_IDX);
        return idx;
    }

    struct Hash
    {
        std::size_t operator()(const NetPortKey &arg) const noexcept
        {
            std::size_t seed = std::hash<IdString>()(arg.net);
            seed ^= std::hash<size_t>()(arg.idx) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
    inline bool operator==(const NetPortKey &other) const { return (net == other.net) && (idx == other.idx); }
};

struct ClockDomainKey
{
    IdString clock;
    ClockEdge edge;
    // probably also need something here to deal with constraints
    inline bool is_async() const { return clock == IdString(); }

    struct Hash
    {
        std::size_t operator()(const ClockDomainKey &arg) const noexcept
        {
            std::size_t seed = std::hash<IdString>()(arg.clock);
            seed ^= std::hash<int>()(int(arg.edge)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
    inline bool operator==(const ClockDomainKey &other) const { return (clock == other.clock) && (edge == other.edge); }
};

struct TimingAnalyser
{
  public:
    TimingAnalyser(Context *ctx) : ctx(ctx){};
    void setup();

  private:
    void init_ports();
    void get_cell_delays();
    void topo_sort();
    // To avoid storing the domain tag structure (which could get large when considering more complex constrained tag
    // cases), assign each domain an ID and use that instead
    typedef int domain_id_t;
    // An arrival or required time entry. Stores both the min/max delays; and the traversal to reach them for critical
    // path reporting
    struct ArrivReqTime
    {
        DelayPair value;
        CellPortKey bwd_min, bwd_max;
        int path_length;
    };
    // Data per port-domain tuple
    struct PortDomainData
    {
        ArrivReqTime arrival, required;
        delay_t setup_slack = std::numeric_limits<delay_t>::max(), hold_slack = std::numeric_limits<delay_t>::max();
        delay_t budget = std::numeric_limits<delay_t>::max();
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
            CLK_TO_Q
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
        NetPortKey net_port;
        PortType type;
        // per domain timings
        std::unordered_map<domain_id_t, PortDomainData> domains;
        // cell timing arcs to (outputs)/from (inputs)  from this port
        std::vector<CellArc> cell_arcs;
        // routing delay into this port (input ports only)
        DelayPair route_delay;
    };

    CellInfo *cell_info(const CellPortKey &key);
    PortInfo &port_info(const CellPortKey &key);

    std::unordered_map<CellPortKey, PerPort, CellPortKey::Hash> ports;
    std::unordered_map<ClockDomainKey, domain_id_t, ClockDomainKey::Hash> domain_to_id;
    std::vector<ClockDomainKey> id_to_domain;

    std::vector<CellPortKey> topological_order;

    Context *ctx;
};

// Evenly redistribute the total path slack amongst all sinks on each path
void assign_budget(Context *ctx, bool quiet = false);

// Perform timing analysis and print out the fmax, and optionally the
//    critical path
void timing_analysis(Context *ctx, bool slack_histogram = true, bool print_fmax = true, bool print_path = false,
                     bool warn_on_failure = false);

// Data for the timing optimisation algorithm
struct NetCriticalityInfo
{
    // One each per user
    std::vector<delay_t> slack;
    std::vector<float> criticality;
    unsigned max_path_length = 0;
    delay_t cd_worst_slack = std::numeric_limits<delay_t>::max();
};

typedef std::unordered_map<IdString, NetCriticalityInfo> NetCriticalityMap;
void get_criticalities(Context *ctx, NetCriticalityMap *net_crit);

NEXTPNR_NAMESPACE_END

#endif

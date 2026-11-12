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

#ifndef GOWIN_CLOCK_PLAN_H
#define GOWIN_CLOCK_PLAN_H

#include <functional>
#include <string>
#include <vector>

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

struct GowinUtils;

// Chooses the BUFG bel, the clock spines and the entries into the clock network of every clock
// net before the global nets are routed.
//
// A BUFG feeds the clock network through its gate wire. In the central clock MUX a gate drives
// only some of the spines of a quadrant, the same indices in every quadrant, so two BUFGs whose
// gates share an index compete in every quadrant both have sinks in. The placer puts a BUFG on
// any BUFG bel near its driver, because a net behind a global buffer has no wirelength to it.
// Behind the spines the taps and branches of a column carry one spine each, but the inputs of
// the HCLK blocks and the PLLs are muxes that several spines reach, and the router takes them
// in the order it routes the nets. The planner replaces both choices with ones that know the
// clock network:
//  - every gate's reach is read from the routing graph, by a search downhill that stops at the
//    spines and the DCS inputs and keeps the path to each of them. The resources are the spines
//    and DCS inputs a gate drives and the wires on the way that more than one gate drives; wires
//    that lie on one path form a component, and a component belongs to one net,
//  - every sink's reach is read the same way, by a search uphill with the pip filter of the
//    router. The resources that reach a sink form its region, one quadrant in practice, two
//    for an HCLK or PLL input at the border of two quadrants. Resources that lie in the same
//    regions form a class, and one resource of a class serves every sink of a net whose region
//    contains the class. A net needs a set of classes that hits all its regions; the planner
//    tries the minimal such sets,
//  - a wire on the way from the spines to a sink that more than one resource reaches is a mux.
//    A mux that the sinks of two nets can reach is contested, and it belongs to one net. For a
//    sink behind contested muxes the planner lists its entries, each a class, the resources of
//    it that lead there and the contested wires on the way, and chooses one,
//  - a depth-first search decides first an entry for every contested sink, sinks that share a
//    wire one after the other, then a bel for each BUFG, its placed bel first and then the other
//    bels of each gate by distance, free bels before bels of BUFGs that may move as well, and a
//    hitting set for each net. Every partial assignment is checked by necessary conditions: a
//    bipartite matching of the demands, one per net and class, to the resources, where a net
//    that is not decided yet asks only for the classes every hitting set needs, and where one
//    undecided sink per net asks for the wire of an entry from a class the net needs anyway or
//    for a resource of another class, a flow from the BUFGs over the bels to the components, and
//    for every BUFG still without a bel a matching with it pinned to one of its gates. BUFGs
//    with the same regions, entries, roles and candidates are interchangeable and take their
//    gates in order. Within this model the search is complete: when the model has an assignment,
//    the search finds one. The model ends at the muxes before the sinks. The wires behind them
//    that only one net can reach are free before the globals are routed.
//  - a DQCE net behind a BUFG gets spines of its own from the same gate, a DHCEN net shares the
//    spines of its BUFG net, and a DCS gets one hardware DCS per quadrant whose clock inputs
//    every BUFG on them drives, one that the clock sources on them reach as well when there is one.
//  - a clock that enters the network without a BUFG (a PLL output, a clock pin) cannot move. It
//    is planned after the BUFGs with the spines its driver reaches, together with the DQCE, DHCEN
//    and DCS nets behind it. When no assignment has room for it, it is left to the router as it
//    is, and the nets behind it route after the planned nets.
// BUFGs fixed by the user are planned first and never move. A BUFG that no assignment can serve
// is an error, because the alternative is a clock that reaches part of its sinks through a second
// gate, with a delay the timing model does not know.
struct GowinClockPlanner
{
    // pip_filter(pip, source wire) is the filter the global router uses for the clock network
    GowinClockPlanner(Context *ctx, GowinUtils &gwu, std::function<bool(PipId, WireId)> pip_filter);

    // Plans every net in buf_nets and the clock sources in clk_nets that fit next to them. Fails
    // with log_error when a BUFG cannot get a gate with free spines for all its users.
    void plan(const std::vector<IdString> &buf_nets, const std::vector<IdString> &clk_nets);

    // A planned net may only use the resources planned for it, every other net may not use the
    // resources planned for someone else.
    bool allows(PipId pip, const NetInfo *net) const;

    // Whether the planner reserved wires for net
    bool is_planned(const NetInfo *net) const;

  private:
    using GateReach = dict<WireId, std::vector<WireId>>; // target -> wires from the gate to it, target included

    // The clock network uphill of a sink, up to the spines
    struct SinkReach
    {
        dict<WireId, pool<WireId>> parents; // wire -> the wires uphill of it
        dict<WireId, pool<int>> above;      // wire -> the resources that reach it
        pool<WireId> muxes;                 // the wires that more than one resource reaches
    };

    // One way into a sink behind contested muxes
    struct Entry
    {
        int cls;                    // the class the clock comes from
        std::vector<int> resources; // the resources of the class that lead to the sink
        std::vector<WireId> wires;  // the contested wires on the way
        WireId key;                 // the wire of them that most nets can reach, for the matching
    };

    struct ContestedSink
    {
        WireId sink;
        std::vector<Entry> entries; // in order of preference
        int choice = -1;            // the entry chosen by the search
    };

    // A net routed from a gate, a clock source or a DCS: the BUFG net or a DQCE net behind it
    struct Item
    {
        NetInfo *net;
        std::vector<WireId> sink_wires;          // the sinks the clock network reaches
        std::vector<pool<int>> regions;          // the distinct sets of resources that reach its sinks
        std::vector<pool<WireId>> region_spines; // the spines of every region
        std::vector<ContestedSink> contested;    // its sinks behind contested muxes
        std::vector<int> own_classes;            // the classes every minimal hitting set of its regions needs
        std::vector<int> cover;                  // the classes chosen by the search
        bool decided = false;
    };

    // One DCS with a BUFG or a clock source on a clock input
    struct DcsDemand
    {
        CellInfo *dcs;
        Item item;                                               // its output net, the regions are hardware DCS bels
        int first_input;                                         // the input route_dcs_net() routes from
        std::vector<std::pair<int, CellInfo *>> buffered_inputs; // clock input -> the BUFG driving it
        std::vector<std::pair<int, NetInfo *>> source_inputs;    // clock input -> the clock source net on it
    };

    struct Buffer
    {
        CellInfo *buf;
        PlaceStrength strength;
        bool fixed;          // placed by the user, stays where it is
        bool source = false; // a clock source without BUFG: its driver stays, and it never fails here
        bool active;         // has sinks or a DCS to plan for
        GateReach own_reach; // what the driver of a clock source reaches
        std::vector<int> own_components;
        std::vector<Item> items;
        std::vector<IdString> aliases;                     // nets behind a DHCEN, they share the spines of the BUFG net
        std::vector<std::vector<BelId>> choices;           // bels per gate in order of preference, see gather_choices()
        std::vector<int> choice_class;                     // the gate of each list in choices
        std::vector<std::pair<CellInfo *, int>> dcs_roles; // the DCS clock inputs this BUFG drives
        int sinks = 0;
        int twin = -1; // an earlier BUFG that is interchangeable with this one
        BelId bel;     // assigned by the search
    };

    // One class of one net, one quadrant of a DCS, or the entry of one undecided sink of a net
    struct Demand
    {
        int buffer = -1, item = -1;
        int dcs = -1;
        int cls = -1;             // -1 for an entry
        std::vector<int> options; // resources, or contested wires offset by the number of resources
    };

    // One contested sink the search decides an entry for
    struct Step
    {
        int buffer = -1, item = -1, dcs = -1;
        int sink;
    };

    bool is_spine(WireId wire) const;
    int spine_number(WireId spine) const;
    GateReach gate_reach(WireId gate) const;
    const SinkReach &sink_reach(WireId sink);
    void add_sinks(Item &item, NetInfo *net, bool dcs, int &sinks);
    void add_pseudo_users(Buffer &b, NetInfo *net);
    void add_dcs_demands(CellInfo *dcs);
    void find_contested();
    void add_entries(Item &item, WireId sink, bool dcs);
    void make_classes();
    WireId dcs_input_wire(BelId dcs_bel, int input) const;
    void gather_resources(std::vector<BelId> &bufg_bels, const std::vector<IdString> &clk_nets);
    void gather_demands(const std::vector<IdString> &buf_nets, const std::vector<IdString> &clk_nets);
    void gather_choices(Buffer &b, const std::vector<BelId> &bufg_bels);
    void find_twins(std::vector<int> &active);
    bool bel_taken(const std::vector<int> &active, int self, BelId bel) const;
    BelId buffer_bel(CellInfo *buf) const;
    const GateReach &reach_of(const Buffer &b, BelId bel) const;
    pool<int> drives(const std::vector<int> &active, int bi, BelId at) const;
    std::vector<int> options_in(const pool<int> &driven, int cls) const;
    std::vector<int> dcs_options(const DcsDemand &dd, int cls, int pinned, BelId pin) const;
    bool dcs_involved(const DcsDemand &dd, const std::vector<int> &active) const;
    bool hitting_sets(const Item &item, const std::function<bool(int)> &usable, const pool<int> &hit,
                      std::vector<std::vector<int>> &sets) const;
    bool item_demands(const Item &item, const std::function<std::vector<int>(int)> &options, int buffer, int ii,
                      int dcs, std::vector<Demand> &demands, std::string *why);
    bool buffer_demands(const std::vector<int> &active, int bi, BelId at, std::vector<Demand> &demands,
                        std::string *why);
    bool dcs_demands_of(const std::vector<int> &active, int pinned, BelId pin, std::vector<Demand> &demands,
                        std::string *why);
    Item &step_item(const Step &s);
    std::vector<int> step_options(const std::vector<int> &active, const Step &s, int cls, int pinned, BelId pin);
    bool entry_viable(const std::vector<int> &active, const Step &s, const Entry &e, int pinned, BelId pin);
    bool entry_demands(const std::vector<int> &active, const std::vector<Demand> &demands, int pinned, BelId pin,
                       std::vector<Demand> &entries, std::string *why);
    bool match_demands(const std::vector<Demand> &demands, std::vector<int> &match, std::string *why);
    bool feasible(const std::vector<int> &active, std::vector<Demand> &demands, std::vector<int> &match,
                  std::string *why = nullptr);
    bool flow_feasible(const std::vector<int> &active, const std::vector<Demand> &demands, std::string *why);
    void reset(const std::vector<int> &active);
    void gather_steps(const std::vector<int> &active);
    bool search_entries(const std::vector<int> &active, size_t k, std::vector<Demand> &demands,
                        std::vector<int> &match);
    bool search(const std::vector<int> &active, size_t idx, std::vector<Demand> &demands, std::vector<int> &match);
    bool decide_cover(const std::vector<int> &active, size_t idx, size_t ii, std::vector<Demand> &demands,
                      std::vector<int> &match);
    bool decide_dcs(const std::vector<int> &active, size_t di, std::vector<Demand> &demands, std::vector<int> &match);
    bool claim(const Entry &e, IdString net);
    void release(const Entry &e, IdString net);
    void reserve(IdString net, const std::vector<WireId> &wires);
    void commit(const std::vector<int> &planned, const std::vector<Demand> &demands, const std::vector<int> &match);
    std::string class_name(int cls) const;
    std::string net_list(const std::vector<Demand> &demands, const std::vector<int> &which) const;
    std::string explain(const std::vector<int> &active);

    Context *ctx;
    GowinUtils &gwu;
    std::function<bool(PipId, WireId)> pip_filter;

    pool<WireId> shared_wires;              // the resources: wires of the clock MUX that a planned net owns
    dict<WireId, IdString> reserved_wires;  // shared wire -> the net it is planned for
    dict<IdString, pool<WireId>> net_plans; // planned net -> the shared wires it may use
    dict<WireId, SinkReach> reach_cache;    // sink -> the clock network uphill of it

    dict<BelId, GateReach> reach;                 // BUFG bel -> what its gate drives
    dict<IdString, GateReach> source_reach;       // clock source net -> what its driver drives
    dict<BelId, int> bel_class;                   // BUFG bel -> gate: bels with the same reach share a class
    dict<BelId, std::vector<int>> bel_components; // BUFG bel -> components its gate reaches
    dict<WireId, int> component;                  // shared wire -> component, a resource that one net owns
    std::vector<int> component_rank;              // component -> order of preference, the lowest spine first
    std::vector<BelId> dcs_bels;                  // hardware DCS bels, the resources behind the components
    dict<BelId, int> dcs_bel_index;
    dict<WireId, int> resource_of_spine;             // spine -> the component or hardware DCS it belongs to
    dict<int, std::vector<WireId>> component_spines; // component -> its spines
    std::vector<int> resource_class;                 // resource -> class, -1 when no sink has it in its region
    std::vector<std::vector<int>> class_resources;   // class -> its resources in order of preference
    std::vector<int> class_rank;                     // class -> rank of its best resource
    dict<WireId, pool<IdString>> mux_nets;           // mux wire -> the nets whose sinks it can reach
    pool<WireId> contested_wires;                    // muxes that two nets can reach
    dict<WireId, int> wire_index;                    // contested wire -> its index behind the resources
    std::vector<WireId> contested_by_index;          // the contested wires in index order
    dict<WireId, std::pair<IdString, int>> claimed;  // contested wire -> the net that chose it, and how often
    std::vector<Buffer> buffers;
    std::vector<DcsDemand> dcs_demands;
    std::vector<Step> steps; // the contested sinks of one search, in the order their entries are decided
    int checks = 0;          // feasibility checks of the search, for the debug output
};

NEXTPNR_NAMESPACE_END

#endif

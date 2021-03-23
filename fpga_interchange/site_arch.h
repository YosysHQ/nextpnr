/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
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

#ifndef SITE_ARCH_H
#define SITE_ARCH_H

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "PhysicalNetlist.capnp.h"
#include "arch_iterators.h"
#include "chipdb.h"
#include "hash_table.h"
#include "log.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context;

struct SiteInformation
{
    const Context *ctx;

    const int32_t tile;
    const int32_t tile_type;
    const int32_t site;
    const std::unordered_set<CellInfo *> &cells_in_site;

    SiteInformation(const Context *ctx, int32_t tile, int32_t site,
                    const std::unordered_set<CellInfo *> &cells_in_site);

    inline const ChipInfoPOD &chip_info() const NPNR_ALWAYS_INLINE;

    inline bool is_wire_in_site(WireId wire) const NPNR_ALWAYS_INLINE;

    inline bool is_bel_in_site(BelId bel) const NPNR_ALWAYS_INLINE;

    inline bool is_pip_part_of_site(PipId pip) const NPNR_ALWAYS_INLINE;

    inline bool is_site_port(PipId pip) const NPNR_ALWAYS_INLINE;
};

// Site routing needs a modification of the routing graph.  Within the site,
// the arch can be consulted for edges.  However the rest of the routing graph
// needs to be reduced for analysis purposes.  Wires within the site are
// SITE_WIRE's.  4 additional nodes are introduced to model out of site
// routing:
//  - OUT_OF_SITE_SOURCE / OUT_OF_SITE_SINK
//   - These represent net sources and sinks that are only reachable via the
//     routing graph (e.g. outside of the site).
//  - SITE_PORT_SOURCE / SITE_PORT_SINK
//   - These represent the routing resources connected to other side of site
//     ports.
//
//  The non-site wire graph is connected like:
//
// ┌─────────────────┐                          ┌────────────────────┐
// │                 │                          │                    │
// │ OUT_OF_SITE_SRC │                          │  OUT_OF_SITE_SINK  │◄────┐
// │                 │                          │                    │     │
// └┬────────────────┘                          └────────────────────┘     │
//  │                                                                      │
//  │                ┌─────────────────────────────────────────────────────┤
//  │                │                                                     │
//  │                │                                                     │
//  │                │                                                     │
//  │                │                                                     │
//  │                ▼                                                     │
//  │      ┌─────────────────┐   ┌─────────────┐       ┌────────────────┐  │
//  │      │                 │   │             │       │                │  │
//  └─────►│  SITE_PORT_SRC  ├──►│    Site     ├──────►│ SITE_PORT_SINK ├──┘
//         │                 │   │             │       │                │
//         └─────────────────┘   └─────────────┘       └────────────────┘
//
struct SiteWire
{
    enum Type
    {
        // This wire is just a plain site wire.
        SITE_WIRE = 0,
        // This wire is a source that is from outside of the site.
        OUT_OF_SITE_SOURCE = 1,
        // This wire is a sink that is from outside of the site.
        OUT_OF_SITE_SINK = 2,
        // This wire is the routing graph wire on the dst side of a site port.
        SITE_PORT_SINK = 3,
        // This wire is the routing graph wire on the src side of a site port.
        SITE_PORT_SOURCE = 4,
        NUMBER_SITE_WIRE_TYPES = 5,
    };

    static inline SiteWire make(const SiteInformation *site_info, WireId site_wire) NPNR_ALWAYS_INLINE;

    static SiteWire make(const SiteInformation *site_info, PortType port_type, NetInfo *net) NPNR_ALWAYS_INLINE
    {
        SiteWire out;
        if (port_type == PORT_OUT) {
            out.type = OUT_OF_SITE_SOURCE;
            out.net = net;
        } else {
            out.type = OUT_OF_SITE_SINK;
            out.net = net;
        }
        return out;
    }

    static inline SiteWire make_site_port(const SiteInformation *site_info, PipId pip, bool dst_wire);

    bool operator==(const SiteWire &other) const
    {
        return wire == other.wire && type == other.type && pip == other.pip && net == other.net;
    }
    bool operator!=(const SiteWire &other) const
    {
        return wire != other.wire || type != other.type || pip != other.pip || net != other.net;
    }
    bool operator<(const SiteWire &other) const
    {
        return std::make_tuple(type, wire, pip, net) < std::make_tuple(other.type, other.wire, other.pip, other.net);
    }

    Type type = NUMBER_SITE_WIRE_TYPES;
    WireId wire;
    PipId pip;
    NetInfo *net = nullptr;
};

struct SitePip
{
    enum Type
    {
        // This is a plain regular site pip.
        SITE_PIP = 0,
        // This pip is a site port, and connects a SITE_WIRE to a SITE_PORT_SINK/SITE_PORT_SRC
        SITE_PORT = 1,
        // This pip connects a OUT_OF_SITE_SOURCE to a SITE_PORT_SRC
        SOURCE_TO_SITE_PORT = 2,
        // This pip connects a SITE_PORT_SINK to a OUT_OF_SITE_SINK
        SITE_PORT_TO_SINK = 3,
        // This pip connects a SITE_PORT_SINK to a SITE_PORT_SRC.
        SITE_PORT_TO_SITE_PORT = 4,
        INVALID_TYPE = 5,
    };

    static inline SitePip make(const SiteInformation *site_info, PipId pip);

    static SitePip make(const SiteInformation *site_info, SiteWire src, PipId dst)
    {
        NPNR_ASSERT(src.type == SiteWire::OUT_OF_SITE_SOURCE);

        SitePip out;
        out.type = SOURCE_TO_SITE_PORT;
        out.pip = dst;
        out.wire = src;

        return out;
    }

    static SitePip make(const SiteInformation *site_info, PipId src, SiteWire dst)
    {
        NPNR_ASSERT(dst.type == SiteWire::OUT_OF_SITE_SINK);

        SitePip out;
        out.type = SITE_PORT_TO_SINK;
        out.pip = src;
        out.wire = dst;

        return out;
    }

    static SitePip make(const SiteInformation *site_info, PipId src_pip, PipId dst_pip)
    {
        SitePip out;
        out.type = SITE_PORT_TO_SITE_PORT;
        out.pip = src_pip;
        out.other_pip = dst_pip;

        return out;
    }

    Type type = INVALID_TYPE;
    // For SITE_PORT_TO_SITE_PORT connections, pip is the site -> routing pip.
    PipId pip;
    SiteWire wire;
    // For SITE_PORT_TO_SITE_PORT connections, other_pip is the routing ->
    // site pip.
    PipId other_pip;

    bool operator==(const SitePip &other) const
    {
        return type == other.type && pip == other.pip && wire == other.wire && other_pip == other.other_pip;
    }
    bool operator!=(const SitePip &other) const
    {
        return type != other.type || pip != other.pip || wire != other.wire || other_pip != other.other_pip;
    }
};
NEXTPNR_NAMESPACE_END

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX SiteWire>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX SiteWire &site_wire) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX SiteWire::Type>()(site_wire.type));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX WireId>()(site_wire.wire));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX PipId>()(site_wire.pip));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX NetInfo *>()(site_wire.net));
        return seed;
    }
};

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX SitePip>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX SitePip &site_pip) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX SitePip::Type>()(site_pip.type));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX PipId>()(site_pip.pip));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX SiteWire>()(site_pip.wire));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX PipId>()(site_pip.other_pip));
        return seed;
    }
};

NEXTPNR_NAMESPACE_BEGIN

struct SitePipDownhillRange;
struct SitePipUphillRange;
struct SiteWireRange;
struct SiteNetInfo;

struct SitePipMap
{
    SitePip pip;
    size_t count;
};

struct SiteNetMap
{
    SiteNetInfo *net;
    size_t count;
};

struct SiteNetInfo
{
    NetInfo *net;
    SiteWire driver;
    HashTables::HashSet<SiteWire> users;

    HashTables::HashMap<SiteWire, SitePipMap> wires;
};

struct SiteArch
{
    const Context *const ctx;
    const SiteInformation *const site_info;

    HashTables::HashMap<NetInfo *, SiteNetInfo> nets;
    HashTables::HashMap<SiteWire, SiteNetMap> wire_to_nets;

    std::vector<PipId> input_site_ports;
    std::vector<PipId> output_site_ports;

    std::vector<SiteWire> out_of_site_sources;
    std::vector<SiteWire> out_of_site_sinks;

    SiteArch(const SiteInformation *site_info);

    inline SiteWire getPipSrcWire(const SitePip &site_pip) const NPNR_ALWAYS_INLINE;
    inline SiteWire getPipDstWire(const SitePip &site_pip) const NPNR_ALWAYS_INLINE;

    // Does this site pip always invert its signal?
    inline bool isInverting(const SitePip &site_pip) const NPNR_ALWAYS_INLINE;

    // Can this site pip optional invert its signal?
    inline bool canInvert(const SitePip &site_pip) const NPNR_ALWAYS_INLINE;

    // For a site port, returns the preferred constant net type.
    //
    // If no preference, then NetType is SIGNAL.
    inline PhysicalNetlist::PhysNetlist::NetType prefered_constant_net_type(const SitePip &site_pip) const;

    inline SitePipDownhillRange getPipsDownhill(const SiteWire &site_wire) const NPNR_ALWAYS_INLINE;
    inline SitePipUphillRange getPipsUphill(const SiteWire &site_wire) const NPNR_ALWAYS_INLINE;
    SiteWireRange getWires() const;

    inline SiteWire getBelPinWire(BelId bel, IdString pin) const NPNR_ALWAYS_INLINE;
    inline PortType getBelPinType(BelId bel, IdString pin) const NPNR_ALWAYS_INLINE;

    const char *nameOfWire(const SiteWire &wire) const;
    const char *nameOfPip(const SitePip &pip) const;
    const char *nameOfNet(const SiteNetInfo *net) const;

    bool debug() const;

    bool bindWire(const SiteWire &wire, SiteNetInfo *net)
    {
        auto result = wire_to_nets.emplace(wire, SiteNetMap{net, 1});
        if (result.first->second.net != net) {
            if (debug()) {
                log_info("Net conflict binding wire %s to net %s, conflicts with net %s\n", nameOfWire(wire),
                         nameOfNet(net), nameOfNet(result.first->second.net));
            }
            return false;
        }

        if (!result.second) {
            result.first->second.count += 1;
        }

        return true;
    }

    SiteNetInfo *unbindWire(const SiteWire &wire)
    {
        auto iter = wire_to_nets.find(wire);
        NPNR_ASSERT(iter != wire_to_nets.end());
        NPNR_ASSERT(iter->second.count >= 1);
        SiteNetInfo *net = iter->second.net;
        iter->second.count -= 1;

        if (iter->second.count == 0) {
            wire_to_nets.erase(iter);
        }

        return net;
    }

    bool bindPip(const SitePip &pip, SiteNetInfo *net);
    void unbindPip(const SitePip &pip);

    void archcheck();

    bool is_pip_synthetic(const SitePip &pip) const NPNR_ALWAYS_INLINE;
    SyntheticType pip_synthetic_type(const SitePip &pip) const NPNR_ALWAYS_INLINE;
};

struct SitePipDownhillIterator
{
    enum DownhillIteratorState
    {
        // Initial state
        BEGIN = 0,
        // Iterating over normal pips.
        NORMAL_PIPS = 1,
        // Iterating off all site port sources.
        PORT_SINK_TO_PORT_SRC = 2,
        // Iterating over out of site sinks.
        OUT_OF_SITE_SINKS = 3,
        // Iterating off all site port sources.
        OUT_OF_SITE_SOURCE_TO_PORT_SRC = 4,
        SITE_PORT = 5,
        END = 6,
        NUMBER_STATES = 7,
    };

    DownhillIteratorState state = BEGIN;
    const SiteArch *site_arch;
    SiteWire site_wire;
    const RelSlice<int32_t> *pips_downhill;
    size_t cursor;

    bool advance_in_state() NPNR_ALWAYS_INLINE
    {
        switch (state) {
        case BEGIN:
            return false;
        case NORMAL_PIPS:
            ++cursor;
            return (cursor < pips_downhill->size());
        case PORT_SINK_TO_PORT_SRC:
            ++cursor;
            return (cursor < site_arch->input_site_ports.size());
        case OUT_OF_SITE_SINKS:
            ++cursor;
            return (cursor < site_arch->out_of_site_sinks.size());
        case OUT_OF_SITE_SOURCE_TO_PORT_SRC:
            ++cursor;
            return (cursor < site_arch->input_site_ports.size());
        case SITE_PORT:
            ++cursor;
            return false;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    bool check_first() const NPNR_ALWAYS_INLINE
    {
        switch (state) {
        case BEGIN:
            return false;
        case NORMAL_PIPS:
            return (cursor < pips_downhill->size());
        case PORT_SINK_TO_PORT_SRC:
            return (cursor < site_arch->input_site_ports.size());
        case OUT_OF_SITE_SINKS:
            return (cursor < site_arch->out_of_site_sinks.size());
        case OUT_OF_SITE_SOURCE_TO_PORT_SRC:
            return (cursor < site_arch->input_site_ports.size());
        case SITE_PORT:
            return true;
        case END:
            return true;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    const std::array<std::array<DownhillIteratorState, NUMBER_STATES>, SiteWire::NUMBER_SITE_WIRE_TYPES>
    get_state_table() const
    {
        std::array<std::array<DownhillIteratorState, NUMBER_STATES>, SiteWire::NUMBER_SITE_WIRE_TYPES> state_table;
        for (size_t j = 0; j < SiteWire::NUMBER_SITE_WIRE_TYPES; ++j) {
            for (size_t i = 0; i < NUMBER_STATES; ++i) {
                state_table[j][i] = NUMBER_STATES;
            }
        }

        state_table[SiteWire::SITE_WIRE][BEGIN] = NORMAL_PIPS;
        state_table[SiteWire::SITE_WIRE][NORMAL_PIPS] = END;

        state_table[SiteWire::OUT_OF_SITE_SOURCE][BEGIN] = OUT_OF_SITE_SOURCE_TO_PORT_SRC;
        state_table[SiteWire::OUT_OF_SITE_SOURCE][OUT_OF_SITE_SOURCE_TO_PORT_SRC] = END;

        state_table[SiteWire::OUT_OF_SITE_SINK][BEGIN] = END;

        state_table[SiteWire::SITE_PORT_SINK][BEGIN] = PORT_SINK_TO_PORT_SRC;
        state_table[SiteWire::SITE_PORT_SINK][PORT_SINK_TO_PORT_SRC] = OUT_OF_SITE_SINKS;
        state_table[SiteWire::SITE_PORT_SINK][OUT_OF_SITE_SINKS] = END;

        state_table[SiteWire::SITE_PORT_SOURCE][BEGIN] = SITE_PORT;
        state_table[SiteWire::SITE_PORT_SOURCE][SITE_PORT] = END;

        return state_table;
    }

    void advance_state() NPNR_ALWAYS_INLINE
    {
        state = get_state_table().at(site_wire.type).at(state);
        cursor = 0;
        NPNR_ASSERT(state >= BEGIN && state <= END);
    }

    void operator++() NPNR_ALWAYS_INLINE
    {
        NPNR_ASSERT(state != END);
        while (state != END) {
            if (advance_in_state()) {
                break;
            } else {
                advance_state();
                if (check_first()) {
                    break;
                }
            }
        }
    }

    bool operator!=(const SitePipDownhillIterator &other) const
    {
        return state != other.state || cursor != other.cursor;
    }

    inline SitePip operator*() const NPNR_ALWAYS_INLINE;
};

struct SitePipDownhillRange
{
    const SiteArch *site_arch;
    SiteWire site_wire;

    SitePipDownhillRange(const SiteArch *site_arch, const SiteWire &site_wire)
            : site_arch(site_arch), site_wire(site_wire)
    {
    }

    inline const RelSlice<int32_t> *init_pip_range() const NPNR_ALWAYS_INLINE;

    inline SitePipDownhillIterator begin() const NPNR_ALWAYS_INLINE;

    SitePipDownhillIterator end() const NPNR_ALWAYS_INLINE
    {
        SitePipDownhillIterator e;
        e.state = SitePipDownhillIterator::END;
        e.cursor = 0;

        return e;
    }
};

struct SitePipUphillIterator
{
    enum UphillIteratorState
    {
        // Initial state
        BEGIN = 0,
        // Iterating over normal pips.
        NORMAL_PIPS = 1,
        // Iterating off all site port sources.
        PORT_SRC_TO_PORT_SINK = 2,
        // Iterating over out of site sinks.
        OUT_OF_SITE_SOURCES = 3,
        // Iterating off all site port sources.
        OUT_OF_SITE_SINK_TO_PORT_SINK = 4,
        SITE_PORT = 5,
        END = 6,
        NUMBER_STATES = 7,
    };

    UphillIteratorState state = BEGIN;
    const SiteArch *site_arch;
    SiteWire site_wire;
    size_t cursor;
    UphillPipIterator iter;
    UphillPipIterator uphill_end;

    bool advance_in_state()
    {
        switch (state) {
        case BEGIN:
            return false;
        case NORMAL_PIPS:
            while (iter != uphill_end) {
                ++iter;
                if (!(iter != uphill_end)) {
                    break;
                }
            }

            return false;
        case PORT_SRC_TO_PORT_SINK:
            ++cursor;
            return (cursor < site_arch->output_site_ports.size());
        case OUT_OF_SITE_SOURCES:
            ++cursor;
            return (cursor < site_arch->out_of_site_sources.size());
        case OUT_OF_SITE_SINK_TO_PORT_SINK:
            ++cursor;
            return (cursor < site_arch->output_site_ports.size());
        case SITE_PORT:
            ++cursor;
            return false;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    bool check_first() const
    {
        switch (state) {
        case BEGIN:
            return false;
        case NORMAL_PIPS:
            if (!(iter != uphill_end)) {
                return false;
            } else {
                return true;
            }
        case PORT_SRC_TO_PORT_SINK:
            return (cursor < site_arch->output_site_ports.size());
        case OUT_OF_SITE_SOURCES:
            return (cursor < site_arch->out_of_site_sources.size());
        case OUT_OF_SITE_SINK_TO_PORT_SINK:
            return (cursor < site_arch->output_site_ports.size());
        case SITE_PORT:
            return true;
        case END:
            return true;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    const std::array<std::array<UphillIteratorState, NUMBER_STATES>, SiteWire::NUMBER_SITE_WIRE_TYPES>
    get_state_table() const
    {
        std::array<std::array<UphillIteratorState, NUMBER_STATES>, SiteWire::NUMBER_SITE_WIRE_TYPES> state_table;
        for (size_t j = 0; j < SiteWire::NUMBER_SITE_WIRE_TYPES; ++j) {
            for (size_t i = 0; i < NUMBER_STATES; ++i) {
                state_table[j][i] = NUMBER_STATES;
            }
        }

        state_table[SiteWire::SITE_WIRE][BEGIN] = NORMAL_PIPS;
        state_table[SiteWire::SITE_WIRE][NORMAL_PIPS] = END;

        state_table[SiteWire::OUT_OF_SITE_SOURCE][BEGIN] = END;

        state_table[SiteWire::OUT_OF_SITE_SINK][BEGIN] = OUT_OF_SITE_SINK_TO_PORT_SINK;
        state_table[SiteWire::OUT_OF_SITE_SINK][OUT_OF_SITE_SINK_TO_PORT_SINK] = END;

        state_table[SiteWire::SITE_PORT_SINK][BEGIN] = SITE_PORT;
        state_table[SiteWire::SITE_PORT_SINK][SITE_PORT] = END;

        state_table[SiteWire::SITE_PORT_SOURCE][BEGIN] = PORT_SRC_TO_PORT_SINK;
        state_table[SiteWire::SITE_PORT_SOURCE][PORT_SRC_TO_PORT_SINK] = OUT_OF_SITE_SOURCES;
        state_table[SiteWire::SITE_PORT_SOURCE][OUT_OF_SITE_SOURCES] = END;

        return state_table;
    }

    void advance_state()
    {
        state = get_state_table().at(site_wire.type).at(state);
        cursor = 0;
        NPNR_ASSERT(state >= BEGIN && state <= END);
    }

    void operator++()
    {
        NPNR_ASSERT(state != END);
        while (state != END) {
            if (advance_in_state()) {
                break;
            } else {
                advance_state();
                if (check_first()) {
                    break;
                }
            }
        }
    }

    bool operator!=(const SitePipUphillIterator &other) const
    {
        return state != other.state || cursor != other.cursor || iter != other.iter;
    }

    SitePip operator*() const;
};

struct SitePipUphillRange
{
    const SiteArch *site_arch;
    SiteWire site_wire;
    UphillPipRange pip_range;

    SitePipUphillRange(const SiteArch *site_arch, SiteWire site_wire);

    SitePipUphillIterator begin() const
    {
        SitePipUphillIterator b;
        b.state = SitePipUphillIterator::BEGIN;
        b.site_arch = site_arch;
        b.site_wire = site_wire;
        b.cursor = 0;
        b.iter = pip_range.b;
        b.uphill_end = pip_range.e;

        ++b;

        return b;
    }

    SitePipUphillIterator end() const
    {
        SitePipUphillIterator e;
        e.state = SitePipUphillIterator::END;
        e.site_arch = site_arch;
        e.site_wire = site_wire;
        e.cursor = 0;
        e.iter = pip_range.e;
        e.uphill_end = pip_range.e;

        return e;
    }
};

inline SitePipDownhillRange SiteArch::getPipsDownhill(const SiteWire &site_wire) const
{
    return SitePipDownhillRange(this, site_wire);
}

inline SitePipUphillRange SiteArch::getPipsUphill(const SiteWire &site_wire) const
{
    return SitePipUphillRange(this, site_wire);
}

struct SiteWireIterator
{
    enum SiteWireIteratorState
    {
        // Initial state
        BEGIN = 0,
        NORMAL_WIRES = 1,
        INPUT_SITE_PORTS = 2,
        OUTPUT_SITE_PORTS = 3,
        OUT_OF_SITE_SOURCES = 4,
        OUT_OF_SITE_SINKS = 5,
        END = 6,
    };

    SiteWireIteratorState state = BEGIN;
    const SiteArch *site_arch;
    const TileTypeInfoPOD *tile_type;
    size_t cursor = 0;

    bool advance_in_state()
    {
        switch (state) {
        case BEGIN:
            return false;
        case NORMAL_WIRES:
            while (true) {
                ++cursor;
                if (cursor >= tile_type->wire_data.size()) {
                    return false;
                }
                if (tile_type->wire_data[cursor].site == site_arch->site_info->site) {
                    return true;
                }
            }
        case INPUT_SITE_PORTS:
            ++cursor;
            return (cursor < site_arch->input_site_ports.size());
        case OUTPUT_SITE_PORTS:
            ++cursor;
            return (cursor < site_arch->output_site_ports.size());
        case OUT_OF_SITE_SOURCES:
            ++cursor;
            return (cursor < site_arch->out_of_site_sources.size());
        case OUT_OF_SITE_SINKS:
            ++cursor;
            return (cursor < site_arch->out_of_site_sinks.size());
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    // See if initial value in state is good.
    bool check_first() const
    {
        switch (state) {
        case BEGIN:
            return false;
        case NORMAL_WIRES:
            if (cursor >= tile_type->wire_data.size()) {
                return false;
            }
            return tile_type->wire_data[cursor].site == site_arch->site_info->site;
        case INPUT_SITE_PORTS:
            return (cursor < site_arch->input_site_ports.size());
        case OUTPUT_SITE_PORTS:
            return (cursor < site_arch->output_site_ports.size());
        case OUT_OF_SITE_SOURCES:
            return (cursor < site_arch->out_of_site_sources.size());
        case OUT_OF_SITE_SINKS:
            return (cursor < site_arch->out_of_site_sinks.size());
        case END:
            return true;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    void advance_state()
    {
        NPNR_ASSERT(state >= BEGIN && state < END);
        state = static_cast<SiteWireIteratorState>(state + 1);
        cursor = 0;
        NPNR_ASSERT(state >= BEGIN && state <= END);
    }

    void operator++()
    {
        NPNR_ASSERT(state != END);
        while (state != END) {
            if (advance_in_state()) {
                break;
            } else {
                advance_state();
                if (check_first()) {
                    break;
                }
            }
        }
    }

    bool operator!=(const SiteWireIterator &other) const { return state != other.state || cursor != other.cursor; }

    SiteWire operator*() const;
};

struct SiteWireRange
{
    const SiteArch *site_arch;
    SiteWireRange(const SiteArch *site_arch) : site_arch(site_arch) {}
    SiteWireIterator begin() const;

    SiteWireIterator end() const
    {
        SiteWireIterator e;

        e.state = SiteWireIterator::END;

        return e;
    }
};

inline SiteWireRange SiteArch::getWires() const { return SiteWireRange(this); }

NEXTPNR_NAMESPACE_END

#endif /* SITE_ARCH_H */

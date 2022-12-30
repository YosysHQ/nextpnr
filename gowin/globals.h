/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2022  YRabbit <rabbit@yrabbit.cyou>
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

#ifndef GOWIN_GLOBALS_H
#define GOWIN_GLOBALS_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

class GowinGlobalRouter
{
  public:
    GowinGlobalRouter() {}

  private:
    // wire -> clock#
    dict<WireId, int> used_wires;

    // ordered nets
    struct globalnet_t
    {
        IdString name;
        int clock_ports;
        BelId clock_bel;
        WireId clock_wire; // clock wire if there is one
        int clock;         // clock #

        globalnet_t()
        {
            name = IdString();
            clock_ports = 0;
            clock_bel = BelId();
            clock_wire = WireId();
            clock = -1;
        }
        globalnet_t(IdString _name)
        {
            name = _name;
            clock_ports = 0;
            clock_bel = BelId();
            clock_wire = WireId();
            clock = -1;
        }

        // sort
        bool operator<(const globalnet_t &other) const
        {
            if ((clock_wire != WireId()) ^ (other.clock_wire != WireId())) {
                return !(clock_wire != WireId());
            }
            return clock_ports < other.clock_ports;
        }
        // search
        bool operator==(const globalnet_t &other) const { return name == other.name; }
    };

    // discovered nets
    std::vector<globalnet_t> nets;

    bool is_clock_port(PortRef const &user);
    std::pair<WireId, BelId> clock_src(Context *ctx, PortRef const &driver);
    void gather_clock_nets(Context *ctx, std::vector<globalnet_t> &clock_nets);
    IdString route_to_non_clock_port(Context *ctx, WireId const dstWire, int clock, pool<IdString> &used_pips,
                                     pool<IdString> &undo_wires);
    void route_net(Context *ctx, globalnet_t const &net);

  public:
    void mark_globals(Context *ctx);
    void route_globals(Context *ctx);
};

NEXTPNR_NAMESPACE_END
#endif // GOWIN_GLOBALS_H

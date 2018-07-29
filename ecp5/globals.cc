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

#include "nextpnr.h"
#include <algorithm>

NEXTPNR_NAMESPACE_BEGIN

class Ecp5GlobalRouter
{
  public:
    Ecp5GlobalRouter(Context *ctx) : ctx(ctx){};
  private:

    bool is_clock_port(const PortRef &user) {
        if (user.cell->type == ctx->id("TRELLIS_LC") && user.port == ctx->id("CLK"))
            return true;
        return false;
    }

    std::vector<NetInfo*> get_clocks()
    {
        std::unordered_map<IdString, int> clockCount;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            clockCount[ni->name] = 0;
            for (const auto &user : ni->users) {
                if (is_clock_port(user))
                    clockCount[ni->name]++;
            }
        }
        std::vector<NetInfo*> clocks;
        while (clocks.size() < 16) {
            auto max = std::max_element(clockCount.begin(), clockCount.end(), [](
                    const decltype(clockCount)::value_type &a, const decltype(clockCount)::value_type &b
                    ) {
                return a.second < b.second;
            });
            if (max == clockCount.end() || max->second < 3)
                break;
            clocks.push_back(ctx->nets.at(max->first).get());
            clockCount.erase(max->first);
        }
        return clocks;
    }

    PipId find_tap_pip(WireId tile_glb)
    {
        std::string wireName = ctx->getWireBasename(tile_glb).str(ctx);
        std::string glbName = wireName.substr(2);
        TapDirection td = ctx->globalInfoAtLoc(tile_glb.location).tap_dir;
        WireId tap_wire;
        Location tap_loc;
        tap_loc.x = ctx->globalInfoAtLoc(tile_glb.location).tap_col;
        tap_loc.y = tile_glb.location.y;
        if (td == TAP_DIR_LEFT) {
            tap_wire = ctx->getWireByLocAndBasename(tap_loc, "L_" + glbName);
        } else {
            tap_wire = ctx->getWireByLocAndBasename(tap_loc, "R_" + glbName);
        }
        return *(ctx->getPipsUphill(tap_wire).begin());
    }

    Context *ctx;
};

void route_ecp5_globals(Context *ctx);

NEXTPNR_NAMESPACE_END

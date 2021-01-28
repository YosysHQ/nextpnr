/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#include "log.h"
#include "nextpnr.h"

#if 0
#define dbg(...) log(__VA_ARGS__)
#else
#define dbg(...)
#endif

USING_NEXTPNR_NAMESPACE

namespace {

void archcheck_names(const Context *ctx)
{
    log_info("Checking entity names.\n");

    log_info("Checking bel names..\n");
    for (BelId bel : ctx->getBels()) {
        IdString name = ctx->getBelName(bel);
        BelId bel2 = ctx->getBelByName(name);
        if (bel != bel2) {
            log_error("bel != bel2, name = %s\n", name.c_str(ctx));
        }
    }

    log_info("Checking wire names..\n");
    for (WireId wire : ctx->getWires()) {
        IdString name = ctx->getWireName(wire);
        WireId wire2 = ctx->getWireByName(name);
        if (wire != wire2) {
            log_error("wire != wire2, name = %s\n", name.c_str(ctx));
        }
    }
#ifndef ARCH_ECP5
    log_info("Checking pip names..\n");
    for (PipId pip : ctx->getPips()) {
        IdString name = ctx->getPipName(pip);
        PipId pip2 = ctx->getPipByName(name);
        if (pip != pip2) {
            log_error("pip != pip2, name = %s\n", name.c_str(ctx));
        }
    }
#endif
    log_break();
}

void archcheck_locs(const Context *ctx)
{
    log_info("Checking location data.\n");

    log_info("Checking all bels..\n");
    for (BelId bel : ctx->getBels()) {
        log_assert(bel != BelId());
        dbg("> %s\n", ctx->getBelName(bel).c_str(ctx));

        Loc loc = ctx->getBelLocation(bel);
        dbg("   ... %d %d %d\n", loc.x, loc.y, loc.z);

        log_assert(0 <= loc.x);
        log_assert(0 <= loc.y);
        log_assert(0 <= loc.z);
        log_assert(loc.x < ctx->getGridDimX());
        log_assert(loc.y < ctx->getGridDimY());
        log_assert(loc.z < ctx->getTileBelDimZ(loc.x, loc.y));

        BelId bel2 = ctx->getBelByLocation(loc);
        dbg("   ... %s\n", ctx->getBelName(bel2).c_str(ctx));
        log_assert(bel == bel2);
    }

    log_info("Checking all locations..\n");
    for (int x = 0; x < ctx->getGridDimX(); x++)
        for (int y = 0; y < ctx->getGridDimY(); y++) {
            dbg("> %d %d\n", x, y);
            std::unordered_set<int> usedz;

            for (int z = 0; z < ctx->getTileBelDimZ(x, y); z++) {
                BelId bel = ctx->getBelByLocation(Loc(x, y, z));
                if (bel == BelId())
                    continue;
                Loc loc = ctx->getBelLocation(bel);
                dbg("   + %d %s\n", z, ctx->getBelName(bel).c_str(ctx));
                log_assert(x == loc.x);
                log_assert(y == loc.y);
                log_assert(z == loc.z);
                usedz.insert(z);
            }

            for (BelId bel : ctx->getBelsByTile(x, y)) {
                Loc loc = ctx->getBelLocation(bel);
                dbg("   - %d %s\n", loc.z, ctx->getBelName(bel).c_str(ctx));
                log_assert(x == loc.x);
                log_assert(y == loc.y);
                log_assert(usedz.count(loc.z));
                usedz.erase(loc.z);
            }

            log_assert(usedz.empty());
        }

    log_break();
}

void archcheck_conn(const Context *ctx)
{
    log_info("Checking connectivity data.\n");

    log_info("Checking all wires...\n");

    for (WireId wire : ctx->getWires()) {
        for (BelPin belpin : ctx->getWireBelPins(wire)) {
            WireId wire2 = ctx->getBelPinWire(belpin.bel, belpin.pin);
            log_assert(wire == wire2);
        }

        for (PipId pip : ctx->getPipsDownhill(wire)) {
            WireId wire2 = ctx->getPipSrcWire(pip);
            log_assert(wire == wire2);
        }

        for (PipId pip : ctx->getPipsUphill(wire)) {
            WireId wire2 = ctx->getPipDstWire(pip);
            log_assert(wire == wire2);
        }
    }

    log_info("Checking all BELs...\n");
    for (BelId bel : ctx->getBels()) {
        for (IdString pin : ctx->getBelPins(bel)) {
            WireId wire = ctx->getBelPinWire(bel, pin);

            if (wire == WireId()) {
                continue;
            }

            bool found_belpin = false;
            for (BelPin belpin : ctx->getWireBelPins(wire)) {
                if (belpin.bel == bel && belpin.pin == pin) {
                    found_belpin = true;
                    break;
                }
            }

            log_assert(found_belpin);
        }
    }

    log_info("Checking all PIPs...\n");
    for (PipId pip : ctx->getPips()) {
        WireId src_wire = ctx->getPipSrcWire(pip);
        if (src_wire != WireId()) {
            bool found_pip = false;
            for (PipId downhill_pip : ctx->getPipsDownhill(src_wire)) {
                if (pip == downhill_pip) {
                    found_pip = true;
                    break;
                }
            }

            log_assert(found_pip);
        }

        WireId dst_wire = ctx->getPipDstWire(pip);
        if (dst_wire != WireId()) {
            bool found_pip = false;
            for (PipId uphill_pip : ctx->getPipsUphill(dst_wire)) {
                if (pip == uphill_pip) {
                    found_pip = true;
                    break;
                }
            }

            log_assert(found_pip);
        }
    }
}

} // namespace

NEXTPNR_NAMESPACE_BEGIN

void Context::archcheck() const
{
    log_info("Running architecture database integrity check.\n");
    log_break();

    archcheck_names(this);
    archcheck_locs(this);
    archcheck_conn(this);
}

NEXTPNR_NAMESPACE_END

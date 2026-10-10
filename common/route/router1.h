/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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

#ifndef ROUTER1_H
#define ROUTER1_H

#include "log.h"
#include "nextpnr.h"
NEXTPNR_NAMESPACE_BEGIN

struct Router1Cfg
{
    Router1Cfg(Context *ctx);

    int maxIterCnt;
    bool cleanupReroute;
    bool fullCleanupReroute;
    bool useEstimate;
    delay_t wireRipupPenalty;
    delay_t netRipupPenalty;
    delay_t reuseBonus;
    delay_t estimatePrecision;
    // Rip up only the arcs through a conflicting wire when a pip held by another net is in the way, instead of that
    // net's every arc
    bool arcRipup;
    // Cap, in units of wireRipupPenalty, on the extra penalty for ripping up a wire shared by several arcs; 0 disables
    int arcPenaltyCap;
    // Log the N nets that consume the most A* node visits when routing finishes (diagnostic); 0 disables
    int reportHeavyNets;
    // Margin (in tiles) added to an arc's bounding box for the first search attempt; negative disables the limit
    int bbMargin;
};

extern bool router1(Context *ctx, const Router1Cfg &cfg);

NEXTPNR_NAMESPACE_END

#endif // ROUTER1_H

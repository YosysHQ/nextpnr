/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  gatecat <gatecat@ds0.me>
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
 *  [[cite]] HeAP
 *  Analytical Placement for Heterogeneous FPGAs, Marcel Gort and Jason H. Anderson
 *  https://janders.eecg.utoronto.ca/pdfs/marcelfpl12.pdf
 *
 *  [[cite]] SimPL
 *  SimPL: An Effective Placement Algorithm, Myung-Chul Kim, Dong-Jin Lee and Igor L. Markov
 *  http://www.ece.umich.edu/cse/awards/pdfs/iccad10-simpl.pdf
 */

#ifndef PLACER_HEAP_H
#define PLACER_HEAP_H
#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

struct PlacerHeapCfg
{
    PlacerHeapCfg(Context *ctx);

    float alpha, beta;
    float criticalityExponent;
    float timingWeight;
    bool timing_driven;
    float solverTolerance;
    bool placeAllAtOnce;
    float netShareWeight;
    bool parallelRefine;
    int cell_placement_timeout;

    int hpwl_scale_x, hpwl_scale_y;
    int spread_scale_x, spread_scale_y;

    // These cell types will be randomly locked to prevent singular matrices
    pool<IdString> ioBufTypes;
    // These cell types are part of the same unit (e.g. slices split into
    // components) so will always be spread together
    std::vector<pool<BelBucketId>> cellGroups;
};

extern bool placer_heap(Context *ctx, PlacerHeapCfg cfg);
NEXTPNR_NAMESPACE_END
#endif

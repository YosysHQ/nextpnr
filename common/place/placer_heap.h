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

#include <functional>

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
    bool chainRipup;
    int cell_placement_timeout;

    int hpwl_scale_x, hpwl_scale_y;
    int spread_scale_x, spread_scale_y;

    // These cell types will be randomly locked to prevent singular matrices
    pool<IdString> ioBufTypes;
    // These cell types are part of the same unit (e.g. slices split into
    // components) so will always be spread together
    std::vector<pool<BelBucketId>> cellGroups;

    // this is an optional callback to prioritise certain cells/clusters for legalisation
    std::function<float(Context *, CellInfo *)> get_cell_legalisation_weight = [](Context *, CellInfo *) { return 1; };

    bool disableCtrlSet;

    /*
    Control set API
    HeAP legalisation can be sped up by directly searching for nearby tiles to place an FF with a compatible control set.
    Only one shared control set is currently supported, however, as a full validity check is always performed too, this doesn't
    need to encompass every possible incompatibility (this is only for performance/QoR not correctness)

    ff_bel_bucket is the bel bucket ID for the flipflop (or logic cell if combined with LUT) bel type

    ff_control_set_groups contains the Z-location of flipflops in a control set group.
    Each entry in this represents a SLICE, i.e. the set of flipflops that share the control set. In XC7 this would be
    the two SLICEs in a tile.

    get_cell_control_set should return a unique index for every control set possibility. i.e. if this function returns the same
    value the flipflops could be placed in the same group.
    */


    BelBucketId ff_bel_bucket = BelBucketId();
    std::vector<std::vector<int>> ff_control_set_groups;

    // ctrl_set_max_radius is specified as a schedule per iteration, in general this should decrease over time
    std::vector<int> ctrl_set_max_radius;

    // TODO: control sets might have a hierarchy, like ultrascale+ CE vs CLK/SR
    std::function<int32_t(Context *, const CellInfo *)> get_cell_control_set = [](Context *, const CellInfo *) { return -1; };

};

extern bool placer_heap(Context *ctx, PlacerHeapCfg cfg);
NEXTPNR_NAMESPACE_END
#endif

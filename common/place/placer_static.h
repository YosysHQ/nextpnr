/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2022-23  gatecat <gatecat@ds0.me>
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
 *
 */

#ifndef PLACER_STATIC_H
#define PLACER_STATIC_H
#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

struct StaticRect
{
    StaticRect() : w(0), h(0){};
    StaticRect(float w, float h) : w(w), h(h){};
    float w, h;
    float area() const { return w * h; }
};

struct StaticCellGroupCfg
{
    // name of the group for debugging purposes
    IdString name;
    // bel buckets in this group
    pool<BelBucketId> bel_buckets;
    // cell & bel types in this group and the (normalised) area of that cell/bel type
    dict<IdString, StaticRect> cell_area, bel_area;
    // these cells (generally auxilliary CLB cells like RAMW; carry; MUX) are considered to have zero area when part of
    // a macro with other non-zero-area cells
    pool<IdString> zero_area_cells;
    // size of spacers to insert
    StaticRect spacer_rect{0.5, 0.5};
};

struct PlacerStaticCfg
{
    PlacerStaticCfg(Context *ctx);

    // These cell types will be randomly locked to prevent singular matrices
    pool<IdString> ioBufTypes;
    int hpwl_scale_x = 1;
    int hpwl_scale_y = 1;
    bool timing_driven = false;
    // for calculating timing estimates based on distance
    // estimate = c + mx*dx + my * dy
    delay_t timing_c = 100, timing_mx = 100, timing_my = 100;
    // groups of cells that should be placed together.
    // groups < logic_groups are logic like LUTs and FFs, further groups for BRAM/DSP/misc
    std::vector<StaticCellGroupCfg> cell_groups;
    int logic_groups = 2;
};

extern bool placer_static(Context *ctx, PlacerStaticCfg cfg);
NEXTPNR_NAMESPACE_END
#endif
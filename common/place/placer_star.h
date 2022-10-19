/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2022  gatecat <gatecat@ds0.me>
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
 *  [[cite]]
 *  An Analytical Timing Driven Placement Tool for Heterogeneous FPGA Architectures
 *  Timothy Martin
 *  https://atrium.lib.uoguelph.ca/xmlui/bitstream/handle/10214/21275/Martin_Timothy_202009_MSc.pdf
 *
 *  [[cite]]
 *  A Completely Parallelizable Analytic Algorithm for Fast and Scalable FPGA
 *  Ryan Pattison
 *  https://atrium.lib.uoguelph.ca/xmlui/bitstream/handle/10214/9082/Pattison_Ryan_201508_Msc.pdf
 *
 */

#ifndef PLACER_STAR_H
#define PLACER_STAR_H
#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

struct PlacerStarCfg
{
    PlacerStarCfg(Context *ctx);

    // These cell types will be randomly locked to prevent singular matrices
    pool<IdString> ioBufTypes;
    int hpwl_scale_x = 1;
    int hpwl_scale_y = 1;
    bool timing_driven = false;
};

extern bool placer_star(Context *ctx, PlacerStarCfg cfg);
NEXTPNR_NAMESPACE_END
#endif

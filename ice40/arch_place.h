/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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
 *  ACTION OF CONTRACT, NeEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifndef ICE40_ARCH_PLACE_H
#define ICE40_ARCH_PLACE_H

#include "nextpnr.h"
// Architecure-specific placement functions

NEXTPNR_NAMESPACE_BEGIN

class PlaceValidityChecker
{
  public:
    PlaceValidityChecker(Context *ctx);
    // Whether or not a given cell can be placed at a given Bel
    // This is not intended for Bel type checks, but finer-grained constraints
    // such as conflicting set/reset signals, etc
    bool isValidBelForCell(CellInfo *cell, BelId bel);

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel);

  private:
    bool logicCellsCompatible(const Context *ctx, const std::vector<const CellInfo *> &cells);
    Context *ctx;
    IdString id_icestorm_lc, id_sb_io, id_sb_gb;
    IdString id_cen, id_clk, id_sr;
    IdString id_i0, id_i1, id_i2, id_i3;
    IdString id_dff_en, id_neg_clk;
};

NEXTPNR_NAMESPACE_END

#endif

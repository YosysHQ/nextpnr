/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
 *  Copyright (C) 2018  David Shah <dave@ds0.me>
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

#include "pack.h"
#include "cells.h"
#include "design_utils.h"
#include "log.h"

#include <unordered_set>

// Pack LUTs and LUT-FF pairs
static void pack_lut_lutffs(Design *design)
{
    std::unordered_set<IdString> packed_cells;
    for (auto cell : design->cells) {
        CellInfo *ci = cell.second;
        if (is_lut(ci)) {
            CellInfo *packed = create_ice_cell(design, "ICESTORM_LC",
                                               std::string(ci->name) + "_LC");
            packed_cells.insert(ci->name);
            // See if we can pack into a DFF
            // TODO: LUT cascade
            NetInfo *o = ci->ports.at("O").net;
            CellInfo *dff = net_only_drives(o, is_ff, "D", true);
            if (dff) {
                lut_to_lc(ci, packed, false);
                dff_to_lc(dff, packed, false);
                packed_cells.insert(dff->name);
            } else {
                lut_to_lc(ci, packed, true);
            }
        }
    }
    for (auto pcell : packed_cells) {
        design->cells.erase(pcell);
    }
}

// Pack FFs not packed as LUTFFs
static void pack_nonlut_ffs(Design *design)
{
    std::unordered_set<IdString> packed_cells;
    for (auto cell : design->cells) {
        CellInfo *ci = cell.second;
        if (is_ff(ci)) {
            CellInfo *packed = create_ice_cell(design, "ICESTORM_LC",
                                               std::string(ci->name) + "_LC");
            packed_cells.insert(ci->name);
            dff_to_lc(ci, packed, true);
        }
    }
    for (auto pcell : packed_cells) {
        design->cells.erase(pcell);
    }
}

// Main pack function
void pack_design(Design *design)
{
    pack_lut_lutffs(design);
    pack_nonlut_ffs(design);
}

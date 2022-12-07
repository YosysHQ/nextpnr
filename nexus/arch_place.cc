/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  gatecat <gatecat@ds0.me>
 *
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

NEXTPNR_NAMESPACE_BEGIN

bool Arch::nexus_logic_tile_valid(LogicTileStatus &lts) const
{
    for (int s = 0; s < 4; s++) {
        if (lts.slices[s].dirty) {
            lts.slices[s].valid = false;
            lts.slices[s].dirty = false;
            CellInfo *lut0 = lts.cells[(s << 3) | BEL_LUT0];
            CellInfo *lut1 = lts.cells[(s << 3) | BEL_LUT1];
            CellInfo *ff0 = lts.cells[(s << 3) | BEL_FF0];
            CellInfo *ff1 = lts.cells[(s << 3) | BEL_FF1];

            if (s == 2) {
                CellInfo *ramw = lts.cells[(s << 3) | BEL_RAMW];
                // Nothing else in SLICEC can be used if the RAMW is used
                if (ramw != nullptr) {
                    if (lut0 != nullptr || lut1 != nullptr || ff0 != nullptr || ff1 != nullptr)
                        return false;
                }
            }

            if (lut0 != nullptr) {
                // Check for overuse of M signal
                if (lut0->lutInfo.mux2_used && ff0 != nullptr && ff0->ffInfo.m != nullptr)
                    return false;
            }
            // Check for correct use of FF0 DI
            if (ff0 != nullptr && ff0->ffInfo.di != nullptr &&
                (lut0 == nullptr || (ff0->ffInfo.di != lut0->lutInfo.f && ff0->ffInfo.di != lut0->lutInfo.ofx)))
                return false;
            if (lut1 != nullptr) {
                // LUT1 cannot contain a MUX2
                if (lut1->lutInfo.mux2_used)
                    return false;
                // If LUT1 is carry then LUT0 must be carry too
                if (lut1->lutInfo.is_carry && (lut0 == nullptr || !lut0->lutInfo.is_carry))
                    return false;
                if (!lut1->lutInfo.is_carry && lut0 != nullptr && lut0->lutInfo.is_carry)
                    return false;
            }
            // Check for correct use of FF1 DI
            if (ff1 != nullptr && ff1->ffInfo.di != nullptr && (lut1 == nullptr || ff1->ffInfo.di != lut1->lutInfo.f))
                return false;
            lts.slices[s].valid = true;
        } else if (!lts.slices[s].valid) {
            return false;
        }
    }
    for (int h = 0; h < 2; h++) {
        if (lts.halfs[h].dirty) {
            bool found_ff = false;
            FFControlSet ctrlset;
            for (int i = 0; i < 2; i++) {
                for (auto bel : {BEL_FF0, BEL_FF1, BEL_RAMW}) {
                    if (bel == BEL_RAMW && (h != 1 || i != 0))
                        continue;
                    CellInfo *ci = lts.cells[(h * 2 + i) << 3 | bel];
                    if (ci == nullptr)
                        continue;
                    if (!found_ff) {
                        ctrlset = ci->ffInfo.ctrlset;
                        found_ff = true;
                    } else if (ci->ffInfo.ctrlset != ctrlset) {
                        return false;
                    }
                }
            }
        } else if (!lts.halfs[h].valid) {
            return false;
        }
    }
    return true;
}

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    if (bel_tile_is(bel, LOC_LOGIC)) {
        LogicTileStatus *lts = tileStatus[bel.tile].lts;
        if (lts == nullptr)
            return true;
        else
            return nexus_logic_tile_valid(*lts);
    } else {
        return true;
    }
}

NEXTPNR_NAMESPACE_END

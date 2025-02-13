/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Beyond Authors.
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

#include "location_map.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
/* clang-format off */
const Loc ng_ultra_place_cy_map[24] = {
        { 0,  1,  0}, // S1 0 -> S2 0  CY24->CY1
        { 0,  0, -1}, // S1 1 -> S1 0  CY23->CY24
        { 0,  0, -1}, // S1 2 -> S1 1  CY22->CY23
        { 0,  0, -1}, // S1 3 -> S1 2  CY21->CY22

        {-1,  0, +3}, // S5 0 -> S1 1  CY20->CY21
        { 0,  0, -1}, // S5 1 -> S5 0  CY19->CY20
        { 0,  0, -1}, // S5 2 -> S5 1  CY18->CY19
        { 0,  0, -1}, // S5 3 -> S5 2  CY17->CY18

        {-1,  0, +3}, // S9 0 -> S5 1  CY16->CY17
        { 0,  0, -1}, // S9 1 -> S9 0  CY15->CY16
        { 0,  0, -1}, // S9 2 -> S9 1  CY14->CY15
        { 0,  0, -1}, // S9 3 -> S9 2  CY13->CY14

        { 0,  0, +1}, // S2 0 -> S2 1  CY1->CY2
        { 0,  0, +1}, // S2 1 -> S2 2  CY2->CY3
        { 0,  0, +1}, // S2 2 -> S2 3  CY3->CY4
        { 1,  0, -3}, // S2 3 -> S6 0  CY4->CY5

        { 0,  0, +1}, // S6 0 -> S6 1  CY5->CY6
        { 0,  0, +1}, // S6 1 -> S6 2  CY6->CY7
        { 0,  0, +1}, // S6 2 -> S6 3  CY7->CY8
        { 1,  0, -3}, // S6 3 -> S10 0 CY8->CY9

        { 0,  0, +1}, // S10 0 -> S10 1 CY9->CY10
        { 0,  0, +1}, // S10 1 -> S10 2 CY10->CY11
        { 0,  0, +1}, // S10 2 -> S10 3 CY11->CY12
        { 0, -1,  0}, // S10 3 -> S9  3 CY12->CY13
};

const Loc ng_ultra_place_xrf[] = {
        {-1, 0, 1},  // I/O1
        {-1, 0, 2},  // I/O2
        {-1, 0, 5},  // I/O3
        {-1, 0, 6},  // I/O4
        {-1, 0, 7},  // I/O5
        {-1, 0, 9},  // I/O6
        {-1, 0, 10}, // I/O7
        {-1, 0, 13}, // I/O8
        {-1, 0, 14}, // I/O9
        {-1, 0, 15}, // I/O10
        {-1, 0, 16}, // I/O11
        {-1, 0, 17}, // I/O12
        {-1, 0, 18}, // I/O13
        {-1, 0, 21}, // I/O14
        {-1, 0, 24}, // I/O15
        {-1, 0, 25}, // I/O16
        {-1, 0, 26}, // I/O17
        {-1, 0, 29}, // I/O18

        {+1, 0, 1},  // I/O19
        {+1, 0, 2},  // I/O20
        {+1, 0, 5},  // I/O21
        {+1, 0, 6},  // I/O22
        {+1, 0, 7},  // I/O23
        {+1, 0, 9},  // I/O24
        {+1, 0, 10}, // I/O25
        {+1, 0, 13}, // I/O26
        {+1, 0, 14}, // I/O27
        {+1, 0, 15}, // I/O28
        {+1, 0, 16}, // I/O29
        {+1, 0, 17}, // I/O30
        {+1, 0, 18}, // I/O31
        {+1, 0, 21}, // I/O32
        {+1, 0, 24}, // I/O33
        {+1, 0, 25}, // I/O34
        {+1, 0, 26}, // I/O35
        {+1, 0, 29}, // I/O36

        {-1, 0, 4},  // RA1
        {-1, 0, 12}, // RA2
        {-1, 0, 20}, // RA3
        {-1, 0, 27}, // RA4
        {-1, 0, 31}, // RA5

        {+1, 0, 4},  // RA6
        {+1, 0, 12}, // RA7
        {+1, 0, 20}, // RA8
        {+1, 0, 27}, // RA9
        {+1, 0, 31}, // RA10

        {-1, 0, 3},  // WA1
        {-1, 0, 11}, // WA2
        {-1, 0, 19}, // WA3
        {-1, 0, 23}, // WA4
        {-1, 0, 28}, // WA5

        {+1, 0, 3}, // WA6

        {-1, 0, 0}, // WE
        {-1, 0, 8}, // WEA

};

const Loc ng_ultra_place_cdc1[] = {
        {+1, 0, 1},  // AI1
        {+1, 0, 2},  // AI2
        {+1, 0, 9},  // AI3
        {+1, 0, 17}, // AI4
        {+1, 0, 18}, // AI5
        {+1, 0, 25}, // AI6

        {+1, 0, 3},  // BI1
        {+1, 0, 10}, // BI2
        {+1, 0, 11}, // BI3
        {+1, 0, 19}, // BI4
        {+1, 0, 26}, // BI5
        {+1, 0, 27}, // BI6

        { 0, 0, 22}, // ASRSTI
        { 0, 0, 30}, // ADRSTI
        {+1, 0, 24}, // BSRSTI
        {+1, 0, 8},  // BDRSTI
};

const Loc ng_ultra_place_cdc2[] = {
        {-1, 0, 4},  // AI1
        {-1, 0, 5},  // AI2
        {-1, 0, 12}, // AI3
        {-1, 0, 20}, // AI4
        {-1, 0, 21}, // AI5
        {-1, 0, 28}, // AI6

        {-1, 0, 6},  // BI1
        {-1, 0, 13}, // BI2
        {-1, 0, 14}, // BI3
        {-1, 0, 22}, // BI4
        {-1, 0, 29}, // BI5
        {-1, 0, 30}, // BI6

        { 0, 0, 22}, // ASRSTI
        { 0, 0, 30}, // ADRSTI
        {-1, 0, 23}, // BSRSTI
        {-1, 0, 7},  // BDRSTI
};

const Loc ng_ultra_place_xcdc[] = {
        { 0, 0, 1},  // AI1
        { 0, 0, 2},  // AI2
        { 0, 0, 9},  // AI3
        { 0, 0, 17}, // AI4
        { 0, 0, 18}, // AI5
        { 0, 0, 25}, // AI6

        { 0, 0, 4},  // BI1
        { 0, 0, 5},  // BI2
        { 0, 0, 12}, // BI3
        { 0, 0, 20}, // BI4
        { 0, 0, 21}, // BI5
        { 0, 0, 28}, // BI6

        {-1, 0, 22}, // ASRSTI
        {-1, 0, 30}, // ADRSTI
        {+1, 0, 22}, // BSRSTI
        {+1, 0, 30}, // BDRSTI

        { 0, 0, 3},  // CI1
        { 0, 0, 10}, // CI2
        { 0, 0, 11}, // CI3
        { 0, 0, 19}, // CI4
        { 0, 0, 26}, // CI5
        { 0, 0, 27}, // CI6

        { 0, 0, 6},  // DI1
        { 0, 0, 13}, // DI2
        { 0, 0, 14}, // DI3
        { 0, 0, 22}, // DI4
        { 0, 0, 29}, // DI5
        { 0, 0, 30}, // DI6

        { 0, 0, 24}, // CSRSTI
        { 0, 0, 8},  // CDRSTI
        { 0, 0, 23}, // DSRSTI
        { 0, 0, 7},  // DDRSTI
};

const Loc ng_ultra_place_fifo1[] = {
        {-1, 0, 1},  // I/O1
        {-1, 0, 2},  // I/O2
        {-1, 0, 5},  // I/O3
        {-1, 0, 6},  // I/O4
        {-1, 0, 7},  // I/O5
        {-1, 0, 9},  // I/O6
        {-1, 0, 10}, // I/O7
        {-1, 0, 13}, // I/O8
        {-1, 0, 14}, // I/O9
        {-1, 0, 15}, // I/O10
        {-1, 0, 16}, // I/O11
        {-1, 0, 17}, // I/O12
        {-1, 0, 18}, // I/O13
        {-1, 0, 21}, // I/O14
        {-1, 0, 24}, // I/O15
        {-1, 0, 25}, // I/O16
        {-1, 0, 26}, // I/O17
        {-1, 0, 29}, // I/O18

        { 0, 0, 0},  // I/O19
        { 0, 0, 0},  // I/O20
        { 0, 0, 0},  // I/O21
        { 0, 0, 0},  // I/O22
        { 0, 0, 0},  // I/O23
        { 0, 0, 0},  // I/O24
        { 0, 0, 0},  // I/O25
        { 0, 0, 0},  // I/O26
        { 0, 0, 0},  // I/O27
        { 0, 0, 0},  // I/O28
        { 0, 0, 0},  // I/O29
        { 0, 0, 0},  // I/O30
        { 0, 0, 0},  // I/O31
        { 0, 0, 0},  // I/O32
        { 0, 0, 0},  // I/O33
        { 0, 0, 0},  // I/O34
        { 0, 0, 0},  // I/O35
        { 0, 0, 0},  // I/O36

        { 0, 0, 3},  // RAI1/RAO1
        { 0, 0, 10}, // RAI2/RAO2
        { 0, 0, 11}, // RAI3/RAO3
        { 0, 0, 19}, // RAI4/RAO4
        { 0, 0, 26}, // RAI5/RAO5
        { 0, 0, 27}, // RAI6/RAO6
        { 0, 0, 0},  // RAI7/RAO7

        { 0, 0, 1},  // WAI1/WAO1
        { 0, 0, 2},  // WAI2/WAO2
        { 0, 0, 9},  // WAI3/WAO3
        { 0, 0, 17}, // WAI4/WAO4
        { 0, 0, 18}, // WAI5/WAO5
        { 0, 0, 25}, // WAI6/WAO6
        { 0, 0, 0},  // WAI7/WAO7

        {-1, 0, 0},  // WE
        {-1, 0, 8},  // WEA

        {-1, 0, 22}, // WRSTI1/WRSTO
        {-1, 0, 30}, // RRSTI1/RRSTO
        { 0, 0, 8},  // WRSTI2
        { 0, 0, 24}, // RRSTI2
        { 0, 0, 0},  // WRSTI3/WRSTO
        { 0, 0, 0},  // RRSTI3/RRSTO
        { 0, 0, 0},  // WRSTI4
        { 0, 0, 0},  // RRSTI4

        {-1, 0, 3}, // WEQ
        {-1, 0, 4}, // REQ
        // {-1, 0, 11}, WEQ
        // {-1, 0, 12}, REQ
        // {-1, 0, 19}, WEQ
        // {-1, 0, 20}, REQ
        // {-1, 0, 27}, WEQ
        // {-1, 0, 28}, REQ
        { 0, 0, 0}, // WEQ2
        { 0, 0, 0}, // REQ2
};

const Loc ng_ultra_place_fifo2[] = {
        {+1, 0, 1},  // I/O1
        {+1, 0, 2},  // I/O2
        {+1, 0, 5},  // I/O3
        {+1, 0, 6},  // I/O4
        {+1, 0, 7},  // I/O5
        {+1, 0, 9},  // I/O6
        {+1, 0, 10}, // I/O7
        {+1, 0, 13}, // I/O8
        {+1, 0, 14}, // I/O9
        {+1, 0, 15}, // I/O10
        {+1, 0, 16}, // I/O11
        {+1, 0, 17}, // I/O12
        {+1, 0, 18}, // I/O13
        {+1, 0, 21}, // I/O14
        {+1, 0, 24}, // I/O15
        {+1, 0, 25}, // I/O16
        {+1, 0, 26}, // I/O17
        {+1, 0, 29}, // I/O18

        { 0, 0, 0},  // I/O19
        { 0, 0, 0},  // I/O20
        { 0, 0, 0},  // I/O21
        { 0, 0, 0},  // I/O22
        { 0, 0, 0},  // I/O23
        { 0, 0, 0},  // I/O24
        { 0, 0, 0},  // I/O25
        { 0, 0, 0},  // I/O26
        { 0, 0, 0},  // I/O27
        { 0, 0, 0},  // I/O28
        { 0, 0, 0},  // I/O29
        { 0, 0, 0},  // I/O30
        { 0, 0, 0},  // I/O31
        { 0, 0, 0},  // I/O32
        { 0, 0, 0},  // I/O33
        { 0, 0, 0},  // I/O34
        { 0, 0, 0},  // I/O35
        { 0, 0, 0},  // I/O36

        { 0, 0, 6},  // RAI1/RAO1
        { 0, 0, 13}, // RAI2/RAO2
        { 0, 0, 14}, // RAI3/RAO3
        { 0, 0, 22}, // RAI4/RAO4
        { 0, 0, 29}, // RAI5/RAO5
        { 0, 0, 30}, // RAI6/RAO6
        { 0, 0, 0},  // RAI7/RAO7

        { 0, 0, 4},  // WAI1/WAO1
        { 0, 0, 5},  // WAI2/WAO2
        { 0, 0, 12}, // WAI3/WAO3
        { 0, 0, 20}, // WAI4/WAO4
        { 0, 0, 21}, // WAI5/WAO5
        { 0, 0, 28}, // WAI6/WAO6
        { 0, 0, 0},  // WAI7/WAO7

        {+1, 0, 0},  // WE
        {+1, 0, 8},  // WEA

        {+1, 0, 22}, // WRSTI1/WRSTO
        {+1, 0, 30}, // RRSTI1/RRSTO
        { 0, 0, 7},  // WRSTI2
        { 0, 0, 23}, // RRSTI2
        { 0, 0, 0},  // WRSTI3/WRSTO
        { 0, 0, 0},  // RRSTI3/RRSTO
        { 0, 0, 0},  // WRSTI4
        { 0, 0, 0},  // RRSTI4

        {+1, 0, 3}, // WEQ
        {+1, 0, 4}, // REQ
        // {+1, 0, 11}, WEQ
        // {+1, 0, 12}, REQ
        // {+1, 0, 19}, WEQ
        // {+1, 0, 20}, REQ
        // {+1, 0, 27}, WEQ
        // {+1, 0, 28}, REQ
        { 0, 0, 0}, // WEQ2
        { 0, 0, 0}, // REQ2
};

const Loc ng_ultra_place_xfifo[] = {
        {-1, 0, 1},  // I/O1
        {-1, 0, 2},  // I/O2
        {-1, 0, 5},  // I/O3
        {-1, 0, 6},  // I/O4
        {-1, 0, 7},  // I/O5
        {-1, 0, 9},  // I/O6
        {-1, 0, 10}, // I/O7
        {-1, 0, 13}, // I/O8
        {-1, 0, 14}, // I/O9
        {-1, 0, 15}, // I/O10
        {-1, 0, 16}, // I/O11
        {-1, 0, 17}, // I/O12
        {-1, 0, 18}, // I/O13
        {-1, 0, 21}, // I/O14
        {-1, 0, 24}, // I/O15
        {-1, 0, 25}, // I/O16
        {-1, 0, 26}, // I/O17
        {-1, 0, 29}, // I/O18
        {+1, 0, 1},  // I/O19
        {+1, 0, 2},  // I/O20
        {+1, 0, 5},  // I/O21
        {+1, 0, 6},  // I/O22
        {+1, 0, 7},  // I/O23
        {+1, 0, 9},  // I/O24
        {+1, 0, 10}, // I/O25
        {+1, 0, 13}, // I/O26
        {+1, 0, 14}, // I/O27
        {+1, 0, 15}, // I/O28
        {+1, 0, 16}, // I/O29
        {+1, 0, 17}, // I/O30
        {+1, 0, 18}, // I/O31
        {+1, 0, 21}, // I/O32
        {+1, 0, 24}, // I/O33
        {+1, 0, 25}, // I/O34
        {+1, 0, 26}, // I/O35
        {+1, 0, 29}, // I/O36

        { 0, 0, 3},  // RAI1/RAO1
        { 0, 0, 10}, // RAI2/RAO2
        { 0, 0, 11}, // RAI3/RAO3
        { 0, 0, 19}, // RAI4/RAO4
        { 0, 0, 26}, // RAI5/RAO5
        { 0, 0, 27}, // RAI6/RAO6
        { 0, 0, 6},  // RAI7/RAO7

        { 0, 0, 1},  // WAI1/WAO1
        { 0, 0, 2},  // WAI2/WAO2
        { 0, 0, 9},  // WAI3/WAO3
        { 0, 0, 17}, // WAI4/WAO4
        { 0, 0, 18}, // WAI5/WAO5
        { 0, 0, 25}, // WAI6/WAO6
        { 0, 0, 4},  // WAI7/WAO7

        {-1, 0, 0},  // WE
        {-1, 0, 8},  // WEA

        {-1, 0, 22}, // WRSTI1/WRSTO
        {-1, 0, 30}, // RRSTI1/RRSTO
        { 0, 0, 8},  // WRSTI2
        { 0, 0, 24}, // RRSTI2
        {+1, 0, 22}, // WRSTI3/WRSTO
        {+1, 0, 30}, // RRSTI3/RRSTO
        { 0, 0, 7},  // WRSTI4
        { 0, 0, 23}, // RRSTI4

        {-1, 0, 3}, // WEQ1
        {-1, 0, 4}, // REQ1
        // {-1, 0, 11}, WEQ1
        // {-1, 0, 12}, REQ1
        // {-1, 0, 19}, WEQ1
        // {-1, 0, 20}, REQ1
        // {-1, 0, 27}, WEQ1
        // {-1, 0, 28}, REQ1
        {+1, 0, 3}, // WEQ2
        {+1, 0, 4}, // REQ2
        // {+1, 0, 11}, WEQ2
        // {+1, 0, 12}, REQ2
        // {+1, 0, 19}, WEQ2
        // {+1, 0, 20}, REQ2
        // {+1, 0, 27}, WEQ2
        // {+1, 0, 28}, REQ2
};
/* clang-format on */

}; // namespace

namespace ng_ultra {

Loc getNextLocInDSPChain(const NgUltraImpl *impl, Loc loc)
{
    BelId bel = impl->ctx->getBelByLocation(loc);
    if (impl->dsp_cascade.count(bel) == 0) {
        loc.z = -1; // End of chain
        return loc;
    }
    BelId dsp = impl->dsp_cascade.at(bel);
    return impl->ctx->getBelLocation(dsp);
}

Loc getNextLocInCYChain(Loc loc)
{
    int section = (loc.x % 4 - 1 + 3 * (loc.y % 4)) * 4 + loc.z - BEL_CY_Z;
    Loc result = ng_ultra_place_cy_map[section];
    result.x += loc.x;
    result.y += loc.y;
    result.z += loc.z;
    return result;
}

Loc getNextLocInLUTChain(Loc loc)
{
    Loc result = loc;
    result.x = loc.x;
    result.y = loc.y;
    result.z = (loc.z + 8) % 32; // BEL_LUT_Z is 0
    return result;
}

Loc getNextLocInDFFChain(Loc loc)
{
    Loc result = loc;
    if (loc.z == 31) {
        if ((loc.x & 3) == 3) {
            result.z = -1; // End of chain
            return result;
        }
        result.z = 0;
        result.x++;
        return result;
    }
    int z = loc.z + 8;
    if (z > 31)
        z++;
    result.z = z % 32; // BEL_LUT_Z is 0
    return result;
}

Loc getCYFE(Loc root, int pos)
{
    int p[] = {2 - 1, 25 - 1, 10 - 1, 17 - 1};
    int cy = root.z - BEL_CY_Z;
    Loc result;
    result.x = root.x;
    result.y = root.y;
    result.z = p[pos] + cy * 2;
    return result;
}

Loc getXLUTFE(Loc root, int pos)
{
    Loc result;
    result.x = root.x;
    result.y = root.y;
    result.z = root.z - BEL_XLUT_Z + 8 * pos;
    return result;
}

Loc getXRFFE(Loc root, int pos)
{
    Loc result = ng_ultra_place_xrf[pos];
    if (root.z == BEL_XRF_Z) {
        // XRF1
        result.x += root.x;
    } else {
        // RF1 or RF2
        result.x = root.x + ((root.z == BEL_RF_Z) ? -1 : +1);
    }
    result.y = root.y;
    return result;
}

Loc getCDCFE(Loc root, int pos)
{
    Loc result;
    if (root.z == BEL_CDC_Z) {
        result = ng_ultra_place_cdc1[pos];
    } else if (root.z == BEL_CDC_Z + 1) {
        result = ng_ultra_place_cdc2[pos];
    } else if (root.z == BEL_XCDC_Z) {
        result = ng_ultra_place_xcdc[pos];
    } else {
        log_error("Trying to place CDC on wrong location.\n");
    }
    result.x += root.x;
    result.y = root.y;
    return result;
}

Loc getFIFOFE(Loc root, int pos)
{
    Loc result;
    if (root.z == BEL_FIFO_Z) {
        result = ng_ultra_place_fifo1[pos];
    } else if (root.z == BEL_FIFO_Z + 1) {
        result = ng_ultra_place_fifo2[pos];
    } else if (root.z == BEL_XFIFO_Z) {
        result = ng_ultra_place_xfifo[pos];
    } else {
        log_error("Trying to place CDC on wrong location.\n");
    }
    result.x += root.x;
    result.y = root.y;
    return result;
}

}; // namespace ng_ultra
NEXTPNR_NAMESPACE_END

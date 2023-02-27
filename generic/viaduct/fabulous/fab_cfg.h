/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-22  gatecat <gatecat@ds0.me>
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
#ifndef FAB_CFG_H
#define FAB_CFG_H

#include "fab_defs.h"
#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"

#include <string>
#include <vector>

NEXTPNR_NAMESPACE_BEGIN

/*
This set of structures is designed to enumerate the different configurable options for a fabulous architecture,
affecting the packer etc...
*/

struct ControlSetConfig
{
    /*
    CLB signal routing masks for fast validity checking
       for each unique CLK/CE/SR input to a CLB, add an entry to this vector, and set the bits to 1 for each ff that
    signal can drive for a CLB with 8 FFs and 2 clocks split at halfway, the first entry would be 0x0F and the second
    0xF0
    */
    std::vector<route_mask_t> routing; // default 1 shared between all
    bool have_signal = true;
    int can_mask = -1;
    bool can_invert = false;
};

struct LogicConfig
{
    // ** Core CLB config
    unsigned lc_per_clb = 8; // number of logic cells per clb
    bool split_lc = false; // whether to represent SLICE as a single bel or separate lut+ff (latter important if ff and
                           // lut can be used separately)

    // ** LUT config
    unsigned lut_k = 4; // base number of inputs for lookup table
    enum LutType
    {
        SINGLE_LUT,
        // ...
    } lut_type = LutType::SINGLE_LUT; // different types of fracturable LUT structure

    enum LutCascade
    {
        NO_CASCADE,
        // ...
    } lut_casc = LutCascade::NO_CASCADE; // different types of cascading between LUTs

    // TODO: other features we might want to represent...
    // TODO: fracLUT/FF/mux/carry output sharing matrices

    // ** Carry config
    enum CarryType
    {
        NO_CARRY,    // no carry chain
        HA_PRE_LUT,  // half addder before LUT (classic fabulous LC)
        PG_POST_LUT, // prop/gen logic after a fractured LUT
        FA_POST_LUT, // full adder after a fractured LUT
    } carry_type = CarryType::HA_PRE_LUT;
    int carry_lut_frac = -1; // how the LUT is fractured for PG_POST_LUT/FA_POST_LUT, if the LUT fracturing is different
                             // (or only supported) for carry modes and not in general

    // ** FF config
    unsigned ff_per_lc = 1;      // number of flipflops per logic cell
    uint32_t dedi_ff_input = 0;  // mask of flipflops in a LC that have dedicated inputs
    uint32_t dedi_ff_output = 0; // mask of flipflops in a LC that have dedicated outputs

    ControlSetConfig clk, sr, en; // flipflop control set routing
};

struct FabricConfig
{
    LogicConfig clb;
    // DSP cascading, BRAM, IP rules, IO, clocking ...
};

NEXTPNR_NAMESPACE_END

#endif

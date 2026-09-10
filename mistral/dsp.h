/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2026  Deano Calver
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
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
 *  IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef MISTRAL_DSP_H
#define MISTRAL_DSP_H

#include <array>

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

struct MistralDspLane
{
    int a_group;
    int b_group;
    int result_offset;
};

// A 3-lane M9 tile uses separate 9-bit slices for each operand. In preadder
// mode the Y slices move to the documented AY groups and each lane gets a Z
// slice. The order is low lane to high lane.
constexpr std::array<int, 3> mistral_dsp_preadder_y_groups{{2, 3, 8}};
constexpr std::array<int, 3> mistral_dsp_preadder_z_groups{{4, 5, 10}};

// The M18X18P36 mode uses AX groups 0/1, AY groups 2/3, and the optional
// 36-bit addend on groups 8/9 (low half) and 6/7 (high half). Tracing
// Quartus 17.0.2's A*B+C input routes confirms this concatenation order.
constexpr std::array<int, 2> mistral_dsp_18x18_a_groups{{0, 1}};
constexpr std::array<int, 2> mistral_dsp_18x18_b_groups{{2, 3}};
constexpr std::array<int, 4> mistral_dsp_18x18_c_groups{{8, 9, 6, 7}};

// A 27x27 multiplier uses the same operand packing as the three 9x9 mode,
// with one 54-bit result. RESULT port 36 is the physical hole between the
// upper and lower 37-bit halves, so the last 18 logical bits start at 37.
constexpr std::array<int, 3> mistral_dsp_27x27_a_groups{{0, 6, 7}};
constexpr std::array<int, 3> mistral_dsp_27x27_b_groups{{2, 8, 9}};

constexpr int mistral_dsp_27x27_result_port(int bit)
{
    return bit < 36 ? bit : bit + 1;
}

// Cyclone V three-multiplier mode packs three 9x9 products into the 27-bit
// A/B inputs and the low 54 logical result bits. The physical RESULT port has
// a one-bit hole at 36, so the third product starts at port 37. The order
// follows Mistral's documented AX/AY packing table: the low lane is the
// existing single-mode mapping and the two upper lanes use groups 6/8 and
// 7/9.
constexpr std::array<MistralDspLane, 3> mistral_dsp_lanes{{
        {0, 2, 0},
        {6, 8, 18},
        {7, 9, 37},
}};

NEXTPNR_NAMESPACE_END

#endif

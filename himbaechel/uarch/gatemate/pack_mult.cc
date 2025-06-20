/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include "idstring.h"
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"
#include "pack.h"
#include "property.h"
#include "uarch/gatemate/extra_data.h"
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

// Propagate A0 through OUT1 and A1 through OUT2; zero COUTX and POUTX.
struct APassThroughCell
{
    APassThroughCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(LUT_D0, 4);   // IN1
        lower->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
        lower->params[id_INIT_L10] = Property(LUT_D0, 4);   // L00 -> COMB2OUT

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(LUT_D0, 4);   // IN5
        upper->params[id_INIT_L03] = Property(LUT_ZERO, 4); // (unused)
        upper->params[id_INIT_L11] = Property(LUT_D0, 4);   // L02
        upper->params[id_INIT_L20] = Property(LUT_D1, 4);   // L11 -> COMB1OUT
        upper->params[id_INIT_L30] = Property(LUT_ONE, 4);  // zero -> COMP_OUT (L30 is inverted)

        upper->params[id_C_SEL_C] = Property(1, 1); // COMP_OUT -> CX_VAL
        upper->params[id_C_SEL_P] = Property(1, 1); // COMP_OUT -> PX_VAL
        upper->params[id_C_CX_I] = Property(1, 1);  // CX_VAL -> COUTX
        upper->params[id_C_PX_I] = Property(1, 1);  // PX_VAL -> POUTX

        upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
        upper->params[id_C_O2] = Property(0b11, 2); // COMB2OUT -> OUT2
    }

    PortRef a0() const { return PortRef{upper, id_IN1}; }
    PortRef a1() const { return PortRef{lower, id_IN1}; }

    CellInfo *upper;
    CellInfo *lower;
};

// Propagate B0 through POUTY1 and B1 through COUTY1
//
// CITE: CPE_ges_Bin.pdf
// TODO: is it worth trying to unify this with APassThroughCell?
struct BPassThroughCell
{
    BPassThroughCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(LUT_D0, 4);   // IN1
        lower->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
        lower->params[id_INIT_L10] = Property(LUT_D0, 4);   // L00 -> COMB2OUT

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(LUT_D0, 4);   // IN5
        upper->params[id_INIT_L03] = Property(LUT_ZERO, 4); // (unused)
        upper->params[id_INIT_L11] = Property(LUT_D0, 4);   // L02
        upper->params[id_INIT_L20] = Property(LUT_D1, 4);   // L11 -> COMB1OUT

        upper->params[id_C_CY1_I] = Property(1, 1); // CY1_VAL -> COUTY1
        upper->params[id_C_PY1_I] = Property(1, 1); // PY1_VAL -> POUTY1

        upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
        upper->params[id_C_O2] = Property(0b11, 2); // COMB2OUT -> OUT2
    }

    PortRef b0() const { return PortRef{upper, id_IN1}; }
    PortRef b1() const { return PortRef{lower, id_IN1}; }

    CellInfo *upper;
    CellInfo *lower;
};

// TODO: Micko points out this is an L2T4 CPE_HALF
struct CarryGenCell
{
    CarryGenCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x) : lower{lower}, upper{upper}
    {
        // TODO: simplify AND with zero/OR with zero into something more sensical.

        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(LUT_ZERO, 4); // (unused)
        lower->params[id_INIT_L01] = Property(LUT_D1, 4);   // CINX
        lower->params[id_INIT_L10] = Property(is_even_x ? LUT_AND : LUT_OR, 4);

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(LUT_D1, 4);   // PINY1
        upper->params[id_INIT_L03] = Property(LUT_ZERO, 4); // (unused)
        upper->params[id_INIT_L11] = Property(is_even_x ? LUT_AND : LUT_OR, 4);
        upper->params[id_INIT_L20] = Property(is_even_x ? LUT_AND : LUT_OR, 4);
        upper->params[id_INIT_L30] = Property(LUT_INV_D0, 4); // OUT1 -> COMP_OUT

        upper->params[id_C_I2] = Property(1, 1); // CINX for L01
        upper->params[id_C_I3] = Property(1, 1); // PINY1 for L02
        upper->params[id_C_FUNCTION] = Property(C_EN_CIN, 3);
        upper->params[id_C_PY1_I] = Property(0, 1); // PINY1 -> POUTY1
        upper->params[id_C_CY1_I] = Property(0, 1); // CINY1 -> COUTY1
        upper->params[id_C_CY2_I] = Property(1, 1); // CY2_VAL -> COUTY2
        upper->params[id_C_SEL_C] = Property(1, 1); // COMP_OUT -> CY2_VAL
        upper->params[id_C_SELY2] = Property(0, 1); // COMP_OUT -> CY2_VAL

        upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
    }

    CellInfo *upper;
    CellInfo *lower;
};

// This prepares B bits for multiplication.
struct MultfabCell
{
    MultfabCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x) : lower{lower}, upper{upper}
    {
        // TODO: perhaps C_I[1234] could be pips?

        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(LUT_D1, 4);                        // PINY1
        lower->params[id_INIT_L01] = Property(is_even_x ? LUT_ZERO : LUT_D1, 4); // CINX
        lower->params[id_INIT_L10] = Property(LUT_XOR, 4);                       // XOR

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(LUT_D1, 4);                              // PINY1
        upper->params[id_INIT_L03] = Property(LUT_ZERO, 4);                            // (unused)
        upper->params[id_INIT_L11] = Property(LUT_D0, 4);                              // L02
        upper->params[id_INIT_L20] = Property(is_even_x ? LUT_AND_INV_D0 : LUT_OR, 4); // L10 AND L11 -> OUT1
        upper->params[id_INIT_L30] = Property(LUT_INV_D1, 4);                          // L10 -> COMP_OUT

        upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
        upper->params[id_C_I2] = Property(1, 1); // CINX for L01
        upper->params[id_C_I3] = Property(1, 1); // PINY1 for L02
        upper->params[id_C_FUNCTION] = Property(C_ADDCIN, 3);
        upper->params[id_C_SELX] = Property(1, 1);  // inverted CINY2 -> CX_VAL
        upper->params[id_C_SEL_C] = Property(1, 1); // inverted CINY2 -> CX_VAL; COMP_OUT -> CY1_VAL
        upper->params[id_C_Y12] = Property(1, 1);   // inverted CINY2 -> CX_VAL
        upper->params[id_C_CX_I] = Property(1, 1);  // CX_VAL -> COUTX
        upper->params[id_C_CY1_I] = Property(1, 1); // CY1_VAL -> COUTY1
        upper->params[id_C_PY1_I] = Property(1, 1); // PY1_VAL -> POUTY1
        upper->params[id_C_SEL_P] = Property(0, 1); // OUT1 -> PY1_VAL
        upper->params[id_C_SELY1] = Property(0, 1); // COMP_OUT -> CY1_VAL; OUT1 -> PY1_VAL

        upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
    }

    CellInfo *upper;
    CellInfo *lower;
};

// CITE: CPE_ges_f-routing-1.pdf
// TODO:
struct FRoutingCell
{
    FRoutingCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x) : lower{lower}, upper{upper}
    {
        // TODO: simplify AND with zero/OR with zero into something more sensical.

        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(LUT_D1, 4);  // PINY1
        lower->params[id_INIT_L01] = Property(LUT_ONE, 4); // (unused)
        lower->params[id_INIT_L10] = Property(LUT_AND, 4);

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(LUT_ZERO, 4); // (unused)
        upper->params[id_INIT_L03] = Property(LUT_ONE, 4);  // (unused)
        upper->params[id_INIT_L11] = Property(LUT_AND, 4);
        upper->params[id_INIT_L20] = Property(LUT_D1, 4);
        upper->params[id_INIT_L30] = Property(is_even_x ? LUT_ONE : LUT_INV_D1, 4); // OUT1 -> COMP_OUT

        upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
        upper->params[id_C_FUNCTION] = Property(C_ADDCIN, 3);
        upper->params[id_C_SELX] = Property(1, 1);
        upper->params[id_C_SEL_C] = Property(1, 1);
        upper->params[id_C_Y12] = Property(1, 1);
        upper->params[id_C_CX_I] = Property(1, 1);
        upper->params[id_C_CY1_I] = Property(is_even_x, 1);
        upper->params[id_C_CY2_I] = Property(1, 1);
        upper->params[id_C_PY1_I] = Property(1, 1);
        upper->params[id_C_PY2_I] = Property(1, 1);

        upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
        upper->params[id_C_O2] = Property(0b11, 2); // COMB2OUT -> OUT2
    }

    CellInfo *upper;
    CellInfo *lower;
};

// Multiply two bits of A with two bits of B.
//
// CITE: CPE_MULT.pdf
struct MultCell
{
    MultCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(LUT_AND, 4);
        lower->params[id_INIT_L01] = Property(LUT_D1, 4); // CINX
        lower->params[id_INIT_L10] = Property(LUT_XOR, 4);

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(LUT_AND, 4);
        upper->params[id_INIT_L03] = Property(LUT_D1, 4); // PINX
        upper->params[id_INIT_L11] = Property(LUT_XOR, 4);
        upper->params[id_INIT_L20] = Property(LUT_D1, 4); // L11

        upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
        upper->params[id_C_I2] = Property(1, 1); // CINX for L01
        upper->params[id_C_I3] = Property(1, 1); // PINY1 for L02
        upper->params[id_C_I4] = Property(1, 1); // PINX for L03
        upper->params[id_C_FUNCTION] = Property(C_MULT, 3);

        upper->params[id_C_O1] = Property(0b10, 2); // CP_OUT1 -> OUT1
        upper->params[id_C_O2] = Property(0b10, 2); // CP_OUT2 -> OUT2
    }

    PortRef a0() const { return PortRef{upper, id_IN4}; }
    PortRef a1() const { return PortRef{upper, id_IN1}; }
    PortRef a2() const { return PortRef{lower, id_IN1}; }

    CellInfo *upper;
    CellInfo *lower;
};

struct MultMsbCell
{
    MultMsbCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(LUT_AND, 4);
        lower->params[id_INIT_L01] = Property(LUT_D1, 4); // CINX
        lower->params[id_INIT_L10] = Property(LUT_XOR, 4);

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(LUT_AND, 4);
        upper->params[id_INIT_L03] = Property(LUT_D1, 4); // PINX
        upper->params[id_INIT_L11] = Property(LUT_XOR, 4);
        upper->params[id_INIT_L20] = Property(LUT_D1, 4); // L11

        upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
        upper->params[id_C_I2] = Property(1, 1); // CINX for L01
        upper->params[id_C_I3] = Property(1, 1); // PINY1 for L02
        upper->params[id_C_I4] = Property(1, 1); // PINX for L03
        upper->params[id_C_FUNCTION] = Property(C_MULT, 3);
        upper->params[id_C_PY1_I] = Property(1, 1);
        upper->params[id_C_C_P] = Property(1, 1);

        upper->params[id_C_O1] = Property(0b10, 2); // CP_OUT1 -> OUT1
    }

    CellInfo *upper;
    CellInfo *lower;
};

// CITE: CPE_ges_MSB-routing.pdf
struct MsbRoutingCell
{
    MsbRoutingCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(LUT_D1, 4);   // PINY1
        lower->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
        lower->params[id_INIT_L10] = Property(LUT_OR, 4);

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(LUT_ONE, 4);
        upper->params[id_INIT_L03] = Property(LUT_ONE, 4);
        upper->params[id_INIT_L11] = Property(LUT_ZERO, 4);
        upper->params[id_INIT_L20] = Property(LUT_D1, 4); // L11
        upper->params[id_INIT_L30] = Property(LUT_ONE, 4);

        upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
        upper->params[id_C_FUNCTION] = Property(C_MULT, 3);
        upper->params[id_C_SELX] = Property(1, 1);
        upper->params[id_C_SEL_P] = Property(1, 1);
        upper->params[id_C_CX_I] = Property(1, 1);
        upper->params[id_C_PX_I] = Property(1, 1);
        upper->params[id_C_PY1_I] = Property(1, 1);
        upper->params[id_C_PY2_I] = Property(1, 1);

        upper->params[id_C_O2] = Property(0b10, 2); // CP_OUT2 -> OUT2
    }

    CellInfo *upper;
    CellInfo *lower;
};

struct MultiplierColumn
{
    // the variables are in bottom-up physical order as a mnemonic.

    BPassThroughCell b_passthru;
    CarryGenCell carry;
    MultfabCell multfab;
    FRoutingCell f_route;
    std::vector<MultCell> mults;
    MultMsbCell multmsb;
    MsbRoutingCell msb_route;
};

// A GateMate multiplier is made up of columns of 2x2 multipliers.
struct Multiplier
{
    std::vector<APassThroughCell> a_passthrus;
    std::vector<MultiplierColumn> cols;
};

void GateMatePacker::pack_mult()
{
    // note to self: use constr_children for recursive constraints
    // fpga_generic.pas in p_r might have useful info

    auto create_mult_cell = [&](IdString name) {
        // instantiate a full CPE by creating two halves.
        auto *lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$lower", name.c_str(ctx)));
        auto *upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$upper", name.c_str(ctx)));
        return MultCell{lower, upper, name};
    };

    auto create_multfab_cell = [&](IdString name, bool is_multfa) {
        // instantiate a full CPE by creating two halves.
        auto *lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$lower", name.c_str(ctx)));
        auto *upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$upper", name.c_str(ctx)));
        return MultfabCell{lower, upper, name, is_multfa};
    };

    log_info("Packing multipliers...\n");

    auto mults = std::vector<CellInfo *>{};

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_CC_MULT)
            mults.push_back(ci);
    }

    for (auto *mult : mults) {
        auto a_width = int_or_default(mult->params, id_A_WIDTH);
        auto b_width = int_or_default(mult->params, id_B_WIDTH);
        auto p_width = int_or_default(mult->params, id_P_WIDTH);
        log_info("    Configuring '%s' as a %d * %d = %d multiplier\n", mult->name.c_str(ctx), a_width, b_width,
                 p_width);

        NPNR_ASSERT(a_width <= 2);
        NPNR_ASSERT(b_width <= 2);

        auto [mult_l, mult_u] = create_mult_cell(mult->name);
    }
}

NEXTPNR_NAMESPACE_END

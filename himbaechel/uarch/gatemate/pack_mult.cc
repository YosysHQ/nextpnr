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
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

// Propagate A0 through OUT1 and A1 through OUT2
//
// TODO: do we need this cell?
struct APassThroughCell
{
    APassThroughCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(0b0101, 4); // IN1
        lower->params[id_INIT_L01] = Property(0b0000, 4); // 0 (unused)
        lower->params[id_INIT_L10] = Property(0b0101, 4); // L00 -> OUT2

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(0b0101, 4); // IN5
        upper->params[id_INIT_L03] = Property(0b0000, 4); // 0 (unused)
        upper->params[id_INIT_L11] = Property(0b0101, 4); // L02
        upper->params[id_INIT_L20] = Property(0b0101, 4); // L11 -> OUT1
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
        lower->params[id_INIT_L00] = Property(0b0101, 4); // IN1
        lower->params[id_INIT_L01] = Property(0b0000, 4); // 0 (unused)
        lower->params[id_INIT_L10] = Property(0b0101, 4); // L00 -> OUT2

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(0b0101, 4); // IN5
        upper->params[id_INIT_L03] = Property(0b0000, 4); // 0 (unused)
        upper->params[id_INIT_L11] = Property(0b0101, 4); // L02
        upper->params[id_INIT_L20] = Property(0b0101, 4); // L11 -> OUT1

        upper->params[id_C_SEL_C] = Property(0, 1); // OUT2 -> CY1_VAL
        upper->params[id_C_SEL_P] = Property(0, 1); // OUT1 -> PY_VAL
        upper->params[id_C_SELY1] = Property(0, 1); // OUT1 -> PY1_VAL; OUT2 -> CY1_VAL
        upper->params[id_C_CY1_I] = Property(1, 1); // CY1_VAL -> COUTY1
        upper->params[id_C_PY1_I] = Property(1, 1); // PY1_VAL -> POUTY1
    }

    PortRef b0() const { return PortRef{upper, id_IN1}; }
    PortRef b1() const { return PortRef{lower, id_IN1}; }

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
        lower->params[id_INIT_L00] = Property(0b1000, 4); // AND
        lower->params[id_INIT_L01] = Property(0b1100, 4); // CINX
        lower->params[id_INIT_L10] = Property(0b0110, 4); // XOR

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(0b1000, 4); // AND
        upper->params[id_INIT_L03] = Property(0b1100, 4); // PINX
        upper->params[id_INIT_L11] = Property(0b0110, 4); // XOR
        upper->params[id_INIT_L20] = Property(0b1100, 4); // L11

        upper->params[id_C_FUNCTION] = Property(C_MULT, 3);
        upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
        upper->params[id_C_I2] = Property(1, 1); // CINX for L01
        upper->params[id_C_I3] = Property(1, 1); // PINY1 for L02
        upper->params[id_C_I4] = Property(1, 1); // PINX for L03
    }

    CellInfo *upper;
    CellInfo *lower;
};

// This seems to prepare B bits for multiplication.
//
// CITE: CPE_MULTFab.pdf
struct MultfabCell
{
    MultfabCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x) : lower{lower}, upper{upper}
    {
        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(0b1100, 4); // PINY1
        lower->params[id_INIT_L01] = Property(0b1100, 4); // CINX
        lower->params[id_INIT_L10] = Property(0b0110, 4); // XOR

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(0b1100, 4); // PINY1
        upper->params[id_INIT_L03] = Property(0b0000, 4); // 0 (unused)
        upper->params[id_INIT_L11] = Property(0b0101, 4); // L02
        if (is_even_x) {
            // TODO: the diagrams suggest that AND has L10 inverted? need to check.
            upper->params[id_INIT_L20] = Property(0b1000, 4); // L10 AND L11 -> OUT1
        } else {
            upper->params[id_INIT_L20] = Property(0b1110, 4); // L10 OR L11 -> OUT1
        }
        upper->params[id_INIT_L30] = Property(0b1100, 4); // L10 -> COMP_OUT

        upper->params[id_C_FUNCTION] = Property(C_MULT, 3);
        upper->params[id_C_I1] = Property(1, 1);    // PINY1 for L00
        upper->params[id_C_I2] = Property(1, 1);    // CINX for L01
        upper->params[id_C_I3] = Property(1, 1);    // PINY1 for L02
        upper->params[id_C_CX_I] = Property(1, 1);  // CX_VAL -> COUTX
        upper->params[id_C_CY1_I] = Property(1, 1); // CY1_VAL -> COUTY1
        upper->params[id_C_PY1_I] = Property(1, 1); // PY1_VAL -> POUTY1
        upper->params[id_C_SELX] = Property(1, 1);  // inverted CINY2 -> CX_VAL
        upper->params[id_C_SELY1] = Property(0, 1); // COMP_OUT -> CY1_VAL; OUT1 -> PY1_VAL
        upper->params[id_C_Y12] = Property(1, 1);   // inverted CINY2 -> CX_VAL
        upper->params[id_C_SEL_C] = Property(1, 1); // inverted CINY2 -> CX_VAL; COMP_OUT -> CY1_VAL
        upper->params[id_C_SEL_P] = Property(0, 1); // OUT1 -> PY1_VAL
    }

    CellInfo *upper;
    CellInfo *lower;
};

// CITE: CPE_ges_carry-gen.pdf
struct CarryGenCell
{
    CarryGenCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x) : lower{lower}, upper{upper}
    {
        lower->cluster = name;
        lower->constr_abs_z = false;
        lower->params[id_INIT_L00] = Property(0b0000, 4); // 0 (unused)
        lower->params[id_INIT_L01] = Property(0b1100, 4); // CINX
        lower->params[id_INIT_L10] = Property(0b1100, 4); // L01 -> OUT2

        upper->cluster = name;
        upper->constr_abs_z = false;
        upper->params[id_INIT_L02] = Property(0b1100, 4); // PINY1
        upper->params[id_INIT_L03] = Property(0b0000, 4); // 0 (unused)
        if (is_even_x) {
            upper->params[id_INIT_L11] = Property(0b1000, 4); // AND
            upper->params[id_INIT_L20] = Property(0b1000, 4); // AND
        } else {
            upper->params[id_INIT_L11] = Property(0b1110, 4); // OR
            upper->params[id_INIT_L20] = Property(0b1110, 4); // OR
        }
        upper->params[id_INIT_L30] = Property(0b0101, 4); // OUT1 -> COMP_OUT

        upper->params[id_C_I2] = Property(1, 1); // CINX for L01
        upper->params[id_C_I3] = Property(1, 1); // PINY1 for L02
        // TODO: how do I get B1 through CIN??
        upper->params[id_C_PY1_I] = Property(0, 1); // PINY1 -> POUTY1
        upper->params[id_C_CY1_I] = Property(0, 1); // CINY1 -> COUTY1
        upper->params[id_C_CY2_I] = Property(1, 1); // CY2_VAL -> COUTY2
        upper->params[id_C_SEL_C] = Property(1, 1); // COMP_OUT -> CY2_VAL
        upper->params[id_C_SELY2] = Property(0, 1); // COMP_OUT -> CY2_VAL
    }

    CellInfo *upper;
    CellInfo *lower;
};

// CITE: CPE_ges_f-routing-1.pdf
// TODO:
struct FRoutingCell
{
    CellInfo *upper;
    CellInfo *lower;
};

// CITE: CPE_ges_MSB-routing.pdf
struct MsbRoutingCell
{
    CellInfo *upper;
    CellInfo *lower;
};

struct MultiplierColumn
{
    MsbRoutingCell msb_route;
    std::vector<MultCell> mults;
    MultfabCell multfab;
    FRoutingCell f_route;
    BPassThroughCell b_passthru;
};

// A GateMate multiplier is made up of columns of 2x2 multipliers.
struct Multiplier
{
    std::vector<APassThroughCell> a_passthrus;
    std::vector<MultiplierColumn> cols;
};

void GateMatePacker::pack_mult()
{
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

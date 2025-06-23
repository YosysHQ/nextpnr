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
        lower->params[id_INIT_L02] = Property(LUT_D0, 4);   // IN5
        lower->params[id_INIT_L03] = Property(LUT_ZERO, 4); // (unused)
        lower->params[id_INIT_L11] = Property(LUT_D0, 4);   // L02
        lower->params[id_INIT_L20] = Property(LUT_D1, 4);   // L11 -> COMB1OUT
        lower->params[id_INIT_L30] = Property(LUT_ONE, 4);  // zero -> COMP_OUT (L30 is inverted)

        upper->params[id_INIT_L00] = Property(LUT_D0, 4);   // IN1
        upper->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
        upper->params[id_INIT_L10] = Property(LUT_D0, 4);   // L00 -> COMB2OUT

        upper->params[id_C_SEL_C] = Property(1, 1); // COMP_OUT -> CX_VAL
        upper->params[id_C_SEL_P] = Property(1, 1); // COMP_OUT -> PX_VAL
        upper->params[id_C_CX_I] = Property(1, 1);  // CX_VAL -> COUTX
        upper->params[id_C_PX_I] = Property(1, 1);  // PX_VAL -> POUTX

        upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
        upper->params[id_C_O2] = Property(0b11, 2); // COMB2OUT -> OUT2
    }

    void clean_up(Context *ctx)
    {
        auto *lower_net = lower->ports.at(id_IN1).net;
        auto *upper_net = lower->ports.at(id_IN1).net;

        if (lower_net) {
            bool net_is_gnd = lower_net->name == ctx->idf("$PACKER_GND");
            bool net_is_vcc = lower_net->name == ctx->idf("$PACKER_VCC");
            if (net_is_gnd || net_is_vcc) {
                lower->params[id_INIT_L02] = Property(LUT_ZERO, 4);
                lower->params[id_INIT_L11] = Property(LUT_ZERO, 4);
                lower->params[id_INIT_L20] = Property(net_is_vcc ? LUT_ONE : LUT_ZERO, 4);
                lower->disconnectPort(id_IN1);
            }
        }

        if (upper_net) {
            bool net_is_gnd = upper_net->name == ctx->idf("$PACKER_GND");
            bool net_is_vcc = upper_net->name == ctx->idf("$PACKER_VCC");
            if (net_is_gnd || net_is_vcc) {
                upper->params[id_INIT_L00] = Property(LUT_ZERO, 4);
                upper->params[id_INIT_L10] = Property(net_is_vcc ? LUT_ONE : LUT_ZERO, 4);
                upper->disconnectPort(id_IN1);
            }
        }
    }

    CellInfo *lower;
    CellInfo *upper;
};

// Propagate B0 through POUTY1 and B1 through COUTY1
//
// CITE: CPE_ges_Bin.pdf
// TODO: is it worth trying to unify this with APassThroughCell?
struct BPassThroughCell
{
    BPassThroughCell() : lower{nullptr}, upper{nullptr} {}

    BPassThroughCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->params[id_INIT_L02] = Property(LUT_D0, 4);   // IN5
        lower->params[id_INIT_L03] = Property(LUT_ZERO, 4); // (unused)
        lower->params[id_INIT_L11] = Property(LUT_D0, 4);   // L02
        lower->params[id_INIT_L20] = Property(LUT_D1, 4);   // L11 -> COMB1OUT

        upper->params[id_INIT_L00] = Property(LUT_D0, 4);   // IN1
        upper->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
        upper->params[id_INIT_L10] = Property(LUT_D0, 4);   // L00 -> COMB2OUT

        upper->params[id_C_CY1_I] = Property(1, 1); // CY1_VAL -> COUTY1
        upper->params[id_C_PY1_I] = Property(1, 1); // PY1_VAL -> POUTY1

        upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
        upper->params[id_C_O2] = Property(0b11, 2); // COMB2OUT -> OUT2
    }

    void clean_up(Context *ctx)
    {
        auto *lower_net = lower->ports.at(id_IN1).net;
        auto *upper_net = lower->ports.at(id_IN1).net;

        if (lower_net) {
            bool net_is_gnd = lower_net->name == ctx->idf("$PACKER_GND");
            bool net_is_vcc = lower_net->name == ctx->idf("$PACKER_VCC");
            if (net_is_gnd || net_is_vcc) {
                lower->params[id_INIT_L02] = Property(LUT_ZERO, 4);
                lower->params[id_INIT_L11] = Property(LUT_ZERO, 4);
                lower->params[id_INIT_L20] = Property(net_is_vcc ? LUT_ONE : LUT_ZERO, 4);
                lower->disconnectPort(id_IN1);
            }
        }

        if (upper_net) {
            bool net_is_gnd = upper_net->name == ctx->idf("$PACKER_GND");
            bool net_is_vcc = upper_net->name == ctx->idf("$PACKER_VCC");
            if (net_is_gnd || net_is_vcc) {
                upper->params[id_INIT_L00] = Property(LUT_ZERO, 4);
                upper->params[id_INIT_L10] = Property(net_is_vcc ? LUT_ONE : LUT_ZERO, 4);
                upper->disconnectPort(id_IN1);
            }
        }
    }

    CellInfo *lower;
    CellInfo *upper;
};

// TODO: Micko points out this is an L2T4 CPE_HALF
struct CarryGenCell
{
    CarryGenCell() : lower{nullptr}, upper{nullptr} {}

    CarryGenCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x) : lower{lower}, upper{upper}
    {
        // TODO: simplify AND with zero/OR with zero into something more sensical.

        lower->params[id_INIT_L02] = Property(LUT_D1, 4);   // PINY1
        lower->params[id_INIT_L03] = Property(LUT_ZERO, 4); // (unused)
        lower->params[id_INIT_L11] = Property(is_even_x ? LUT_AND : LUT_OR, 4);
        lower->params[id_INIT_L20] = Property(is_even_x ? LUT_AND : LUT_OR, 4);
        lower->params[id_INIT_L30] = Property(LUT_INV_D0, 4); // OUT1 -> COMP_OUT

        upper->params[id_INIT_L00] = Property(LUT_ZERO, 4); // (unused)
        upper->params[id_INIT_L01] = Property(LUT_D1, 4);   // CINX
        upper->params[id_INIT_L10] = Property(is_even_x ? LUT_AND : LUT_OR, 4);

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

    CellInfo *lower;
    CellInfo *upper;
};

// This prepares B bits for multiplication.
struct MultfabCell
{
    MultfabCell() : lower{nullptr}, upper{nullptr} {}

    MultfabCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x) : lower{lower}, upper{upper}
    {
        // TODO: perhaps C_I[1234] could be pips?

        lower->params[id_INIT_L02] = Property(LUT_D1, 4);                              // PINY1
        lower->params[id_INIT_L03] = Property(LUT_ZERO, 4);                            // (unused)
        lower->params[id_INIT_L11] = Property(LUT_D0, 4);                              // L02
        lower->params[id_INIT_L20] = Property(is_even_x ? LUT_AND_INV_D0 : LUT_OR, 4); // L10 AND L11 -> OUT1
        lower->params[id_INIT_L30] = Property(LUT_INV_D1, 4);                          // L10 -> COMP_OUT

        upper->params[id_INIT_L00] = Property(LUT_D1, 4);                        // PINY1
        upper->params[id_INIT_L01] = Property(is_even_x ? LUT_ZERO : LUT_D1, 4); // CINX
        upper->params[id_INIT_L10] = Property(LUT_XOR, 4);                       // XOR

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

    CellInfo *lower;
    CellInfo *upper;
};

// CITE: CPE_ges_f-routing-1.pdf
struct FRoutingCell
{
    FRoutingCell() : lower{nullptr}, upper{nullptr} {}

    FRoutingCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x) : lower{lower}, upper{upper}
    {
        // TODO: simplify AND with zero/OR with zero into something more sensical.

        lower->params[id_INIT_L02] = Property(LUT_ZERO, 4); // (unused)
        lower->params[id_INIT_L03] = Property(LUT_ONE, 4);  // (unused)
        lower->params[id_INIT_L11] = Property(LUT_AND, 4);
        lower->params[id_INIT_L20] = Property(LUT_D1, 4);
        lower->params[id_INIT_L30] = Property(is_even_x ? LUT_ONE : LUT_INV_D1, 4); // OUT1 -> COMP_OUT

        upper->params[id_INIT_L00] = Property(LUT_D1, 4);  // PINY1
        upper->params[id_INIT_L01] = Property(LUT_ONE, 4); // (unused)
        upper->params[id_INIT_L10] = Property(LUT_AND, 4);

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

    CellInfo *lower;
    CellInfo *upper;
};

// Multiply two bits of A with two bits of B.
//
// CITE: CPE_MULT.pdf
struct MultCell
{
    MultCell() : lower{nullptr}, upper{nullptr} {}

    MultCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->params[id_INIT_L02] = Property(LUT_AND, 4);
        lower->params[id_INIT_L03] = Property(LUT_D1, 4); // PINX
        lower->params[id_INIT_L11] = Property(LUT_XOR, 4);
        lower->params[id_INIT_L20] = Property(LUT_D1, 4); // L11

        upper->params[id_INIT_L00] = Property(LUT_AND, 4);
        upper->params[id_INIT_L01] = Property(LUT_D1, 4); // CINX
        upper->params[id_INIT_L10] = Property(LUT_XOR, 4);

        upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
        upper->params[id_C_I2] = Property(1, 1); // CINX for L01
        upper->params[id_C_I3] = Property(1, 1); // PINY1 for L02
        upper->params[id_C_I4] = Property(1, 1); // PINX for L03
        upper->params[id_C_FUNCTION] = Property(C_MULT, 3);

        upper->params[id_C_O1] = Property(0b10, 2); // CP_OUT1 -> OUT1
        upper->params[id_C_O2] = Property(0b10, 2); // CP_OUT2 -> OUT2
    }

    CellInfo *lower;
    CellInfo *upper;
};

struct MultMsbCell
{
    MultMsbCell() : lower{nullptr}, upper{nullptr} {}

    MultMsbCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->params[id_INIT_L02] = Property(LUT_AND, 4);
        lower->params[id_INIT_L03] = Property(LUT_D1, 4); // PINX
        lower->params[id_INIT_L11] = Property(LUT_XOR, 4);
        lower->params[id_INIT_L20] = Property(LUT_D1, 4); // L11

        upper->params[id_INIT_L00] = Property(LUT_AND, 4);
        upper->params[id_INIT_L01] = Property(LUT_D1, 4); // CINX
        upper->params[id_INIT_L10] = Property(LUT_XOR, 4);

        upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
        upper->params[id_C_I2] = Property(1, 1); // CINX for L01
        upper->params[id_C_I3] = Property(1, 1); // PINY1 for L02
        upper->params[id_C_I4] = Property(1, 1); // PINX for L03
        upper->params[id_C_FUNCTION] = Property(C_MULT, 3);
        upper->params[id_C_PY1_I] = Property(1, 1);
        upper->params[id_C_C_P] = Property(1, 1);

        upper->params[id_C_O1] = Property(0b10, 2); // CP_OUT1 -> OUT1
    }

    CellInfo *lower;
    CellInfo *upper;
};

// CITE: CPE_ges_MSB-routing.pdf
struct MsbRoutingCell
{
    MsbRoutingCell() : lower{nullptr}, upper{nullptr} {}

    MsbRoutingCell(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
    {
        lower->params[id_INIT_L02] = Property(LUT_ONE, 4);
        lower->params[id_INIT_L03] = Property(LUT_ONE, 4);
        lower->params[id_INIT_L11] = Property(LUT_ZERO, 4);
        lower->params[id_INIT_L20] = Property(LUT_D1, 4); // L11
        lower->params[id_INIT_L30] = Property(LUT_ONE, 4);

        upper->params[id_INIT_L00] = Property(LUT_D1, 4);   // PINY1
        upper->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
        upper->params[id_INIT_L10] = Property(LUT_OR, 4);

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

    CellInfo *lower;
    CellInfo *upper;
};

struct MultiplierColumn
{
    BPassThroughCell b_passthru;
    CarryGenCell carry;
    MultfabCell multfab;
    FRoutingCell f_route;
    std::vector<MultCell> mults;
    MultMsbCell mult_msb;
    MsbRoutingCell msb_route;
};

// A GateMate multiplier is made up of columns of 2x2 multipliers.
struct Multiplier
{
    std::vector<APassThroughCell> a_passthrus;
    std::vector<MultiplierColumn> cols;

    size_t cpe_count() const
    {
        auto count = a_passthrus.size();
        for (const auto &col : cols) {
            count += 6 + col.mults.size();
        }
        return count;
    }
};

void GateMatePacker::pack_mult()
{
    // note to self: use constr_children for recursive constraints
    // fpga_generic.pas in p_r might have useful info

    auto create_a_passthru = [&](IdString name) {
        auto *a_passthru_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$a_passthru_lower", name.c_str(ctx)));
        auto *a_passthru_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$a_passthru_upper", name.c_str(ctx)));
        return APassThroughCell{a_passthru_lower, a_passthru_upper, name};
    };

    auto create_mult_col = [&](IdString name, int a_width, bool is_even_x) {
        // Ideally this would be the MultiplierColumn constructor, but we need create_cell_ptr here.
        auto col = MultiplierColumn{};

        {
            auto *b_passthru_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$b_passthru_lower", name.c_str(ctx)));
            auto *b_passthru_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$b_passthru_upper", name.c_str(ctx)));
            col.b_passthru = BPassThroughCell{b_passthru_lower, b_passthru_upper, name};
        }

        {
            auto *carry_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$carry_lower", name.c_str(ctx)));
            auto *carry_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$carry_upper", name.c_str(ctx)));
            col.carry = CarryGenCell{carry_lower, carry_upper, name, is_even_x};
        }

        {
            auto *multfab_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$multfab_lower", name.c_str(ctx)));
            auto *multfab_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$multfab_upper", name.c_str(ctx)));
            col.multfab = MultfabCell{multfab_lower, multfab_upper, name, is_even_x};
        }

        {
            auto *f_route_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$f_route_lower", name.c_str(ctx)));
            auto *f_route_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$f_route_upper", name.c_str(ctx)));
            col.f_route = FRoutingCell{f_route_lower, f_route_upper, name, is_even_x};
        }

        for (int i = 0; i < a_width / 2; i++) {
            auto *mult_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$row%d$mult_lower", name.c_str(ctx), i));
            auto *mult_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$row%d$mult_upper", name.c_str(ctx), i));
            col.mults.push_back(MultCell{mult_lower, mult_upper, name});
        }

        {
            auto *mult_msb_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$mult_msb_lower", name.c_str(ctx)));
            auto *mult_msb_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$mult_msb_upper", name.c_str(ctx)));
            col.mult_msb = MultMsbCell{mult_msb_lower, mult_msb_upper, name};
        }

        {
            auto *msb_route_lower = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$msb_route_lower", name.c_str(ctx)));
            auto *msb_route_upper = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$msb_route_upper", name.c_str(ctx)));
            col.msb_route = MsbRoutingCell{msb_route_lower, msb_route_upper, name};
        }

        return col;
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

        // Sign-extend odd A_WIDTH to even, because we're working with 2x2 multiplier cells.
        if (a_width % 2 == 1) {
            mult->copyPortTo(ctx->idf("A[%d]", a_width), mult, ctx->idf("A[%d]", a_width + 1));
            a_width += 1;
        }

        // Sign-extend odd B_WIDTH to even, because we're working with 2x2 multiplier cells.
        if (b_width % 2 == 1) {
            mult->copyPortTo(ctx->idf("B[%d]", b_width), mult, ctx->idf("B[%d]", b_width + 1));
            b_width += 1;
        }

        log_info("    Configuring '%s' as a %d-bit * %d-bit = %d-bit multiplier.\n", mult->name.c_str(ctx), a_width,
                 b_width, p_width);

        auto m = Multiplier{};

        // Step 1: instantiate all the CPEs.
        for (int a = 0; a < a_width / 2; a++)
            m.a_passthrus.push_back(create_a_passthru(ctx->idf("%s$col0$row%d", mult->name.c_str(ctx), a)));
        for (int b = 0; b < b_width / 2; b++)
            m.cols.push_back(create_mult_col(ctx->idf("%s$col%d", mult->name.c_str(ctx), b + 1), a_width, b % 2 == 0));

        // Step 2: constrain them together.
        // We define (0,0) to be the B passthrough cell of column 1.
        auto *root = m.cols[0].b_passthru.upper;
        root->cluster = root->name;

        auto constrain_cell = [&](CellInfo *cell, int x_offset, int y_offset) {
            if (cell == root)
                return;
            root->constr_children.push_back(cell);
            cell->cluster = root->name;
            cell->constr_abs_z = true;
            cell->constr_x = x_offset;
            cell->constr_y = y_offset;
            cell->constr_z = cell->type == id_CPE_HALF_L ? 1 : 0;
        };

        // Constrain A passthrough cells.
        for (int a = 0; a < a_width / 2; a++) {
            auto &a_passthru = m.a_passthrus.at(a);
            constrain_cell(a_passthru.lower, -1, 4 + a);
            constrain_cell(a_passthru.upper, -1, 4 + a);
        }

        // Constrain multiplier columns.
        for (int b = 0; b < b_width / 2; b++) {
            auto &col = m.cols.at(b);
            constrain_cell(col.b_passthru.lower, b, b);
            constrain_cell(col.b_passthru.upper, b, b);

            constrain_cell(col.carry.lower, b, b + 1);
            constrain_cell(col.carry.upper, b, b + 1);

            constrain_cell(col.multfab.lower, b, b + 2);
            constrain_cell(col.multfab.upper, b, b + 2);

            constrain_cell(col.f_route.lower, b, b + 3);
            constrain_cell(col.f_route.upper, b, b + 3);

            for (size_t mult_idx = 0; mult_idx < col.mults.size(); mult_idx++) {
                constrain_cell(col.mults[mult_idx].lower, b, b + 4 + mult_idx);
                constrain_cell(col.mults[mult_idx].upper, b, b + 4 + mult_idx);
            }

            constrain_cell(col.mult_msb.lower, b, b + 4 + col.mults.size());
            constrain_cell(col.mult_msb.upper, b, b + 4 + col.mults.size());

            constrain_cell(col.msb_route.lower, b, b + 4 + col.mults.size() + 1);
            constrain_cell(col.msb_route.upper, b, b + 4 + col.mults.size() + 1);
        }

        // Step 3: connect them.

        // TODO: check this with odd bus widths.

        for (int a = 0; a < a_width; a++) {
            auto &a_passthru = m.a_passthrus.at(a / 2);
            auto *cpe_half = (a % 2 == 1) ? a_passthru.upper : a_passthru.lower;

            // Connect A input passthrough cell.
            mult->movePortTo(ctx->idf("A[%d]", a), cpe_half, id_IN1);

            // This may be GND/VCC; if so, clean it up.
            a_passthru.clean_up(ctx);

            // Connect A passthrough output to multiplier inputs.
            auto *a_net = ctx->createNet(cpe_half->name);
            cpe_half->connectPort(id_OUT, a_net);

            for (int b = 0; b < b_width / 2; b++) {
                {
                    auto &mult_row = m.cols.at(b).mults.at(a / 2);
                    auto *mult_cell = (a % 2 == 1) ? mult_row.upper : mult_row.lower;

                    mult_cell->connectPort(id_IN1, a_net);
                }

                // A upper out signals must also go to the CPE above.
                if ((a % 2) == 1) {
                    auto &mult_col = m.cols.at(b);
                    if ((a / 2 + 1) < (a_width / 2)) {
                        auto *mult_cell = mult_col.mults.at(a / 2 + 1).lower;

                        mult_cell->connectPort(id_IN4, a_net); // IN8
                    } else {
                        auto &mult_cell = mult_col.mult_msb;

                        mult_cell.upper->connectPort(id_IN1, a_net); // IN1
                        mult_cell.lower->connectPort(id_IN1, a_net); // IN5
                        mult_cell.lower->connectPort(id_IN4, a_net); // IN8
                    }
                }
            }
        }

        for (int b = 0; b < b_width; b++) {
            auto &b_passthru = m.cols[b / 2].b_passthru;
            auto *cpe_half = (b % 2 == 1) ? b_passthru.upper : b_passthru.lower;

            // Connect B input passthrough cell.
            mult->movePortTo(ctx->idf("B[%d]", b), cpe_half, id_IN1);

            // This may be GND/VCC; if so, clean it up.
            b_passthru.clean_up(ctx);
        }
        {
            auto &b_passthru = m.cols.back().b_passthru;
            mult->copyPortTo(ctx->idf("B[%d]", b_width - 1), b_passthru.upper, id_IN1);
            mult->copyPortTo(ctx->idf("B[%d]", b_width - 1), b_passthru.lower, id_IN1);
        }

        // Connect P outputs.
        auto diagonal_p_width = std::min(b_width, p_width);
        auto vertical_p_width = std::max(p_width - b_width, 0);

        for (int p = 0; p < diagonal_p_width; p++) {
            auto &mult_cell = m.cols[p / 2].mults[0];
            auto *cpe_half = (p % 2 == 1) ? mult_cell.upper : mult_cell.lower;

            mult->movePortTo(ctx->idf("P[%d]", p), cpe_half, id_OUT);
        }

        for (int p = 0; p < vertical_p_width; p++) {
            auto &mult_cell = m.cols.back().mults[1 + (p / 2)];
            auto *cpe_half = (p % 2 == 1) ? mult_cell.upper : mult_cell.lower;

            mult->movePortTo(ctx->idf("P[%d]", p + diagonal_p_width), cpe_half, id_OUT);
        }

        ctx->cells.erase(mult->name);

        log_info("        Created %zu CPEs.\n", m.cpe_count());
    }
}

NEXTPNR_NAMESPACE_END

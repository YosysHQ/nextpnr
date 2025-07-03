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
#include "log.h"
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"
#include "pack.h"
#include "property.h"
#include "uarch/gatemate/extra_data.h"
#include "uarch/gatemate/gatemate.h"
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

// Constant zero.
struct ZeroDriver
{
    ZeroDriver() : lower{nullptr}, upper{nullptr} {}
    ZeroDriver(CellInfo *lower, CellInfo *upper, IdString name);

    CellInfo *lower;
    CellInfo *upper;
};

// Propagate A0 through OUT1 and A1 through OUT2; zero COUTX and POUTX.
struct APassThroughCell
{
    APassThroughCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name);

    void clean_up_cell(Context *ctx, CellInfo *cell);

    CellInfo *lower;
    CellInfo *upper;
    CellInfo *comp;
    CellInfo *cplines;
};

// Propagate B0 through POUTY1 and B1 through COUTY1
struct BPassThroughCell
{
    BPassThroughCell() : lower{nullptr}, upper{nullptr}, cplines{nullptr} {}
    BPassThroughCell(CellInfo *lower, CellInfo *upper, CellInfo *cplines, IdString name);

    void clean_up_cell(Context *ctx, CellInfo *cell);

    CellInfo *lower;
    CellInfo *upper;
    CellInfo *cplines;
};

// TODO: Micko points out this is an L2T4 CPE_HALF
struct CarryGenCell
{
    CarryGenCell() : lower{nullptr}, upper{nullptr}, comp{nullptr}, cplines{nullptr} {}
    CarryGenCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name, bool is_odd_x,
                 bool enable_cinx);

    CellInfo *lower;
    CellInfo *upper;
    CellInfo *comp;
    CellInfo *cplines;
};

// This prepares B bits for multiplication.
// CITE: CPE_MULTFab.pdf
struct MultfabCell
{
    MultfabCell() : lower{nullptr}, upper{nullptr}, comp{nullptr}, cplines{nullptr} {}
    MultfabCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name, bool is_even_x,
                bool enable_cinx);

    CellInfo *lower;
    CellInfo *upper;
    CellInfo *comp;
    CellInfo *cplines;
};

// CITE: CPE_ges_f-routing1.pdf for !is_even_x; CPE_ges_f-routing2 for is_even_x
struct FRoutingCell
{
    FRoutingCell() : lower{nullptr}, upper{nullptr}, comp{nullptr}, cplines{nullptr} {}
    FRoutingCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name, bool is_even_x);

    CellInfo *lower;
    CellInfo *upper;
    CellInfo *comp;
    CellInfo *cplines;
};

// Multiply two bits of A with two bits of B.
//
// CITE: CPE_MULT.pdf
struct MultCell
{
    MultCell() : lower{nullptr}, upper{nullptr}, cplines{nullptr} {}
    MultCell(CellInfo *lower, CellInfo *upper, CellInfo *cplines, IdString name, bool is_msb);

    CellInfo *lower;
    CellInfo *upper;
    CellInfo *cplines;
};

// CITE: CPE_ges_MSB-routing.pdf
struct MsbRoutingCell
{
    MsbRoutingCell() : lower{nullptr}, upper{nullptr}, comp{nullptr}, cplines{nullptr} {}
    MsbRoutingCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name);

    CellInfo *lower;
    CellInfo *upper;
    CellInfo *comp;
    CellInfo *cplines;
};

struct MultiplierColumn
{
    BPassThroughCell b_passthru;
    CarryGenCell carry;
    MultfabCell multfab;
    FRoutingCell f_route;
    std::vector<MultCell> mults;
    MsbRoutingCell msb_route;
};

// A GateMate multiplier is made up of columns of 2x2 multipliers.
struct Multiplier
{
    ZeroDriver zero;
    std::vector<APassThroughCell> a_passthrus;
    std::vector<MultiplierColumn> cols;

    size_t cpe_count() const
    {
        auto count = 1 /* (zero driver) */ + a_passthrus.size();
        for (const auto &col : cols) {
            count += 4 /* (b_passthru, carry, multfab, f_route) */ + col.mults.size() + 1 /* (msb_route) */;
        }
        return count;
    }
};

ZeroDriver::ZeroDriver(CellInfo *lower, CellInfo *upper, IdString name) : lower{lower}, upper{upper}
{
    upper->params[id_INIT_L00] = Property(LUT_ZERO, 4); // (unused)
    upper->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
    upper->params[id_INIT_L10] = Property(LUT_ZERO, 4); // (unused)
}

APassThroughCell::APassThroughCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name)
        : lower{lower}, upper{upper}, comp{comp}, cplines{cplines}
{
    lower->params[id_INIT_L00] = Property(LUT_D0, 4);   // IN5
    lower->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
    lower->params[id_INIT_L10] = Property(LUT_D0, 4);   // L02

    comp->params[id_INIT_L30] = Property(LUT_ONE, 4); // zero -> COMP_OUT (L30 is inverted)

    upper->params[id_INIT_L00] = Property(LUT_D0, 4);   // IN1
    upper->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
    upper->params[id_INIT_L10] = Property(LUT_D0, 4);   // L00 -> COMB2OUT

    cplines->params[id_C_SEL_C] = Property(1, 1); // COMP_OUT -> CX_VAL
    cplines->params[id_C_SEL_P] = Property(1, 1); // COMP_OUT -> PX_VAL
    cplines->params[id_C_CX_I] = Property(1, 1);  // CX_VAL -> COUTX
    cplines->params[id_C_PX_I] = Property(1, 1);  // PX_VAL -> POUTX
}

void APassThroughCell::clean_up_cell(Context *ctx, CellInfo *cell)
{
    auto *net = cell->ports.at(id_IN1).net;

    NPNR_ASSERT(net != nullptr);

    bool net_is_gnd = net->name == ctx->idf("$PACKER_GND");
    bool net_is_vcc = net->name == ctx->idf("$PACKER_VCC");
    if (net_is_gnd || net_is_vcc) {
        cell->params[id_INIT_L00] = Property(LUT_ZERO, 4);
        cell->params[id_INIT_L10] = Property(net_is_vcc ? LUT_ONE : LUT_ZERO, 4);
        cell->disconnectPort(id_IN1);
    }
}

// B0 -> POUTY1; B1 -> COUTY1
BPassThroughCell::BPassThroughCell(CellInfo *lower, CellInfo *upper, CellInfo *cplines, IdString name)
        : lower{lower}, upper{upper}, cplines{cplines}
{
    lower->params[id_INIT_L00] = Property(LUT_D0, 4);   // IN5
    lower->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
    lower->params[id_INIT_L10] = Property(LUT_D0, 4);   // L02

    upper->params[id_INIT_L00] = Property(LUT_D0, 4);   // IN1
    upper->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
    upper->params[id_INIT_L10] = Property(LUT_D0, 4);   // L00 -> COMB2OUT

    cplines->params[id_C_SEL_C] = Property(0, 1); // COMB2OUT -> CY1_VAL
    cplines->params[id_C_SEL_P] = Property(0, 1); // COMB1OUT -> PY1_VAL
    cplines->params[id_C_SELY1] = Property(0, 1); // COMB1OUT -> PY1_VAL; COMB2OUT -> CY1_VAL
    cplines->params[id_C_CY1_I] = Property(1, 1); // CY1_VAL -> COUTY1
    cplines->params[id_C_PY1_I] = Property(1, 1); // PY1_VAL -> POUTY1
}

void BPassThroughCell::clean_up_cell(Context *ctx, CellInfo *cell)
{
    auto *net = cell->ports.at(id_IN1).net;

    NPNR_ASSERT(net != nullptr);

    bool net_is_gnd = net->name == ctx->idf("$PACKER_GND");
    bool net_is_vcc = net->name == ctx->idf("$PACKER_VCC");
    if (net_is_gnd || net_is_vcc) {
        cell->params[id_INIT_L00] = Property(LUT_ZERO, 4);
        cell->params[id_INIT_L10] = Property(net_is_vcc ? LUT_ONE : LUT_ZERO, 4);
        cell->disconnectPort(id_IN1);
    }
}

CarryGenCell::CarryGenCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name,
                           bool is_odd_x, bool enable_cinx)
        : lower{lower}, upper{upper}, comp{comp}, cplines{cplines}
{
    lower->params[id_INIT_L00] = Property(LUT_D1, 4);   // PINY1
    lower->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (overriden by CIN)
    lower->params[id_INIT_L10] = Property(is_odd_x ? LUT_OR : LUT_ZERO, 4);
    lower->params[id_INIT_L20] = Property(is_odd_x ? LUT_OR : LUT_ZERO, 4);
    lower->params[id_C_FUNCTION] = Property(C_EN_CIN, 3);
    lower->params[id_C_I3] = Property(1, 1); // PINY1 for L02
    lower->params[id_C_HORIZ] = Property(0, 1); // CINY1 for CIN_ for L03

    upper->params[id_INIT_L00] = Property(LUT_ZERO, 4);                        // (unused)
    upper->params[id_INIT_L01] = Property(enable_cinx ? LUT_D1 : LUT_ZERO, 4); // CINX
    upper->params[id_INIT_L10] = Property(LUT_D1, 4);
    upper->params[id_C_I2] = Property(1, 1); // CINX for L01

    comp->params[id_INIT_L30] = Property(LUT_INV_D0, 4); // OUT1 -> COMP_OUT

    cplines->params[id_C_PY1_I] = Property(0, 1); // PINY1 -> POUTY1
    cplines->params[id_C_CY1_I] = Property(0, 1); // CINY1 -> COUTY1
    cplines->params[id_C_CY2_I] = Property(1, 1); // CY2_VAL -> COUTY2
    cplines->params[id_C_SEL_C] = Property(1, 1); // COMP_OUT -> CY2_VAL
    cplines->params[id_C_SELY2] = Property(0, 1); // COMP_OUT -> CY2_VAL

    //upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
}

MultfabCell::MultfabCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name,
                         bool is_even_x, bool enable_cinx)
        : lower{lower}, upper{upper}, comp{comp}, cplines{cplines}
{
    // TODO: perhaps C_I[1234] could be pips?

    lower->params[id_INIT_L00] = Property(LUT_D1, 4);                              // PINY1
    //lower->params[id_INIT_L01] = Property(LUT_ZERO, 4);                            // (unused)
    lower->params[id_INIT_L10] = Property(LUT_D0, 4);                              // L02
    lower->params[id_INIT_L20] = Property(is_even_x ? LUT_AND_INV_D0 : LUT_OR, 4); // L10 AND L11 -> OUT1
    lower->params[id_C_FUNCTION] = Property(C_ADDCIN, 3);

    comp->params[id_INIT_L30] = Property(LUT_INV_D1, 4); // L10 -> COMP_OUT

    upper->params[id_INIT_L00] = Property(LUT_D1, 4);                          // PINY1
    upper->params[id_INIT_L01] = Property(enable_cinx ? LUT_D1 : LUT_ZERO, 4); // CINX
    upper->params[id_INIT_L10] = Property(LUT_XOR, 4);                         // XOR

    upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
    upper->params[id_C_I2] = Property(1, 1); // CINX for L01
    lower->params[id_C_I3] = Property(1, 1); // PINY1 for L02
    //upper->params[id_C_FUNCTION] = Property(C_ADDCIN, 3);

    cplines->params[id_C_SELX] = Property(1, 1);  // inverted CINY2 -> CX_VAL
    cplines->params[id_C_SEL_C] = Property(1, 1); // inverted CINY2 -> CX_VAL; COMP_OUT -> CY1_VAL
    cplines->params[id_C_Y12] = Property(1, 1);   // inverted CINY2 -> CX_VAL
    cplines->params[id_C_CX_I] = Property(1, 1);  // CX_VAL -> COUTX
    cplines->params[id_C_CY1_I] = Property(1, 1); // CY1_VAL -> COUTY1
    cplines->params[id_C_PY1_I] = Property(1, 1); // PY1_VAL -> POUTY1
    cplines->params[id_C_SEL_P] = Property(0, 1); // OUT1 -> PY1_VAL
    cplines->params[id_C_SELY1] = Property(0, 1); // COMP_OUT -> CY1_VAL; OUT1 -> PY1_VAL

    //upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
}

FRoutingCell::FRoutingCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name,
                           bool is_even_x)
        : lower{lower}, upper{upper}, comp{comp}, cplines{cplines}
{
    // TODO: simplify AND with zero/OR with zero into something more sensical.

    lower->params[id_INIT_L00] = Property(LUT_ZERO, 4); // (unused)
    lower->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
    lower->params[id_INIT_L10] = Property(LUT_ZERO, 4);
    lower->params[id_INIT_L20] = Property(LUT_D1, 4);
    lower->params[id_C_FUNCTION] = Property(C_ADDCIN, 3);

    comp->params[id_INIT_L30] = Property(is_even_x ? LUT_ONE : LUT_INV_D1, 4); // L10 -> COMP_OUT

    upper->params[id_INIT_L00] = Property(LUT_D1, 4);  // PINY1
    //upper->params[id_INIT_L01] = Property(LUT_ONE, 4); // (unused)
    upper->params[id_INIT_L10] = Property(LUT_D0, 4);
    upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
    //upper->params[id_C_FUNCTION] = Property(C_ADDCIN, 3);

    cplines->params[id_C_SELX] = Property(1, 1);
    cplines->params[id_C_SEL_C] = Property(1, 1);
    cplines->params[id_C_Y12] = Property(1, 1);
    cplines->params[id_C_CX_I] = Property(1, 1);
    cplines->params[id_C_CY1_I] = Property(is_even_x, 1);
    cplines->params[id_C_CY2_I] = Property(1, 1);
    cplines->params[id_C_PY1_I] = Property(1, 1);
    cplines->params[id_C_PY2_I] = Property(1, 1);

    //upper->params[id_C_O1] = Property(0b11, 2); // COMB1OUT -> OUT1
    //upper->params[id_C_O2] = Property(0b11, 2); // COMB2OUT -> OUT2
}

MultCell::MultCell(CellInfo *lower, CellInfo *upper, CellInfo *cplines, IdString name, bool is_msb)
        : lower{lower}, upper{upper}, cplines{cplines}
{
    lower->params[id_INIT_L02] = Property(LUT_AND, 4);
    lower->params[id_INIT_L03] = Property(LUT_D1, 4); // PINX
    lower->params[id_INIT_L11] = Property(LUT_XOR, 4);
    //lower->params[id_INIT_L20] = Property(LUT_D1, 4); // L11
    lower->params[id_C_FUNCTION] = Property(C_MULT, 3);

    upper->params[id_INIT_L00] = Property(LUT_AND, 4);
    upper->params[id_INIT_L01] = Property(LUT_D1, 4); // CINX
    upper->params[id_INIT_L10] = Property(LUT_XOR, 4);

    upper->params[id_C_I1] = Property(1, 1); // PINY1 for L00
    upper->params[id_C_I2] = Property(1, 1); // CINX for L01
    lower->params[id_C_I3] = Property(1, 1); // PINY1 for L02
    lower->params[id_C_I4] = Property(1, 1); // PINX for L03
    upper->params[id_C_FUNCTION] = Property(C_MULT, 3);

    if (is_msb) {
        cplines->params[id_C_PY1_I] = Property(1, 1);
        lower->params[id_C_C_P] = Property(1, 1);
    } else {
        cplines->params[id_C_PY1_I] = Property(0, 1);
        lower->params[id_C_C_P] = Property(0, 1);
    }

    //upper->params[id_C_O1] = Property(0b10, 2); // CP_OUT1 -> OUT1
    //upper->params[id_C_O2] = Property(0b10, 2); // CP_OUT2 -> OUT2
}

MsbRoutingCell::MsbRoutingCell(CellInfo *lower, CellInfo *upper, CellInfo *comp, CellInfo *cplines, IdString name)
        : lower{lower}, upper{upper}, comp{comp}, cplines{cplines}
{
    //lower->params[id_INIT_L02] = Property(LUT_ZERO, 4); // (unused)
    //lower->params[id_INIT_L03] = Property(LUT_ZERO, 4); // (unused)
    //lower->params[id_INIT_L11] = Property(LUT_ZERO, 4); // (unused)
    //lower->params[id_INIT_L20] = Property(LUT_ZERO, 4); // (unused)

    comp->params[id_INIT_L30] = Property(LUT_ONE, 4); // zero -> COMP_OUT (L30 is inverted)

    upper->params[id_INIT_L00] = Property(LUT_D1, 4);   // PINY1
    upper->params[id_INIT_L01] = Property(LUT_ZERO, 4); // (unused)
    upper->params[id_INIT_L10] = Property(LUT_D0, 4);   // L00 -> COMB2OUT
    upper->params[id_C_I1] = Property(1, 1);            // PINY1 for L00

    cplines->params[id_C_SELX] = Property(1, 1);  // COMB2OUT -> CX_VAL; PINY1 -> PX_VAL
    cplines->params[id_C_SELY1] = Property(0, 1); // COMP_OUT -> PY1_VAL
    cplines->params[id_C_SELY2] = Property(0, 1); // COMP_OUT -> PY2_VAL
    cplines->params[id_C_SEL_P] = Property(1, 1); // PINY1 -> PX_VAL; COMP_OUT -> PY1_VAL; COMP_OUT -> PY2_VAL
    cplines->params[id_C_CX_I] = Property(1, 1);  // CX_VAL -> COUTX
    cplines->params[id_C_PX_I] = Property(1, 1);  // PX_VAL -> POUTX
    cplines->params[id_C_PY1_I] = Property(1, 1); // PY1_VAL -> POUTY1
    cplines->params[id_C_PY2_I] = Property(1, 1); // PY2_VAL -> POUTY2

    //upper->params[id_C_O2] = Property(0b11, 2); // COMB2 -> OUT2
}

void GateMatePacker::pack_mult()
{
    // note to self: use constr_children for recursive constraints
    // fpga_generic.pas in p_r might have useful info

    auto create_zero_driver = [&](IdString name) {
        auto *zero_lower = create_cell_ptr(id_CPE_DUMMY, ctx->idf("%s$zero_lower", name.c_str(ctx)));
        auto *zero_upper = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$zero", name.c_str(ctx)));
        return ZeroDriver{zero_lower, zero_upper, name};
    };

    auto create_a_passthru = [&](IdString name) {
        auto *a_passthru_lower = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$a_passthru_lower", name.c_str(ctx)));
        auto *a_passthru_upper = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$a_passthru_upper", name.c_str(ctx)));
        auto *a_passthru_comp = create_cell_ptr(id_CPE_COMP, ctx->idf("%s$a_passthru_comp", name.c_str(ctx)));
        auto *a_passthru_lines = create_cell_ptr(id_CPE_CPLINES, ctx->idf("%s$a_passthru_cplines", name.c_str(ctx)));

        NetInfo *comp_conn = ctx->createNet(ctx->idf("%s$a_passthru_comp$compout", name.c_str(ctx)));
        a_passthru_comp->connectPort(id_COMPOUT, comp_conn);
        a_passthru_lines->connectPort(id_COMPOUT, comp_conn);

        return APassThroughCell{a_passthru_lower, a_passthru_upper, a_passthru_comp, a_passthru_lines, name};
    };

    auto create_mult_col = [&](IdString name, int a_width, bool is_even_x, bool carry_enable_cinx,
                               bool multfab_enable_cinx) {
        // Ideally this would be the MultiplierColumn constructor, but we need create_cell_ptr here.
        auto col = MultiplierColumn{};

        {
            auto *b_passthru_lower = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$b_passthru_lower", name.c_str(ctx)));
            auto *b_passthru_upper = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$b_passthru_upper", name.c_str(ctx)));
            auto *b_passthru_lines =
                    create_cell_ptr(id_CPE_CPLINES, ctx->idf("%s$b_passthru_cplines", name.c_str(ctx)));

            NetInfo *comb1_conn = ctx->createNet(ctx->idf("%s$b_passthru$comb1", name.c_str(ctx)));
            b_passthru_lower->connectPort(id_OUT, comb1_conn);
            b_passthru_lines->connectPort(id_OUT1, comb1_conn);

            NetInfo *comb2_conn = ctx->createNet(ctx->idf("%s$b_passthru$comb2", name.c_str(ctx)));
            b_passthru_upper->connectPort(id_OUT, comb2_conn);
            b_passthru_lines->connectPort(id_OUT2, comb2_conn);

            col.b_passthru = BPassThroughCell{b_passthru_lower, b_passthru_upper, b_passthru_lines, name};
        }

        {
            auto *carry_lower = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$carry_lower", name.c_str(ctx)));
            auto *carry_upper = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$carry_upper", name.c_str(ctx)));
            auto *carry_comp = create_cell_ptr(id_CPE_COMP, ctx->idf("%s$carry_comp", name.c_str(ctx)));
            auto *carry_lines = create_cell_ptr(id_CPE_CPLINES, ctx->idf("%s$carry_lines", name.c_str(ctx)));

            NetInfo *comp_in = ctx->createNet(ctx->idf("%s$carry$comp_in", name.c_str(ctx)));
            carry_lower->connectPort(id_OUT, comp_in);
            carry_comp->connectPort(id_COMB1, comp_in);

            NetInfo *comp_out = ctx->createNet(ctx->idf("%s$carry$comp_out", name.c_str(ctx)));
            carry_comp->connectPort(id_COMPOUT, comp_out);
            carry_lines->connectPort(id_COMPOUT, comp_out);

            col.carry = CarryGenCell{carry_lower, carry_upper, carry_comp,       carry_lines,
                                     name,        !is_even_x,  carry_enable_cinx};
        }

        {
            auto *multfab_lower =
                    create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$multf%c_lower", name.c_str(ctx), is_even_x ? 'a' : 'b'));
            auto *multfab_upper =
                    create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$multf%c_upper", name.c_str(ctx), is_even_x ? 'a' : 'b'));
            auto *multfab_comp =
                    create_cell_ptr(id_CPE_COMP, ctx->idf("%s$multf%c_comp", name.c_str(ctx), is_even_x ? 'a' : 'b'));
            auto *multfab_lines = create_cell_ptr(
                    id_CPE_CPLINES, ctx->idf("%s$multf%c_cplines", name.c_str(ctx), is_even_x ? 'a' : 'b'));

            NetInfo *comb1_conn = ctx->createNet(ctx->idf("%s$multf%c$comb1", name.c_str(ctx), is_even_x ? 'a' : 'b'));
            multfab_lower->connectPort(id_OUT, comb1_conn);
            multfab_lines->connectPort(id_OUT1, comb1_conn);

            NetInfo *comb2_conn = ctx->createNet(ctx->idf("%s$multf%c$comb2", name.c_str(ctx), is_even_x ? 'a' : 'b'));
            multfab_upper->connectPort(id_OUT, comb2_conn);
            multfab_comp->connectPort(id_COMB2, comb2_conn);

            NetInfo *comp_out = ctx->createNet(ctx->idf("%s$multf%c$comp_out", name.c_str(ctx), is_even_x ? 'a' : 'b'));
            multfab_comp->connectPort(id_COMPOUT, comp_out);
            multfab_lines->connectPort(id_COMPOUT, comp_out);

            col.multfab = MultfabCell{multfab_lower, multfab_upper, multfab_comp,       multfab_lines,
                                      name,          is_even_x,     multfab_enable_cinx};
        }

        {
            auto *f_route_lower = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$f_route_lower", name.c_str(ctx)));
            auto *f_route_upper = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$f_route_upper", name.c_str(ctx)));
            auto *f_route_comp = create_cell_ptr(id_CPE_COMP, ctx->idf("%s$f_route_comp", name.c_str(ctx)));
            auto *f_route_lines = create_cell_ptr(id_CPE_CPLINES, ctx->idf("%s$f_route_lines", name.c_str(ctx)));

            NetInfo *comb1_conn = ctx->createNet(ctx->idf("%s$f_route$comb1", name.c_str(ctx)));
            f_route_lower->connectPort(id_OUT, comb1_conn);
            f_route_lines->connectPort(id_OUT1, comb1_conn);

            NetInfo *comb2_conn = ctx->createNet(ctx->idf("%s$f_route$comb2", name.c_str(ctx)));
            f_route_upper->connectPort(id_OUT, comb2_conn);
            f_route_lines->connectPort(id_OUT2, comb2_conn);
            if (!is_even_x) {
                f_route_comp->connectPort(id_COMB2, comb2_conn);
            }

            NetInfo *comp_out = ctx->createNet(ctx->idf("%s$f_route$comp_out", name.c_str(ctx)));
            f_route_comp->connectPort(id_COMPOUT, comp_out);
            f_route_lines->connectPort(id_COMPOUT, comp_out);

            col.f_route = FRoutingCell{f_route_lower, f_route_upper, f_route_comp, f_route_lines, name, is_even_x};
        }

        for (int i = 0; i < (a_width / 2); i++) {
            auto *mult_lower = create_cell_ptr(id_CPE_LT_L, ctx->idf("%s$row%d$mult_lower", name.c_str(ctx), i));
            auto *mult_upper = create_cell_ptr(id_CPE_LT_U, ctx->idf("%s$row%d$mult_upper", name.c_str(ctx), i));
            auto *mult_lines = create_cell_ptr(id_CPE_CPLINES, ctx->idf("%s$row%d$mult_lines", name.c_str(ctx), i));

            col.mults.push_back(MultCell{mult_lower, mult_upper, mult_lines, name, i == ((a_width / 2) - 1)});
            uarch->multipliers.push_back(mult_lower);
        }

        {
            auto *msb_route_lower = create_cell_ptr(id_CPE_DUMMY, ctx->idf("%s$msb_route_lower", name.c_str(ctx)));
            auto *msb_route_upper = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$msb_route", name.c_str(ctx)));
            auto *msb_route_comp = create_cell_ptr(id_CPE_COMP, ctx->idf("%s$msb_route_comp", name.c_str(ctx)));
            auto *msb_route_lines = create_cell_ptr(id_CPE_CPLINES, ctx->idf("%s$msb_route_lines", name.c_str(ctx)));

            NetInfo *comp_conn = ctx->createNet(ctx->idf("%s$msb_route$compout", name.c_str(ctx)));
            msb_route_comp->connectPort(id_COMPOUT, comp_conn);
            msb_route_lines->connectPort(id_COMPOUT, comp_conn);

            col.msb_route = MsbRoutingCell{msb_route_lower, msb_route_upper, msb_route_comp, msb_route_lines, name};
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
        while (a_width < p_width || a_width % 2 == 1) {
            mult->copyPortTo(ctx->idf("A[%d]", a_width - 1), mult, ctx->idf("A[%d]", a_width));
            a_width += 1;
        }

        // Sign-extend odd B_WIDTH to even, because we're working with 2x2 multiplier cells.
        while (b_width < p_width || b_width % 2 == 1) {
            mult->copyPortTo(ctx->idf("B[%d]", b_width - 1), mult, ctx->idf("B[%d]", b_width));
            b_width += 1;
        }

        log_info("    Configuring '%s' as a %d-bit * %d-bit = %d-bit multiplier.\n", mult->name.c_str(ctx), a_width,
                 b_width, p_width);

        auto m = Multiplier{};

        // Step 1: instantiate all the CPEs.
        m.zero = create_zero_driver(ctx->idf("%s$col0", mult->name.c_str(ctx)));
        for (int a = 0; a < a_width / 2; a++)
            m.a_passthrus.push_back(create_a_passthru(ctx->idf("%s$col0$row%d", mult->name.c_str(ctx), a)));
        for (int b = 0; b < b_width / 2; b++)
            m.cols.push_back(create_mult_col(ctx->idf("%s$col%d", mult->name.c_str(ctx), b + 1), a_width, b % 2 == 0,
                                             b == 2 /* ??? */, b > 0 /* ??? */));

        // Step 2: constrain them together.
        // We define (0, 0) to be the B passthrough cell of column 1.
        // we also constrain it to proper Z location
        auto *root = m.cols[0].b_passthru.upper;
        root->cluster = root->name;
        root->constr_abs_z = true;
        root->constr_z = CPE_LT_U_Z;

        auto constrain_cell = [&](CellInfo *cell, int x_offset, int y_offset, int z_offset) {
            if (cell == root)
                return;
            root->constr_children.push_back(cell);
            cell->cluster = root->name;
            cell->constr_abs_z = true;
            cell->constr_x = x_offset;
            cell->constr_y = y_offset;
            cell->constr_z = z_offset;
        };

        // Constrain zero driver.
        constrain_cell(m.zero.lower, -1, 3, CPE_LT_L_Z);
        constrain_cell(m.zero.upper, -1, 3, CPE_LT_U_Z);

        // Constrain A passthrough cells.
        for (int a = 0; a < a_width / 2; a++) {
            auto &a_passthru = m.a_passthrus.at(a);
            constrain_cell(a_passthru.lower, -1, 4 + a, CPE_LT_L_Z);
            constrain_cell(a_passthru.upper, -1, 4 + a, CPE_LT_U_Z);
            constrain_cell(a_passthru.comp, -1, 4 + a, CPE_COMP_Z);
            constrain_cell(a_passthru.cplines, -1, 4 + a, CPE_CPLINES_Z);
        }

        // Constrain multiplier columns.
        for (int b = 0; b < b_width / 2; b++) {
            auto &col = m.cols.at(b);
            constrain_cell(col.b_passthru.lower, b, b, CPE_LT_L_Z);
            constrain_cell(col.b_passthru.upper, b, b, CPE_LT_U_Z);
            constrain_cell(col.b_passthru.cplines, b, b, CPE_CPLINES_Z);

            constrain_cell(col.carry.lower, b, b + 1, CPE_LT_L_Z);
            constrain_cell(col.carry.upper, b, b + 1, CPE_LT_U_Z);
            constrain_cell(col.carry.comp, b, b + 1, CPE_COMP_Z);
            constrain_cell(col.carry.cplines, b, b + 1, CPE_CPLINES_Z);

            constrain_cell(col.multfab.lower, b, b + 2, CPE_LT_L_Z);
            constrain_cell(col.multfab.upper, b, b + 2, CPE_LT_U_Z);
            constrain_cell(col.multfab.comp, b, b + 2, CPE_COMP_Z);
            constrain_cell(col.multfab.cplines, b, b + 2, CPE_CPLINES_Z);

            constrain_cell(col.f_route.lower, b, b + 3, CPE_LT_L_Z);
            constrain_cell(col.f_route.upper, b, b + 3, CPE_LT_U_Z);
            constrain_cell(col.f_route.comp, b, b + 3, CPE_COMP_Z);
            constrain_cell(col.f_route.cplines, b, b + 3, CPE_CPLINES_Z);

            for (size_t mult_idx = 0; mult_idx < col.mults.size(); mult_idx++) {
                constrain_cell(col.mults[mult_idx].lower, b, b + 4 + mult_idx, CPE_LT_L_Z);
                constrain_cell(col.mults[mult_idx].upper, b, b + 4 + mult_idx, CPE_LT_U_Z);
                constrain_cell(col.mults[mult_idx].cplines, b, b + 4 + mult_idx, CPE_CPLINES_Z);
            }

            constrain_cell(col.msb_route.lower, b, b + 4 + col.mults.size(), CPE_LT_L_Z);
            constrain_cell(col.msb_route.upper, b, b + 4 + col.mults.size(), CPE_LT_U_Z);
            constrain_cell(col.msb_route.comp, b, b + 4 + col.mults.size(), CPE_COMP_Z);
            constrain_cell(col.msb_route.cplines, b, b + 4 + col.mults.size(), CPE_CPLINES_Z);
        }

        // Step 3: connect them.

        // Zero driver.
        auto *zero_net = ctx->createNet(ctx->idf("%s$out", m.zero.upper->name.c_str(ctx)));
        m.zero.upper->connectPort(id_OUT, zero_net);

        // A input.
        for (size_t a = 0; a < m.a_passthrus.size(); a++) {
            auto &a_passthru = m.a_passthrus.at(a);

            // Connect A input passthrough cell.
            mult->movePortTo(ctx->idf("A[%d]", 2 * a), a_passthru.lower, id_IN1);
            mult->movePortTo(ctx->idf("A[%d]", 2 * a + 1), a_passthru.upper, id_IN1);

            // Prepare A passthrough nets.
            auto lower_name = a_passthru.lower->name;
            auto upper_name = a_passthru.upper->name;
            auto lower_net_name = a_passthru.lower->ports.at(id_IN1).net->name;
            auto upper_net_name = a_passthru.upper->ports.at(id_IN1).net->name;

            auto *lower_net = ctx->createNet(
                    ctx->idf("%s$%s$a%d_passthru", lower_name.c_str(ctx), lower_net_name.c_str(ctx), 2 * a));
            a_passthru.lower->connectPort(id_OUT, lower_net);

            auto *upper_net = ctx->createNet(
                    ctx->idf("%s$%s$a%d_passthru", upper_name.c_str(ctx), upper_net_name.c_str(ctx), 2 * a + 1));
            a_passthru.upper->connectPort(id_OUT, upper_net);

            // Inputs may be GND/VCC; if so, clean them up.
            a_passthru.clean_up_cell(ctx, a_passthru.lower);
            a_passthru.clean_up_cell(ctx, a_passthru.upper);

            // Connect A passthrough outputs to multiplier inputs.
            {
                // Sum output connections.
                auto &mult_row = m.cols.at(0).mults.at(a);

                auto *so1_net = ctx->createNet(ctx->idf("%s$so1", upper_name.c_str(ctx)));
                a_passthru.cplines->connectPort(id_COUTX, so1_net);
                mult_row.lower->connectPort(id_CINX, so1_net);

                auto *so2_net = ctx->createNet(ctx->idf("%s$so2", upper_name.c_str(ctx)));
                a_passthru.cplines->connectPort(id_POUTX, so2_net);
                mult_row.lower->connectPort(id_PINX, so2_net);
            }

            for (size_t b = 0; b < m.cols.size(); b++) {
                auto &mult_row = m.cols.at(b).mults.at(a);
                mult_row.lower->connectPort(id_IN1, lower_net);
                mult_row.upper->connectPort(id_IN1, upper_net);

                if (a == 0) {
                    mult_row.lower->connectPort(id_IN4, zero_net);
                } else {
                    auto &mult_row_below = m.cols.at(b).mults.at(a - 1);
                    auto *a_net_below = mult_row_below.upper->ports.at(id_IN1).net;
                    mult_row.lower->connectPort(id_IN4, a_net_below);
                }
            }
        }

        // B input.
        for (size_t b = 0; b < m.cols.size(); b++) {
            auto &b_passthru = m.cols.at(b).b_passthru;

            // Connect B input passthrough cell.
            mult->movePortTo(ctx->idf("B[%d]", 2 * b), b_passthru.upper, id_IN1);
            mult->movePortTo(ctx->idf("B[%d]", 2 * b + 1), b_passthru.lower, id_IN1);
        }

        // Intermediate multiplier connections.
        for (size_t b = 0; b < m.cols.size(); b++) {
            auto &b_passthru = m.cols.at(b).b_passthru;
            auto &b_carry = m.cols.at(b).carry;
            auto &b_multfab = m.cols.at(b).multfab;
            auto &b_f_route = m.cols.at(b).f_route;
            auto &b_msb_route = m.cols.at(b).msb_route;

            auto lower_net_name = b_passthru.lower->ports.at(id_IN1).net->name;
            auto upper_net_name = b_passthru.upper->ports.at(id_IN1).net->name;

            // B Passthrough (POUTY1, COUTY1) -> Carry Gen (PINY1, CINY1)
            {
                auto lines_name = b_passthru.cplines->name;

                auto *lower_net = ctx->createNet(
                        ctx->idf("%s$%s$b%d_passthru", lines_name.c_str(ctx), lower_net_name.c_str(ctx), 2 * b));
                b_passthru.cplines->connectPort(id_POUTY1, lower_net);
                b_carry.cplines->connectPort(id_PINY1, lower_net);
                b_carry.lower->connectPort(id_PINY1, lower_net);

                auto *upper_net = ctx->createNet(
                        ctx->idf("%s$%s$b%d_passthru", lines_name.c_str(ctx), upper_net_name.c_str(ctx), 2 * b + 1));
                b_passthru.cplines->connectPort(id_COUTY1, upper_net);
                b_carry.cplines->connectPort(id_CINY1, upper_net);
                b_carry.lower->connectPort(id_CINY1, upper_net);
            }

            // Carry Gen (POUTY1, COUTY1, COUTY2) -> MULTFab (PINY1, CINY1, CINY2)
            {
                auto lines_name = b_carry.cplines->name;

                auto *lower_net = ctx->createNet(
                        ctx->idf("%s$%s$b%d_passthru", lines_name.c_str(ctx), lower_net_name.c_str(ctx), 2 * b));
                b_carry.cplines->connectPort(id_POUTY1, lower_net);
                b_multfab.cplines->connectPort(id_PINY1, lower_net);
                b_multfab.lower->connectPort(id_PINY1, lower_net);

                auto *upper_net = ctx->createNet(
                        ctx->idf("%s$%s$b%d_passthru", lines_name.c_str(ctx), upper_net_name.c_str(ctx), 2 * b + 1));
                b_carry.cplines->connectPort(id_COUTY1, upper_net);
                b_multfab.cplines->connectPort(id_CINY1, upper_net);

                auto *ccs_net = ctx->createNet(ctx->idf("%s$ccs", lines_name.c_str(ctx)));
                b_carry.cplines->connectPort(id_COUTY2, ccs_net);
                b_multfab.cplines->connectPort(id_CINY2, ccs_net);
            }

            // MULTFab (POUTY1, COUTY1, COUTY2) -> FRoute (PINY1, CINY1, CINY2)
            {
                auto lines_name = b_multfab.cplines->name;

                auto *lower_net = ctx->createNet(
                        ctx->idf("%s$%s$f%d", lines_name.c_str(ctx), lower_net_name.c_str(ctx), 2 * b + 1));
                b_multfab.cplines->connectPort(id_POUTY1, lower_net);
                b_f_route.cplines->connectPort(id_PINY1, lower_net);
                b_f_route.lower->connectPort(id_PINY1, lower_net);

                auto *upper_net =
                        ctx->createNet(ctx->idf("%s$%s$f%d", lines_name.c_str(ctx), upper_net_name.c_str(ctx), 2 * b));
                b_multfab.cplines->connectPort(id_COUTY1, upper_net);
                b_f_route.cplines->connectPort(id_CINY1, upper_net);

                auto *ccs_net = ctx->createNet(ctx->idf("%s$ccs", lines_name.c_str(ctx)));
                b_multfab.cplines->connectPort(id_COUTY2, ccs_net);
                b_f_route.cplines->connectPort(id_CINY2, ccs_net);
            }

            // MULTFab (COUTX) -> Carry Gen (CINX)
            if (b + 1 < m.cols.size()) {
                auto &b_carry_right = m.cols.at(b + 1).carry;

                auto lines_name = b_multfab.cplines->name;

                auto *cco_net = ctx->createNet(ctx->idf("%s$cco", lines_name.c_str(ctx)));
                b_multfab.cplines->connectPort(id_COUTX, cco_net);
                b_carry_right.cplines->connectPort(id_CINX, cco_net);
                b_carry_right.upper->connectPort(id_CINX, cco_net);
            }

            // FRoute (POUTY1, POUTY2, COUTY1, COUTY2) -> C_MULT (PINY1, PINY2, CINY1, CINY2)
            {
                auto &b_mult = m.cols.at(b).mults.front();

                auto lines_name = b_multfab.cplines->name;

                auto *f_p1_net = ctx->createNet(
                        ctx->idf("%s$%s$f%d_p1", lines_name.c_str(ctx), lower_net_name.c_str(ctx), 2 * b));
                b_f_route.cplines->connectPort(id_POUTY1, f_p1_net);
                b_mult.cplines->connectPort(id_PINY1, f_p1_net);

                auto *f_p2_net = ctx->createNet(
                        ctx->idf("%s$%s$f%d_p2", lines_name.c_str(ctx), lower_net_name.c_str(ctx), 2 * b + 1));
                b_f_route.cplines->connectPort(id_POUTY2, f_p2_net);
                b_mult.cplines->connectPort(id_PINY2, f_p2_net);

                auto *f_c1_net = ctx->createNet(
                        ctx->idf("%s$%s$f%d_c1", lines_name.c_str(ctx), upper_net_name.c_str(ctx), 2 * b));
                b_f_route.cplines->connectPort(id_COUTY1, f_c1_net);
                b_mult.cplines->connectPort(id_CINY1, f_c1_net);

                auto *f_c2_net = ctx->createNet(
                        ctx->idf("%s$%s$f%d_c2", lines_name.c_str(ctx), upper_net_name.c_str(ctx), 2 * b + 1));
                b_f_route.cplines->connectPort(id_COUTY2, f_c2_net);
                b_mult.cplines->connectPort(id_CINY2, f_c2_net);
            }

            // FRoute (COUTX) -> MULTFab (CINX)
            if (b + 1 < m.cols.size()) {
                auto &b_multfab_right = m.cols.at(b + 1).multfab;

                auto lines_name = b_f_route.cplines->name;

                auto *cco_net = ctx->createNet(ctx->idf("%s$cco", lines_name.c_str(ctx)));
                b_f_route.cplines->connectPort(id_COUTX, cco_net);
                b_multfab_right.cplines->connectPort(id_CINX, cco_net);
                b_multfab_right.lower->connectPort(id_CINX, cco_net);
            }

            // C_MULT (POUTY1, POUTY2, COUTY1, COUTY2) -> C_MULT (PINY1, PINY2, CINY1, CINY2)
            for (size_t row = 0; row < m.cols.at(b).mults.size() - 1; row++) {
                auto &b_mult = m.cols.at(b).mults.at(row);
                auto &b_mult_up = m.cols.at(b).mults.at(row + 1);

                auto lines_name = b_mult.cplines->name;

                auto *lower_b_net =
                        ctx->createNet(ctx->idf("%s$%s$b%d", lines_name.c_str(ctx), lower_net_name.c_str(ctx), 2 * b));
                b_mult.cplines->connectPort(id_POUTY1, lower_b_net);
                b_mult_up.cplines->connectPort(id_PINY1, lower_b_net);
                b_mult_up.lower->connectPort(id_PINY1, lower_b_net);

                auto *upper_b_net = ctx->createNet(
                        ctx->idf("%s$%s$b%d", lines_name.c_str(ctx), upper_net_name.c_str(ctx), 2 * b + 1));
                b_mult.cplines->connectPort(id_POUTY2, upper_b_net);
                b_mult_up.cplines->connectPort(id_PINY2, upper_b_net);

                auto *lower_co_net =
                        ctx->createNet(ctx->idf("%s$%s$co%d", lines_name.c_str(ctx), upper_net_name.c_str(ctx), 2 * b));
                b_mult.cplines->connectPort(id_COUTY1, lower_co_net);
                b_mult_up.cplines->connectPort(id_CINY1, lower_co_net);

                auto *upper_co_net = ctx->createNet(
                        ctx->idf("%s$%s$co%d", lines_name.c_str(ctx), upper_net_name.c_str(ctx), 2 * b + 1));
                b_mult.cplines->connectPort(id_COUTY2, upper_co_net);
                b_mult_up.cplines->connectPort(id_CINY2, upper_co_net);
            }

            // C_MULT (POUTX, COUTX) -> C_MULT (PINX, CINX)
            if (b + 1 < m.cols.size()) {
                for (size_t row = 1; row < m.cols.at(b).mults.size(); row++) {
                    auto &b_mult = m.cols.at(b).mults.at(row);
                    auto &b_mult_right = m.cols.at(b + 1).mults.at(row - 1);

                    auto lines_name = b_mult.cplines->name;

                    auto *so1_net = ctx->createNet(ctx->idf("%s$so1", lines_name.c_str(ctx)));
                    b_mult.cplines->connectPort(id_POUTX, so1_net);
                    b_mult_right.cplines->connectPort(id_PINX, so1_net);
                    b_mult_right.lower->connectPort(id_PINX, so1_net);

                    auto *so2_net = ctx->createNet(ctx->idf("%s$so2", lines_name.c_str(ctx)));
                    b_mult.cplines->connectPort(id_COUTX, so2_net);
                    b_mult_right.cplines->connectPort(id_CINX, so2_net);
                    b_mult_right.lower->connectPort(id_CINX, so2_net);
                }
            }

            // C_MULT (POUTY1, POUTY2) -> MsbRouting (PINY1, PINY2)
            {
                auto &b_mult = m.cols.at(b).mults.back();

                auto lines_name = b_mult.cplines->name;

                auto *lower_net =
                        ctx->createNet(ctx->idf("%s$%s$b%d", lines_name.c_str(ctx), lower_net_name.c_str(ctx), 2 * b));
                b_mult.cplines->connectPort(id_POUTY1, lower_net);
                b_msb_route.cplines->connectPort(id_PINY1, lower_net);
                b_msb_route.upper->connectPort(id_PINY1, lower_net);

                auto *upper_net = ctx->createNet(
                        ctx->idf("%s$%s$b%d", lines_name.c_str(ctx), lower_net_name.c_str(ctx), 2 * b + 1));
                b_mult.cplines->connectPort(id_POUTY2, upper_net);
                b_msb_route.cplines->connectPort(id_PINY2, upper_net);
            }

            // MsbRouting (POUTX, COUTX) -> C_MULT (PINX, CINX)
            if (b + 1 < m.cols.size()) {
                auto &b_mult_right = m.cols.at(b + 1).mults.back();

                auto lines_name = b_msb_route.cplines->name;

                auto *so1_net = ctx->createNet(ctx->idf("%s$so1", lines_name.c_str(ctx)));
                b_msb_route.cplines->connectPort(id_POUTX, so1_net);
                b_mult_right.cplines->connectPort(id_PINX, so1_net);

                auto *so2_net = ctx->createNet(ctx->idf("%s$so2", lines_name.c_str(ctx)));
                b_msb_route.cplines->connectPort(id_COUTX, so2_net);
                b_mult_right.cplines->connectPort(id_CINX, so2_net);
            }
        }

        // P output.
        auto diagonal_p_width = std::min(b_width, p_width);
        auto vertical_p_width = std::max(p_width - b_width, 0);

        for (int p = 0; p < diagonal_p_width; p++) {
            auto &mult_cell = m.cols[p / 2].mults[0];
            auto *cpe_half = (p % 2 == 1) ? mult_cell.upper : mult_cell.lower;

            mult->movePortTo(ctx->idf("P[%d]", p), cpe_half, id_CPOUT);
        }

        for (int p = 0; p < vertical_p_width; p++) {
            auto &mult_cell = m.cols.back().mults[1 + (p / 2)];
            auto *cpe_half = (p % 2 == 1) ? mult_cell.upper : mult_cell.lower;

            mult->movePortTo(ctx->idf("P[%d]", p + diagonal_p_width), cpe_half, id_CPOUT);
        }

        // Clean up the multiplier.
        for (size_t b = 0; b < m.cols.size(); b++) {
            auto &b_passthru = m.cols.at(b).b_passthru;

            // This may be GND/VCC.
            b_passthru.clean_up_cell(ctx, b_passthru.lower);
            b_passthru.clean_up_cell(ctx, b_passthru.upper);
        }

        ctx->cells.erase(mult->name);

        log_info("        Created %zu CPEs.\n", m.cpe_count());
    }
}

NEXTPNR_NAMESPACE_END

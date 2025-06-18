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

#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"
#include "pack.h"
#include "property.h"
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

void GateMatePacker::pack_mult()
{
    auto create_mult_cell = [&](IdString name) {
        // instantiate a full CPE by creating two halves.
        auto* cpe_l = create_cell_ptr(id_CPE_HALF_L, ctx->idf("%s$lower", name.c_str(ctx)));
        cpe_l->cluster = name;
        cpe_l->constr_abs_z = false;
        cpe_l->params[id_INIT_L00] = Property(0b1000, 4); // AND
        cpe_l->params[id_INIT_L01] = Property(0b1000, 4); // AND
        cpe_l->params[id_INIT_L10] = Property(0b0110, 4); // XOR

        auto* cpe_u = create_cell_ptr(id_CPE_HALF_U, ctx->idf("%s$upper", name.c_str(ctx)));
        cpe_u->cluster = name;
        cpe_u->constr_abs_z = false;
        cpe_u->params[id_INIT_L02] = Property(0b1000, 4); // AND
        cpe_u->params[id_INIT_L03] = Property(0b1000, 4); // AND
        cpe_u->params[id_INIT_L11] = Property(0b0110, 4); // XOR
        cpe_u->params[id_INIT_L20] = Property(0b0101, 4); // D1
        cpe_u->params[id_C_FUNCTION] = Property(C_MULT, 3);

        return std::make_pair(cpe_l, cpe_u);
    };

    log_info("Packing multipliers...\n");

    auto mults = std::vector<CellInfo*>{};

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_CC_MULT)
            mults.push_back(ci);
    }

    for (auto* mult : mults) {
        auto a_width = int_or_default(mult->params, id_A_WIDTH);
        auto b_width = int_or_default(mult->params, id_B_WIDTH);
        auto p_width = int_or_default(mult->params, id_P_WIDTH);
        log_info("    Configuring '%s' as a %d * %d = %d multiplier\n", mult->name.c_str(ctx), a_width, b_width, p_width);

        NPNR_ASSERT(a_width <= 2);
        NPNR_ASSERT(b_width <= 2);

        auto [mult_l, mult_u] = create_mult_cell(mult->name);

        
    }
}

NEXTPNR_NAMESPACE_END

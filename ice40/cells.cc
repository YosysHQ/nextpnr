/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

#include "cells.h"
#include "design_utils.h"
#include "log.h"

static void add_port(CellInfo *cell, IdString name, PortType dir)
{
    cell->ports[name] = PortInfo{name, nullptr, dir};
}

CellInfo *create_ice_cell(Design *design, IdString type, IdString name)
{
    static int auto_idx = 0;
    CellInfo *new_cell = new CellInfo();
    if (name == IdString()) {
        new_cell->name =
                IdString("$nextpnr_" + type + "_" + std::to_string(auto_idx++));
    } else {
        new_cell->name = name;
    }
    if (type == "ICESTORM_LC") {
        new_cell->params["LUT_INIT"] = "0";
        new_cell->params["NEG_CLK"] = "0";
        new_cell->params["CARRY_ENABLE"] = "0";
        new_cell->params["DFF_ENABLE"] = "0";
        new_cell->params["SET_NORESET"] = "0";
        new_cell->params["ASYNC_SR"] = "0";

        add_port(new_cell, "I0", PORT_IN);
        add_port(new_cell, "I1", PORT_IN);
        add_port(new_cell, "I2", PORT_IN);
        add_port(new_cell, "I3", PORT_IN);
        add_port(new_cell, "CIN", PORT_IN);

        add_port(new_cell, "CLK", PORT_IN);
        add_port(new_cell, "CEN", PORT_IN);
        add_port(new_cell, "SR", PORT_IN);

        add_port(new_cell, "LO", PORT_OUT);
        add_port(new_cell, "O", PORT_OUT);
        add_port(new_cell, "OUT", PORT_OUT);
    } else {
        log_error("unable to create iCE40 cell of type %s", type.c_str());
    }
    design->cells[new_cell->name] = new_cell;
    return new_cell;
}

void lut_to_lc(CellInfo *lut, CellInfo *lc, bool no_dff)
{
    lc->params["LUT_INIT"] = lut->params["LUT_INIT"];
    replace_port(lut, "I0", lc, "I0");
    replace_port(lut, "I1", lc, "I1");
    replace_port(lut, "I2", lc, "I2");
    replace_port(lut, "I3", lc, "I3");
    if (no_dff) {
        replace_port(lut, "O", lc, "O");
        lc->params["DFF_ENABLE"] = "0";
    }
}

void dff_to_lc(CellInfo *dff, CellInfo *lc, bool pass_thru_lut)
{
    lc->params["DFF_ENABLE"] = "1";
    std::string config = std::string(dff->type).substr(6);
    auto citer = config.begin();
    replace_port(dff, "C", lc, "CLK");

    if (citer != config.end() && *citer == 'N') {
        lc->params["NEG_CLK"] = "1";
        ++citer;
    } else {
        lc->params["NEG_CLK"] = "0";
    }

    if (citer != config.end() && *citer == 'E') {
        replace_port(dff, "E", lc, "CEN");
        ++citer;
    }

    if (citer != config.end()) {
        if ((config.end() - citer) >= 2) {
            assert(*(citer++) == 'S');
            lc->params["ASYNC_SR"] = "1";
        } else {
            lc->params["ASYNC_SR"] = "0";
        }

        if (*citer == 'S') {
            replace_port(dff, "S", lc, "SR");
            lc->params["SET_NORESET"] = "1";
        } else {
            assert(*citer == 'R');
            replace_port(dff, "R", lc, "SR");
            lc->params["SET_NORESET"] = "0";
        }
    }

    assert(citer == config.end());

    if (pass_thru_lut) {
        lc->params["LUT_INIT"] = "2";
        replace_port(dff, "D", lc, "I0");
    }
}

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#ifndef XILINX_INTERCHANGE_CELL_TRANSFORM_H
#define XILINX_INTERCHANGE_CELL_TRANSFORM_H

#include "context.h"
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

// For cell transforms (equivalent to "retargets") outside of the RapidWright data
struct XFormRule
{
    IdString new_type;
    dict<IdString, IdString> port_xform;
    dict<IdString, IdString> param_xform;
    std::vector<std::pair<IdString, std::string>> set_attrs;
    std::vector<std::pair<IdString, Property>> set_params;
};

void transform_cell(Context *ctx, const dict<IdString, XFormRule> &rules, CellInfo *ci);

NEXTPNR_NAMESPACE_END

#endif
/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#ifndef PLACE_COMMON_H
#define PLACE_COMMON_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

typedef int64_t wirelen_t;

enum class MetricType
{
    COST,
    WIRELENGTH
};

// Return the wirelength of a net
wirelen_t get_net_metric(const Context *ctx, const NetInfo *net, MetricType type, float &tns);

// Return the wirelength of all nets connected to a cell
wirelen_t get_cell_metric(const Context *ctx, const CellInfo *cell, MetricType type);

// Return the wirelength of all nets connected to a cell, when the cell is at a given bel
wirelen_t get_cell_metric_at_bel(const Context *ctx, CellInfo *cell, BelId bel, MetricType type);

// Place a single cell in the lowest wirelength Bel available, optionally requiring validity check
bool place_single_cell(Context *ctx, CellInfo *cell, bool require_legality);

// Modify a design s.t. all relative placement constraints are satisfied
bool legalise_relative_constraints(Context *ctx);

// Get the total distance from satisfied constraints for a cell
int get_constraints_distance(const Context *ctx, const CellInfo *cell);
NEXTPNR_NAMESPACE_END

#endif

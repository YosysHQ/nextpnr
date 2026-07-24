/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-2026  The FABulous maintainers <fpga.research.group@gmail.com>
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

#ifndef FABULOUS_BEL_TIMING_H
#define FABULOUS_BEL_TIMING_H

#include <vector>

#include "fabric_parsing.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// A single predicate gating a timing arc, parsed from a bel.v3 condition field.
// PARAM matches a config bit / cell parameter; PORT_ANY/NONE_CONNECTED match the
// connectivity of one or more ports (the '/'-list expresses OR).
struct TimingPredicate
{
    enum Kind
    {
        PARAM,
        PORT_ANY_CONNECTED,
        PORT_NONE_CONNECTED
    } kind;
    IdString param;
    bool param_value = true;
    std::vector<IdString> ports;
};

// One timing arc for a BEL type, mapping to an addCellTiming* call when its
// (optional, AND-ed) conditions hold for a given cell.
struct BelTimingArc
{
    enum Kind
    {
        DELAY,
        SETUPHOLD,
        CLK2OUT,
        CLOCK
    } kind;
    IdString from, to;
    float v0 = 0, v1 = 0;
    std::vector<TimingPredicate> cond;
};

float parse_float(parser_view v);
double parse_double(parser_view v);

// Parse a bel.v3 condition field (e.g. "FF=0&I0MUX=1" or "Ci/Co?") into a
// list of AND-ed predicates. An empty field means the arc always applies.
std::vector<TimingPredicate> parse_timing_condition(Context *ctx, parser_view field);

// Build one timing arc from the fields after the command (shared by the BEL
// file and the placement_estimate.txt placement estimate).
BelTimingArc parse_one_arc(Context *ctx, CsvParser &csv, IdString cmd);

// True if every predicate holds for this cell (empty cond always holds).
bool timing_cond_holds(const CellInfo *ci, const std::vector<TimingPredicate> &cond);

// Register one arc on the cell as the matching addCellTiming* call, if its condition holds.
void apply_arc(Context *ctx, CellInfo *ci, const BelTimingArc &arc);

NEXTPNR_NAMESPACE_END

#endif

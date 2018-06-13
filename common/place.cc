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

#include "place.h"
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <ostream>
#include <queue>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "arch_place.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

void place_design(Design *design)
{
    std::set<IdString> types_used;
    std::set<IdString>::iterator not_found, element;
    std::set<BelType> used_bels;

    log_info("Placing..\n");

    // Initial constraints placer
    for (auto cell_entry : design->cells) {
        CellInfo *cell = cell_entry.second;
        auto loc = cell->attrs.find("BEL");
        if (loc != cell->attrs.end()) {
            std::string loc_name = loc->second;
            BelId bel = design->chip.getBelByName(IdString(loc_name));
            if (bel == BelId()) {
                log_error("No Bel named \'%s\' located for "
                          "this chip (processing BEL attribute on \'%s\')\n",
                          loc_name.c_str(), cell->name.c_str());
            }

            BelType bel_type = design->chip.getBelType(bel);
            if (bel_type != belTypeFromId(cell->type)) {
                log_error("Bel \'%s\' of type \'%s\' does not match cell "
                          "\'%s\' of type \'%s\'",
                          loc_name.c_str(), belTypeToId(bel_type).c_str(),
                          cell->name.c_str(), cell->type.c_str());
            }

            cell->bel = bel;
            design->chip.bindBel(bel, cell->name);
        }
    }

    for (auto cell_entry : design->cells) {
        CellInfo *cell = cell_entry.second;
        // Ignore already placed cells
        if (cell->bel != BelId())
            continue;

        BelType bel_type;

        element = types_used.find(cell->type);
        if (element != types_used.end()) {
            continue;
        }

        bel_type = belTypeFromId(cell->type);
        if (bel_type == BelType()) {
            log_error("No Bel of type \'%s\' defined for "
                      "this chip\n",
                      cell->type.c_str());
        }
        types_used.insert(cell->type);
    }

    for (auto bel_type_name : types_used) {
        auto blist = design->chip.getBels();
        BelType bel_type = belTypeFromId(bel_type_name);
        auto bi = blist.begin();

        for (auto cell_entry : design->cells) {
            CellInfo *cell = cell_entry.second;

            // Ignore already placed cells
            if (cell->bel != BelId())
                continue;
            // Only place one type of Bel at a time
            if (cell->type != bel_type_name)
                continue;

            while ((bi != blist.end()) &&
                   ((design->chip.getBelType(*bi) != bel_type ||
                     !design->chip.checkBelAvail(*bi)) ||
                    !isValidBelForCell(design, cell, *bi)))
                bi++;
            if (bi == blist.end())
                log_error("Too many \'%s\' used in design\n",
                          cell->type.c_str());
            cell->bel = *bi++;
            design->chip.bindBel(cell->bel, cell->name);

            // Back annotate location
            cell->attrs["BEL"] = design->chip.getBelName(cell->bel).str();
        }
    }
}

static void place_cell(Design *design, CellInfo *cell, BelId closeTo,
                       size_t &placed_cells,
                       std::queue<CellInfo *> &visit_cells)
{
    assert(cell->bel == BelId());
    float best_distance = std::numeric_limits<float>::infinity();
    BelId best_bel = BelId();
    Chip &chip = design->chip;
    BelType targetType = belTypeFromId(cell->type);
    PosInfo origin;
    if (closeTo != BelId()) {
        origin = chip.getBelPosition(closeTo);
        for (auto bel : chip.getBelsAtSameTile(closeTo)) {
            if (chip.getBelType(bel) == targetType && chip.checkBelAvail(bel) &&
                isValidBelForCell(design, cell, bel)) {
                // prefer same tile
                best_bel = bel;
                goto placed;
            }
        }
    }
    for (auto bel : chip.getBels()) {
        if (chip.getBelType(bel) == targetType && chip.checkBelAvail(bel) &&
            isValidBelForCell(design, cell, bel)) {
            if (closeTo == BelId()) {
                best_distance = 0;
                best_bel = bel;
                goto placed;
            } else {
                float distance =
                        chip.estimateDelay(origin, chip.getBelPosition(bel));
                if (distance < best_distance) {
                    best_distance = distance;
                    best_bel = bel;
                }
            }
        }
    }
placed:
    if (best_bel == BelId()) {
        log_error("failed to place cell '%s' of type '%s'\n",
                  cell->name.c_str(), cell->type.c_str());
    }
    cell->bel = best_bel;
    chip.bindBel(cell->bel, cell->name);

    // Back annotate location
    cell->attrs["BEL"] = chip.getBelName(cell->bel).str();
    placed_cells++;
    visit_cells.push(cell);
}

static void place_cell_neighbours(Design *design, CellInfo *cell,
                                  size_t &placed_cells,
                                  std::queue<CellInfo *> &visit_cells)
{
    if (cell->bel == BelId())
        return;
    int placed_count = 0;
    const int count_thresh = 15;
    for (auto port : cell->ports) {
        NetInfo *net = port.second.net;
        if (net != nullptr) {
            for (auto user : net->users) {
                if (net->users.size() > 3)
                    continue;
                if (user.cell != nullptr && user.cell->bel == BelId()) {
                    place_cell(design, user.cell, cell->bel, placed_cells,
                               visit_cells);
                    placed_count++;
                    if (placed_count > count_thresh)
                        return;
                }
            }
            if (net->driver.cell != nullptr && net->driver.cell->bel == BelId()) {
                place_cell(design, net->driver.cell, cell->bel, placed_cells,
                           visit_cells);
                placed_count++;
            }
            if (placed_count > count_thresh)
                return;
        }
    }

}

void place_design_heuristic(Design *design)
{
    size_t total_cells = design->cells.size(), placed_cells = 0;
    std::queue<CellInfo *> visit_cells;
    // Initial constraints placer
    for (auto cell_entry : design->cells) {
        CellInfo *cell = cell_entry.second;
        auto loc = cell->attrs.find("BEL");
        if (loc != cell->attrs.end()) {
            std::string loc_name = loc->second;
            BelId bel = design->chip.getBelByName(IdString(loc_name));
            if (bel == BelId()) {
                log_error("No Bel named \'%s\' located for "
                          "this chip (processing BEL attribute on \'%s\')\n",
                          loc_name.c_str(), cell->name.c_str());
            }

            BelType bel_type = design->chip.getBelType(bel);
            if (bel_type != belTypeFromId(cell->type)) {
                log_error("Bel \'%s\' of type \'%s\' does not match cell "
                          "\'%s\' of type \'%s\'",
                          loc_name.c_str(), belTypeToId(bel_type).c_str(),
                          cell->name.c_str(), cell->type.c_str());
            }

            cell->bel = bel;
            design->chip.bindBel(bel, cell->name);
            placed_cells++;
            visit_cells.push(cell);
        }
    }
    log_info("place_constraints placed %d\n", placed_cells);
    while (placed_cells < total_cells) {
        if (!visit_cells.empty()) {
            CellInfo *next = visit_cells.front();
            visit_cells.pop();
            place_cell_neighbours(design, next, placed_cells, visit_cells);
        } else {
            // Nothing to visit (netlist is split), pick the next unplaced cell
            for (auto cell : design->cells) {
                CellInfo *ci = cell.second;
                if (ci->bel == BelId()) {
                    place_cell(design, ci, BelId(), placed_cells, visit_cells);
                    break;
                }
            }
        }
        log_info("placed %d/%d\n", placed_cells, total_cells);
    }
}

NEXTPNR_NAMESPACE_END

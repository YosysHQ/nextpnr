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

#include <iostream>
#include <list>
#include <map>
#include <ostream>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "arch_place.h"
#include "log.h"
#include "place.h"

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

NEXTPNR_NAMESPACE_END

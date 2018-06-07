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
#include <ostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <vector>
#include <list>
#include <map>
#include <set>

#include "log.h"
#include "design.h"
#include "place.h"

void place_design(Design *design) {
	std::set<IdString>		types_used;
	std::set<IdString>::iterator	not_found, element;
	std::set<BelType>		used_bels;

	for(auto cell_entry : design->cells) {
		CellInfo	*cell = cell_entry.second;
		BelType		bel_type;

		element = types_used.find(cell->type);
		if (element != types_used.end()) {
			continue;
		}

		bel_type = belTypeFromId(cell->type);
		if (bel_type == TYPE_NIL) {
			log_error("No Bel of type \'%s\' defined for "
				"this chip\n", cell->type.c_str());
		}
		types_used.insert(cell->type);
		std::cout << cell->type << std::endl;
	}

	for(auto bel_type_name : types_used) {
		BelRange	blist = design->chip.getBels();
		BelType		bel_type = belTypeFromId(bel_type_name);
		BelIterator	bi = blist.begin();

		for(auto cell_entry : design->cells) {
			CellInfo	*cell = cell_entry.second;

			// Only place one type of Bel at a time
			if (cell->type.compare(bel_type_name)!=0)
				continue;

			while((bi != blist.end())
					&&(design->chip.getBelType(*bi) != bel_type))
				bi++;
			if (bi == blist.end())
				log_error("Too many \'%s\' used in design\n",
					cell->type.c_str());
			cell->bel = *bi++;
		}
	}
}


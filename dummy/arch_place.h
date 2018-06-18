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

#ifndef DUMMY_ARCH_PLACE_H
#define DUMMY_ARCH_PLACE_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// Architecure-specific placement functions

// Whether or not a given cell can be placed at a given Bel
// This is not intended for Bel type checks, but finer-grained constraints
// such as conflicting set/reset signals, etc
bool isValidBelForCell(Design *design, CellInfo *cell, BelId bel);

// Return true whether all Bels at a given location are valid
bool isBelLocationValid(Design *design, BelId bel);

NEXTPNR_NAMESPACE_END

#endif

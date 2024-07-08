/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  Miodrag Milanovic <micko@yosyshq.com>
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

#include "nextpnr.h"
#include "ng_ultra.h"

#ifndef NG_ULTRA_LOCATION_MAP_H
#define NG_ULTRA_LOCATION_MAP_H

NEXTPNR_NAMESPACE_BEGIN

namespace ng_ultra {

Loc getNextLocInDSPChain(const NgUltraImpl *impl, Loc loc);
Loc getNextLocInCYChain(Loc loc);
Loc getCYFE(Loc root, int pos);
Loc getXLUTFE(Loc root, int pos);
Loc getXRFFE(Loc root, int pos);
Loc getCDCFE(Loc root, int pos);
Loc getFIFOFE(Loc root, int pos);

};

NEXTPNR_NAMESPACE_END
#endif

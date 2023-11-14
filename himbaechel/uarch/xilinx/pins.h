/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  Myrtle Shah <gatecat@ds0.me>
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

#ifndef HB_XILINX_PINS_H
#define HB_XILINX_PINS_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

void get_invertible_pins(Context *ctx, dict<IdString, pool<IdString>> &invertible_pins);
void get_tied_pins(Context *ctx, dict<IdString, dict<IdString, bool>> &tied_pins);
void get_bram36_ul_pins(Context *ctx, std::vector<std::pair<IdString, std::vector<std::string>>> &ul_pins);
void get_top_level_pins(Context *ctx, dict<IdString, pool<IdString>> &toplevel_pins);

NEXTPNR_NAMESPACE_END

#endif
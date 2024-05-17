/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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

#include "design_utils.h"
#include <algorithm>
#include <map>
#include "log.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

// Print utilisation of a design
void print_utilisation(const Context *ctx)
{
    // Sort by Bel type
    std::map<IdString, int> used_types;
    for (auto &cell : ctx->cells) {
        used_types[ctx->getBelBucketName(ctx->getBelBucketForCellType(cell.second.get()->type))]++;
    }
    std::map<IdString, int> available_types;
    for (auto bel : ctx->getBels()) {
        if (!ctx->getBelHidden(bel)) {
            available_types[ctx->getBelBucketName(ctx->getBelBucketForBel(bel))]++;
        }
    }
    log_break();
    log_info("Device utilisation:\n");
    for (auto type : available_types) {
        IdString type_id = type.first;
        int used_bels = get_or_default(used_types, type.first, 0);
        log_info("\t%20s: %7d/%7d %5d%%\n", type_id.c_str(ctx), used_bels, type.second, 100 * used_bels / type.second);
    }
    log_break();
}

NEXTPNR_NAMESPACE_END

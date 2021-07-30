/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "json11.hpp"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

using namespace json11;

namespace {
dict<IdString, std::pair<int, int>> get_utilization(const Context *ctx)
{
    // Sort by Bel type
    dict<IdString, std::pair<int, int>> result;
    for (auto &cell : ctx->cells) {
        result[ctx->getBelBucketName(ctx->getBelBucketForCellType(cell.second.get()->type))].first++;
    }
    for (auto bel : ctx->getBels()) {
        if (!ctx->getBelHidden(bel)) {
            result[ctx->getBelBucketName(ctx->getBelBucketForBel(bel))].second++;
        }
    }
    return result;
}
} // namespace
void Context::writeReport(std::ostream &out) const
{
    auto util = get_utilization(this);
    dict<std::string, Json> util_json;
    for (const auto &kv : util) {
        util_json[kv.first.str(this)] = Json::object{
                {"used", kv.second.first},
                {"available", kv.second.second},
        };
    }
    dict<std::string, Json> fmax_json;
    for (const auto &kv : timing_result.clock_fmax) {
        fmax_json[kv.first.str(this)] = Json::object{
                {"achieved", kv.second.achieved},
                {"constraint", kv.second.constraint},
        };
    }
    out << Json(Json::object{
                        {"utilization", util_json},
                        {"fmax", fmax_json},
                })
                    .dump()
        << std::endl;
}

NEXTPNR_NAMESPACE_END

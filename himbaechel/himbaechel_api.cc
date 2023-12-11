/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-23  gatecat <gatecat@ds0.me>
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

#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

void HimbaechelAPI::init(Context *ctx) { this->ctx = ctx; }

std::vector<IdString> HimbaechelAPI::getCellTypes() const
{
    std::vector<IdString> result;
    // TODO
    return result;
}

BelBucketId HimbaechelAPI::getBelBucketForBel(BelId bel) const { return ctx->getBelType(bel); }

BelBucketId HimbaechelAPI::getBelBucketForCellType(IdString cell_type) const { return cell_type; }

bool HimbaechelAPI::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    return ctx->getBelType(bel) == cell_type;
}

delay_t HimbaechelAPI::estimateDelay(WireId src, WireId dst) const
{
    // TODO: configurable lookahead
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);
    return 100 * (std::abs(dx - sx) + std::abs(dy - sy) + 2);
}

delay_t HimbaechelAPI::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    Loc src_loc = ctx->getBelLocation(src_bel), dst_loc = ctx->getBelLocation(dst_bel);
    return 100 * (std::abs(dst_loc.x - src_loc.x) + std::abs(dst_loc.y - src_loc.y));
}

BoundingBox HimbaechelAPI::getRouteBoundingBox(WireId src, WireId dst) const
{
    BoundingBox bb;
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);
    bb.x0 = std::min(sx, dx);
    bb.y0 = std::min(sy, dy);
    bb.x1 = std::max(sx, dx);
    bb.y1 = std::max(sy, dy);
    return bb;
}

CellInfo *HimbaechelAPI::getClusterRootCell(ClusterId cluster) const
{
    return ctx->BaseArch::getClusterRootCell(cluster);
}
BoundingBox HimbaechelAPI::getClusterBounds(ClusterId cluster) const
{
    return ctx->BaseArch::getClusterBounds(cluster);
}
Loc HimbaechelAPI::getClusterOffset(const CellInfo *cell) const { return ctx->BaseArch::getClusterOffset(cell); }
bool HimbaechelAPI::isClusterStrict(const CellInfo *cell) const { return ctx->BaseArch::isClusterStrict(cell); }
bool HimbaechelAPI::getClusterPlacement(ClusterId cluster, BelId root_bel,
                                        std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    return ctx->BaseArch::getClusterPlacement(cluster, root_bel, placement);
}

HimbaechelArch *HimbaechelArch::list_head;
HimbaechelArch::HimbaechelArch(const std::string &name) : name(name)
{
    list_next = HimbaechelArch::list_head;
    HimbaechelArch::list_head = this;
}
std::string HimbaechelArch::list()
{
    std::string result;
    HimbaechelArch *cursor = HimbaechelArch::list_head;
    while (cursor) {
        if (!result.empty())
            result += ", ";
        result += cursor->name;
        cursor = cursor->list_next;
    }
    return result;
}

HimbaechelArch *HimbaechelArch::find_match(const std::string &device)
{
    HimbaechelArch *cursor = HimbaechelArch::list_head;
    while (cursor) {
        if (!cursor->match_device(device)) {
            cursor = cursor->list_next;
            continue;
        }
        return cursor;
    }
    return nullptr;
}
NEXTPNR_NAMESPACE_END

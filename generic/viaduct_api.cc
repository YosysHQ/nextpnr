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

#include "viaduct_api.h"
#include "nextpnr.h"

// Default implementations for Viaduct API hooks

NEXTPNR_NAMESPACE_BEGIN

void ViaductAPI::init(Context *ctx) { this->ctx = ctx; }

std::vector<IdString> ViaductAPI::getCellTypes() const
{
    pool<IdString> cell_types;
    for (auto bel : ctx->bels) {
        cell_types.insert(bel.type);
    }

    return std::vector<IdString>{cell_types.begin(), cell_types.end()};
}
BelBucketId ViaductAPI::getBelBucketForBel(BelId bel) const { return ctx->getBelType(bel); }
BelBucketId ViaductAPI::getBelBucketForCellType(IdString cell_type) const { return cell_type; }
bool ViaductAPI::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    return ctx->getBelType(bel) == cell_type;
}

delay_t ViaductAPI::estimateDelay(WireId src, WireId dst) const
{
    const WireInfo &s = ctx->wire_info(src);
    const WireInfo &d = ctx->wire_info(dst);
    int dx = abs(s.x - d.x);
    int dy = abs(s.y - d.y);
    return (dx + dy) * ctx->args.delayScale + ctx->args.delayOffset;
}
delay_t ViaductAPI::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    NPNR_UNUSED(src_pin);
    NPNR_UNUSED(dst_pin);
    auto driver_loc = ctx->getBelLocation(src_bel);
    auto sink_loc = ctx->getBelLocation(dst_bel);

    int dx = abs(sink_loc.x - driver_loc.x);
    int dy = abs(sink_loc.y - driver_loc.y);
    return (dx + dy) * ctx->args.delayScale + ctx->args.delayOffset;
}
BoundingBox ViaductAPI::getRouteBoundingBox(WireId src, WireId dst) const
{
    BoundingBox bb;
    int src_x = ctx->wire_info(src).x;
    int src_y = ctx->wire_info(src).y;
    int dst_x = ctx->wire_info(dst).x;
    int dst_y = ctx->wire_info(dst).y;

    bb.x0 = src_x;
    bb.y0 = src_y;
    bb.x1 = src_x;
    bb.y1 = src_y;

    auto extend = [&](int x, int y) {
        bb.x0 = std::min(bb.x0, x);
        bb.x1 = std::max(bb.x1, x);
        bb.y0 = std::min(bb.y0, y);
        bb.y1 = std::max(bb.y1, y);
    };
    extend(dst_x, dst_y);
    return bb;
}

ViaductArch *ViaductArch::list_head;
ViaductArch::ViaductArch(const std::string &name) : name(name)
{
    list_next = ViaductArch::list_head;
    ViaductArch::list_head = this;
}
std::string ViaductArch::list()
{
    std::string result;
    ViaductArch *cursor = ViaductArch::list_head;
    while (cursor) {
        if (!result.empty())
            result += ", ";
        result += cursor->name;
        cursor = cursor->list_next;
    }
    return result;
}
std::unique_ptr<ViaductAPI> ViaductArch::create(const std::string &name, const dict<std::string, std::string> &args)
{
    ViaductArch *cursor = ViaductArch::list_head;
    while (cursor) {
        if (cursor->name != name) {
            cursor = cursor->list_next;
            continue;
        }
        return cursor->create(args);
    }
    return {};
}

NEXTPNR_NAMESPACE_END

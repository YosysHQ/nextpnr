
/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021 gatecat <gatecat@ds0.me>
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
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include <queue>

NEXTPNR_NAMESPACE_BEGIN

namespace {

// Gets the ports attached to a nextpnr IO buffer
pool<std::pair<IdString, IdString>> get_attached_ports(CellInfo *npnr_iob)
{
    pool<std::pair<IdString, IdString>> result;
    NetInfo *o = npnr_iob->getPort(id_O);
    if (o != nullptr) {
        for (auto &usr : o->users)
            result.emplace(usr.cell->name, usr.port);
    }
    NetInfo *i = npnr_iob->getPort(id_I);
    if (i != nullptr && i->driver.cell != nullptr)
        result.emplace(i->driver.cell->name, i->driver.port);
    return result;
}

bool search_routing_for_placement(Context *ctx, WireId start_wire, CellInfo *cell, IdString cell_pin, bool downhill)
{
    if (ctx->debug)
        log_info("    search_routing_for_placement %s %s.%s\n", ctx->nameOfWire(start_wire), ctx->nameOf(cell),
                 ctx->nameOf(cell_pin));
    std::queue<WireId> visit_queue;
    pool<WireId> already_visited;
    visit_queue.push(start_wire);
    already_visited.insert(start_wire);
    int iter = 0;
    while (!visit_queue.empty() && iter++ < 1000) {
        WireId next = visit_queue.front();
        visit_queue.pop();
        if (ctx->debug)
            log("           visit wire %s\n", ctx->nameOfWire(next));
        for (auto bp : ctx->getWireBelPins(next)) {
            if (ctx->debug)
                log("               bel pin %s.%s\n", ctx->nameOfBel(bp.bel), ctx->nameOf(bp.pin));
            if (!ctx->isValidBelForCellType(cell->type, bp.bel))
                continue;
            if (!ctx->checkBelAvail(bp.bel))
                continue;
            // We need to do a test placement to update the bel pin map
            ctx->bindBel(bp.bel, cell, STRENGTH_FIXED);
            ctx->update_cell_bel_pins(cell);
            for (IdString bel_pin : ctx->getBelPinsForCellPin(cell, cell_pin)) {
                if (bel_pin == bp.pin)
                    return true;
            }
            // Bel pin doesn't match
            ctx->unbindBel(bp.bel);
        }
        auto do_visit = [&](PipId pip) {
            WireId dst = downhill ? ctx->getPipDstWire(pip) : ctx->getPipSrcWire(pip);
            if (already_visited.count(dst))
                return;
            visit_queue.push(dst);
            already_visited.insert(dst);
        };
        if (downhill) {
            for (auto pip : ctx->getPipsDownhill(next))
                do_visit(pip);
        } else {
            for (auto pip : ctx->getPipsUphill(next))
                do_visit(pip);
        }
    }
    return false;
}

// Replace a nextpnr IOBUF with a port cell. Note that we currently require all IOs to be constrained
void iob_to_pad(Context *ctx, CellInfo *npnr_iob)
{
    // Determine pad bel
    if (!npnr_iob->attrs.count(id_LOC))
        log_error("Found unconstrained IO pin %s, which is unsupported.\n", ctx->nameOf(npnr_iob));

    std::string loc = npnr_iob->attrs.at(id_LOC).as_string();
    const PadInfoPOD *pad_data = ctx->pad_by_name(loc);
    if (!pad_data)
        log_error("Pin '%s' does not exist in package '%s'\n", loc.c_str(),
                  ctx->nameOf(IdString(ctx->package_info->name)));

    BelId bel = ctx->get_pad_bel(pad_data);
    if (bel == BelId())
        log_error("Pin '%s' (%s) does not have an associated bel and cannot be used.\n", loc.c_str(),
                  ctx->nameOf(IdString(pad_data->pad_function)));

    log_info("Using pad bel '%s' for IO pin '%s'\n", ctx->nameOfBel(bel), ctx->nameOf(npnr_iob));

    // Save these, we'll need them later...
    NetInfo *i = npnr_iob->getPort(id_I);
    NetInfo *o = npnr_iob->getPort(id_O);

    // Convert the nextpnr IOBUF to a suitable top level pad
    npnr_iob->disconnectPort(id_I);
    npnr_iob->disconnectPort(id_O);
    npnr_iob->type = id_PAD;

    NetInfo *pad_net = nullptr;
    if (o) {
        pad_net = o;
        if (i) {
            // Tristate case - recombine the split port
            // This should be true because we disconnected the nextpnr_iobuf
            NPNR_ASSERT(i->users.empty());
            auto drv = i->driver;
            if (drv.cell) {
                drv.cell->disconnectPort(drv.port);
                drv.cell->connectPort(drv.port, pad_net);
            }
        }
    } else if (i) {
        pad_net = i;
    }

    npnr_iob->ports.clear();
    // TODO: should this always be INOUT ?
    if (i == nullptr && o != nullptr)
        npnr_iob->addOutput(id_PAD);
    else
        npnr_iob->addInout(id_PAD);
    npnr_iob->connectPort(id_PAD, pad_net);
    ctx->bindBel(bel, npnr_iob, STRENGTH_LOCKED);
    ctx->update_cell_bel_pins(npnr_iob);
}

bool is_io_buffer(Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("$nextpnr_ibuf") || cell->type == ctx->id("$nextpnr_obuf") ||
           cell->type == ctx->id("$nextpnr_iobuf");
}

WireId get_pad_wire(Context *ctx, BelId pad_bel)
{
    auto bel_pins = ctx->getBelPins(pad_bel);
    if (bel_pins.size() != 1)
        log_error("Expected only 1 pin on pad bel '%s', got %d\n", ctx->nameOfBel(pad_bel), int(bel_pins.size()));

    return ctx->getBelPinWire(pad_bel, bel_pins.at(0));
}

void place_attached_cells(Context *ctx, WireId pad_wire, const pool<std::pair<IdString, IdString>> &attached)
{
    // First try and place tightly attached cells
    std::queue<CellInfo *> place_queue;
    for (auto cell_port : attached) {
        CellInfo *ci = ctx->cells.at(cell_port.first).get();
        bool downhill = (ci->ports.at(cell_port.second).type != PORT_OUT);
        if (ci->bel != BelId())
            continue;
        if (search_routing_for_placement(ctx, pad_wire, ci, cell_port.second, downhill)) {
            log_info("    placed IO cell '%s' at '%s'.\n", ctx->nameOf(ci), ctx->nameOfBel(ci->bel));
            place_queue.push(ci);
        } else {
            log_error("Failed to find a possible placement for IO cell '%s'\n", ctx->nameOf(ci));
        }
    }
    // Also try, on a best-effort basis, to preplace other cells in the macro based on downstream routing. This is
    // needed for the split INBUF+IBUFCTRL arrangement in the UltraScale+, as just placing the INBUF will result in an
    // unrouteable site and illegal placement.
    while (!place_queue.empty()) {
        CellInfo *cursor = place_queue.front();
        place_queue.pop();
        // Ignore cells not part of a macro
        if (cursor->macro_parent == IdString())
            continue;
        for (auto &port : cursor->ports) {
            if (port.second.net == nullptr)
                continue;
            NetInfo *ni = port.second.net;
            if (port.second.type == PORT_OUT) {
                WireId src_wire = ctx->getNetinfoSourceWire(ni);
                for (auto &usr : ni->users) {
                    // Look for unplaced users in the same macro
                    if (usr.cell->bel != BelId() || usr.cell->macro_parent != cursor->macro_parent)
                        continue;
                    // Try and place using dedicated routing
                    if (search_routing_for_placement(ctx, src_wire, usr.cell, usr.port, true)) {
                        // Successful
                        place_queue.push(usr.cell);
                        log_info("    placed %s at %s based on dedicated IO macro routing.\n", ctx->nameOf(usr.cell),
                                 ctx->nameOfBel(usr.cell->bel));
                    }
                }
            } else {
                auto &drv = ni->driver;
                // Look for unplaced driver in the same macro
                if (drv.cell == nullptr || drv.cell->bel != BelId() || drv.cell->macro_parent != cursor->macro_parent)
                    continue;
                for (auto bel_pin : ctx->getBelPinsForCellPin(cursor, port.first)) {
                    // Try and place using dedicated routing
                    WireId dst_wire = ctx->getBelPinWire(cursor->bel, bel_pin);
                    if (search_routing_for_placement(ctx, dst_wire, drv.cell, drv.port, false)) {
                        // Successful
                        place_queue.push(drv.cell);
                        log_info("    placed %s at %s based on dedicated IO macro routing.\n", ctx->nameOf(drv.cell),
                                 ctx->nameOfBel(drv.cell->bel));
                    }
                }
            }
        }
    }
    // TODO: look beyond macros to place IOSERDES/IODELAYs using similar business logic
}

} // namespace

const PadInfoPOD *Arch::pad_by_name(const std::string &name) const
{
    IdString name_id = id(name);
    for (const auto &pad : package_info->pads) {
        if (IdString(pad.package_pin) == name_id)
            return &pad;
    }
    return nullptr;
}

BelId Arch::get_pad_bel(const PadInfoPOD *pad) const
{
    if (pad->tile == -1 || pad->site == -1)
        return BelId();
    const TileTypePOD &tile_data = chip_tile_info(chip_info, pad->tile);
    for (int i = 0; i < tile_data.bels.ssize(); i++) {
        auto &bel_data = tile_data.bels[i];
        if (bel_data.site == pad->site && bel_data.site_variant == 0 && bel_data.name == pad->bel_name)
            return BelId(pad->tile, i);
    }
    return BelId();
}

void Arch::pack_io()
{
    Context *ctx = getCtx();
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (!is_io_buffer(ctx, ci))
            continue;
        // Find which other cell ports are tightly attached to the pad
        pool<std::pair<IdString, IdString>> attached = get_attached_ports(ci);
        // Convert the nextpnr IO buffer to a pad, and place it based on location constraints
        iob_to_pad(ctx, ci);
        // Place cells attached to to the pad based on routeing
        WireId pad_wire = get_pad_wire(ctx, ci->bel);
        place_attached_cells(ctx, pad_wire, attached);
    }
}

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include <queue>

NEXTPNR_NAMESPACE_BEGIN

namespace {
bool search_routing_for_placement(Arch *arch, WireId start_wire, CellInfo *cell, IdString cell_pin, bool downhill)
{
    std::queue<WireId> visit_queue;
    pool<WireId> already_visited;
    visit_queue.push(start_wire);
    already_visited.insert(start_wire);
    int iter = 0;
    while (!visit_queue.empty() && iter++ < 1000) {
        WireId next = visit_queue.front();
        visit_queue.pop();
        for (auto bp : arch->getWireBelPins(next)) {
            if (!arch->isValidBelForCellType(cell->type, bp.bel))
                continue;
            if (!arch->checkBelAvail(bp.bel))
                continue;
            // We need to do a test placement to update the bel pin map
            arch->bindBel(bp.bel, cell, STRENGTH_FIXED);
            for (IdString bel_pin : arch->getBelPinsForCellPin(cell, cell_pin)) {
                if (bel_pin == bp.pin)
                    return true;
            }
            // Bel pin doesn't match
            arch->unbindBel(bp.bel);
        }
        auto do_visit = [&](PipId pip) {
            WireId dst = downhill ? arch->getPipDstWire(pip) : arch->getPipSrcWire(pip);
            if (already_visited.count(dst))
                return;
            visit_queue.push(dst);
            already_visited.insert(dst);
        };
        if (downhill) {
            for (auto pip : arch->getPipsDownhill(next))
                do_visit(pip);
        } else {
            for (auto pip : arch->getPipsUphill(next))
                do_visit(pip);
        }
    }
    return false;
}
} // namespace

void Arch::place_iobufs(WireId pad_wire, NetInfo *net,
                        const dict<CellInfo *, IdString, hash_ptr_ops> &tightly_attached_bels,
                        pool<CellInfo *, hash_ptr_ops> *placed_cells)
{
    Context *ctx = getCtx();
    for (auto cell_port : tightly_attached_bels) {
        bool downhill = (cell_port.first->ports.at(cell_port.second).type != PORT_OUT);
        if (cell_port.first->bel != BelId())
            continue;
        if (search_routing_for_placement(this, pad_wire, cell_port.first, cell_port.second, downhill)) {
            if (ctx->verbose)
                log_info("Placed IO cell %s:%s at %s.\n", ctx->nameOf(cell_port.first),
                         ctx->nameOf(cell_port.first->type), ctx->nameOfBel(cell_port.first->bel));
            placed_cells->insert(cell_port.first);
        }
    }

    // Also try, on a best-effort basis, to preplace other cells in the macro based on downstream routing. This is
    // needed for the split INBUF+IBUFCTRL arrangement in the UltraScale+, as just placing the INBUF will result in an
    // unrouteable site and illegal placement.
    std::queue<CellInfo *> place_queue;
    for (auto pc : *placed_cells)
        place_queue.push(pc);
    while (!place_queue.empty()) {
        CellInfo *cursor = place_queue.front();
        place_queue.pop();
        // Ignore cells not part of a macro
        if (cursor->macro_parent == IdString())
            continue;
        for (auto &port : cursor->ports) {
            // Only consider routing downstream from outputs for now
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
                    if (search_routing_for_placement(this, src_wire, usr.cell, usr.port, true)) {
                        // Successful
                        placed_cells->insert(usr.cell);
                        place_queue.push(usr.cell);
                        if (ctx->verbose)
                            log_info("Placed %s at %s based on dedicated IO macro routing.\n", ctx->nameOf(usr.cell),
                                     ctx->nameOfBel(usr.cell->bel));
                    }
                }
            } else {
                auto &drv = ni->driver;
                // Look for unplaced driver in the same macro
                if (drv.cell->bel != BelId() || drv.cell->macro_parent != cursor->macro_parent)
                    continue;
                for (auto bel_pin : ctx->getBelPinsForCellPin(cursor, port.first)) {
                    // Try and place using dedicated routing
                    WireId dst_wire = ctx->getBelPinWire(cursor->bel, bel_pin);
                    if (search_routing_for_placement(this, dst_wire, drv.cell, drv.port, false)) {
                        // Successful
                        placed_cells->insert(drv.cell);
                        place_queue.push(drv.cell);
                        if (ctx->verbose)
                            log_info("Placed %s at %s based on dedicated IO macro routing.\n", ctx->nameOf(drv.cell),
                                     ctx->nameOfBel(drv.cell->bel));
                    }
                }
            }
        }
    }
    // TODO: for even more complex cases, if any future devices hit them, we probably should do a full validity check of
    // all placed cells here, and backtrack and try a different placement if the first one we choose isn't legal overall
}

void Arch::pack_ports()
{
    dict<IdString, const TileInstInfoPOD *> tile_type_prototypes;
    for (size_t i = 0; i < chip_info->tiles.size(); ++i) {
        const auto &tile = chip_info->tiles[i];
        const auto &tile_type = chip_info->tile_types[tile.type];
        IdString tile_type_name(tile_type.name);
        tile_type_prototypes.emplace(tile_type_name, &tile);
    }

    // set(site_types) for package pins
    pool<IdString> package_sites;
    // Package pin -> (Site type -> BelId)
    dict<IdString, std::vector<std::pair<IdString, BelId>>> package_pin_bels;
    // Placed cells across all IO
    pool<CellInfo *, hash_ptr_ops> all_placed_io;
    for (const PackagePinPOD &package_pin : chip_info->packages[package_index].pins) {
        IdString pin(package_pin.package_pin);
        IdString bel(package_pin.bel);

        IdString site(package_pin.site);
        package_sites.emplace(site);

        for (size_t i = 0; i < chip_info->tiles.size(); ++i) {
            const auto &tile = chip_info->tiles[i];
            pool<uint32_t> package_pin_sites;
            for (size_t j = 0; j < tile.sites.size(); ++j) {
                auto &site_data = chip_info->sites[tile.sites[j]];
                if (site == id(site_data.site_name.get())) {
                    package_pin_sites.emplace(j);
                }
            }

            const auto &tile_type = chip_info->tile_types[tile.type];
            for (size_t j = 0; j < tile_type.bel_data.size(); ++j) {
                const BelInfoPOD &bel_data = tile_type.bel_data[j];
                if (bel == IdString(bel_data.name) && package_pin_sites.count(bel_data.site)) {
                    auto &site_data = chip_info->sites[tile.sites[bel_data.site]];
                    IdString site_type(site_data.site_type);
                    BelId bel;
                    bel.tile = i;
                    bel.index = j;
                    package_pin_bels[pin].push_back(std::make_pair(site_type, bel));
                }
            }
        }
    }

    // Determine for each package site type, which site types are possible.
    pool<IdString> package_pin_site_types;
    dict<IdString, pool<IdString>> possible_package_site_types;
    for (const TileInstInfoPOD &tile : chip_info->tiles) {
        for (size_t site_index : tile.sites) {
            const SiteInstInfoPOD &site = chip_info->sites[site_index];
            IdString site_name = getCtx()->id(site.site_name.get());
            if (package_sites.count(site_name) == 1) {
                possible_package_site_types[site_name].emplace(IdString(site.site_type));
                package_pin_site_types.emplace(IdString(site.site_type));
            }
        }
    }

    // IO sites are usually pretty weird, so see if we can define some
    // constraints between the port cell create by nextpnr and cells that are
    // immediately attached to that port cell.
    for (auto port_pair : port_cells) {
        IdString port_name = port_pair.first;
        CellInfo *port_cell = port_pair.second;
        dict<CellInfo *, IdString, hash_ptr_ops> tightly_attached_bels;

        for (auto port_pair : port_cell->ports) {
            const PortInfo &port_info = port_pair.second;
            const NetInfo *net = port_info.net;
            if (net->driver.cell) {
                tightly_attached_bels.emplace(net->driver.cell, net->driver.port);
            }

            for (const PortRef &port_ref : net->users) {
                if (port_ref.cell) {
                    tightly_attached_bels.emplace(port_ref.cell, port_ref.port);
                }
            }
        }

        if (getCtx()->verbose) {
            log_info("Tightly attached BELs for port %s\n", port_name.c_str(getCtx()));
            for (auto cell_port : tightly_attached_bels) {
                log_info(" - %s : %s\n", cell_port.first->name.c_str(getCtx()), cell_port.first->type.c_str(getCtx()));
            }
        }

        NPNR_ASSERT(tightly_attached_bels.erase(port_cell) == 1);
        pool<IdString> cell_types_in_io_group;
        for (auto cell_port : tightly_attached_bels) {
            NPNR_ASSERT(port_cells.find(cell_port.first->name) == port_cells.end());
            cell_types_in_io_group.emplace(cell_port.first->type);
        }

        // Get possible placement locations for tightly coupled BELs with
        // port.
        pool<IdString> possible_site_types;
        for (const TileTypeInfoPOD &tile_type : chip_info->tile_types) {
            IdString tile_type_name(tile_type.name);
            for (const BelInfoPOD &bel_info : tile_type.bel_data) {
                if (bel_info.category != BEL_CATEGORY_LOGIC) {
                    break;
                }

                for (IdString cell_type : cell_types_in_io_group) {
                    size_t cell_type_index = get_cell_type_index(cell_type);
                    if (bel_info.category == BEL_CATEGORY_LOGIC && bel_info.pin_map[cell_type_index] != -1) {
                        auto *tile = tile_type_prototypes.at(tile_type_name);
                        const SiteInstInfoPOD &site = chip_info->sites[tile->sites[bel_info.site]];

                        IdString site_type(site.site_type);
                        if (package_pin_site_types.count(site_type)) {
                            possible_site_types.emplace(site_type);
                        }
                    }
                }
            }
        }

        if (possible_site_types.empty()) {
            if (getCtx()->verbose)
                log_info("Port '%s' has no possible site types, falling back to all types!\n",
                         port_name.c_str(getCtx()));
            possible_site_types = package_pin_site_types;
        }

        if (getCtx()->verbose) {
            log_info("Possible site types for port %s\n", port_name.c_str(getCtx()));
            for (IdString site_type : possible_site_types) {
                log_info(" - %s\n", site_type.c_str(getCtx()));
            }
        }

        auto iter = port_cell->attrs.find(id("PACKAGE_PIN"));
        if (iter == port_cell->attrs.end()) {
            iter = port_cell->attrs.find(id("LOC"));
            if (iter == port_cell->attrs.end()) {
                log_error("Port '%s' is missing PACKAGE_PIN or LOC property\n", port_cell->name.c_str(getCtx()));
            }
        }

        // dict<IdString, dict<IdString, BelId>> package_pin_bels;
        IdString package_pin_id = id(iter->second.as_string());
        auto pin_iter = package_pin_bels.find(package_pin_id);
        if (pin_iter == package_pin_bels.end()) {
            log_error("Package pin '%s' not found in part %s\n", package_pin_id.c_str(getCtx()), get_part().c_str());
        }
        NPNR_ASSERT(pin_iter != package_pin_bels.end());

        // Select the first BEL from package_bel_pins that is a legal site
        // type.
        //
        // This is likely the most generic (versus specialized) site type.
        BelId package_bel;
        for (auto site_type_and_bel : pin_iter->second) {
            IdString legal_site_type = site_type_and_bel.first;
            BelId bel = site_type_and_bel.second;

            if (possible_site_types.count(legal_site_type)) {
                // FIXME: Need to handle case where a port can be in multiple
                // modes, but only one of the modes works.
                package_bel = bel;
                break;
            }
        }

        if (package_bel == BelId()) {
            log_info("Failed to find BEL for package pin '%s' in any possible site types:\n",
                     package_pin_id.c_str(getCtx()));
            for (IdString site_type : possible_site_types) {
                log_info(" - %s\n", site_type.c_str(getCtx()));
            }
            log_error("Failed to find BEL for package pin '%s'\n", package_pin_id.c_str(getCtx()));
        }

        if (getCtx()->verbose) {
            log_info("Binding port %s to BEL %s\n", port_name.c_str(getCtx()), getCtx()->nameOfBel(package_bel));
        }

        pool<CellInfo *, hash_ptr_ops> placed_cells;
        bindBel(package_bel, port_cell, STRENGTH_FIXED);
        placed_cells.emplace(port_cell);

        IdStringRange package_bel_pins = getBelPins(package_bel);
        IdString pad_pin = get_only_value(package_bel_pins);

        WireId pad_wire = getBelPinWire(package_bel, pad_pin);
        place_iobufs(pad_wire, ports[port_pair.first].net, tightly_attached_bels, &placed_cells);

        for (CellInfo *cell : placed_cells)
            all_placed_io.insert(cell);
    }

    // Check at the end of IO placement, because differential pairs might need P and N sides to both be placed to be
    // legal.
    for (CellInfo *cell : all_placed_io) {
        log_info("%s\n", getCtx()->nameOf(cell));
        NPNR_ASSERT(cell->bel != BelId());
        if (!isBelLocationValid(cell->bel)) {
            explain_bel_status(cell->bel);
            log_error("Tightly bound BEL %s was not valid!\n", nameOfBel(cell->bel));
        }
    }
}

NEXTPNR_NAMESPACE_END

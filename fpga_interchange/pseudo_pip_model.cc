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

#include "pseudo_pip_model.h"

#include "context.h"

//#define DEBUG_PSEUDO_PIP

NEXTPNR_NAMESPACE_BEGIN

void PseudoPipData::init_tile_type(const Context *ctx, int32_t tile_type)
{
    if (max_pseudo_pip_for_tile_type.count(tile_type)) {
        return;
    }

    const TileTypeInfoPOD &type_data = ctx->chip_info->tile_types[tile_type];
    int32_t max_pseudo_pip_index = -1;
    for (int32_t pip_idx = 0; pip_idx < type_data.pip_data.ssize(); ++pip_idx) {
        const PipInfoPOD &pip_data = type_data.pip_data[pip_idx];
        if (pip_data.pseudo_cell_wires.size() == 0) {
            continue;
        }

        if (pip_idx > max_pseudo_pip_index) {
            max_pseudo_pip_index = pip_idx;
        }

        HashTables::HashSet<size_t> sites;
        std::vector<PseudoPipBel> pseudo_pip_bels;
        for (int32_t wire_index : pip_data.pseudo_cell_wires) {
            const TileWireInfoPOD &wire_data = type_data.wire_data[wire_index];
            if (wire_data.site == -1) {
                continue;
            }

            // Only use primary site types for psuedo pips
            //
            // Note: This assumption may be too restrictive.  If so, then
            // need to update database generators to provide
            // pseudo_cell_wires for each site type, not just the primary.
            if (wire_data.site_variant != -1) {
                continue;
            }

            sites.emplace(wire_data.site);

            int32_t driver_bel = -1;
            int32_t output_pin = -1;
            for (const BelPortPOD &bel_pin : wire_data.bel_pins) {
                const BelInfoPOD &bel_data = type_data.bel_data[bel_pin.bel_index];
                if (bel_data.synthetic != NOT_SYNTH) {
                    // Ignore synthetic BELs
                    continue;
                }

                if (bel_data.category != BEL_CATEGORY_LOGIC) {
                    // Ignore site ports and site routing
                    continue;
                }

                int32_t bel_pin_idx = -1;
                for (int32_t i = 0; i < bel_data.num_bel_wires; ++i) {
                    if (bel_data.ports[i] == bel_pin.port) {
                        bel_pin_idx = i;
                        break;
                    }
                }

                NPNR_ASSERT(bel_pin_idx != -1);
                if (bel_data.types[bel_pin_idx] != PORT_OUT) {
                    // Only care about output ports.  Input ports may not be
                    // part of the pseudo pip.
                    continue;
                }

                // Each site wire should have 1 driver!
                NPNR_ASSERT(driver_bel == -1);
                driver_bel = bel_pin.bel_index;
                output_pin = bel_pin_idx;
            }

            if (driver_bel != -1) {
                NPNR_ASSERT(output_pin != -1);
                PseudoPipBel bel;
                bel.bel_index = driver_bel;
                bel.output_bel_pin = output_pin;

                pseudo_pip_bels.push_back(bel);
            }
        }

        std::pair<int32_t, int32_t> key{tile_type, pip_idx};
        std::vector<size_t> &sites_for_pseudo_pip = possibles_sites_for_pip[key];
        sites_for_pseudo_pip.clear();
        sites_for_pseudo_pip.insert(sites_for_pseudo_pip.begin(), sites.begin(), sites.end());
        std::sort(sites_for_pseudo_pip.begin(), sites_for_pseudo_pip.end());

        // Initialize "logic_bels_for_pip" for every site that this pseudo pip
        // appears.  This means that if there are no pseudo_pip_bels, those
        // vectors will be empty.
        for (int32_t site : sites_for_pseudo_pip) {
            logic_bels_for_pip[LogicBelKey{tile_type, pip_idx, site}].clear();
        }

        if (!pseudo_pip_bels.empty()) {
            HashTables::HashSet<int32_t> pseudo_cell_wires;
            pseudo_cell_wires.insert(pip_data.pseudo_cell_wires.begin(), pip_data.pseudo_cell_wires.end());

            // For each BEL, find the input bel pin used, and attach it to
            // the vector for that site.
            //
            // Note: Intentially copying the bel for mutation, and then
            // pushing onto vector.
            for (PseudoPipBel bel : pseudo_pip_bels) {
                const BelInfoPOD &bel_data = type_data.bel_data[bel.bel_index];
                int32_t site = bel_data.site;

                int32_t input_bel_pin = -1;
                int32_t output_bel_pin = -1;
                for (int32_t i = 0; i < bel_data.num_bel_wires; ++i) {
                    if (!pseudo_cell_wires.count(bel_data.wires[i])) {
                        continue;
                    }

                    if (bel_data.types[i] == PORT_OUT) {
                        NPNR_ASSERT(output_bel_pin == -1);
                        output_bel_pin = i;
                    }

                    if (bel_data.types[i] == PORT_IN && input_bel_pin == -1) {
                        // Take first input BEL pin
                        //
                        // FIXME: This heuristic feels fragile.
                        // This data oaught come from the database.
                        input_bel_pin = i;
                    }
                }

                NPNR_ASSERT(output_bel_pin == bel.output_bel_pin);
                bel.input_bel_pin = input_bel_pin;

                logic_bels_for_pip[LogicBelKey{tile_type, pip_idx, site}].push_back(bel);
            }
        }
    }

    max_pseudo_pip_for_tile_type[tile_type] = max_pseudo_pip_index;
}

const std::vector<size_t> &PseudoPipData::get_possible_sites_for_pip(const Context *ctx, PipId pip) const
{
    int32_t tile_type = ctx->chip_info->tiles[pip.tile].type;
    return possibles_sites_for_pip.at(std::make_pair(tile_type, pip.index));
}

size_t PseudoPipData::get_max_pseudo_pip(int32_t tile_type) const { return max_pseudo_pip_for_tile_type.at(tile_type); }

const std::vector<PseudoPipBel> &PseudoPipData::get_logic_bels_for_pip(const Context *ctx, int32_t site,
                                                                       PipId pip) const
{
    int32_t tile_type = ctx->chip_info->tiles[pip.tile].type;
    return logic_bels_for_pip.at(LogicBelKey{tile_type, pip.index, site});
}

void PseudoPipModel::init(Context *ctx, int32_t tile_idx)
{
    int32_t tile_type = ctx->chip_info->tiles[tile_idx].type;

    this->tile = tile_idx;

    allowed_pseudo_pips.resize(ctx->pseudo_pip_data.get_max_pseudo_pip(tile_type) + 1);
    allowed_pseudo_pips.fill(true);
}

void PseudoPipModel::prepare_for_routing(const Context *ctx, const std::vector<SiteRouter> &sites)
{
    // First determine which sites have placed cells, these sites are consider
    // active.
    HashTables::HashSet<size_t> active_sites;
    for (size_t site = 0; site < sites.size(); ++site) {
        if (!sites[site].cells_in_site.empty()) {
            active_sites.emplace(site);
        }
    }

    // Assign each pseudo pip in this tile a site, which is either the active
    // site (if the site / alt site is in use) or the first site that pseudo
    // pip appears in.
    int32_t tile_type = ctx->chip_info->tiles[tile].type;
    const TileTypeInfoPOD &type_data = ctx->chip_info->tile_types[tile_type];

    pseudo_pip_sites.clear();
    site_to_pseudo_pips.clear();

    for (size_t pip_idx = 0; pip_idx < type_data.pip_data.size(); ++pip_idx) {
        const PipInfoPOD &pip_data = type_data.pip_data[pip_idx];
        if (pip_data.pseudo_cell_wires.size() == 0) {
            continue;
        }

        PipId pip;
        pip.tile = tile;
        pip.index = pip_idx;
        const std::vector<size_t> &sites = ctx->pseudo_pip_data.get_possible_sites_for_pip(ctx, pip);

        int32_t site_for_pip = -1;
        for (size_t possible_site : sites) {
            if (active_sites.count(possible_site)) {
                site_for_pip = possible_site;
                break;
            }
        }

        if (site_for_pip < 0) {
            site_for_pip = sites.at(0);
        }

        pseudo_pip_sites[pip_idx] = site_for_pip;
        site_to_pseudo_pips[site_for_pip].push_back(pip_idx);
    }

    for (auto &site_pair : site_to_pseudo_pips) {
        update_site(ctx, site_pair.first);
    }
}

bool PseudoPipModel::checkPipAvail(const Context *ctx, PipId pip) const
{
    bool allowed = allowed_pseudo_pips.get(pip.index);
    if (!allowed) {
#ifdef DEBUG_PSEUDO_PIP
        if (ctx->verbose) {
            log_info("Pseudo pip %s not allowed\n", ctx->nameOfPip(pip));
        }
#endif
    }

    return allowed;
}

void PseudoPipModel::bindPip(const Context *ctx, PipId pip)
{
    // If pseudo_pip_sites is empty, then prepare_for_routing was never
    // invoked.  This is likely because PseudoPipModel was constructed during
    // routing.
    if (pseudo_pip_sites.empty()) {
        prepare_for_routing(ctx, ctx->tileStatus.at(tile).sites);
    }

    // Do not allow pseudo pips to be bound if they are not allowed!
    NPNR_ASSERT(allowed_pseudo_pips.get(pip.index));

    // Mark that this pseudo pip is active.
    auto result = active_pseudo_pips.emplace(pip.index);
    NPNR_ASSERT(result.second);

    // Update the site this pseudo pip is within.
    size_t site = pseudo_pip_sites.at(pip.index);
    update_site(ctx, site);
}

void PseudoPipModel::unbindPip(const Context *ctx, PipId pip)
{
    // It should not be possible for unbindPip to be invoked with
    // pseudo_pip_sites being empty.
    NPNR_ASSERT(!pseudo_pip_sites.empty());

    NPNR_ASSERT(active_pseudo_pips.erase(pip.index));

    // Remove the site this pseudo pip is within.
    size_t site = pseudo_pip_sites.at(pip.index);
    update_site(ctx, site);
}

void PseudoPipModel::update_site(const Context *ctx, size_t site)
{
    // update_site consists of several steps:
    //
    //  - Find all BELs within the site used by pseudo pips.
    //  - Trivially marking other pseudo pips as unavailable if it requires
    //    logic BELs used by active pseudo pips (or bound by cells).
    //  - Determine if remaining pseudo pips can be legally placed.  This
    //    generally consists of:
    //     - Checking LUT element
    //     - FIXME: Checking constraints (when metadata is available)

    const std::vector<int32_t> pseudo_pips_for_site = site_to_pseudo_pips.at(site);

    std::vector<int32_t> &unused_pseudo_pips = scratch;
    unused_pseudo_pips.clear();
    unused_pseudo_pips.reserve(pseudo_pips_for_site.size());

    HashTables::HashMap<int32_t, PseudoPipBel> used_bels;
    for (int32_t pseudo_pip : pseudo_pips_for_site) {
        if (!active_pseudo_pips.count(pseudo_pip)) {
            unused_pseudo_pips.push_back(pseudo_pip);
            continue;
        }

        PipId pip;
        pip.tile = tile;
        pip.index = pseudo_pip;
        for (const PseudoPipBel &bel : ctx->pseudo_pip_data.get_logic_bels_for_pip(ctx, site, pip)) {
            used_bels.emplace(bel.bel_index, bel);
        }
    }

    if (unused_pseudo_pips.empty()) {
        return;
    }

    int32_t tile_type = ctx->chip_info->tiles[tile].type;
    const TileTypeInfoPOD &type_data = ctx->chip_info->tile_types[tile_type];

    // This section builds up LUT mapping logic to determine which LUT wires
    // are availble and which are not.
    const std::vector<LutElement> &lut_elements = ctx->lut_elements.at(tile_type);
    std::vector<LutMapper> lut_mappers;
    lut_mappers.reserve(lut_elements.size());
    for (size_t i = 0; i < lut_elements.size(); ++i) {
        lut_mappers.push_back(LutMapper(lut_elements[i]));
    }

    const TileStatus &tile_status = ctx->tileStatus.at(tile);
    for (CellInfo *cell : tile_status.sites[site].cells_in_site) {
        if (cell->lut_cell.pins.empty()) {
            continue;
        }

        BelId bel = cell->bel;
        const auto &bel_data = bel_info(ctx->chip_info, bel);
        if (bel_data.lut_element != -1) {
            lut_mappers[bel_data.lut_element].cells.push_back(cell);
        }
    }

    std::vector<CellInfo> lut_cells;
    lut_cells.reserve(used_bels.size());
    for (const auto &bel_pair : used_bels) {
        const PseudoPipBel &bel = bel_pair.second;
        const BelInfoPOD &bel_data = type_data.bel_data[bel.bel_index];

        // This used BEL isn't a LUT, skip it!
        if (bel_data.lut_element == -1) {
            continue;
        }

        lut_cells.emplace_back();
        CellInfo &cell = lut_cells.back();

        cell.bel.tile = tile;
        cell.bel.index = bel_pair.first;

        if (ctx->wire_lut == nullptr) {
            continue;
        }

        cell.type = IdString(ctx->wire_lut->cell);
        NPNR_ASSERT(ctx->wire_lut->input_pins.size() == 1);
        cell.lut_cell.pins.push_back(IdString(ctx->wire_lut->input_pins[0]));

        if (bel.input_bel_pin == -1) {
            // FIXME: currently assume that LUT route-throughs with no input pins are GND drivers as this is all we need
            // for Nexus/Xilinx where Vcc is readily available and cheap This won't be true for other arches
            cell.lut_cell.equation.resize(2);
            cell.lut_cell.equation.set(0, false);
            cell.lut_cell.equation.set(1, false);
        } else {
            cell.lut_cell.equation.resize(2);
            cell.lut_cell.equation.set(0, false);
            cell.lut_cell.equation.set(1, true);

            // Map LUT input to input wire used by pseudo pip.
            IdString input_bel_pin(bel_data.ports[bel.input_bel_pin]);
            cell.cell_bel_pins[IdString(ctx->wire_lut->input_pins[0])].push_back(input_bel_pin);
        }

        lut_mappers[bel_data.lut_element].cells.push_back(&cell);
    }

    std::vector<uint32_t> lut_wires_unavailable;
    lut_wires_unavailable.reserve(lut_elements.size());
    for (LutMapper &lut_mapper : lut_mappers) {
        lut_wires_unavailable.push_back(lut_mapper.check_wires(ctx));
    }

    // For unused pseudo pips, see if the BEL used is idle.
    for (int32_t pseudo_pip : unused_pseudo_pips) {
        PipId pip;
        pip.tile = tile;
        pip.index = pseudo_pip;

        bool blocked_by_bel = false;
        const std::vector<PseudoPipBel> &bels = ctx->pseudo_pip_data.get_logic_bels_for_pip(ctx, site, pip);
        for (const PseudoPipBel &bel : bels) {
            if (tile_status.boundcells[bel.bel_index] != nullptr) {
                blocked_by_bel = true;

#ifdef DEBUG_PSEUDO_PIP
                if (ctx->verbose) {
                    BelId abel;
                    abel.tile = tile;
                    abel.index = bel.bel_index;
                    log_info("Pseudo pip %s is block by a bound BEL %s\n", ctx->nameOfPip(pip), ctx->nameOfBel(abel));
                }
#endif
                break;
            }

            if (used_bels.count(bel.bel_index)) {
#ifdef DEBUG_PSEUDO_PIP
                if (ctx->verbose) {
                    log_info("Pseudo pip %s is block by another pseudo pip\n", ctx->nameOfPip(pip));
                }
#endif
                blocked_by_bel = true;
                break;
            }
        }

        if (blocked_by_bel) {
            allowed_pseudo_pips.set(pseudo_pip, false);
            continue;
        }

        bool blocked_by_lut_eq = false;

        // See if any BELs are part of a LUT element.  If so, see if using
        // that pseudo pip violates the LUT element equation.
        for (const PseudoPipBel &bel : bels) {
            const BelInfoPOD &bel_data = type_data.bel_data[bel.bel_index];
            if (bel_data.lut_element == -1) {
                continue;
            }

            // FIXME: Check if the pseudo cell satifies the constraint system.
            // Will become important for LUT-RAM/SRL testing.

            // FIXME: This lookup is static, consider moving to PseudoPipBel?
            IdString bel_name(bel_data.name);
            if (bel.input_bel_pin == -1) {
                // No input bel pin (e.g. LUT as constant driver) - check that *any* input is available, i.e. there is
                // some room in the LUT equation still
                size_t pin_count = lut_elements.at(bel_data.lut_element).lut_bels.at(bel_name).pins.size();
                uint32_t pin_mask = (1 << uint32_t(pin_count)) - 1;
                uint32_t blocked_inputs = lut_wires_unavailable.at(bel_data.lut_element);
                if ((blocked_inputs & pin_mask) == pin_mask) {
                    blocked_by_lut_eq = true;
                    break;
                }
            } else {
                IdString input_bel_pin(bel_data.ports[bel.input_bel_pin]);
                size_t pin_idx =
                        lut_elements.at(bel_data.lut_element).lut_bels.at(bel_name).pin_to_index.at(input_bel_pin);

                uint32_t blocked_inputs = lut_wires_unavailable.at(bel_data.lut_element);
                if ((blocked_inputs & (1 << pin_idx)) != 0) {
                    blocked_by_lut_eq = true;
                    break;
                }
            }
        }

        if (blocked_by_lut_eq) {
#ifdef DEBUG_PSEUDO_PIP
            if (ctx->verbose) {
                log_info("Pseudo pip %s is blocked by lut eq\n", ctx->nameOfPip(pip));
            }
#endif
            allowed_pseudo_pips.set(pseudo_pip, false);
            continue;
        }

        // Pseudo pip should be allowed, mark as such.
        //
        // FIXME: Handle non-LUT constraint cases, as needed.
        allowed_pseudo_pips.set(pseudo_pip, true);
    }
}

NEXTPNR_NAMESPACE_END

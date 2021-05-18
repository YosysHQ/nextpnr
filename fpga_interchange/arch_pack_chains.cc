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

#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "arch.h"
#include "util.h"

#include <boost/algorithm/string.hpp>
#include <queue>

NEXTPNR_NAMESPACE_BEGIN

CellInfo* Arch::getClusterRootCell(ClusterId cluster) const
{
    NPNR_ASSERT(cluster != ClusterId());
    CellInfo *ci = nullptr;
    auto cell = getCtx()->cells.find(cluster);
    if (cell != getCtx()->cells.end()) {
        ci = cell->second.get();
    }
    return ci;
}

bool Arch::getClusterPlacement(ClusterId cluster, BelId root_bel,
                         std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    const Context *ctx = getCtx();
    IdString belType = getBelType(root_bel);

    // Place root
    CellInfo *root_cell = getClusterRootCell(cluster);
    placement.push_back(std::make_pair(root_cell, root_bel));

    // Place cluster
    auto cluster_coord_cfgs = cluster_to_coord_configs.find(cluster);
    std::vector<std::pair<ChainCoord, int>> coord_configs = cluster_coord_cfgs->second;
    CellInfo *next_cell = root_cell;
    NetInfo *next_net = nullptr;
    BelId next_bel = root_bel;
    Loc loc = getBelLocation(next_bel);
    log_info("Cluster '%s' [%s] Placement:\n", root_cell->cluster.c_str(ctx), belType.c_str(ctx));
    log_info("Cell '%s' placed on (%d, %d, %d)\n", next_cell->name.c_str(ctx), loc.x, loc.y, loc.z);
    while (true) {
        // Get pattern to find next_cell
        auto cell_pattern = cell_pattern_map.find(next_cell->name);
        std::pair<IdString, IdString> pattern_cfg = cell_pattern->second;
        IdString port = pattern_cfg.second;
        if (port == IdString()) {
            log_error("Cluster misconfiguration! None of patterns match cell: '%s'\n", next_cell->type.c_str(ctx));
        }
        next_net = next_cell->ports[port].net;
        if (next_net == nullptr || next_net->users.size() == 0) {
            break;
        }
        next_cell = next_net->users.at(0).cell;

        loc = getBelLocation(next_bel);
        bool bel_found = false;
        Loc next_loc = loc;
        for (auto cfg : coord_configs) {
            ChainCoord coord = cfg.first;
            if (coord == CHAIN_X_COORD) {
                next_loc.x += cfg.second;
            } else {
                next_loc.y += cfg.second;
            }
            // HACK / FIXME
            if (next_loc.x > 64 || next_loc.x < 0) {
                return false;
            }
            if (next_loc.y > 154 || next_loc.y < 0) {
                return false;
            }
            // ------------
            next_bel = getBelByLocation(next_loc);
            bel_found = next_bel.index == root_bel.index;
            if(bel_found) {
                break;
            }
        }
        if (!bel_found) {
            // TODO: improve this message
            log_error("Cannot place cell: '%s'\n", next_cell->name.c_str(ctx));
        }

        Loc bel_loc = getBelLocation(next_bel);
        placement.push_back(std::make_pair(next_cell, next_bel));
        log_info("Cell '%s' placed on (%d, %d, %d)\n", next_cell->name.c_str(ctx), bel_loc.x, bel_loc.y, bel_loc.z);
    }

    log_info("Cluster placement for root cell: '%s'\n", root_cell->name.c_str(ctx));

    return true;
}

ArcBounds Arch::getClusterBounds(ClusterId cluster) const
{
    ArcBounds bounds(0, 0, 0, 0);

    for (auto cell : sorted(getCtx()->cells)) {
        CellInfo *ci = cell.second;
        Loc cell_loc = getBelLocation(ci->bel);
        if (ci->cluster == cluster) {
            bounds.x0 = std::min(bounds.x0, cell_loc.x);
            bounds.y0 = std::min(bounds.y0, cell_loc.y);
            bounds.x1 = std::max(bounds.x1, cell_loc.x);
            bounds.y1 = std::max(bounds.y1, cell_loc.y);
        }
    }
}

Loc Arch::getClusterOffset(const CellInfo *cell) const
{
    Loc offset;
    if (cell->bel != BelId()) {
        CellInfo *root = getClusterRootCell(cell->cluster);
        Loc root_loc = getBelLocation(root->bel);
        Loc cell_loc = getBelLocation(cell->bel);
        offset.x = cell_loc.x - root_loc.x;
        offset.y = cell_loc.y - root_loc.y;
        offset.z = cell_loc.z - root_loc.z;
    }

    return offset;
}

bool Arch::isClusterStrict(const CellInfo *cell) const
{
    return true;
}

void dump_chains(const ChipInfoPOD *chip_info, Context *ctx)
{
    std::unordered_map<IdString, const BelChainPOD *> bel_chain_prototypes;
    for (size_t i = 0; i < chip_info->bel_chains.size(); ++i) {
        const auto &bel_chain = chip_info->bel_chains[i];
        IdString bel_chain_name(bel_chain.name);
        bel_chain_prototypes.emplace(bel_chain_name, &bel_chain);
        log_info("BEL chain '%s' loaded! Parameters:\n", bel_chain_name.c_str(ctx));
        log_info("  - sites:\n");
            for (auto site : bel_chain.sites) {
                IdString site_name(site);
                log_info("      - %s\n", site_name.c_str(ctx));
            }
        log_info("  - cells:\n");
            for (auto cell : bel_chain.cells) {
                IdString cell_name(cell);
                log_info("      - %s\n", cell_name.c_str(ctx));
            }
        log_info("  - coord_configs:\n");
            for (auto cfg : bel_chain.chain_coord_configs) {
                log_info("      - coord: %d | step: %d\n", cfg.coord, cfg.step);
            }
        log_info("  - patterns:\n");
            for (auto &pattern : bel_chain.chain_patterns) {
                IdString source_type(pattern.source->type);
                IdString source_port(pattern.source->port);
                IdString sink_type(pattern.sink->type);
                IdString sink_port(pattern.sink->port);
                log_info("      - %s.%s -> %s.%s\n", source_type.c_str(ctx), source_port.c_str(ctx), sink_type.c_str(ctx), sink_port.c_str(ctx));
            }
    }
}

void Arch::prepare_cluster(const BelChainPOD *chain)
{
    Context *ctx = getCtx();
    IdString chain_name(chain->name);

    // Get chainable cells
    std::vector<CellInfo *> chainable_cells;
    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        for (auto chain_cell : chain->cells) {
            IdString chain_cell_type(chain_cell);
            if (ci->type == chain_cell_type) {
                chainable_cells.push_back(ci);
            }
        }
    }

    // Find roots from chainable cells
    std::vector<CellInfo *> roots;
    for (auto cell : chainable_cells) {
        for (auto &pattern : chain->chain_patterns) {
            IdString snk_cell_type(pattern.sink->type);
            IdString snk_cell_port(pattern.sink->port);

            if (snk_cell_type != cell->type || cell->ports.find(snk_cell_port) == cell->ports.end()) {
                continue;
            }
            PortRef driver = cell->ports[snk_cell_port].net->driver;
            if (driver.cell == nullptr) {
                goto handle_root;
            } else if (driver.cell->type != cell->type) {
            handle_root:
                // We hit a root cell
                cell->cluster.set(ctx, cell->name.str(ctx));
                roots.push_back(cell);
                std::vector<std::pair<ChainCoord, int>> configs;
                for (auto coord_cfg : chain->chain_coord_configs) {
                    std::pair<ChainCoord, int> cfg((ChainCoord)coord_cfg.coord, (int)coord_cfg.step);
                    configs.push_back(cfg);
                }
                cluster_to_coord_configs.emplace(cell->cluster, configs);
                break;
            }
        }
    }

    // Generate unique clusters starting from each root
    for (auto root : roots) {
        CellInfo *next_cell = root;
        std::string cluster_path = "";
        while (next_cell != nullptr) {
            cluster_path += next_cell->name.str(ctx) + " -> ";
            // Find possible source type/port to follow in cluster building
            std::pair<IdString, IdString> config;
            for (auto &pattern : chain->chain_patterns) {
                IdString src_cell_type(pattern.source->type);
                IdString src_cell_port(pattern.source->port);
                if (src_cell_type != next_cell->type || next_cell->ports.find(src_cell_port) == next_cell->ports.end()) {
                    src_cell_type = IdString();
                    src_cell_port = IdString();
                    continue;
                }
                config = std::make_pair(src_cell_type, src_cell_port);
            }
            if (config.first == IdString() || config.second == IdString()) {
                log_error("Chain pattern not found for cell: '%s'\n", next_cell->name.c_str(ctx));
                break;
            }
            cell_pattern_map.emplace(next_cell->name, config);
            IdString src_cell_port = cell_pattern_map[next_cell->name].second;

            next_cell->cluster = root->cluster;
            NetInfo *next_net = next_cell->ports[src_cell_port].net;

            if (next_net != nullptr) {
                if (next_net->users.size() > 1) {
                    log_error("Chain cell '%s' has multiple fanout on net: %s\n", next_cell->name.c_str(ctx), next_net->name.c_str(ctx));
                } else if (next_net->users.size() == 1) {
                    // We have a next cell in a cluster
                    next_cell = next_net->users.at(0).cell;
                } else {
                    // We hit the end of a cluster
                    next_cell = nullptr;
                }
            } else {
                // Cluster contains only root cell
                next_cell = nullptr;
            }

            if (next_cell == nullptr) {
                cluster_path += "|end|\n";
            }
        }
        log_info("Created cluster: '%s' with following path:\n  |root| %s", root->cluster.c_str(ctx), cluster_path.c_str());
    }
}   

void Arch::pack_chains()
{
    Context *ctx = getCtx();

    // Dump loaded chain configurations
    dump_chains(chip_info, ctx);

    for (size_t i = 0; i < chip_info->bel_chains.size(); ++i) {
        const auto &chain = chip_info->bel_chains[i];

        // Build clusters and find roots
        prepare_cluster(&chain);
    }
}

NEXTPNR_NAMESPACE_END

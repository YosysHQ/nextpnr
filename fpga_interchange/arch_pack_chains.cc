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
    for (auto cell : sorted(getCtx()->cells)) {
        ci = cell.second;
        if (ci->cluster == cluster && cluster_roots.find(ci->name) != cluster_roots.end()) {
            break;
        }
    }
    return ci;
}

bool getClusterPlacement(ClusterId cluster, BelId root_bel,
                         std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    // Load coord config if available?
    // Place cluster
    NPNR_ASSERT_FALSE("unimplemented");
}

// TODO
//ArcBounds getClusterBounds(ClusterId cluster) const
//{
//}
//Loc getClusterOffset(const CellInfo *cell) const
//{
//}
//bool isClusterStrict(const CellInfo *cell) const
//{
//}

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
        log_info("  - patterns:\n");
            for (auto pattern : bel_chain.chain_patterns) {
                IdString source(pattern.source);
                IdString sink(pattern.sink);
                log_info("      - %s -> %s\n", source.c_str(ctx), sink.c_str(ctx));
            }
        log_info("  - coord_configs:\n");
            for (auto cfg : bel_chain.chain_coord_configs) {
                log_info("      - coord: %d | step: %d\n", cfg.coord, cfg.step);
            }
    }
}

void Arch::prepare_cluster(const BelChainPOD *chain)
{
    Context *ctx = getCtx();

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

    // Prepare sources and sinks pairs from provided patterns
    // pair: <cell_type, cell_port>
    std::vector<std::pair<IdString, IdString>> sources, sinks;
    std::string delimiter = ".";
    for (auto pattern : chain->chain_patterns) {
        IdString source(pattern.source);
        IdString sink(pattern.sink);
        IdString source_type;
        IdString sink_type;
        std::string source_str = source.c_str(ctx);
        std::string sink_str = sink.c_str(ctx);
        std::string source_type_str;
        std::string sink_type_str;

        source_type_str = source_str.substr(0, source_str.find(delimiter));
        source_str = source_str.substr(source_str.find(delimiter) + 1, source_str.back());
        source_type.set(ctx, source_type_str);
        source.set(ctx, source_str);

        sink_type_str = sink_str.substr(0, sink_str.find(delimiter));
        sink_str = sink_str.substr(sink_str.find(delimiter) + 1, sink_str.back());
        sink_type.set(ctx, sink_type_str);
        sink.set(ctx, sink_str);

        std::pair<IdString, IdString> source_pair(source_type, source);
        sources.push_back(source_pair);

        std::pair<IdString, IdString> sink_pair(sink_type, sink);
        sinks.push_back(sink_pair);
    }

    // Find roots from chainable cells
    std::vector<CellInfo *> roots;
    for (auto cell : chainable_cells) {
        for (auto pair : sinks) {
            IdString cell_type = pair.first;
            IdString cell_port = pair.second;

            if (cell_type != cell->type || cell->ports.find(cell_port) == cell->ports.end()) {
                continue;
            }

            PortRef driver = cell->ports[cell_port].net->driver;
            if (driver.cell->type == id_GND || driver.cell->type == id_VCC) {
                // We hit a root cell
                roots.push_back(cell);
                break;
            }
        }
    }

    // Generate unique clusters starting from each root
    int cluster_index = 0;
    std::unordered_map<ClusterId, CellInfo *> roots_map;
    for (auto root : roots) {
        CellInfo *next_cell = root;

        // Create unique ClusterId for a root cell
        ClusterId new_cluster_id;
        new_cluster_id.set(ctx, IdString(chain->name).str(ctx) + "_" + std::to_string(cluster_index));

        // Append root cell to cluster_roots
        cluster_roots.emplace(root->name, new_cluster_id);

        std::string cluster_path = "";
        while (next_cell != nullptr) {
            cluster_path += next_cell->name.str(ctx) + " -> ";
            IdString src_cell_type;
            IdString src_cell_port;

            // Find possible source type/port to follow in cluster building
            for (auto source : sources) {
                src_cell_type = source.first;
                src_cell_port = source.second;
                if (src_cell_type != next_cell->type || next_cell->ports.find(src_cell_port) == next_cell->ports.end()) {
                    src_cell_type = IdString();
                    src_cell_port = IdString();
                    continue;
                }
            }
            if (src_cell_type == IdString() || src_cell_port == IdString()) {
                log_error("Chain pattern not found for cell: '%s'\n", next_cell->name.c_str(ctx));
                break;
            }

            next_cell->cluster = new_cluster_id;
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
        log_info("Created cluster: %s with following path:\n  |root| %s", new_cluster_id.c_str(ctx), cluster_path.c_str());
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

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
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

static const MacroPOD *lookup_macro(const ChipInfoPOD *chip, IdString cell_type)
{
    for (const auto &macro : chip->macros) {
        if (IdString(macro.name) == cell_type)
            return &macro;
    }
    return nullptr;
}

static IdString derived_name(Context *ctx, IdString base_name, IdString suffix)
{
    return ctx->id(stringf("%s/%s", base_name.c_str(ctx), suffix.c_str(ctx)));
}

void Arch::expand_macros()
{
    // Make up a list of cells, so we don't have modify-while-iterating issues
    Context *ctx = getCtx();
    std::vector<CellInfo *> cells;
    for (auto cell : sorted(ctx->cells))
        cells.push_back(cell.second);

    std::vector<CellInfo *> next_cells;

    do {
        // Expand cells
        for (auto cell : cells) {
            // TODO: consult exception map
            const MacroPOD *macro = lookup_macro(chip_info, cell->type);
            if (macro == nullptr)
                continue;
            // Create child instances
            for (const auto &inst : macro->cell_insts) {
                CellInfo *inst_cell =
                        ctx->createCell(derived_name(ctx, cell->name, IdString(inst.name)), IdString(inst.type));
                for (const auto &param : inst.parameters) {
                    inst_cell->params[IdString(param.key)] = IdString(param.value).str(ctx);
                }
                next_cells.push_back(inst_cell);
            }
            // Create and connect nets
            for (const auto &net_data : macro->nets) {
                NetInfo *net = nullptr;
                // If there is a top level port, use that as the net
                for (const auto &net_port : net_data.ports) {
                    if (net_port.instance != 0)
                        continue;
                    // TODO: case of multiple top level ports on the same net?
                    NPNR_ASSERT(net == nullptr);
                    // Use the corresponding pre-expansion port net
                    net = get_net_or_empty(cell, IdString(net_port.port));
                    // Disconnect the original port pre-expansion
                    disconnect_port(ctx, cell, IdString(net_port.port));
                }
                // If not on a top level port, create a new net
                if (net == nullptr)
                    net = ctx->createNet(derived_name(ctx, cell->name, IdString(net_data.name)));
                // Create and connect instance ports
                for (const auto &net_port : net_data.ports) {
                    if (net_port.instance == 0)
                        continue;
                    IdString port_name(net_port.port);
                    CellInfo *inst_cell =
                            ctx->cells.at(derived_name(ctx, cell->name, IdString(net_port.instance))).get();
                    inst_cell->ports[port_name].name = port_name;
                    inst_cell->ports[port_name].type = PortType(net_port.dir);
                    connect_port(ctx, net, inst_cell, port_name);
                }
            }
            // TODO: apply parameter rules from exception map
            // Remove the now-expanded cell, but first make sure we don't leave behind any dangling references
            for (const auto &port : cell->ports)
                if (port.second.net != nullptr)
                    log_error("Macro expansion of %s:%s left dangling port %s.", ctx->nameOf(cell),
                              ctx->nameOf(cell->type), ctx->nameOf(port.first));
            ctx->cells.erase(cell->name);
        }

        // Iterate until no more expansions are possible
        // The next iteration only needs to look at cells created in this iteration
        std::swap(next_cells, cells);
        next_cells.clear();
    } while (!cells.empty());
}

NEXTPNR_NAMESPACE_END

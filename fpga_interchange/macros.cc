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

static const MacroExpansionPOD *lookup_macro_rules(const ChipInfoPOD *chip, IdString cell_type)
{
    for (const auto &rule : chip->macro_rules) {
        if (IdString(rule.prim_name) == cell_type)
            return &rule;
    }
    return nullptr;
}

static IdString derived_name(Context *ctx, IdString base_name, IdString suffix)
{
    return ctx->idf("%s/%s", base_name.c_str(ctx), suffix.c_str(ctx));
}

void Arch::expand_macros()
{
    log_info("Expand macros\n");
    // Make up a list of cells, so we don't have modify-while-iterating issues
    Context *ctx = getCtx();
    std::vector<CellInfo *> cells;
    for (auto &cell : ctx->cells)
        cells.push_back(cell.second.get());

    std::vector<CellInfo *> next_cells;

    bool first_iter = false;
    do {
        // Expand cells
        for (auto cell : cells) {
            // TODO: consult exception map
            const MacroExpansionPOD *exp = lookup_macro_rules(chip_info, cell->type);

            // Block infinite expansion loop due to a macro being expanded in the same primitive.
            // E.g.: OBUFTDS expands into the following cells, with an infinite loop being generated:
            //          - 2 OBUFTDS
            //          - 1 INV
            if (exp && first_iter)
                continue;

            const MacroPOD *macro = lookup_macro(chip_info, exp ? IdString(exp->macro_name) : cell->type);
            if (macro == nullptr)
                continue;

            // Get the ultimate root of this macro expansion
            IdString parent = (cell->macro_parent == IdString()) ? cell->name : cell->macro_parent;
            // Create child instances
            for (const auto &inst : macro->cell_insts) {
                CellInfo *inst_cell =
                        ctx->createCell(derived_name(ctx, cell->name, IdString(inst.name)), IdString(inst.type));
                for (const auto &param : inst.parameters) {
                    inst_cell->params[IdString(param.key)] = IdString(param.value).str(ctx);
                }
                inst_cell->macro_parent = parent;
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
                    net = cell->getPort(IdString(net_port.port));
                    // Disconnect the original port pre-expansion
                    cell->disconnectPort(IdString(net_port.port));
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
                    inst_cell->connectPort(port_name, net);
                }
            }

            if (exp != nullptr) {
                // Convert parameters, according to the exception rules
                for (const auto &param_rule : exp->param_rules) {
                    IdString prim_param(param_rule.prim_param);
                    if (!cell->params.count(prim_param))
                        continue;
                    const auto &prim_param_val = cell->params.at(prim_param);
                    IdString inst_name = derived_name(ctx, cell->name, IdString(param_rule.inst_name));
                    CellInfo *inst_cell = ctx->cells.at(inst_name).get();
                    IdString inst_param(param_rule.inst_param);
                    if (param_rule.rule_type == PARAM_MAP_COPY) {
                        inst_cell->params[inst_param] = prim_param_val;
                    } else if (param_rule.rule_type == PARAM_MAP_SLICE) {
                        auto prim_bits = cell_parameters.parse_int_like(ctx, cell->type, prim_param, prim_param_val);
                        Property value(0, param_rule.slice_bits.ssize());
                        for (int i = 0; i < param_rule.slice_bits.ssize(); i++) {
                            size_t bit = param_rule.slice_bits[i];
                            if (bit >= prim_bits.size())
                                continue;
                            value.str.at(i) = prim_bits.get(bit) ? Property::S1 : Property::S0;
                        }
                        inst_cell->params[inst_param] = value;
                    } else if (param_rule.rule_type == PARAM_MAP_TABLE) {
                        const std::string &prim_str = prim_param_val.as_string();
                        IdString prim_id = ctx->id(prim_str);
                        for (auto &tbl_entry : param_rule.map_table) {
                            if (IdString(tbl_entry.key) == prim_id) {
                                inst_cell->params[inst_param] = IdString(tbl_entry.value).str(ctx);
                                break;
                            }
                        }
                        if (!inst_cell->params.count(inst_param))
                            log_error("Unsupported value '%s' for property '%s' of cell %s:%s\n", prim_str.c_str(),
                                      ctx->nameOf(prim_param), ctx->nameOf(cell), ctx->nameOf(cell->type));
                    }
                }
            }

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

        first_iter = true;
    } while (!cells.empty());
    // Do this at the end, otherwise we might add cells that are later destroyed
    for (auto &cell : ctx->cells)
        macro_to_cells[cell.second->macro_parent].push_back(cell.second.get());
}

NEXTPNR_NAMESPACE_END

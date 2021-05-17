/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "cell_transform.h"
#include "design_utils.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
static const pool<std::string> pseudo_diff_iotypes = {
        "BLVDS_25",           "DIFF_HSTL_I",        "DIFF_HSTL_I_12",    "DIFF_HSTL_I_18",  "DIFF_HSTL_I_DCI",
        "DIFF_HSTL_I_DCI_12", "DIFF_HSTL_I_DCI_18", "DIFF_HSTL_II",      "DIFF_HSTL_II_18", "DIFF_HSUL_12",
        "DIFF_HSUL_12_DCI",   "DIFF_MOBILE_DDR",    "DIFF_POD10",        "DIFF_POD10_DCI",  "DIFF_POD12",
        "DIFF_POD12_DCI",     "DIFF_SSTL12",        "DIFF_SSTL12_DCI",   "DIFF_SSTL135",    "DIFF_SSTL135_DCI",
        "DIFF_SSTL135_II",    "DIFF_SSTL135_R",     "DIFF_SSTL15",       "DIFF_SSTL15_DCI", "DIFF_SSTL15_II",
        "DIFF_SSTL15_R",      "DIFF_SSTL18_I",      "DIFF_SSTL18_I_DCI", "DIFF_SSTL18_II",  "MIPI_DPHY_DCI",
};

IdString get_macro_override(const CellInfo *base_cell, IdString inst_type)
{
    if (inst_type == id_OBUFDS || inst_type == id_OBUFTDS) {
        std::string ios = str_or_default(base_cell->attrs, id_IOSTANDARD);
        if (pseudo_diff_iotypes.count(ios))
            return (inst_type == id_OBUFDS) ? id_OBUFDS_DUAL_BUF : id_OBUFTDS_DUAL_BUF;
        else
            return inst_type;
    }
    return inst_type;
}

const MacroPOD *lookup_macro(const ChipInfoPOD *chip_info, IdString macro_name)
{
    for (const auto &macro : chip_info->macros) {
        if (IdString(macro.name) == macro_name)
            return &macro;
    }
    return nullptr;
}

static IdString derived_name(Context *ctx, IdString base_name, IdString suffix)
{
    return ctx->id(stringf("%s/%s", base_name.c_str(ctx), suffix.c_str(ctx)));
}

} // namespace

void Arch::apply_transforms()
{
    dict<IdString, XFormRule> transform_rules;
    // Legacy FF types
    transform_rules[id_FD].new_type = id_FDRE;
    transform_rules[id_FD_1].new_type = id_FDRE;
    transform_rules[id_FD_1].set_params.emplace_back(id_IS_C_INVERTED, 1);
    transform_rules[id_FDC].new_type = id_FDCE;
    transform_rules[id_FDC_1].new_type = id_FDCE;
    transform_rules[id_FDC_1].set_params.emplace_back(id_IS_C_INVERTED, 1);
    transform_rules[id_FDCE_1].new_type = id_FDCE;
    transform_rules[id_FDCE_1].set_params.emplace_back(id_IS_C_INVERTED, 1);
    transform_rules[id_FDE].new_type = id_FDRE;
    transform_rules[id_FDE_1].new_type = id_FDRE;
    transform_rules[id_FDE_1].set_params.emplace_back(id_IS_C_INVERTED, 1);
    transform_rules[id_FDP].new_type = id_FDPE;
    transform_rules[id_FDP_1].new_type = id_FDPE;
    transform_rules[id_FDP_1].set_params.emplace_back(id_IS_C_INVERTED, 1);
    transform_rules[id_FDPE_1].new_type = id_FDPE;
    transform_rules[id_FDPE_1].set_params.emplace_back(id_IS_C_INVERTED, 1);
    transform_rules[id_FDS].new_type = id_FDSE;
    transform_rules[id_FDS_1].new_type = id_FDSE;
    transform_rules[id_FDS_1].set_params.emplace_back(id_IS_C_INVERTED, 1);
    transform_rules[id_FDSE_1].new_type = id_FDSE;
    transform_rules[id_FDSE_1].set_params.emplace_back(id_IS_C_INVERTED, 1);
    // Dangling buffers/inverters that weren't folded into invertible cell pins
    // N.B. this must run *before* macro expansion which can create inverters as part of pseudo-diff outputs that must
    // be left alone
    transform_rules[id_BUF].new_type = id_LUT1;
    transform_rules[id_BUF].port_xform[id_I] = id_I0;
    transform_rules[id_BUF].set_params.emplace_back(id_INIT, 2);
    transform_rules[id_INV].new_type = id_LUT1;
    transform_rules[id_INV].port_xform[id_I] = id_I0;
    transform_rules[id_INV].set_params.emplace_back(id_INIT, 1);

    if (family != ArchFamily::XC7) {
        transform_rules[id_BUFG].new_type = id_BUFGCE;
        // Upgrade PLLs
        transform_rules[id_MMCME2_ADV].new_type = id_MMCME4_ADV;
    }

    // Apply transforms
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (!transform_rules.count(ci->type))
            continue;
        transform_cell(getCtx(), transform_rules, ci);
    }
}

void Arch::expand_macros()
{
    // Make up a list of cells, so we don't have modify-while-iterating issues
    Context *ctx = getCtx();
    std::vector<CellInfo *> cells;
    for (auto &cell : ctx->cells)
        cells.push_back(cell.second.get());

    std::vector<CellInfo *> next_cells;

    do {
        // Expand cells
        for (auto cell : cells) {
            IdString macro_type = get_macro_override(cell, cell->type);
            bool is_override = macro_type != cell->type;

            const MacroPOD *macro = lookup_macro(chip_info, macro_type);
            if (macro == nullptr)
                continue;

            if (cell->macro_parent == IdString()) {
                // Track the expansion for the sake of the logical netlist writer, which skips macro content
                auto &exp = expanded_macros[cell->name];
                exp.type = cell->type;
                for (auto &port : cell->ports) {
                    exp.ports[port.first] = port.second.net ? port.second.net->name : IdString();
                }
                exp.params = cell->params;
                exp.attrs = cell->attrs;
            }

            // Get the ultimate root of this macro expansion
            IdString parent = (cell->macro_parent == IdString()) ? cell->name : cell->macro_parent;
            // Create child instances
            for (const auto &inst : macro->cell_insts) {
                CellInfo *inst_cell =
                        ctx->createCell(derived_name(ctx, cell->name, IdString(inst.name)), IdString(inst.type));
                for (const auto &param : inst.parameters)
                    inst_cell->params[IdString(param.key)] = IdString(param.value).str(ctx);
                for (const auto &attr : cell->attrs)
                    inst_cell->attrs[attr.first] = attr.second;
                inst_cell->macro_parent = parent;
                inst_cell->macro_inst = IdString(inst.name);
                inst_cell->hierpath = cell->hierpath;
                // Prevent infinite recursive expanding pseudo-diff IOBs
                if (!is_override)
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
                if (net == nullptr) {
                    net = ctx->createNet(derived_name(ctx, cell->name, IdString(net_data.name)));
                    net->hierpath = cell->hierpath;
                    net->macro_parent = parent;
                }
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

            // TODO: parameter expansion

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

    for (auto &cell : ctx->cells) {
        if (cell.second->macro_parent != IdString())
            expanded_macros.at(cell.second->macro_parent).expanded_cells.push_back(cell.first);
    }
}

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
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

namespace {
bool is_enabled(CellInfo *ci, IdString prop) { return str_or_default(ci->params, prop, "") == "ENABLED"; }
} // namespace

struct NexusPacker
{
    Context *ctx;

    // Generic cell transformation
    // Given cell name map and port map
    // If port name is not found in port map; it will be copied as-is but stripping []
    struct XFormRule
    {
        IdString new_type;
        std::unordered_map<IdString, IdString> port_xform;
        std::unordered_map<IdString, std::vector<IdString>> port_multixform;
        std::unordered_map<IdString, IdString> param_xform;
        std::vector<std::pair<IdString, std::string>> set_attrs;
        std::vector<std::pair<IdString, Property>> set_params;
        std::vector<std::pair<IdString, Property>> default_params;
    };

    void xform_cell(const std::unordered_map<IdString, XFormRule> &rules, CellInfo *ci)
    {
        auto &rule = rules.at(ci->type);
        ci->type = rule.new_type;
        std::vector<IdString> orig_port_names;
        for (auto &port : ci->ports)
            orig_port_names.push_back(port.first);

        for (auto pname : orig_port_names) {
            if (rule.port_multixform.count(pname)) {
                auto old_port = ci->ports.at(pname);
                disconnect_port(ctx, ci, pname);
                ci->ports.erase(pname);
                for (auto new_name : rule.port_multixform.at(pname)) {
                    ci->ports[new_name].name = new_name;
                    ci->ports[new_name].type = old_port.type;
                    connect_port(ctx, old_port.net, ci, new_name);
                }
            } else {
                IdString new_name;
                if (rule.port_xform.count(pname)) {
                    new_name = rule.port_xform.at(pname);
                } else {
                    std::string stripped_name;
                    for (auto c : pname.str(ctx))
                        if (c != '[' && c != ']')
                            stripped_name += c;
                    new_name = ctx->id(stripped_name);
                }
                if (new_name != pname) {
                    rename_port(ctx, ci, pname, new_name);
                }
            }
        }

        std::vector<IdString> xform_params;
        for (auto &param : ci->params)
            if (rule.param_xform.count(param.first))
                xform_params.push_back(param.first);
        for (auto param : xform_params)
            ci->params[rule.param_xform.at(param)] = ci->params[param];

        for (auto &attr : rule.set_attrs)
            ci->attrs[attr.first] = attr.second;

        for (auto &param : rule.default_params)
            if (!ci->params.count(param.first))
                ci->params[param.first] = param.second;

        for (auto &param : rule.set_params)
            ci->params[param.first] = param.second;
    }

    void generic_xform(const std::unordered_map<IdString, XFormRule> &rules, bool print_summary = false)
    {
        std::map<std::string, int> cell_count;
        std::map<std::string, int> new_types;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (rules.count(ci->type)) {
                cell_count[ci->type.str(ctx)]++;
                xform_cell(rules, ci);
                new_types[ci->type.str(ctx)]++;
            }
        }
        if (print_summary) {
            for (auto &nt : new_types) {
                log_info("    Created %d %s cells from:\n", nt.second, nt.first.c_str());
                for (auto &cc : cell_count) {
                    if (rules.at(ctx->id(cc.first)).new_type != ctx->id(nt.first))
                        continue;
                    log_info("        %6dx %s\n", cc.second, cc.first.c_str());
                }
            }
        }
    }

    void pack_luts()
    {
        log_info("Packing LUTs...\n");
        std::unordered_map<IdString, XFormRule> lut_rules;
        lut_rules[id_LUT4].new_type = id_OXIDE_COMB;
        lut_rules[id_LUT4].port_xform[id_Z] = id_F;
        generic_xform(lut_rules);
    }

    void pack_ffs()
    {
        log_info("Packing FFs...\n");
        std::unordered_map<IdString, XFormRule> ff_rules;
        for (auto type : {id_FD1P3BX, id_FD1P3DX, id_FD1P3IX, id_FD1P3JX}) {
            ff_rules[type].new_type = id_OXIDE_FF;
            ff_rules[type].port_xform[id_CK] = id_CLK;
            ff_rules[type].port_xform[id_D] = id_M; // will be rerouted to DI later if applicable
            ff_rules[type].port_xform[id_SP] = id_CE;
            ff_rules[type].port_xform[id_Q] = id_Q;

            ff_rules[id_FD1P3BX].default_params.emplace_back(id_CLKMUX, std::string("CLK"));
            ff_rules[id_FD1P3BX].default_params.emplace_back(id_CEMUX, std::string("CE"));
            ff_rules[id_FD1P3BX].default_params.emplace_back(id_LSRMUX, std::string("LSR"));
            ff_rules[id_FD1P3BX].set_params.emplace_back(id_LSRMODE, std::string("LSR"));
        }
        // Async preload
        ff_rules[id_FD1P3BX].set_params.emplace_back(id_SRMODE, std::string("ASYNC"));
        ff_rules[id_FD1P3BX].set_params.emplace_back(id_REGSET, std::string("SET"));
        ff_rules[id_FD1P3BX].port_xform[id_PD] = id_LSR;
        // Async clear
        ff_rules[id_FD1P3DX].set_params.emplace_back(id_SRMODE, std::string("ASYNC"));
        ff_rules[id_FD1P3DX].set_params.emplace_back(id_REGSET, std::string("RESET"));
        ff_rules[id_FD1P3DX].port_xform[id_CD] = id_LSR;
        // Sync preload
        ff_rules[id_FD1P3JX].set_params.emplace_back(id_SRMODE, std::string("LSR_OVER_CE"));
        ff_rules[id_FD1P3JX].set_params.emplace_back(id_REGSET, std::string("SET"));
        ff_rules[id_FD1P3JX].port_xform[id_PD] = id_LSR;
        // Sync clear
        ff_rules[id_FD1P3IX].set_params.emplace_back(id_SRMODE, std::string("LSR_OVER_CE"));
        ff_rules[id_FD1P3IX].set_params.emplace_back(id_REGSET, std::string("RESET"));
        ff_rules[id_FD1P3IX].port_xform[id_CD] = id_LSR;

        generic_xform(ff_rules, true);
    }

    explicit NexusPacker(Context *ctx) : ctx(ctx) {}

    void operator()()
    {
        pack_ffs();
        pack_luts();
    }
};

bool Arch::pack()
{
    (NexusPacker(getCtx()))();
    attrs[id("step")] = std::string("pack");
    archInfoToAttributes();
    return true;
}

// -----------------------------------------------------------------------

void Arch::assignArchInfo()
{
    for (auto cell : sorted(cells)) {
        assignCellInfo(cell.second);
    }
}

void Arch::assignCellInfo(CellInfo *cell)
{
    if (cell->type == id_OXIDE_COMB) {
        cell->lutInfo.is_memory = str_or_default(cell->params, id_MODE, "LOGIC") == "DPRAM";
        cell->lutInfo.is_carry = str_or_default(cell->params, id_MODE, "LOGIC") == "CCU2";
        cell->lutInfo.mux2_used = port_used(cell, id_OFX);
        cell->lutInfo.f = get_net_or_empty(cell, id_F);
        cell->lutInfo.ofx = get_net_or_empty(cell, id_OFX);
    } else if (cell->type == id_OXIDE_FF) {
        cell->ffInfo.ctrlset.async = str_or_default(cell->params, id_SRMODE, "LSR_OVER_CE") == "ASYNC";
        cell->ffInfo.ctrlset.regddr_en = is_enabled(cell, id_REGDDR);
        cell->ffInfo.ctrlset.gsr_en = is_enabled(cell, id_GSR);
        cell->ffInfo.ctrlset.clkmux = id(str_or_default(cell->params, id_CLKMUX, "CLK")).index;
        cell->ffInfo.ctrlset.cemux = id(str_or_default(cell->params, id_CEMUX, "CE")).index;
        cell->ffInfo.ctrlset.lsrmux = id(str_or_default(cell->params, id_LSRMUX, "LSR")).index;
        cell->ffInfo.ctrlset.clk = get_net_or_empty(cell, id_CLK);
        cell->ffInfo.ctrlset.ce = get_net_or_empty(cell, id_CE);
        cell->ffInfo.ctrlset.lsr = get_net_or_empty(cell, id_LSR);
        cell->ffInfo.di = get_net_or_empty(cell, id_DI);
        cell->ffInfo.m = get_net_or_empty(cell, id_M);
    } else if (cell->type == ID_RAMW) {
        cell->ffInfo.ctrlset.async = false;
        cell->ffInfo.ctrlset.regddr_en = false;
        cell->ffInfo.ctrlset.gsr_en = false;
        cell->ffInfo.ctrlset.clkmux = id(str_or_default(cell->params, id_CLKMUX, "CLK")).index;
        cell->ffInfo.ctrlset.cemux = ID_CE;
        cell->ffInfo.ctrlset.lsrmux = id(str_or_default(cell->params, id_LSRMUX, "LSR")).index;
        cell->ffInfo.ctrlset.clk = get_net_or_empty(cell, id_CLK);
        cell->ffInfo.ctrlset.ce = nullptr;
        cell->ffInfo.ctrlset.lsr = get_net_or_empty(cell, id_LSR);
        cell->ffInfo.di = nullptr;
        cell->ffInfo.m = nullptr;
    }
}

NEXTPNR_NAMESPACE_END
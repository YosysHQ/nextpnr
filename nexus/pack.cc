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

#include <boost/algorithm/string.hpp>

NEXTPNR_NAMESPACE_BEGIN

namespace {
bool is_enabled(CellInfo *ci, IdString prop) { return str_or_default(ci->params, prop, "") == "ENABLED"; }
} // namespace

// Parse a possibly-Lattice-style (C literal in Verilog string) style parameter
Property Arch::parse_lattice_param(const CellInfo *ci, IdString prop, int width, int64_t defval) const
{
    auto fnd = ci->params.find(prop);
    if (fnd == ci->params.end())
        return Property(defval, width);
    const auto &val = fnd->second;
    if (val.is_string) {
        const std::string &s = val.str;
        Property temp;

        if (boost::starts_with(s, "0b")) {
            for (int i = int(s.length()) - 1; i >= 2; i--) {
                char c = s.at(i);
                if (c != '0' && c != '1' && c != 'x')
                    log_error("Invalid binary digit '%c' in property %s.%s\n", c, nameOf(ci), nameOf(prop));
                temp.str.push_back(c);
            }
        } else if (boost::starts_with(s, "0x")) {
            for (int i = int(s.length()) - 1; i >= 2; i--) {
                char c = s.at(i);
                int nibble;
                if (c >= '0' && c <= '9')
                    nibble = (c - '0');
                else if (c >= 'a' && c <= 'f')
                    nibble = (c - 'a') + 10;
                else if (c >= 'A' && c <= 'F')
                    nibble = (c - 'A') + 10;
                else
                    log_error("Invalid hex digit '%c' in property %s.%s\n", c, nameOf(ci), nameOf(prop));
                for (int j = 0; j < 4; j++)
                    temp.str.push_back(((nibble >> j) & 0x1) ? Property::S1 : Property::S0);
            }
        } else {
            int64_t ival = 0;
            try {
                if (boost::starts_with(s, "0d"))
                    ival = std::stoll(s.substr(2));
                else
                    ival = std::stoll(s);
            } catch (std::runtime_error &e) {
                log_error("Invalid decimal value for property %s.%s", nameOf(ci), nameOf(prop));
            }
            temp = Property(ival);
        }

        for (auto b : temp.str.substr(width)) {
            if (b == Property::S1)
                log_error("Found value for property %s.%s with width greater than %d\n", nameOf(ci), nameOf(prop),
                          width);
        }
        temp.update_intval();
        return temp.extract(0, width);
    } else {
        for (auto b : val.str.substr(width)) {
            if (b == Property::S1)
                log_error("Found bitvector value for property %s.%s with width greater than %d - perhaps a string was "
                          "converted to bits?\n",
                          nameOf(ci), nameOf(prop), width);
        }
        return val.extract(0, width);
    }
}

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
        std::vector<std::tuple<IdString, IdString, int, int64_t>> parse_params;
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

        {
            IdString old_param, new_param;
            int width;
            int64_t def;
            for (const auto &p : rule.parse_params) {
                std::tie(old_param, new_param, width, def) = p;
                ci->params[new_param] = ctx->parse_lattice_param(ci, old_param, width, def);
            }
        }

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
        lut_rules[id_LUT4].parse_params.emplace_back(id_INIT, id_INIT, 16, 0);

        lut_rules[id_INV].new_type = id_OXIDE_COMB;
        lut_rules[id_INV].port_xform[id_Z] = id_F;
        lut_rules[id_INV].port_xform[id_A] = id_A;
        lut_rules[id_INV].set_params.emplace_back(id_INIT, 0x5555);

        lut_rules[id_VHI].new_type = id_OXIDE_COMB;
        lut_rules[id_VHI].port_xform[id_Z] = id_F;
        lut_rules[id_VHI].set_params.emplace_back(id_INIT, 0xFFFF);

        lut_rules[id_VLO].new_type = id_OXIDE_COMB;
        lut_rules[id_VLO].port_xform[id_Z] = id_F;
        lut_rules[id_VLO].set_params.emplace_back(id_INIT, 0x0000);

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

            ff_rules[type].default_params.emplace_back(id_CLKMUX, std::string("CLK"));
            ff_rules[type].default_params.emplace_back(id_CEMUX, std::string("CE"));
            ff_rules[type].default_params.emplace_back(id_LSRMUX, std::string("LSR"));
            ff_rules[type].set_params.emplace_back(id_LSRMODE, std::string("LSR"));
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

    std::unordered_map<IdString, BelId> reference_bels;

    void autocreate_ports(CellInfo *cell)
    {
        // Automatically create ports for all inputs of a cell; even if they were left off the instantiation
        // so we can tie them to constants as appropriate
        // This also checks for any cells that don't have corresponding bels

        if (!reference_bels.count(cell->type)) {
            // We need to look up a corresponding bel to get the list of input ports
            BelId ref_bel;
            for (BelId bel : ctx->getBels()) {
                if (ctx->getBelType(bel) != cell->type)
                    continue;
                ref_bel = bel;
                break;
            }
            if (ref_bel == BelId())
                log_error("Cell type '%s' instantiated as '%s' is not supported by this device.\n",
                          ctx->nameOf(cell->type), ctx->nameOf(cell));
            reference_bels[cell->type] = ref_bel;
        }

        BelId bel = reference_bels.at(cell->type);
        for (IdString pin : ctx->getBelPins(bel)) {
            PortType dir = ctx->getBelPinType(bel, pin);
            if (dir != PORT_IN)
                continue;
            if (cell->ports.count(pin))
                continue;
            cell->ports[pin].name = pin;
            cell->ports[pin].type = dir;
        }
    }

    bool is_port_inverted(CellInfo *cell, IdString port)
    {
        NetInfo *net = get_net_or_empty(cell, port);
        if (net == nullptr || net->driver.cell == nullptr)
            return false;
        return (net->driver.cell->type == id_INV);
    }

    void uninvert_port(CellInfo *cell, IdString port)
    {
        // Rewire a port so it is driven by the input to an inverter
        NetInfo *net = get_net_or_empty(cell, port);
        NPNR_ASSERT(net != nullptr && net->driver.cell != nullptr && net->driver.cell->type == id_INV);
        CellInfo *inv = net->driver.cell;
        disconnect_port(ctx, cell, port);

        NetInfo *inv_a = get_net_or_empty(inv, id_A);
        if (inv_a != nullptr) {
            connect_port(ctx, inv_a, cell, port);
        }
    }

    void trim_design()
    {
        // Remove unused inverters and high/low drivers
        std::vector<IdString> trim_cells;
        std::vector<IdString> trim_nets;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type != id_INV && ci->type != id_VLO && ci->type != id_VHI)
                continue;
            NetInfo *z = get_net_or_empty(ci, id_Z);
            if (z == nullptr) {
                trim_cells.push_back(ci->name);
                continue;
            }
            if (!z->users.empty())
                continue;

            disconnect_port(ctx, ci, id_A);

            trim_cells.push_back(ci->name);
            trim_nets.push_back(z->name);
        }

        for (IdString rem_net : trim_nets)
            ctx->nets.erase(rem_net);
        for (IdString rem_cell : trim_cells)
            ctx->cells.erase(rem_cell);
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
    assignArchInfo();
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

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
#include <queue>

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

    void autocreate_ports(CellInfo *cell, bool include_outputs = false)
    {
        // Automatically create ports for all inputs, and maybe outputs, of a cell; even if they were left off the
        // instantiation so we can tie them to constants as appropriate This also checks for any cells that don't have
        // corresponding bels

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
            if (dir != PORT_IN && !include_outputs)
                continue;
            if (cell->ports.count(pin))
                continue;
            if (cell->type == id_OXIDE_COMB && pin == id_SEL)
                continue; // doesn't always exist and not needed
            cell->ports[pin].name = pin;
            cell->ports[pin].type = dir;
        }
    }

    NetInfo *get_const_net(IdString type)
    {
        // Gets a constant net, given the driver type (VHI or VLO)
        // If one doesn't exist already; then create it
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type != type)
                continue;
            NetInfo *z = get_net_or_empty(ci, id_Z);
            if (z == nullptr)
                continue;
            return z;
        }

        NetInfo *new_net = ctx->createNet(ctx->id(stringf("$CONST_%s_NET_", type.c_str(ctx))));
        CellInfo *new_cell = ctx->createCell(ctx->id(stringf("$CONST_%s_DRV_", type.c_str(ctx))), type);
        new_cell->addOutput(id_Z);
        connect_port(ctx, new_net, new_cell, id_Z);
        return new_net;
    }

    CellPinMux get_pin_needed_muxval(CellInfo *cell, IdString port)
    {
        NetInfo *net = get_net_or_empty(cell, port);
        if (net == nullptr || net->driver.cell == nullptr) {
            // Pin is disconnected
            // If a mux value exists already, honour it
            CellPinMux exist_mux = ctx->get_cell_pinmux(cell, port);
            if (exist_mux != PINMUX_SIG)
                return exist_mux;
            // Otherwise, look up the default value and use that
            CellPinStyle pin_style = ctx->get_cell_pin_style(cell, port);
            if ((pin_style & PINDEF_MASK) == PINDEF_0)
                return PINMUX_0;
            else if ((pin_style & PINDEF_MASK) == PINDEF_1)
                return PINMUX_1;
            else
                return PINMUX_SIG;
        }
        // Look to see if the driver is an inverter or constant
        IdString drv_type = net->driver.cell->type;
        if (drv_type == id_INV)
            return PINMUX_INV;
        else if (drv_type == id_VLO)
            return PINMUX_0;
        else if (drv_type == id_VHI)
            return PINMUX_1;
        else
            return PINMUX_SIG;
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
            if (ci->type != id_INV && ci->type != id_VLO && ci->type != id_VHI && ci->type != id_VCC_DRV)
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

    std::string remove_brackets(const std::string &name)
    {
        std::string new_name;
        new_name.reserve(name.size());
        for (char c : name)
            if (c != '[' && c != ']')
                new_name.push_back(c);
        return new_name;
    }

    void prim_to_core(CellInfo *cell, IdString new_type = {})
    {
        // Convert a primitive to a '_CORE' variant
        if (new_type == IdString())
            new_type = ctx->id(cell->type.str(ctx) + "_CORE");
        cell->type = new_type;
        std::set<IdString> port_names;
        for (auto port : cell->ports)
            port_names.insert(port.first);
        for (IdString port : port_names) {
            IdString new_name = ctx->id(remove_brackets(port.str(ctx)));
            if (new_name != port)
                rename_port(ctx, cell, port, new_name);
        }
    }

    NetInfo *gnd_net = nullptr, *vcc_net = nullptr, *dedi_vcc_net = nullptr;

    void process_inv_constants(CellInfo *cell)
    {
        // Automatically create any extra inputs needed; so we can set them accordingly
        autocreate_ports(cell);

        for (auto &port : cell->ports) {
            // Iterate over all inputs
            if (port.second.type != PORT_IN)
                continue;
            IdString port_name = port.first;

            CellPinMux req_mux = get_pin_needed_muxval(cell, port_name);
            if (req_mux == PINMUX_SIG) {
                // No special setting required, ignore
                continue;
            }

            CellPinStyle pin_style = ctx->get_cell_pin_style(cell, port_name);

            if (req_mux == PINMUX_INV) {
                // Pin is inverted. If there is a hard inverter; then use it
                if (pin_style & PINOPT_INV) {
                    uninvert_port(cell, port_name);
                    ctx->set_cell_pinmux(cell, port_name, PINMUX_INV);
                }
            } else if (req_mux == PINMUX_0 || req_mux == PINMUX_1) {
                // Pin is tied to a constant
                // If there is a hard constant option; use it
                if ((pin_style & int(req_mux)) == req_mux) {

                    if ((cell->type == id_OXIDE_COMB) && (req_mux == PINMUX_1)) {
                        // We need to add a connection to the dedicated Vcc resource that can feed these cell ports
                        disconnect_port(ctx, cell, port_name);
                        connect_port(ctx, dedi_vcc_net, cell, port_name);
                        continue;
                    }

                    disconnect_port(ctx, cell, port_name);
                    ctx->set_cell_pinmux(cell, port_name, req_mux);
                } else if (port.second.net == nullptr) {
                    // If the port is disconnected; and there is no hard constant
                    // then we need to connect it to the relevant soft-constant net
                    connect_port(ctx, (req_mux == PINMUX_1) ? vcc_net : gnd_net, cell, port_name);
                }
            }
        }
    }

    void prepare_io()
    {
        // Find the actual IO buffer corresponding to a port; and copy attributes across to it
        // Note that this relies on Yosys to do IO buffer inference, to match vendor tooling behaviour
        // In all cases the nextpnr-inserted IO buffers are removed as redundant.
        for (auto &port : sorted_ref(ctx->ports)) {
            if (!ctx->cells.count(port.first))
                log_error("Port '%s' doesn't seem to have a corresponding top level IO\n", ctx->nameOf(port.first));
            CellInfo *ci = ctx->cells.at(port.first).get();

            PortRef top_port;
            top_port.cell = nullptr;
            bool is_npnr_iob = false;

            if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
                // Might have an input buffer (IB etc) connected to it
                is_npnr_iob = true;
                NetInfo *o = get_net_or_empty(ci, id_O);
                if (o == nullptr)
                    ;
                else if (o->users.size() > 1)
                    log_error("Top level pin '%s' has multiple input buffers\n", ctx->nameOf(port.first));
                else if (o->users.size() == 1)
                    top_port = o->users.at(0);
            }
            if (ci->type == ctx->id("$nextpnr_obuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
                // Might have an output buffer (OB etc) connected to it
                is_npnr_iob = true;
                NetInfo *i = get_net_or_empty(ci, id_I);
                if (i != nullptr && i->driver.cell != nullptr) {
                    if (top_port.cell != nullptr)
                        log_error("Top level pin '%s' has multiple input/output buffers\n", ctx->nameOf(port.first));
                    top_port = i->driver;
                }
                // Edge case of a bidirectional buffer driving an output pin
                if (i->users.size() > 2) {
                    log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
                } else if (i->users.size() == 2) {
                    if (top_port.cell != nullptr)
                        log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
                    for (auto &usr : i->users) {
                        if (usr.cell->type == ctx->id("$nextpnr_obuf") || usr.cell->type == ctx->id("$nextpnr_iobuf"))
                            continue;
                        top_port = usr;
                        break;
                    }
                }
            }
            if (!is_npnr_iob)
                log_error("Port '%s' doesn't seem to have a corresponding top level IO (internal cell type mismatch)\n",
                          ctx->nameOf(port.first));

            if (top_port.cell == nullptr) {
                log_info("Trimming port '%s' as it is unused.\n", ctx->nameOf(port.first));
            } else {
                // Copy attributes to real IO buffer
                if (ctx->io_attr.count(port.first)) {
                    for (auto &kv : ctx->io_attr.at(port.first)) {
                        top_port.cell->attrs[kv.first] = kv.second;
                    }
                }
                // Make sure that top level net is set correctly
                port.second.net = top_port.cell->ports.at(top_port.port).net;
            }
            // Now remove the nextpnr-inserted buffer
            disconnect_port(ctx, ci, id_I);
            disconnect_port(ctx, ci, id_O);
            ctx->cells.erase(port.first);
        }
    }

    BelId get_bel_attr(const CellInfo *ci)
    {
        if (!ci->attrs.count(id_BEL))
            return BelId();
        return ctx->getBelByName(ctx->id(ci->attrs.at(id_BEL).as_string()));
    }

    void pack_io()
    {
        std::unordered_set<IdString> iob_types = {id_IB,          id_OB,          id_OBZ,          id_BB,
                                                  id_BB_I3C_A,    id_SEIO33,      id_SEIO18,       id_DIFFIO18,
                                                  id_SEIO33_CORE, id_SEIO18_CORE, id_DIFFIO18_CORE};

        std::unordered_map<IdString, XFormRule> io_rules;

        // For the low level primitives, make sure we always preserve their type
        io_rules[id_SEIO33_CORE].new_type = id_SEIO33_CORE;
        io_rules[id_SEIO18_CORE].new_type = id_SEIO18_CORE;
        io_rules[id_DIFFIO18_CORE].new_type = id_DIFFIO18_CORE;

        // Some IO buffer types need a bit of pin renaming, too
        io_rules[id_SEIO33].new_type = id_SEIO33_CORE;
        io_rules[id_SEIO33].port_xform[id_PADDI] = id_O;
        io_rules[id_SEIO33].port_xform[id_PADDO] = id_I;
        io_rules[id_SEIO33].port_xform[id_PADDT] = id_T;
        io_rules[id_SEIO33].port_xform[id_IOPAD] = id_B;

        io_rules[id_BB_I3C_A] = io_rules[id_SEIO33];

        io_rules[id_SEIO18] = io_rules[id_SEIO33];
        io_rules[id_SEIO18].new_type = id_SEIO18_CORE;

        io_rules[id_DIFFIO18] = io_rules[id_SEIO33];
        io_rules[id_DIFFIO18].new_type = id_DIFFIO18_CORE;

        // Stage 0: deal with top level inserted IO buffers
        prepare_io();

        // Stage 1: setup constraints
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            // Iterate through all IO buffer primitives
            if (!iob_types.count(ci->type))
                continue;
            // We need all IO constrained so we can pick the right IO bel type
            // An improvement would be to allocate unconstrained IO here
            if (!ci->attrs.count(id_LOC))
                log_error("Found unconstrained IO '%s', these are currently unsupported\n", ctx->nameOf(ci));
            // Convert package pin constraint to bel constraint
            std::string loc = ci->attrs.at(id_LOC).as_string();
            auto pad_info = ctx->get_pkg_pin_data(loc);
            if (pad_info == nullptr)
                log_error("IO '%s' is constrained to invalid pin '%s'\n", ctx->nameOf(ci), loc.c_str());
            auto func = ctx->get_pad_functions(pad_info);
            BelId bel = ctx->get_pad_pio_bel(pad_info);

            if (bel == BelId()) {
                log_error("IO '%s' is constrained to pin %s (%s) which is not a general purpose IO pin.\n",
                          ctx->nameOf(ci), loc.c_str(), func.c_str());
            } else {

                // Get IO type for reporting purposes
                std::string io_type = str_or_default(ci->attrs, id_IO_TYPE, "LVCMOS33");

                if (ctx->is_io_type_diff(io_type)) {
                    // Convert from SEIO18 to DIFFIO18
                    if (ctx->getBelType(bel) != id_SEIO18_CORE)
                        log_error("IO '%s' uses differential type '%s' but is placed on wide range pin '%s'\n",
                                  ctx->nameOf(ci), io_type.c_str(), loc.c_str());
                    Loc bel_loc = ctx->getBelLocation(bel);
                    if (bel_loc.z != 0)
                        log_error("IO '%s' uses differential type '%s' but is placed on 'B' side pin '%s'\n",
                                  ctx->nameOf(ci), io_type.c_str(), loc.c_str());
                    bel_loc.z = 2;
                    bel = ctx->getBelByLocation(bel_loc);
                }

                log_info("Constraining %s IO '%s' to pin %s (%s%sbel %s)\n", io_type.c_str(), ctx->nameOf(ci),
                         loc.c_str(), func.c_str(), func.empty() ? "" : "; ", ctx->nameOfBel(bel));
                ci->attrs[id_BEL] = ctx->getBelName(bel).str(ctx);
            }
        }
        // Stage 2: apply rules for primitives that need them
        generic_xform(io_rules, false);
        // Stage 3: all other IO primitives become their bel type
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            // Iterate through all IO buffer primitives
            if (!iob_types.count(ci->type))
                continue;
            // Skip those dealt with in stage 2
            if (io_rules.count(ci->type))
                continue;
            // For non-bidirectional IO, we also need to configure tristate and rename B
            if (ci->type == id_IB) {
                ctx->set_cell_pinmux(ci, id_T, PINMUX_1);
                rename_port(ctx, ci, id_I, id_B);
            } else if (ci->type == id_OB) {
                ctx->set_cell_pinmux(ci, id_T, PINMUX_0);
                rename_port(ctx, ci, id_O, id_B);
            } else if (ci->type == id_OBZ) {
                ctx->set_cell_pinmux(ci, id_T, PINMUX_SIG);
                rename_port(ctx, ci, id_O, id_B);
            }
            // Get the IO bel
            BelId bel = get_bel_attr(ci);
            // Set the cell type to the bel type
            IdString type = ctx->getBelType(bel);
            NPNR_ASSERT(type != IdString());
            ci->type = type;
        }
    }

    void pack_constants()
    {
        // Make sure we have high and low nets available
        vcc_net = get_const_net(id_VHI);
        gnd_net = get_const_net(id_VLO);
        dedi_vcc_net = get_const_net(id_VCC_DRV);
        // Iterate through cells
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            // Skip certain cells at this point
            if (ci->type != id_LUT4 && ci->type != id_INV && ci->type != id_VHI && ci->type != id_VLO &&
                ci->type != id_VCC_DRV)
                process_inv_constants(cell.second);
        }
        // Remove superfluous inverters and constant drivers
        trim_design();
    }

    // Using a BFS, search for bels of a given type either upstream or downstream of another cell
    void find_connected_bels(const CellInfo *cell, IdString port, IdString dest_type, IdString dest_pin, int iter_limit,
                             std::vector<BelId> &candidates)
    {
        int iter = 0;
        std::queue<WireId> visit;
        std::unordered_set<WireId> seen_wires;
        std::unordered_set<BelId> seen_bels;

        BelId bel = get_bel_attr(cell);
        NPNR_ASSERT(bel != BelId());
        WireId start_wire = ctx->getBelPinWire(bel, port);
        NPNR_ASSERT(start_wire != WireId());
        PortType dir = ctx->getBelPinType(bel, port);

        visit.push(start_wire);

        while (!visit.empty() && (iter++ < iter_limit)) {
            WireId cursor = visit.front();
            visit.pop();
            // Check to see if we have reached a valid bel pin
            for (auto bp : ctx->getWireBelPins(cursor)) {
                if (ctx->getBelType(bp.bel) != dest_type)
                    continue;
                if (dest_pin != IdString() && bp.pin != dest_pin)
                    continue;
                if (seen_bels.count(bp.bel))
                    continue;
                seen_bels.insert(bp.bel);
                candidates.push_back(bp.bel);
            }
            // Search in the appropriate direction up/downstream of the cursor
            if (dir == PORT_OUT) {
                for (PipId p : ctx->getPipsDownhill(cursor))
                    if (ctx->checkPipAvail(p)) {
                        WireId dst = ctx->getPipDstWire(p);
                        if (seen_wires.count(dst))
                            continue;
                        seen_wires.insert(dst);
                        visit.push(dst);
                    }
            } else {
                for (PipId p : ctx->getPipsUphill(cursor))
                    if (ctx->checkPipAvail(p)) {
                        WireId src = ctx->getPipSrcWire(p);
                        if (seen_wires.count(src))
                            continue;
                        seen_wires.insert(src);
                        visit.push(src);
                    }
            }
        }
    }

    // Find the nearest bel of a given type; matching a closure predicate
    template <typename Tpred> BelId find_nearest_bel(const CellInfo *cell, IdString dest_type, Tpred predicate)
    {
        BelId origin = get_bel_attr(cell);
        if (origin == BelId())
            return BelId();
        Loc origin_loc = ctx->getBelLocation(origin);
        int best_distance = std::numeric_limits<int>::max();
        BelId best_bel = BelId();

        for (BelId bel : ctx->getBels()) {
            if (ctx->getBelType(bel) != dest_type)
                continue;
            if (!predicate(bel))
                continue;
            Loc bel_loc = ctx->getBelLocation(bel);
            int dist = std::abs(origin_loc.x - bel_loc.x) + std::abs(origin_loc.y - bel_loc.y);
            if (dist < best_distance) {
                best_distance = dist;
                best_bel = bel;
            }
        }
        return best_bel;
    }

    std::unordered_set<BelId> used_bels;

    // Pre-place a primitive based on routeability first and distance second
    bool preplace_prim(CellInfo *cell, IdString pin, bool strict_routing)
    {
        std::vector<BelId> routeability_candidates;

        if (cell->attrs.count(id_BEL))
            return false;

        NetInfo *pin_net = get_net_or_empty(cell, pin);
        if (pin_net == nullptr)
            return false;

        CellInfo *pin_drv = pin_net->driver.cell;
        if (pin_drv == nullptr)
            return false;

        // Check based on routeability
        find_connected_bels(pin_drv, pin_net->driver.port, cell->type, pin, 25000, routeability_candidates);

        for (BelId cand : routeability_candidates) {
            if (used_bels.count(cand))
                continue;
            log_info("    constraining %s '%s' to bel '%s' based on dedicated routing\n", ctx->nameOf(cell),
                     ctx->nameOf(cell->type), ctx->nameOfBel(cand));
            cell->attrs[id_BEL] = ctx->getBelName(cand).str(ctx);
            used_bels.insert(cand);
            return true;
        }

        // Unless in strict mode; check based on simple distance too
        BelId nearest = find_nearest_bel(pin_drv, cell->type, [&](BelId bel) { return !used_bels.count(bel); });

        if (nearest != BelId()) {
            log_info("    constraining %s '%s' to bel '%s'\n", ctx->nameOf(cell), ctx->nameOf(cell->type),
                     ctx->nameOfBel(nearest));
            cell->attrs[id_BEL] = ctx->getBelName(nearest).str(ctx);
            used_bels.insert(nearest);
            return true;
        }

        return false;
    }

    // Pre-place a singleton primitive; so decisions can be made on routeability downstream of it
    bool preplace_singleton(CellInfo *cell)
    {
        if (cell->attrs.count(id_BEL))
            return false;
        bool did_something = false;
        for (BelId bel : ctx->getBels()) {
            if (ctx->getBelType(bel) != cell->type)
                continue;
            // Check that the bel really is a singleton...
            NPNR_ASSERT(!cell->attrs.count(id_BEL));
            cell->attrs[id_BEL] = ctx->getBelName(bel).str(ctx);
            log_info("    constraining %s '%s' to bel '%s'\n", ctx->nameOf(cell), ctx->nameOf(cell->type),
                     ctx->nameOfBel(bel));
            did_something = true;
        }
        return did_something;
    }

    // Insert a buffer primitive in a signal; moving all users that match a predicate behind it
    template <typename Tpred>
    CellInfo *insert_buffer(NetInfo *net, IdString buffer_type, std::string name_postfix, IdString i, IdString o,
                            Tpred pred)
    {
        // Create the buffered net
        NetInfo *buffered_net = ctx->createNet(ctx->id(stringf("%s$%s", ctx->nameOf(net), name_postfix.c_str())));
        // Create the buffer cell
        CellInfo *buffer = ctx->createCell(
                ctx->id(stringf("%s$drv_%s", ctx->nameOf(buffered_net), ctx->nameOf(buffer_type))), buffer_type);
        buffer->addInput(i);
        buffer->addOutput(o);
        // Drive the buffered net with the buffer
        connect_port(ctx, buffered_net, buffer, o);
        // Filter users
        std::vector<PortRef> remaining_users;

        for (auto &usr : net->users) {
            if (pred(usr)) {
                usr.cell->ports[usr.port].net = buffered_net;
                buffered_net->users.push_back(usr);
            } else {
                remaining_users.push_back(usr);
            }
        }

        std::swap(net->users, remaining_users);

        // Connect buffer input to original net
        connect_port(ctx, net, buffer, i);

        return buffer;
    }

    // Insert global buffers
    void promote_globals()
    {
        std::vector<std::pair<int, IdString>> clk_fanout;
        int available_globals = 16;
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            // Skip undriven nets; and nets that are already global
            if (ni->driver.cell == nullptr)
                continue;
            if (ni->driver.cell->type == id_DCC) {
                --available_globals;
                continue;
            }
            // Count the number of clock ports
            int clk_count = 0;
            for (const auto &usr : ni->users) {
                auto port_style = ctx->get_cell_pin_style(usr.cell, usr.port);
                if (port_style & PINGLB_CLK)
                    ++clk_count;
            }
            if (clk_count > 0)
                clk_fanout.emplace_back(clk_count, ni->name);
        }
        if (available_globals <= 0)
            return;
        // Sort clocks by max fanout
        std::sort(clk_fanout.begin(), clk_fanout.end(), std::greater<std::pair<int, IdString>>());
        log_info("Promoting globals...\n");
        // Promote the N highest fanout clocks
        for (size_t i = 0; i < std::min<size_t>(clk_fanout.size(), available_globals); i++) {
            NetInfo *net = ctx->nets.at(clk_fanout.at(i).second).get();
            log_info("     promoting clock net '%s'\n", ctx->nameOf(net));
            insert_buffer(net, id_DCC, "glb_clk", id_CLKI, id_CLKO, [](const PortRef &port) { return true; });
        }
    }

    // Place certain global cells
    void place_globals()
    {
        // Keep running until we reach a fixed point
        log_info("Placing globals...\n");
        bool did_something = true;
        while (did_something) {
            did_something = false;
            for (auto cell : sorted(ctx->cells)) {
                CellInfo *ci = cell.second;
                if (ci->type == id_OSC_CORE)
                    did_something |= preplace_singleton(ci);
                else if (ci->type == id_DCC)
                    did_something |= preplace_prim(ci, id_CLKI, false);
            }
        }
    }

    // Get a bus port name
    IdString bus(const std::string &base, int i) { return ctx->id(stringf("%s[%d]", base.c_str(), i)); }

    IdString bus_flat(const std::string &base, int i) { return ctx->id(stringf("%s%d", base.c_str(), i)); }

    // Pack a LUTRAM into COMB and RAMW cells
    void pack_lutram()
    {
        // Do this so we don't have an iterate-and-modfiy situation
        std::vector<CellInfo *> lutrams;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type != id_DPR16X4)
                continue;
            lutrams.push_back(ci);
        }

        // Port permutation vectors
        IdString ramw_wdo[4] = {id_D1, id_C1, id_A1, id_B1};
        IdString ramw_wado[4] = {id_D0, id_B0, id_C0, id_A0};
        IdString comb0_rad[4] = {id_D, id_B, id_C, id_A};
        IdString comb1_rad[4] = {id_C, id_B, id_D, id_A};

        for (CellInfo *ci : lutrams) {
            // Create constituent cells
            CellInfo *ramw = ctx->createCell(ctx->id(stringf("%s$lutram_ramw$", ctx->nameOf(ci))), id_RAMW);
            std::vector<CellInfo *> combs;
            for (int i = 0; i < 4; i++)
                combs.push_back(
                        ctx->createCell(ctx->id(stringf("%s$lutram_comb[%d]$", ctx->nameOf(ci), i)), id_OXIDE_COMB));
            // Rewiring - external WCK and WRE
            replace_port(ci, id_WCK, ramw, id_CLK);
            replace_port(ci, id_WRE, ramw, id_LSR);

            // Internal WCK and WRE signals
            ramw->addOutput(id_WCKO);
            ramw->addOutput(id_WREO);
            NetInfo *int_wck = ctx->createNet(ctx->id(stringf("%s$lutram_wck$", ctx->nameOf(ci))));
            NetInfo *int_wre = ctx->createNet(ctx->id(stringf("%s$lutram_wre$", ctx->nameOf(ci))));
            connect_port(ctx, int_wck, ramw, id_WCKO);
            connect_port(ctx, int_wre, ramw, id_WREO);

            uint64_t initval = ctx->parse_lattice_param(ci, id_INITVAL, 64, 0).as_int64();

            // Rewiring - buses
            for (int i = 0; i < 4; i++) {
                // Write address - external
                replace_port(ci, bus("WAD", i), ramw, ramw_wado[i]);
                // Write data - external
                replace_port(ci, bus("DI", i), ramw, ramw_wdo[i]);
                // Read data
                replace_port(ci, bus("DO", i), combs[i], id_F);
                // Read address
                NetInfo *rad = get_net_or_empty(ci, bus("RAD", i));
                if (rad != nullptr) {
                    for (int j = 0; j < 4; j++) {
                        IdString port = (j % 2) ? comb1_rad[i] : comb0_rad[i];
                        combs[j]->addInput(port);
                        connect_port(ctx, rad, combs[j], port);
                    }
                    disconnect_port(ctx, ci, bus("RAD", i));
                }
                // Write address - internal
                NetInfo *int_wad = ctx->createNet(ctx->id(stringf("%s$lutram_wad[%d]$", ctx->nameOf(ci), i)));
                ramw->addOutput(bus_flat("WADO", i));
                connect_port(ctx, int_wad, ramw, bus_flat("WADO", i));
                for (int j = 0; j < 4; j++) {
                    combs[j]->addInput(bus_flat("WAD", i));
                    connect_port(ctx, int_wad, combs[j], bus_flat("WAD", i));
                }
                // Write data - internal
                NetInfo *int_wd = ctx->createNet(ctx->id(stringf("%s$lutram_wd[%d]$", ctx->nameOf(ci), i)));
                ramw->addOutput(bus_flat("WDO", i));
                connect_port(ctx, int_wd, ramw, bus_flat("WDO", i));
                combs[i]->addInput(id_WDI);
                connect_port(ctx, int_wd, combs[i], id_WDI);
                // Write clock and enable - internal
                combs[i]->addInput(id_WCK);
                combs[i]->addInput(id_WRE);
                connect_port(ctx, int_wck, combs[i], id_WCK);
                connect_port(ctx, int_wre, combs[i], id_WRE);
                // Remap init val
                uint64_t split_init = 0;
                for (int j = 0; j < 16; j++)
                    if (initval & (1ULL << (4 * j + i)))
                        split_init |= (1 << j);
                combs[i]->params[id_INIT] = Property(split_init, 16);

                combs[i]->params[id_MODE] = std::string("DPRAM");
            }

            // Setup relative constraints
            combs[0]->constr_z = 0;
            combs[0]->constr_abs_z = true;
            for (int i = 1; i < 4; i++) {
                combs[i]->constr_x = 0;
                combs[i]->constr_y = 0;
                combs[i]->constr_z = ((i / 2) << 3) | (i % 2);
                combs[i]->constr_abs_z = true;
                combs[i]->constr_parent = combs[0];
                combs[0]->constr_children.push_back(combs[i]);
            }

            ramw->constr_x = 0;
            ramw->constr_y = 0;
            ramw->constr_z = (2 << 3) | Arch::BEL_RAMW;
            ramw->constr_abs_z = true;
            ramw->constr_parent = combs[0];
            combs[0]->constr_children.push_back(ramw);
            // Remove now-packed cell
            ctx->cells.erase(ci->name);
        }
    }

    void convert_prims()
    {
        // Convert primitives from their non-CORE variant to their CORE variant
        static const std::unordered_map<IdString, IdString> prim_map = {
                {id_OSCA, id_OSC_CORE},          {id_DP16K, id_DP16K_MODE}, {id_PDP16K, id_PDP16K_MODE},
                {id_PDPSC16K, id_PDPSC16K_MODE}, {id_SP16K, id_SP16K_MODE}, {id_FIFO16K, id_FIFO16K_MODE},
        };

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (!prim_map.count(ci->type))
                continue;
            prim_to_core(ci, prim_map.at(ci->type));
        }
    }

    void add_bus_xform(XFormRule &rule, const std::string &o, const std::string &n, int width, int old_offset = 0,
                       int new_offset = 0)
    {
        for (int i = 0; i < width; i++)
            rule.port_xform[bus_flat(o, i + old_offset)] = bus_flat(n, i + new_offset);
    }

    void pack_bram()
    {
        std::unordered_map<IdString, XFormRule> bram_rules;
        bram_rules[id_DP16K_MODE].new_type = id_OXIDE_EBR;
        bram_rules[id_DP16K_MODE].set_params.emplace_back(id_MODE, std::string("DP16K"));
        bram_rules[id_DP16K_MODE].parse_params.emplace_back(id_CSDECODE_A, id_CSDECODE_A, 3, 7);
        bram_rules[id_DP16K_MODE].parse_params.emplace_back(id_CSDECODE_B, id_CSDECODE_B, 3, 7);
        // Pseudo dual port
        bram_rules[id_PDP16K_MODE].new_type = id_OXIDE_EBR;
        bram_rules[id_PDP16K_MODE].set_params.emplace_back(id_MODE, std::string("PDP16K"));
        bram_rules[id_PDP16K_MODE].set_params.emplace_back(id_WEAMUX, std::string("1"));
        bram_rules[id_PDP16K_MODE].parse_params.emplace_back(id_CSDECODE_R, id_CSDECODE_R, 3, 7);
        bram_rules[id_PDP16K_MODE].parse_params.emplace_back(id_CSDECODE_W, id_CSDECODE_W, 3, 7);
        bram_rules[id_PDP16K_MODE].port_xform[id_CLKW] = id_CLKA;
        bram_rules[id_PDP16K_MODE].port_xform[id_CLKR] = id_CLKB;
        bram_rules[id_PDP16K_MODE].port_xform[id_CEW] = id_CEA;
        bram_rules[id_PDP16K_MODE].port_xform[id_CER] = id_CEB;
        bram_rules[id_PDP16K_MODE].port_multixform[id_RST] = {id_RSTA, id_RSTB};
        add_bus_xform(bram_rules[id_PDP16K_MODE], "ADW", "ADA", 14);
        add_bus_xform(bram_rules[id_PDP16K_MODE], "ADR", "ADB", 14);
        add_bus_xform(bram_rules[id_PDP16K_MODE], "CSW", "CSA", 3);
        add_bus_xform(bram_rules[id_PDP16K_MODE], "CSR", "CSB", 3);
        add_bus_xform(bram_rules[id_PDP16K_MODE], "DI", "DIA", 18, 0, 0);
        add_bus_xform(bram_rules[id_PDP16K_MODE], "DI", "DIB", 18, 18, 0);
        add_bus_xform(bram_rules[id_PDP16K_MODE], "DO", "DOB", 18, 0, 0);
        add_bus_xform(bram_rules[id_PDP16K_MODE], "DO", "DOA", 18, 18, 0);

        // Pseudo dual port; single clock
        bram_rules[id_PDPSC16K_MODE] = bram_rules[id_PDP16K_MODE];
        bram_rules[id_PDPSC16K_MODE].set_params.clear();
        bram_rules[id_PDPSC16K_MODE].set_params.emplace_back(id_MODE, std::string("PDPSC16K"));
        bram_rules[id_PDPSC16K_MODE].set_params.emplace_back(id_WEAMUX, std::string("1"));
        bram_rules[id_PDPSC16K_MODE].port_multixform[id_CLK] = {id_CLKA, id_CLKB};

        log_info("Packing BRAM...\n");
        generic_xform(bram_rules, true);

        int wid = 2;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type != id_OXIDE_EBR)
                continue;
            if (ci->params.count(id_WID))
                continue;
            ci->params[id_WID] = wid++;
        }
    }

    void pack_widefn()
    {
        std::vector<CellInfo *> widefns;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type != id_WIDEFN9)
                continue;
            widefns.push_back(ci);
        }

        for (CellInfo *ci : widefns) {
            std::vector<CellInfo *> combs;
            for (int i = 0; i < 2; i++)
                combs.push_back(
                        ctx->createCell(ctx->id(stringf("%s$widefn_comb[%d]$", ctx->nameOf(ci), i)), id_OXIDE_COMB));

            for (int i = 0; i < 2; i++) {
                replace_port(ci, bus_flat("A", i), combs[i], id_A);
                replace_port(ci, bus_flat("B", i), combs[i], id_B);
                replace_port(ci, bus_flat("C", i), combs[i], id_C);
                replace_port(ci, bus_flat("D", i), combs[i], id_D);
            }

            replace_port(ci, id_SEL, combs[0], id_SEL);
            replace_port(ci, id_Z, combs[0], id_OFX);

            NetInfo *f1 = ctx->createNet(ctx->id(stringf("%s$widefn_f1$", ctx->nameOf(ci))));
            combs[0]->addInput(id_F1);
            combs[1]->addOutput(id_F);
            connect_port(ctx, f1, combs[1], id_F);
            connect_port(ctx, f1, combs[0], id_F1);

            combs[0]->params[id_INIT] = ctx->parse_lattice_param(ci, id_INIT0, 16, 0);
            combs[1]->params[id_INIT] = ctx->parse_lattice_param(ci, id_INIT1, 16, 0);

            combs[1]->constr_parent = combs[0];
            combs[1]->constr_x = 0;
            combs[1]->constr_y = 0;
            combs[1]->constr_z = 1;
            combs[1]->constr_abs_z = false;
            combs[0]->constr_children.push_back(combs[1]);

            ctx->cells.erase(ci->name);
        }
    }

    void pack_carries()
    {
        // Find root carry cells
        log_info("Packing carries...\n");
        std::vector<CellInfo *> roots;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type != id_CCU2)
                continue;
            if (get_net_or_empty(ci, id_CIN) != nullptr)
                continue;
            roots.push_back(ci);
        }
        for (CellInfo *root : roots) {
            CellInfo *ci = root;
            CellInfo *constr_base = nullptr;
            int idx = 0;
            do {
                if (ci->type != id_CCU2)
                    log_error("Found non-carry cell '%s' in carry chain!\n", ctx->nameOf(ci));
                // Split the carry into two COMB cells
                std::vector<CellInfo *> combs;
                for (int i = 0; i < 2; i++)
                    combs.push_back(
                            ctx->createCell(ctx->id(stringf("%s$ccu2_comb[%d]$", ctx->nameOf(ci), i)), id_OXIDE_COMB));
                // Rewire LUT ports
                for (int i = 0; i < 2; i++) {
                    combs[i]->params[id_MODE] = std::string("CCU2");
                    replace_port(ci, bus_flat("A", i), combs[i], id_A);
                    replace_port(ci, bus_flat("B", i), combs[i], id_B);
                    replace_port(ci, bus_flat("C", i), combs[i], id_C);
                    replace_port(ci, bus_flat("D", i), combs[i], id_D);
                    replace_port(ci, bus_flat("S", i), combs[i], id_F);
                }

                // External carry chain
                replace_port(ci, id_CIN, combs[0], id_FCI);
                replace_port(ci, id_COUT, combs[1], id_FCO);

                // Copy parameters
                if (ci->params.count(id_INJECT))
                    combs[0]->params[id_INJECT] = ci->params[id_INJECT];
                combs[0]->params[id_INIT] = ctx->parse_lattice_param(ci, id_INIT0, 16, 0);
                combs[1]->params[id_INIT] = ctx->parse_lattice_param(ci, id_INIT1, 16, 0);

                // Internal carry net between the two split COMB cells
                NetInfo *int_cy = ctx->createNet(ctx->id(stringf("%s$widefn_int_cy$", ctx->nameOf(ci))));
                combs[0]->addOutput(id_FCO);
                combs[1]->addInput(id_FCI);
                connect_port(ctx, int_cy, combs[0], id_FCO);
                connect_port(ctx, int_cy, combs[1], id_FCI);

                // Relative constraints
                for (int i = 0; i < 2; i++) {
                    int z = (idx % 8);
                    combs[i]->constr_z = ((z / 2) << 3) | (z % 2);
                    combs[i]->constr_abs_z = true;
                    if (constr_base == nullptr) {
                        // This is the very first cell in the chain
                        constr_base = combs[i];
                    } else {
                        combs[i]->constr_x = (idx / 8);
                        combs[i]->constr_y = 0;
                        combs[i]->constr_parent = constr_base;
                        constr_base->constr_children.push_back(combs[i]);
                    }

                    ++idx;
                }

                ctx->cells.erase(ci->name);

                // Find next cell in chain, if it exists
                NetInfo *fco = get_net_or_empty(combs[1], id_FCO);
                ci = nullptr;
                if (fco != nullptr) {
                    if (fco->users.size() > 1)
                        log_error("Carry cell '%s' has multiple fanout on FCO\n", ctx->nameOf(combs[1]));
                    else if (fco->users.size() == 1) {
                        NPNR_ASSERT(fco->users.at(0).port == id_CIN);
                        ci = fco->users.at(0).cell;
                    }
                }
            } while (ci != nullptr);
        }
    }

    // Function to check if a wire is general routing; and therefore skipped for cascade purposes
    bool is_general_routing(WireId wire)
    {
        std::string name = ctx->nameOf(ctx->wire_data(wire).name);
        if (name.size() == 3 && (name.substr(0, 2) == "JF" || name.substr(0, 2) == "JQ"))
            return true;
        if (name.size() == 12 && (name.substr(0, 10) == "JCIBMUXOUT"))
            return true;
        return false;
    }

    // Automatically generate cascade connections downstream of a cell
    // using the temporary placement that we use solely to access the routing graph
    void auto_cascade_cell(CellInfo *cell, BelId bel, const std::unordered_map<BelId, CellInfo *> &bel2cell)
    {
        for (auto port : sorted_ref(cell->ports)) {
            // Skip if not an output, or being used already for something else
            if (port.second.type != PORT_OUT || port.second.net != nullptr)
                continue;
            // Get the corresponding start wire
            WireId start_wire = ctx->getBelPinWire(bel, port.first);

            // Skip if the start wire doesn't actually exist
            if (start_wire == WireId())
                continue;

            if (ctx->debug)
                log_info("     searching cascade routing for wire %s:\n", ctx->nameOfWire(start_wire));

            // Standard BFS-type exploration
            std::queue<WireId> visit;
            std::unordered_set<WireId> in_queue;
            visit.push(start_wire);
            in_queue.insert(start_wire);
            int iter = 0;
            const int iter_limit = 1000;

            while (!visit.empty() && (iter++ < iter_limit)) {
                WireId cursor = visit.front();
                visit.pop();

                if (ctx->debug)
                    log_info("         visit '%s'\n", ctx->nameOfWire(cursor));

                // Check for downstream bel pins
                bool found_active_pins = false;
                for (auto bp : ctx->getWireBelPins(cursor)) {
                    auto fnd_cell = bel2cell.find(bp.bel);
                    // Always skip unused bels, and don't set found_active_pins
                    // so we can route through these if needed
                    if (fnd_cell == bel2cell.end())
                        continue;
                    // Skip outputs
                    if (ctx->getBelPinType(bp.bel, bp.pin) != PORT_IN)
                        continue;

                    if (ctx->debug)
                        log_info("             bel %s pin %s\n", ctx->nameOfBel(bp.bel), ctx->nameOf(bp.pin));

                    found_active_pins = true;
                    CellInfo *other_cell = fnd_cell->second;

                    if (other_cell == cell)
                        continue;

                    // Skip pins that are already in use
                    if (get_net_or_empty(other_cell, bp.pin) != nullptr)
                        continue;
                    // Create the input if it doesn't exist
                    if (!other_cell->ports.count(bp.pin))
                        other_cell->addInput(bp.pin);
                    // Make the connection
                    connect_ports(ctx, cell, port.first, other_cell, bp.pin);

                    if (ctx->debug)
                        log_info("         found %s.%s\n", ctx->nameOf(other_cell), ctx->nameOf(bp.pin));
                }

                // By doing this we never attempt to route-through bels
                // that are actually in use
                if (found_active_pins)
                    continue;

                // Search downstream pips for wires to add to the queue
                for (auto pip : ctx->getPipsDownhill(cursor)) {
                    WireId dst = ctx->getPipDstWire(pip);
                    // Ignore general routing, as that isn't a useful cascade path
                    if (is_general_routing(dst))
                        continue;
                    if (in_queue.count(dst))
                        continue;
                    in_queue.insert(dst);
                    visit.push(dst);
                }
            }
        }
    }

    // Insert all the cascade connections for a group of cells given the root
    void auto_cascade_group(CellInfo *root)
    {

        auto get_child_loc = [&](Loc base, const CellInfo *sub) {
            Loc l = base;
            l.x += sub->constr_x;
            l.y += sub->constr_y;
            l.z = sub->constr_abs_z ? sub->constr_z : (sub->constr_z + base.z);
            return l;
        };

        // We first create a temporary placement so we can access the routing graph
        bool found = false;
        std::unordered_map<BelId, CellInfo *> bel2cell;
        std::unordered_map<IdString, BelId> cell2bel;

        for (BelId root_bel : ctx->getBels()) {
            if (ctx->getBelType(root_bel) != root->type)
                continue;
            Loc root_loc = ctx->getBelLocation(root_bel);
            found = true;
            bel2cell.clear();
            cell2bel.clear();
            bel2cell[root_bel] = root;
            cell2bel[root->name] = root_bel;

            for (auto child : root->constr_children) {
                // Check that a valid placement exists for all children in the macro at this location
                Loc c_loc = get_child_loc(root_loc, child);
                BelId c_bel = ctx->getBelByLocation(c_loc);
                if (c_bel == BelId()) {
                    found = false;
                    break;
                }
                if (ctx->getBelType(c_bel) != child->type) {
                    found = false;
                    break;
                }
                bel2cell[c_bel] = child;
                cell2bel[child->name] = c_bel;
            }

            if (found)
                break;
        }

        if (!found)
            log_error("Failed to create temporary placement for cell '%s' of type '%s'\n", ctx->nameOf(root),
                      ctx->nameOf(root->type));

        // Create the necessary new ports
        autocreate_ports(root, true);
        for (auto child : root->constr_children)
            autocreate_ports(child, true);

        // Insert cascade connections from all cells in the macro
        auto_cascade_cell(root, cell2bel.at(root->name), bel2cell);
        for (auto child : root->constr_children)
            auto_cascade_cell(child, cell2bel.at(child->name), bel2cell);
    }

    // Create a DSP cell
    CellInfo *create_dsp_cell(IdString base_name, IdString type, CellInfo *constr_base, int dx, int dz)
    {
        IdString name = ctx->id(stringf("%s/%s_x%d_z%d", ctx->nameOf(base_name), ctx->nameOf(type), dx, dz));
        CellInfo *cell = ctx->createCell(name, type);
        if (constr_base != nullptr) {
            // We might be constraining against an already-constrained cell
            if (constr_base->constr_parent != nullptr) {
                cell->constr_x = dx + constr_base->constr_x;
                cell->constr_y = constr_base->constr_y;
                cell->constr_z = dz + constr_base->constr_z;
                cell->constr_abs_z = false;
                cell->constr_parent = constr_base->constr_parent;
                constr_base->constr_parent->constr_children.push_back(cell);
            } else {
                cell->constr_x = dx;
                cell->constr_y = 0;
                cell->constr_z = dz;
                cell->constr_abs_z = false;
                cell->constr_parent = constr_base;
                constr_base->constr_children.push_back(cell);
            }
        }
        // Setup some default parameters
        if (type == id_PREADD9_CORE) {
            cell->params[id_SIGNEDSTATIC_EN] = std::string("DISABLED");
            cell->params[id_BYPASS_PREADD9] = std::string("BYPASS");
            cell->params[id_CSIGNED] = std::string("DISABLED");
            cell->params[id_GSR] = std::string("DISABLED");
            cell->params[id_OPC] = std::string("INPUT_B_AS_PREADDER_OPERAND");
            cell->params[id_PREADDCAS_EN] = std::string("DISABLED");
            cell->params[id_REGBYPSBL] = std::string("REGISTER");
            cell->params[id_REGBYPSBR0] = std::string("BYPASS");
            cell->params[id_REGBYPSBR1] = std::string("BYPASS");
            cell->params[id_RESET] = std::string("SYNC");
            cell->params[id_SHIFTBL] = std::string("BYPASS");
            cell->params[id_SHIFTBR] = std::string("REGISTER");
            cell->params[id_SIGNEDSTATIC_EN] = std::string("DISABLED");
            cell->params[id_SR_18BITSHIFT_EN] = std::string("DISABLED");
            cell->params[id_SUBSTRACT_EN] = std::string("SUBTRACTION");
        } else if (type == id_MULT9_CORE) {
            cell->params[id_ASIGNED_OPERAND_EN] = std::string("DISABLED");
            cell->params[id_BYPASS_MULT9] = std::string("USED");
            cell->params[id_GSR] = std::string("DISABLED");
            cell->params[id_REGBYPSA1] = std::string("BYPASS");
            cell->params[id_REGBYPSA2] = std::string("BYPASS");
            cell->params[id_REGBYPSB] = std::string("BYPASS");
            cell->params[id_RESET] = std::string("SYNC");
            cell->params[id_GSR] = std::string("DISABLED");
            cell->params[id_SHIFTA] = std::string("DISABLED");
            cell->params[id_SIGNEDSTATIC_EN] = std::string("DISABLED");
            cell->params[id_SR_18BITSHIFT_EN] = std::string("DISABLED");
        } else if (type == id_MULT18_CORE) {
            cell->params[id_MULT18X18] = std::string("ENABLED");
            cell->params[id_ROUNDBIT] = std::string("ROUND_TO_BIT0");
            cell->params[id_ROUNDHALFUP] = std::string("DISABLED");
            cell->params[id_ROUNDRTZI] = std::string("ROUND_TO_ZERO");
            cell->params[id_SFTEN] = std::string("DISABLED");
        } else if (type == id_MULT18X36_CORE) {
            cell->params[id_SFTEN] = std::string("DISABLED");
            cell->params[id_MULT18X36] = std::string("ENABLED");
            cell->params[id_MULT36] = std::string("DISABLED");
            cell->params[id_MULT36X36H] = std::string("USED_AS_LOWER_BIT_GENERATION");
            cell->params[id_ROUNDHALFUP] = std::string("DISABLED");
            cell->params[id_ROUNDRTZI] = std::string("ROUND_TO_ZERO");
            cell->params[id_ROUNDBIT] = std::string("ROUND_TO_BIT0");
        } else if (type == id_MULT36_CORE) {
            cell->params[id_MULT36X36] = std::string("ENABLED");
        } else if (type == id_REG18_CORE) {
            cell->params[id_GSR] = std::string("DISABLED");
            cell->params[id_REGBYPS] = std::string("BYPASS");
            cell->params[id_RESET] = std::string("SYNC");
        } else if (type == id_ACC54_CORE) {
            cell->params[id_ACC108CASCADE] = std::string("BYPASSCASCADE");
            cell->params[id_ACCUBYPS] = std::string("USED");
            cell->params[id_ACCUMODE] = std::string("MODE7");
            cell->params[id_ADDSUBSIGNREGBYPS1] = std::string("BYPASS");
            cell->params[id_ADDSUBSIGNREGBYPS2] = std::string("BYPASS");
            cell->params[id_ADDSUBSIGNREGBYPS3] = std::string("BYPASS");
            cell->params[id_ADDSUB_CTRL] = std::string("ADD_ADD_CTRL_54_BIT_ADDER");
            cell->params[id_CASCOUTREGBYPS] = std::string("BYPASS");
            cell->params[id_CINREGBYPS1] = std::string("BYPASS");
            cell->params[id_CINREGBYPS2] = std::string("BYPASS");
            cell->params[id_CINREGBYPS3] = std::string("BYPASS");
            cell->params[id_CONSTSEL] = std::string("BYPASS");
            cell->params[id_CREGBYPS1] = std::string("BYPASS");
            cell->params[id_CREGBYPS2] = std::string("BYPASS");
            cell->params[id_CREGBYPS3] = std::string("BYPASS");
            cell->params[id_DSPCASCADE] = std::string("DISABLED");
            cell->params[id_GSR] = std::string("DISABLED");
            cell->params[id_LOADREGBYPS1] = std::string("BYPASS");
            cell->params[id_LOADREGBYPS2] = std::string("BYPASS");
            cell->params[id_LOADREGBYPS3] = std::string("BYPASS");
            cell->params[id_M9ADDSUBREGBYPS1] = std::string("BYPASS");
            cell->params[id_M9ADDSUBREGBYPS2] = std::string("BYPASS");
            cell->params[id_M9ADDSUBREGBYPS3] = std::string("BYPASS");
            cell->params[id_M9ADDSUB_CTRL] = std::string("ADDITION");
            cell->params[id_OUTREGBYPS] = std::string("BYPASS");
            cell->params[id_RESET] = std::string("SYNC");
            cell->params[id_ROUNDHALFUP] = std::string("DISABLED");
            cell->params[id_ROUNDRTZI] = std::string("ROUND_TO_ZERO");
            cell->params[id_ROUNDBIT] = std::string("ROUND_TO_BIT0");
            cell->params[id_SFTEN] = std::string("DISABLED");
            cell->params[id_SIGN] = std::string("DISABLED");
            cell->params[id_STATICOPCODE_EN] = std::string("DISABLED");
        }
        return cell;
    }

    void copy_global_dsp_params(CellInfo *orig, CellInfo *root)
    {
        if (root->params.count(id_GSR) && orig->params.count(id_GSR))
            root->params[id_GSR] = orig->params.at(id_GSR);
        if (root->params.count(id_RESET) && orig->params.count(id_RESETMODE))
            root->params[id_RESET] = orig->params.at(id_RESETMODE);
        for (auto child : root->constr_children)
            copy_global_dsp_params(orig, child);
    }

    void copy_param(CellInfo *orig, IdString orig_name, CellInfo *dst, IdString dst_name)
    {
        if (!orig->params.count(orig_name))
            return;
        dst->params[dst_name] = orig->params[orig_name];
    }

    struct DSPMacroType
    {
        int a_width;     // width of 'A' input
        int b_width;     // width of 'B' input
        int c_width;     // width of 'C' input
        int z_width;     // width of 'Z' output
        int N9x9;        // number of 9x9 mult+preadds
        int N18x18;      // number of 18x18 mult
        int N18x36;      // number of 18x36 mult
        bool has_preadd; // preadder is used
        bool has_addsub; // post-multiply ALU addsub is used
    };

    const std::unordered_map<IdString, DSPMacroType> dsp_types = {
            {id_MULT9X9, {9, 9, 0, 18, 1, 0, 0, false, false}},
            {id_MULT18X18, {18, 18, 0, 36, 2, 1, 0, false, false}},
            {id_MULT18X36, {18, 36, 0, 54, 4, 2, 1, false, false}},
            {id_MULT36X36, {36, 36, 0, 72, 8, 4, 2, false, false}},
            {id_MULTPREADD9X9, {9, 9, 9, 18, 1, 0, 0, true, false}},
            {id_MULTPREADD18X18, {18, 18, 18, 36, 2, 1, 0, true, false}},
            {id_MULTADDSUB18X18, {18, 18, 54, 54, 2, 1, 0, false, true}},
            {id_MULTADDSUB36X36, {36, 36, 108, 108, 8, 4, 2, false, true}},
    };

    void pack_dsps()
    {
        log_info("Packing DSPs...\n");
        std::vector<CellInfo *> to_remove;

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (!dsp_types.count(ci->type))
                continue;
            auto &mt = dsp_types.at(ci->type);
            int Nreg18 = mt.z_width / 18;

            // Create consituent cells
            std::vector<CellInfo *> preadd9(mt.N9x9), mult9(mt.N9x9), mult18(mt.N18x18), mult18x36(mt.N18x36),
                    reg18(Nreg18);
            for (int i = 0; i < mt.N9x9; i++) {
                preadd9[i] = create_dsp_cell(ci->name, id_PREADD9_CORE, preadd9[0], (i / 4) * 4 + (i / 2) % 2, (i % 2));
                mult9[i] = create_dsp_cell(ci->name, id_MULT9_CORE, preadd9[0], (i / 4) * 4 + (i / 2) % 2, (i % 2) + 2);
            }
            for (int i = 0; i < mt.N18x18; i++)
                mult18[i] = create_dsp_cell(ci->name, id_MULT18_CORE, preadd9[0], (i / 2) * 4 + i % 2, 4);
            for (int i = 0; i < mt.N18x36; i++)
                mult18x36[i] = create_dsp_cell(ci->name, id_MULT18X36_CORE, preadd9[0], (i * 4) + 2, 4);
            for (int i = 0; i < Nreg18; i++) {
                int idx = i;
                if (mt.has_addsub && (i >= 4))
                    idx += 2;
                reg18[i] = create_dsp_cell(ci->name, id_REG18_CORE, preadd9[0], (idx / 4) * 4 + 2, idx % 4);
            }

            // Configure the 9x9 preadd+multiply blocks
            for (int i = 0; i < mt.N9x9; i++) {
                // B input split across pre-adders
                int b_start = (9 * i) % mt.b_width;
                copy_bus(ctx, ci, id_B, b_start, true, preadd9[i], id_B, 0, false, 9);
                // A input split across MULT9s
                int a_start = 9 * (i % 2) + 18 * (i / 4);
                copy_bus(ctx, ci, id_A, a_start, true, mult9[i], id_A, 0, false, 9);
                // Connect control set signals
                copy_port(ctx, ci, id_CLK, mult9[i], id_CLK);
                copy_port(ctx, ci, id_CEA, mult9[i], id_CEA);
                copy_port(ctx, ci, id_RSTA, mult9[i], id_RSTA);
                copy_port(ctx, ci, id_CLK, preadd9[i], id_CLK);
                copy_port(ctx, ci, id_CEB, preadd9[i], id_CEB);
                copy_port(ctx, ci, id_RSTB, preadd9[i], id_RSTB);
                // Copy register configuration
                copy_param(ci, id_REGINPUTA, mult9[i], id_REGBYPSA1);
                copy_param(ci, id_REGINPUTB, preadd9[i], id_REGBYPSBR0);

                // Connect and configure pre-adder if it isn't bypassed
                if (mt.has_preadd) {
                    copy_bus(ctx, ci, id_C, 9 * i, true, preadd9[i], id_C, 0, false, 9);
                    if (i == (mt.N9x9 - 1))
                        copy_port(ctx, ci, id_SIGNEDC, preadd9[i], id_C9);
                    copy_param(ci, id_REGINPUTC, preadd9[i], id_REGBYPSBL);
                    copy_port(ctx, ci, id_CEC, preadd9[i], id_CECL);
                    copy_port(ctx, ci, id_RSTC, preadd9[i], id_RSTCL);
                    // Enable preadder
                    preadd9[i]->params[id_BYPASS_PREADD9] = std::string("USED");
                    preadd9[i]->params[id_OPC] = std::string("INPUT_C_AS_PREADDER_OPERAND");
                    if (i > 0)
                        preadd9[i]->params[id_PREADDCAS_EN] = std::string("ENABLED");
                } else if (mt.has_addsub) {
                    // Connect only for routeability reasons
                    copy_bus(ctx, ci, id_C, 10 * i + ((i >= 4) ? 14 : 0), true, preadd9[i], id_C, 0, false, 10);
                }

                // Connect up signedness for the most significant nonet
                if ((b_start + 9) == mt.b_width)
                    copy_port(ctx, ci, mt.has_addsub ? id_SIGNED : id_SIGNEDB, preadd9[i], id_BSIGNED);
                if ((a_start + 9) == mt.a_width)
                    copy_port(ctx, ci, mt.has_addsub ? id_SIGNED : id_SIGNEDA, mult9[i], id_ASIGNED);
            }

            bool mult36_used = (mt.a_width >= 36) && (mt.b_width >= 36);
            // Configure mult18x36s
            for (int i = 0; i < mt.N18x36; i++) {
                mult18x36[i]->params[id_MULT36] = mult36_used ? std::string("ENABLED") : std::string("DISABLED");
                mult18x36[i]->params[id_MULT36X36H] = (i == 1) ? std::string("USED_AS_HIGHER_BIT_GENERATION")
                                                               : std::string("USED_AS_LOWER_BIT_GENERATION");
            }
            // Create final mult36 if needed
            CellInfo *mult36 = nullptr;
            if (mult36_used) {
                mult36 = create_dsp_cell(ci->name, id_MULT36_CORE, preadd9[0], 6, 6);
            }

            // Configure output registers
            for (int i = 0; i < Nreg18; i++) {
                // Output split across reg18s
                if (!mt.has_addsub)
                    replace_bus(ctx, ci, id_Z, i * 18, true, reg18[i], id_PP, 0, false, 18);
                // Connect control set signals
                copy_port(ctx, ci, id_CLK, reg18[i], id_CLK);
                copy_port(ctx, ci, mt.has_addsub ? id_CEPIPE : id_CEOUT, reg18[i], id_CEP);
                copy_port(ctx, ci, mt.has_addsub ? id_RSTPIPE : id_RSTOUT, reg18[i], id_RSTP);
                // Copy register configuration
                copy_param(ci, mt.has_addsub ? id_REGPIPELINE : id_REGOUTPUT, reg18[i], id_REGBYPS);
            }

            if (mt.has_addsub) {
                // Create and configure ACC54s
                int Nacc54 = mt.c_width / 54;
                std::vector<CellInfo *> acc54(Nacc54);
                for (int i = 0; i < Nacc54; i++)
                    acc54[i] = create_dsp_cell(ci->name, id_ACC54_CORE, preadd9[0], (i * 4) + 2, 5);
                for (int i = 0; i < Nacc54; i++) {
                    // C addsub input
                    copy_bus(ctx, ci, id_C, 54 * i, true, acc54[i], id_CINPUT, 0, false, 54);
                    // Output
                    replace_bus(ctx, ci, id_Z, i * 54, true, acc54[i], id_SUM0, 0, false, 36);
                    replace_bus(ctx, ci, id_Z, i * 54 + 36, true, acc54[i], id_SUM1, 0, false, 18);
                    // Control set
                    copy_port(ctx, ci, id_CLK, acc54[i], id_CLK);
                    copy_port(ctx, ci, id_RSTCTRL, acc54[i], id_RSTCTRL);
                    copy_port(ctx, ci, id_CECTRL, acc54[i], id_CECTRL);
                    copy_port(ctx, ci, id_RSTCIN, acc54[i], id_RSTCIN);
                    copy_port(ctx, ci, id_CECIN, acc54[i], id_CECIN);
                    copy_port(ctx, ci, id_RSTOUT, acc54[i], id_RSTO);
                    copy_port(ctx, ci, id_CEOUT, acc54[i], id_CEO);
                    copy_port(ctx, ci, id_RSTC, acc54[i], id_RSTC);
                    copy_port(ctx, ci, id_CEC, acc54[i], id_CEC);
                    // Add/acc control
                    if (i == 0)
                        copy_port(ctx, ci, id_CIN, acc54[i], id_CIN);
                    else
                        ctx->set_cell_pinmux(acc54[i], id_CIN, PINMUX_1);
                    if (i == (Nacc54 - 1))
                        copy_port(ctx, ci, id_SIGNED, acc54[i], id_SIGNEDI);
                    copy_port(ctx, ci, id_ADDSUB, acc54[i], id_ADDSUB0);
                    copy_port(ctx, ci, id_ADDSUB, acc54[i], id_ADDSUB1);
                    copy_port(ctx, ci, id_LOADC, acc54[i], id_LOAD);
                    // Configuration
                    copy_param(ci, id_REGINPUTC, acc54[i], id_CREGBYPS1);
                    copy_param(ci, id_REGADDSUB, acc54[i], id_ADDSUBSIGNREGBYPS1);
                    copy_param(ci, id_REGADDSUB, acc54[i], id_M9ADDSUBREGBYPS1);
                    copy_param(ci, id_REGLOADC, acc54[i], id_LOADREGBYPS1);
                    copy_param(ci, id_REGLOADC2, acc54[i], id_LOADREGBYPS2);
                    copy_param(ci, id_REGCIN, acc54[i], id_CINREGBYPS1);

                    copy_param(ci, id_REGPIPELINE, acc54[i], id_CREGBYPS2);
                    copy_param(ci, id_REGPIPELINE, acc54[i], id_ADDSUBSIGNREGBYPS2);
                    copy_param(ci, id_REGPIPELINE, acc54[i], id_CINREGBYPS2);
                    copy_param(ci, id_REGPIPELINE, acc54[i], id_M9ADDSUBREGBYPS2);
                    copy_param(ci, id_REGOUTPUT, acc54[i], id_OUTREGBYPS);

                    if (i == 1) {
                        // Top ACC54 in a 108-bit config
                        acc54[i]->params[id_ACCUMODE] = std::string("MODE6");
                        acc54[i]->params[id_ACC108CASCADE] = std::string("CASCADE2ACCU54TOFORMACCU108");
                    } else if ((i == 0) && (Nacc54 == 2)) {
                        // Bottom ACC54 in a 108-bit config
                        acc54[i]->params[id_ACCUMODE] = std::string("MODE2");
                    }
                }
            }

            // Misc finalisation
            copy_global_dsp_params(ci, preadd9[0]);
            auto_cascade_group(preadd9[0]);
            to_remove.push_back(ci);
        }

        for (auto cell : to_remove) {
            for (auto port : sorted_ref(cell->ports))
                disconnect_port(ctx, cell, port.first);
            ctx->cells.erase(cell->name);
        }
    }

    explicit NexusPacker(Context *ctx) : ctx(ctx) {}

    void operator()()
    {
        pack_io();
        pack_dsps();
        convert_prims();
        pack_bram();
        pack_lutram();
        pack_carries();
        pack_widefn();
        pack_ffs();
        pack_constants();
        pack_luts();
        promote_globals();
        place_globals();
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
    cell->tmg_index = -1;
    if (cell->type == id_OXIDE_COMB) {
        cell->lutInfo.is_memory = str_or_default(cell->params, id_MODE, "LOGIC") == "DPRAM";
        cell->lutInfo.is_carry = str_or_default(cell->params, id_MODE, "LOGIC") == "CCU2";
        cell->lutInfo.mux2_used = port_used(cell, id_OFX);
        cell->lutInfo.f = get_net_or_empty(cell, id_F);
        cell->lutInfo.ofx = get_net_or_empty(cell, id_OFX);
        if (cell->lutInfo.is_carry) {
            cell->tmg_portmap[id_A] = id_A0;
            cell->tmg_portmap[id_B] = id_B0;
            cell->tmg_portmap[id_C] = id_C0;
            cell->tmg_portmap[id_D] = id_D0;
            cell->tmg_portmap[id_F] = id_F0;
            cell->tmg_index = get_cell_timing_idx(id_OXIDE_COMB, id_CCU2);
        } else if (cell->lutInfo.ofx != nullptr) {
            cell->tmg_index = get_cell_timing_idx(id_OXIDE_COMB, id_WIDEFN9);
        } else if (cell->lutInfo.is_memory) {
            cell->tmg_index = get_cell_timing_idx(id_OXIDE_COMB, id_DPRAM);
        } else {
            cell->tmg_index = get_cell_timing_idx(id_OXIDE_COMB, id_LUT4);
        }
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
        cell->tmg_index = get_cell_timing_idx(id_OXIDE_FF, id("PPP:SYNC"));
    } else if (cell->type == id_RAMW) {
        cell->ffInfo.ctrlset.async = true;
        cell->ffInfo.ctrlset.regddr_en = false;
        cell->ffInfo.ctrlset.gsr_en = false;
        cell->ffInfo.ctrlset.clkmux = id(str_or_default(cell->params, id_CLKMUX, "CLK")).index;
        cell->ffInfo.ctrlset.cemux = ID_CE;
        cell->ffInfo.ctrlset.lsrmux = ID_INV;
        cell->ffInfo.ctrlset.clk = get_net_or_empty(cell, id_CLK);
        cell->ffInfo.ctrlset.ce = nullptr;
        cell->ffInfo.ctrlset.lsr = get_net_or_empty(cell, id_LSR);
        cell->ffInfo.di = nullptr;
        cell->ffInfo.m = nullptr;
        cell->tmg_index = get_cell_timing_idx(id_RAMW);
    } else if (cell->type == id_OXIDE_EBR) {
        // Strip off bus indices to get the timing ports
        // as timing is generally word-wide
        for (const auto &port : cell->ports) {
            const std::string &name = port.first.str(this);
            size_t idx_end = name.find_last_not_of("0123456789");
            std::string base = name.substr(0, idx_end + 1);
            if (base == "ADA" || base == "ADB") {
                // [4:0] and [13:5] have different timing
                int idx = std::stoi(name.substr(idx_end + 1));
                cell->tmg_portmap[port.first] = id(base + ((idx >= 5) ? "_13_5" : "_4_0"));
            } else {
                // Just strip off bus index
                cell->tmg_portmap[port.first] = id(base);
            }
        }

        cell->tmg_index = get_cell_timing_idx(id(str_or_default(cell->params, id_MODE, "DP16K") + "_MODE"));
        NPNR_ASSERT(cell->tmg_index != -1);
    }
}

NEXTPNR_NAMESPACE_END

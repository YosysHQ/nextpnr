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
        new_cell->addInput(id_Z);
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

    NetInfo *gnd_net = nullptr, *vcc_net = nullptr;

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

    BelId get_io_bel(CellInfo *ci)
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
            BelId bel = get_io_bel(ci);
            // Set the cell type to the bel type
            IdString type = ctx->getBelType(bel);
            NPNR_ASSERT(type != IdString());
            ci->type = type;
        }
    }

    void pack_constants()
    {
        // Make sure we have high and low nets available
        get_const_net(id_VHI);
        get_const_net(id_VLO);
        // Iterate through cells
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            // Skip certain cells at this point
            if (ci->type != id_LUT4 && ci->type != id_INV && ci->type != id_VHI && ci->type != id_VLO)
                process_inv_constants(cell.second);
        }
        // Remove superfluous inverters and constant drivers
        trim_design();
    }

    explicit NexusPacker(Context *ctx) : ctx(ctx) {}

    void operator()()
    {
        pack_io();
        pack_ffs();
        pack_constants();
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

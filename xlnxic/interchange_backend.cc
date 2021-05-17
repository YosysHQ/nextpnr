/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2021  Symbiflow Authors
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

#include "nextpnr.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/std/iostream.h>
#include <sstream>

#include "LogicalNetlist.capnp.h"
#include "PhysicalNetlist.capnp.h"
#include "zlib.h"

#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

void write_message(::capnp::MallocMessageBuilder &message, const std::string &filename)
{
    kj::Array<capnp::word> words = messageToFlatArray(message);
    kj::ArrayPtr<kj::byte> bytes = words.asBytes();

    gzFile file = gzopen(filename.c_str(), "w");
    NPNR_ASSERT(file != Z_NULL);

    NPNR_ASSERT(gzwrite(file, &bytes[0], bytes.size()) == (int)bytes.size());
    NPNR_ASSERT(gzclose(file) == Z_OK);
}

struct StringEnumerator
{
    std::vector<std::string> strings;
    dict<std::string, size_t> string_to_index;

    size_t get_index(const std::string &s)
    {
        auto result = string_to_index.emplace(s, strings.size());
        if (result.second) {
            // This string was inserted, append.
            strings.push_back(s);
        }

        return result.first->second;
    }
};

// (cell, pin) -> property
dict<std::pair<IdString, IdString>, IdString> get_invertible_pins(const Context *ctx)
{
    dict<std::pair<IdString, IdString>, IdString> result;
    for (const auto &cell_type : ctx->chip_info->cell_types) {
        IdString type_name(cell_type.cell_type);
        for (const auto &inv_entry : cell_type.inversions)
            result[std::make_pair(type_name, IdString(inv_entry.pin_name))] = IdString(inv_entry.parameter);
    }
    return result;
}

struct ParsedPort
{
    IdString m_base;
    bool m_is_bus;
    int m_bit;

    ParsedPort() : m_base(), m_is_bus(false), m_bit(0){};
    explicit ParsedPort(IdString base) : m_base(base), m_is_bus(false), m_bit(0){};
    ParsedPort(IdString base, int bit) : m_base(base), m_is_bus(true), m_bit(bit){};

    IdString base() const { return m_base; }
    bool is_bus() const { return m_is_bus; }
    int bus_bit() const
    {
        NPNR_ASSERT(m_is_bus);
        return m_bit;
    }
};

struct PortData
{
    IdString name;
    PortType dir;
    bool is_bus;
    int width;
};

struct CellDecl
{
    IdString type;
    IdString library;
    std::vector<int> ports;
};

struct PortInst
{
    int inst_idx = -1;
    int port_idx;
    bool is_bus;
    int bus_bit;
};

struct FormattedProperty
{
    FormattedProperty() : format(PARAM_FMT_STRING), value(std::string("")){};
    FormattedProperty(ParameterFormat format, Property value) : format(format), value(value){};
    FormattedProperty(ParameterFormat format, const std::string &value) : format(format), value(value){};
    ParameterFormat format;
    Property value;
};

struct NetData
{
    IdString name;
    std::vector<PortInst> port_insts;
    dict<IdString, FormattedProperty> properties;
};

struct CellInst
{
    IdString name;
    int cell_idx;
    dict<IdString, FormattedProperty> properties;
};

LogicalNetlist::Netlist::Direction convert_dir(PortType dir)
{
    switch (dir) {
    case PORT_IN:
        return LogicalNetlist::Netlist::Direction::INPUT;
    case PORT_OUT:
        return LogicalNetlist::Netlist::Direction::OUTPUT;
    case PORT_INOUT:
        return LogicalNetlist::Netlist::Direction::INOUT;
    default:
        NPNR_ASSERT_FALSE("unknown direction");
    }
}

struct LogicalNetlistWriter
{
    LogicalNetlistWriter(Context *ctx) : ctx(ctx){};
    Context *ctx;

    StringEnumerator strs;

    // Map library cell type to CellIdx
    dict<IdString, int> celltype2idx;
    // Map library cell port to PortIdx
    dict<std::pair<IdString, IdString>, int> cellport2idx;
    // Map top level port to PortIdx
    dict<IdString, int> topport2idx;

    int top_cell_idx = -1;

    std::vector<PortData> port_data;
    std::vector<CellDecl> cell_decls;
    std::vector<NetData> nets;

    std::vector<CellInst> instances;
    dict<IdString, int> inst2idx;

    dict<IdString, const CellTypePOD *> celltype2db;
    dict<std::pair<IdString, IdString>, IdString> invertible_pins;

    pool<std::pair<IdString, IdString>> overriden_inversion_props;

    // Default formatter for unknown properties
    FormattedProperty default_format_prop(Property p)
    {
        if (p.is_string) {
            return FormattedProperty(PARAM_FMT_STRING, p.as_string());
        } else {
            // TODO: truncation?
            return FormattedProperty(PARAM_FMT_INTEGER, Property(p.as_int64()));
        }
    }

    Property get_default_value(const CellParameterPOD &param)
    {
        const std::string &strval = IdString(param.default_value).str(ctx);
        if (param.format == PARAM_FMT_INTEGER)
            return Property(std::stoi(strval));
        else if (param.format == PARAM_FMT_BOOLEAN)
            return Property((strval == "1" || strval == "TRUE" || strval == "YES") ? 1 : 0, 1);
        else
            return Property(strval);
    }

    // (1101, 1, 5) => 5'b01101
    // (10100101, 4, 8) => 8'hA5
    std::string format_bitstring(Property value, int l2base, int width)
    {
        NPNR_ASSERT(l2base == 4 || l2base == 1);
        NPNR_ASSERT(!value.is_string);

        static const std::string chars = "0123456789ABCDEF";
        std::string result;
        for (int i = 0; i < width; i += l2base) {
            Property chunk = value.extract(i, l2base);
            result += chars.at(chunk.as_int64());
        }
        std::reverse(result.begin(), result.end());
        result = stringf("%d'%c", width, (l2base == 4) ? 'h' : 'b') + result;
        return result;
    }

    // Formatter for cell properties
    template <typename Tinst>
    FormattedProperty format_cell_param(IdString inst_name, const Tinst &inst, const CellParameterPOD &param)
    {
        IdString prop_name(param.name);
        ParameterFormat format = ParameterFormat(param.format);

        Property value;
        if (overriden_inversion_props.count(std::make_pair(inst_name, prop_name)))
            value = Property(0);
        else if (inst.params.count(prop_name))
            value = inst.params.at(prop_name);
        else if (inst.attrs.count(prop_name))
            value = inst.attrs.at(prop_name);
        else
            return FormattedProperty(format, get_default_value(param));

        switch (format) {
        case PARAM_FMT_STRING:
            if (!value.is_string)
                log_error("Expected string for property '%s' on cell '%s', got %lld.\n", ctx->nameOf(prop_name),
                          ctx->nameOf(inst_name), static_cast<long long int>(value.as_int64()));
            return FormattedProperty(format, value.as_string());
        case PARAM_FMT_FLOAT:
            return FormattedProperty(format, value.is_string ? value.as_string() : std::to_string(value.as_int64()));
        case PARAM_FMT_INTEGER:
            if (value.is_string)
                log_error("Expected integer for property '%s' on cell '%s', got '%s'.\n", ctx->nameOf(prop_name),
                          ctx->nameOf(inst_name), value.as_string().c_str());
            return FormattedProperty(format, value);
        case PARAM_FMT_BOOLEAN: {
            bool bool_value = false;
            if (value.is_string) {
                const std::string &s = value.as_string();
                if (s == "1" || s == "TRUE" || s == "YES")
                    bool_value = true;
                else if (s != "1" && s != "FALSE" && s != "NO")
                    log_error("Expected boolean for property '%s' on cell '%s', got '%s'.\n", ctx->nameOf(prop_name),
                              ctx->nameOf(inst_name), s.c_str());
            } else {
                bool_value = (value.as_int64() == 1);
            }
            return FormattedProperty(format, Property(bool_value ? 1 : 0, 1));
        }
        case PARAM_FMT_VBIN:
        case PARAM_FMT_VHEX:
            if (value.is_string)
                log_error("Expected integer for property '%s' on cell '%s', got '%s'.\n", ctx->nameOf(prop_name),
                          ctx->nameOf(inst_name), value.as_string().c_str());
            return FormattedProperty(format, format_bitstring(value, format == PARAM_FMT_VHEX ? 4 : 1, param.width));
        default:
            NPNR_ASSERT_FALSE("unable to handle parameter type");
        }
    }

    // foo -> ParsedPort(foo)
    // foo[4] -> ParsedPort(foo, 4)
    // foo[3][4] -> ParsedPort(foo[3], 4)
    ParsedPort parse_port(IdString port)
    {
        const std::string &port_str = port.str(ctx);
        if (port_str.empty() || port_str.back() != ']')
            return ParsedPort(port);
        std::size_t bracket_pos = port_str.find_last_of('[');
        if (bracket_pos == std::string::npos)
            return ParsedPort(port);
        return ParsedPort(ctx->id(port_str.substr(0, bracket_pos)),
                          std::stoi(port_str.substr(bracket_pos + 1, port_str.size() - (bracket_pos + 2))));
    }

    PortType get_port_dir(const CellInfo *pad)
    {
        const NetInfo *pad_net = pad->getPort(id_PAD);
        bool has_output = false, has_input = false;
        if (pad_net) {
            if (pad_net->driver.cell && pad_net->driver.cell != pad)
                has_output = true;
            for (const auto &usr : pad_net->users) {
                if (usr.cell == pad)
                    continue;
                if (usr.cell->ports.at(usr.port).type == PORT_INOUT)
                    has_output = true;
                has_input = true;
            }
        }
        return has_output ? (has_input ? PORT_INOUT : PORT_OUT) : PORT_IN;
    }

    void group_ports()
    {
        // Find all the top level ports in a design (PAD cells) and group them back into buses
        for (const auto &cell : ctx->cells) {
            const CellInfo *ci = cell.second.get();
            // All top level ports have a PAD cell associated with them
            if (ci->type != id_PAD)
                continue;
            ParsedPort name = parse_port(ci->name);
            if (!topport2idx.count(name.base())) {
                topport2idx[name.base()] = port_data.size();
                port_data.emplace_back();
                auto &p = port_data.back();
                p.name = name.base();
                p.dir = get_port_dir(ci);
                p.is_bus = name.is_bus();
                p.width = name.is_bus() ? (name.bus_bit() + 1) : 1;
            } else {
                NPNR_ASSERT(name.is_bus());
                auto &p = port_data.at(topport2idx.at(name.base()));
                if (p.width < (name.bus_bit() + 1))
                    p.width = (name.bus_bit() + 1);
                PortType port_dir = get_port_dir(ci);
                if (port_dir != p.dir)
                    p.dir = PORT_INOUT; // case of mixed directionality in a bus, downcast whole bus to INOUT
            }
        }
    }

    void import_lib_cells()
    {
        pool<IdString> seen_cells;
        // Discover what library cells are used in the design
        for (const auto &cell : ctx->cells) {
            const CellInfo *ci = cell.second.get();
            if (ci->type == id_PAD)
                continue; // these only exist in the physical design
            IdString lib_type =
                    (ci->macro_parent == IdString()) ? ci->type : ctx->expanded_macros.at(ci->macro_parent).type;
            seen_cells.insert(lib_type);
        }
        // Import based on logical cells in the database
        for (const auto &db_cell : ctx->chip_info->cell_types) {
            IdString cell_type(db_cell.cell_type);
            celltype2db.emplace(cell_type, &db_cell);
            if (!seen_cells.count(cell_type))
                continue;
            seen_cells.erase(cell_type);
            // Create a cell declaration
            celltype2idx[cell_type] = cell_decls.size();
            cell_decls.emplace_back();
            auto &decl = cell_decls.back();
            decl.type = cell_type;
            decl.library = IdString(db_cell.library);
            // Import ports (to the global port list)
            for (const auto &db_port : db_cell.logical_ports) {
                PortData port;
                port.name = IdString(db_port.name);
                port.dir = PortType(db_port.dir);
                if (db_port.bus_start == -1) {
                    port.is_bus = false;
                    port.width = 1;
                } else {
                    port.is_bus = true;
                    port.width = (db_port.bus_end - db_port.bus_start) + 1;
                }
                decl.ports.push_back(port_data.size());
                cellport2idx[std::make_pair(cell_type, port.name)] = port_data.size();
                port_data.push_back(port);
            }
        }
        // This means we had leaf cells not in the database, which should never happen by this point.
        NPNR_ASSERT(seen_cells.empty());
    }

    void import_top_celldecl()
    {
        top_cell_idx = cell_decls.size();
        cell_decls.emplace_back();
        auto &decl = cell_decls.back();
        decl.type = ctx->top_module;
        decl.library = ctx->id("work");
        for (const auto &port_idx : topport2idx) {
            decl.ports.push_back(port_idx.second);
        }
    }

    void import_net(const NetInfo *net)
    {
        NetData result;
        result.name = net->name;

        pool<IdString> seen_macros;

        auto import_portref = [&](const PortRef &pr) {
            if (!pr.cell)
                return;
            if (pr.cell->macro_parent != IdString()) {
                seen_macros.insert(pr.cell->macro_parent);
                return; // expanded macro contents are a special case and aren't imported directly
            }
            if (pr.port == id__TIED_0 || pr.port == id__TIED_1) {
                // these ports are used for physical-only ties and skipped in the logical netlist
                return;
            }
            ParsedPort parsed;
            PortInst inst;
            if (pr.cell->type == id_PAD) {
                // Top level
                parsed = parse_port(pr.cell->name);
                inst.inst_idx = -1;
                inst.port_idx = topport2idx.at(parsed.base());
            } else {
                parsed = parse_port(pr.port);
                inst.inst_idx = inst2idx.at(pr.cell->name);
                inst.port_idx = cellport2idx.at(std::make_pair(pr.cell->type, parsed.base()));
            }
            inst.is_bus = parsed.is_bus();
            inst.bus_bit = parsed.is_bus() ? parsed.bus_bit() : 1;
            result.port_insts.push_back(inst);
        };

        // Import direct driver/users
        import_portref(net->driver);
        for (const auto &usr : net->users) {
            if (net->name == id_GLOBAL_LOGIC1) {
                // Inverted tied-one pins are zero in Vivado's viewpoint but one (with the inversion flag set) in
                // nextpnr's viewpoint
                auto fnd_inversion = invertible_pins.find(std::make_pair(usr.cell->type, usr.port));
                if (fnd_inversion != invertible_pins.end() &&
                    bool_or_default(usr.cell->params, fnd_inversion->second)) {
                    continue;
                }
            }
            import_portref(usr);
        }

        if (net->name == id_GLOBAL_LOGIC0) {
            // Add inverted tied-one pins to this logical net
            const NetInfo *logic1 = ctx->nets.at(id_GLOBAL_LOGIC1).get();
            for (auto &logic1_usr : logic1->users) {
                auto fnd_inversion = invertible_pins.find(std::make_pair(logic1_usr.cell->type, logic1_usr.port));
                if (fnd_inversion != invertible_pins.end() &&
                    bool_or_default(logic1_usr.cell->params, fnd_inversion->second))
                    import_portref(logic1_usr);
            }
        }

        // Import logical ports that originated from expanded macros (hence no longer exist in nextpnr's main netlist)
        for (IdString macro : seen_macros) {
            const auto &exp = ctx->expanded_macros.at(macro);
            for (const auto &macro_port : exp.ports) {
                if (macro_port.second != net->name)
                    continue;
                // Macro port is indeed connected to this net
                ParsedPort parsed = parse_port(macro_port.first);
                PortInst inst;
                inst.inst_idx = inst2idx.at(macro);
                inst.port_idx = cellport2idx.at(std::make_pair(exp.type, parsed.base()));
                inst.is_bus = parsed.is_bus();
                inst.bus_bit = parsed.is_bus() ? parsed.bus_bit() : 1;
                result.port_insts.push_back(inst);
            }
        }

        // Import attributes as properties
        for (const auto &attr : net->attrs) {
            if (attr.first == id_ROUTING)
                continue; // skip nextpnr-internal routing
            result.properties[attr.first] = default_format_prop(attr.second);
        }

        nets.push_back(result);
    }

    void import_nets()
    {
        for (auto &net : ctx->nets) {
            if (net.second->macro_parent != IdString())
                continue; // nets contained within macros are out-of-scope
            import_net(net.second.get());
        }
    }

    template <typename Tinst> void import_instance(IdString name, const Tinst &inst)
    {
        CellInst result;
        result.name = name;
        result.cell_idx = celltype2idx.at(inst.type);

        // Import properties defined in database
        auto cell_data = celltype2db.at(inst.type);
        for (const auto &param : cell_data->parameters) {
            result.properties[IdString(param.name)] = format_cell_param(name, inst, param);
        }

        // Import other properties using default rules
        for (const auto &param : inst.params) {
            if (result.properties.count(param.first))
                continue;
            result.properties[param.first] = default_format_prop(param.second);
        }
        for (const auto &attr : inst.attrs) {
            if (attr.first == id_NEXTPNR_BEL || attr.first == id_BEL_STRENGTH)
                continue; // Skip nextpnr-internal attributes
            if (result.properties.count(attr.first))
                continue;
            result.properties[attr.first] = default_format_prop(attr.second);
        }

        inst2idx[name] = instances.size();
        instances.push_back(result);
    }

    void import_instances()
    {
        for (const auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->macro_parent != IdString() || ci->type == id_PAD)
                continue; // neither macro expansion innards nor top pads become instances
            import_instance(cell.first, *ci);
        }
        for (const auto &macro : ctx->expanded_macros) {
            import_instance(macro.first, macro.second);
        }
    }

    void do_import()
    {
        invertible_pins = get_invertible_pins(ctx);

        for (const auto &usr : ctx->nets.at(id_GLOBAL_LOGIC1)->users) {
            // Inverted tied-one pins are zero in Vivado's viewpoint but one (with the inversion flag set) in
            // nextpnr's viewpoint
            auto fnd_inversion = invertible_pins.find(std::make_pair(usr.cell->type, usr.port));
            if (fnd_inversion != invertible_pins.end() && bool_or_default(usr.cell->params, fnd_inversion->second)) {
                overriden_inversion_props.emplace(usr.cell->name, fnd_inversion->second);
            }
        }

        group_ports();
        import_lib_cells();
        import_instances();
        import_nets();
        import_top_celldecl();
    }

    size_t id2str(IdString id) { return strs.get_index(id.str(ctx)); }

    template <typename Tprops> void write_properties(Tprops &out_props, const dict<IdString, FormattedProperty> &props)
    {
        auto entries = out_props.initEntries(props.size());
        auto iter = entries.begin();
        for (const auto &prop : props) {
            iter->setKey(id2str(prop.first));
            switch (prop.second.format) {
            case PARAM_FMT_INTEGER:
                iter->setIntValue(prop.second.value.as_int64());
                break;
            case PARAM_FMT_BOOLEAN:
                iter->setBoolValue(prop.second.value.as_int64() != 0);
                break;
            default:
                iter->setTextValue(strs.get_index(prop.second.value.as_string()));
                break;
            }
            ++iter;
        }
    }

    void run(const std::string &filename)
    {
        do_import();
        ::capnp::MallocMessageBuilder message;
        LogicalNetlist::Netlist::Builder log_netlist = message.initRoot<LogicalNetlist::Netlist>();

        log_netlist.setName("top");

        // Write all ports
        // TODO: properties on ports
        auto ports = log_netlist.initPortList(port_data.size());
        auto ports_iter = ports.begin();
        for (const auto &port : port_data) {
            ports_iter->setName(id2str(port.name));
            ports_iter->setDir(convert_dir(port.dir));
            if (port.is_bus) {
                auto bus = ports_iter->initBus();
                bus.setBusStart(0);
                bus.setBusEnd(port.width - 1);
            } else {
                ports_iter->setBit();
            }
            ++ports_iter;
        }

        // Write all cell declarations
        auto cell_decls_out = log_netlist.initCellDecls(cell_decls.size());
        auto cell_decls_iter = cell_decls_out.begin();
        for (const auto &decl : cell_decls) {
            cell_decls_iter->setName(id2str(decl.type));
            cell_decls_iter->setView(strs.get_index("netlist"));
            cell_decls_iter->setLib(id2str(decl.library));
            // List of decl ports indexing into portList
            auto cell_decl_ports = cell_decls_iter->initPorts(decl.ports.size());
            int decl_port_idx = 0;
            for (auto port_idx : decl.ports)
                cell_decl_ports.set(decl_port_idx++, port_idx);
            ++cell_decls_iter;
        }

        // Write all cell instances
        auto cell_insts = log_netlist.initInstList(instances.size());
        auto cell_insts_iter = cell_insts.begin();
        for (const auto &inst : instances) {
            cell_insts_iter->setName(id2str(inst.name));
            auto inst_props = cell_insts_iter->initPropMap();
            write_properties(inst_props, inst.properties);
            cell_insts_iter->setView(strs.get_index("netlist"));
            cell_insts_iter->setCell(inst.cell_idx);
            ++cell_insts_iter;
        }

        // Write top level inst
        auto top_inst = log_netlist.initTopInst();
        top_inst.setName(id2str(ctx->top_module));
        top_inst.setView(strs.get_index("netlist"));
        top_inst.setCell(top_cell_idx);

        // Write cell content
        auto cells_out = log_netlist.initCellList(cell_decls.size());
        auto cells_iter = cells_out.begin();
        for (int cell_idx = 0; cell_idx < int(cell_decls.size()); cell_idx++) {
            cells_iter->setIndex(cell_idx);
            // Only the top cell is non-blackbox, everything else is a library cell
            if (cell_idx == top_cell_idx) {
                // All instances are directly inside the top cell (flat hierarchy at this point)
                auto top_insts = cells_iter->initInsts(instances.size());
                for (int inst_idx = 0; inst_idx < int(instances.size()); inst_idx++)
                    top_insts.set(inst_idx, inst_idx);
                auto top_nets = cells_iter->initNets(nets.size());
                auto top_nets_iter = top_nets.begin();
                // Write top level nets
                for (const auto &net : nets) {
                    top_nets_iter->setName(id2str(net.name));
                    auto net_props = top_nets_iter->initPropMap();
                    write_properties(net_props, net.properties);
                    // List of ports on net
                    auto net_port_insts = top_nets_iter->initPortInsts(net.port_insts.size());
                    auto port_inst_iter = net_port_insts.begin();
                    for (const auto &port_inst : net.port_insts) {
                        port_inst_iter->setPort(port_inst.port_idx);
                        auto bus_idx = port_inst_iter->initBusIdx();
                        if (port_inst.is_bus)
                            bus_idx.setIdx(port_inst.bus_bit);
                        else
                            bus_idx.setSingleBit();
                        if (port_inst.inst_idx == -1)
                            port_inst_iter->setExtPort();
                        else
                            port_inst_iter->setInst(port_inst.inst_idx);
                        ++port_inst_iter;
                    }
                    ++top_nets_iter;
                }
            }
            ++cells_iter;
        }

        auto strs_out = log_netlist.initStrList(strs.strings.size());
        for (size_t i = 0; i < strs.strings.size(); i++)
            strs_out.set(i, strs.strings.at(i));

        write_message(message, filename);
    }
};

struct PhysicalNetlistWriter
{
    PhysicalNetlistWriter(Context *ctx) : ctx(ctx){};
    Context *ctx;

    StringEnumerator strs;

    size_t id2str(IdString id) { return strs.get_index(id.str(ctx)); }

    using RouteBranchBuilder = PhysicalNetlist::PhysNetlist::RouteBranch::Builder;
    using NetBuilder = PhysicalNetlist::PhysNetlist::PhysNet::Builder;

    dict<PipId, PlaceStrength> pip_place_strength;
    dict<WireId, std::vector<PipId>> pip_downhill;
    dict<WireId, std::vector<BelPin>> wire_sinks;
    pool<PipId> seen_pips;
    pool<PipId> inverted_pips;
    std::vector<PipId> root_pips;

    dict<std::pair<IdString, IdString>, IdString> invertible_pins;

    // Some extra data used because what nextpnr sees as part of logic-1 (with inversion done at the pin level) is
    // logic-0 for Vivado's purposes...
    pool<PipId> inv_one_roots;
    pool<PipId> inv_one_pips;
    pool<std::pair<BelId, IdString>> inv_one_sinks;

    void find_inverted_pips(const NetInfo *net)
    {
        bool is_inv_one = (net->name == id_GLOBAL_LOGIC1);
        for (auto &usr : net->users) {
            auto found_inv = invertible_pins.find(std::make_pair(usr.cell->type, usr.port));
            if (found_inv == invertible_pins.end())
                continue; // not invertible
            if (!bool_or_default(usr.cell->params, found_inv->second))
                continue; // not inverted

            for (IdString phys_pin : ctx->getBelPinsForCellPin(usr.cell, usr.port)) {
                WireId wire = ctx->getBelPinWire(usr.cell->bel, phys_pin);
                if (is_inv_one)
                    inv_one_sinks.emplace(usr.cell->bel, phys_pin);
                WireId cursor = wire;
                while (true) {
                    PipId pip = net->wires.at(cursor).pip;
                    NPNR_ASSERT(pip != PipId()); // somehow, we hit the source without finding an inverter
                    const auto &pip_data = chip_pip_info(ctx->chip_info, pip);
                    if (pip_data.flags & PipDataPOD::FLAG_CAN_INV) {
                        // TODO: 7-series style inversion bels which have different pips rather than configurable pips
                        inverted_pips.insert(pip);
                        inv_one_roots.insert(pip);
                        break;
                    }
                    if (is_inv_one)
                        inv_one_pips.insert(pip);
                    cursor = ctx->getPipSrcWire(pip);
                }
            }
        }
    }

    void reset_route_data()
    {
        // Clears state inbetween writing routing for different nets
        pip_place_strength.clear();
        pip_downhill.clear();
        root_pips.clear();
        wire_sinks.clear();
        seen_pips.clear();
        root_pips.clear();
        inverted_pips.clear();
    }

    RouteBranchBuilder emit_branch(PipId pip, RouteBranchBuilder branch)
    {
        const PipDataPOD &pip_data = chip_pip_info(ctx->chip_info, pip);
        const TileTypePOD &tile_type = chip_tile_info(ctx->chip_info, pip.tile);

        NPNR_ASSERT((pip_data.flags & PipDataPOD::FLAG_SYNTHETIC) == 0);

        if (pip_data.site == -1) {
            // This is a PIP
            auto pip_obj = branch.getRouteSegment().initPip();
            pip_obj.setTile(id2str(ctx->tile_name(pip.tile)));

            IdString src_wire_name(tile_type.wires[pip_data.src_wire].name);
            IdString dst_wire_name(tile_type.wires[pip_data.dst_wire].name);
            if (pip_data.flags & PipDataPOD::FLAG_REVERSED) {
                // Reversed bidirectional PIP
                pip_obj.setWire0(id2str(dst_wire_name));
                pip_obj.setWire1(id2str(src_wire_name));
                pip_obj.setForward(false);
            } else {
                // Unidrectional/forward bidirectional PIP
                pip_obj.setWire0(id2str(src_wire_name));
                pip_obj.setWire1(id2str(dst_wire_name));
                pip_obj.setForward(true);
            }
            pip_obj.setIsFixed(pip_place_strength.at(pip) >= STRENGTH_FIXED);

            // If this is a pseudo PIP, get the name of the site it routes through
            if ((pip_data.flags & PipDataPOD::FLAG_PSEUDO) && pip_data.pseudo_pip.size() > 0) {
                for (const auto &bel_pin : pip_data.pseudo_pip) {
                    const BelDataPOD &bel_data = tile_type.bels[bel_pin.bel_index];
                    if (bel_data.site == -1)
                        continue;
                    pip_obj.setSite(id2str(ctx->site_name(pip.tile, bel_data.site)));
                    // It is assumed that a pseudo PIP traverses one site only
                    break;
                }
            }

            return branch;
        } else {
            auto site_name = id2str(ctx->site_name(pip.tile, pip_data.site));
            if (pip_data.type == PipDataPOD::SITE_ENTRANCE) {
                // Routing -> site
                auto port_name = id2str(IdString(pip_data.site_port.port_name));
                auto site_pin = branch.getRouteSegment().initSitePin();
                site_pin.setSite(site_name);
                site_pin.setPin(port_name);

                auto subbranch = branch.initBranches(1);
                auto bel_pin_branch = subbranch[0];
                auto bel_pin = bel_pin_branch.getRouteSegment().initBelPin();
                bel_pin.setSite(site_name);
                bel_pin.setBel(port_name);
                bel_pin.setPin(port_name);
                return bel_pin_branch;
            } else if (pip_data.type == PipDataPOD::SITE_EXIT) {
                // Site -> routing
                auto port_name = id2str(IdString(pip_data.site_port.port_name));
                auto bel_pin = branch.getRouteSegment().initBelPin();
                bel_pin.setSite(site_name);
                bel_pin.setBel(port_name);
                bel_pin.setPin(port_name);

                auto subbranch = branch.initBranches(1);
                auto site_pin_branch = subbranch[0];
                auto site_pin = site_pin_branch.getRouteSegment().initSitePin();
                site_pin.setSite(site_name);
                site_pin.setPin(port_name);
                return site_pin_branch;
            } else {
                // Internal site routing
                BelId bel(pip.tile, pip_data.site_pip.bel);
                auto &bel_data = chip_bel_info(ctx->chip_info, bel);
                auto bel_name = id2str(IdString(bel_data.name));
                if (bel_data.flags & BelDataPOD::FLAG_RBEL) {
                    // Site PIP
                    auto site_pip = branch.getRouteSegment().initSitePIP();
                    site_pip.setSite(site_name);
                    site_pip.setBel(bel_name);
                    site_pip.setPin(id2str(IdString(bel_data.pins[pip_data.site_pip.from_pin].name)));
                    site_pip.setIsFixed(pip_place_strength.at(pip) >= STRENGTH_FIXED);

                    // Mark inverter state.
                    // This is required for US/US+ inverters, because those inverters
                    // only have 1 input.
                    if (inverted_pips.count(pip))
                        site_pip.setInverts();

                    return branch;
                } else {
                    // Route through logic BEL
                    auto in_bel_pin = branch.getRouteSegment().initBelPin();

                    in_bel_pin.setSite(site_name);
                    in_bel_pin.setBel(bel_name);
                    in_bel_pin.setPin(id2str(IdString(bel_data.pins[pip_data.site_pip.from_pin].name)));

                    auto subbranch = branch.initBranches(1);
                    auto bel_pin_branch = subbranch[0];
                    auto out_bel_pin = bel_pin_branch.getRouteSegment().initBelPin();
                    out_bel_pin.setSite(site_name);
                    out_bel_pin.setBel(bel_name);
                    out_bel_pin.setPin(id2str(IdString(bel_data.pins[pip_data.site_pip.to_pin].name)));

                    return bel_pin_branch;
                }
            }
        }
    }

    void emit_bel_pin(RouteBranchBuilder branch, BelId bel, IdString pin)
    {
        const BelDataPOD &bel_data = chip_bel_info(ctx->chip_info, bel);

        if (bel_data.flags & BelDataPOD::FLAG_RBEL) {
            // Is an inverter delimiting vcc and gnd
            auto out_pip = branch.getRouteSegment().initSitePIP();

            out_pip.setSite(id2str(ctx->site_name(bel.tile, bel_data.site)));
            out_pip.setBel(id2str(IdString(bel_data.name)));
            out_pip.setPin(id2str(pin));
            out_pip.setIsInverting(true);
        } else {
            auto out_bel_pin = branch.getRouteSegment().initBelPin();
            out_bel_pin.setSite(id2str(ctx->site_name(bel.tile, bel_data.site)));
            out_bel_pin.setBel(id2str(IdString(bel_data.name)));
            out_bel_pin.setPin(id2str(pin));
        }
    }

    void find_non_synthetic_edges(WireId root_wire)
    {
        std::vector<WireId> wires_to_expand;

        wires_to_expand.push_back(root_wire);
        while (!wires_to_expand.empty()) {
            WireId wire = wires_to_expand.back();
            wires_to_expand.pop_back();

            auto downhill_iter = pip_downhill.find(wire);
            if (downhill_iter == pip_downhill.end()) {
                if (root_wire != wire) {
                    log_warning("Wire %s never entered the real fabric?\n", ctx->nameOfWire(wire));
                }
                continue;
            }

            for (PipId pip : pip_downhill.at(wire)) {
                auto &pip_data = chip_pip_info(ctx->chip_info, pip);
                if (pip_data.flags & PipDataPOD::FLAG_SYNTHETIC) {
                    // Continue to follow synthetic edges.
                    wires_to_expand.push_back(ctx->getPipDstWire(pip));
                } else {
                    // Stop following edges that are non-synthetic, they will be
                    // followed during emit_net
                    root_pips.push_back(pip);
                }
            }
        }
    }

    void emit_net_segment(WireId wire, RouteBranchBuilder branch)
    {
        size_t number_branches = 0;
        auto downhill_iter = pip_downhill.find(wire);
        if (downhill_iter != pip_downhill.end()) {
            number_branches += downhill_iter->second.size();
        }

        auto sink_iter = wire_sinks.find(wire);
        if (sink_iter != wire_sinks.end()) {
            number_branches += sink_iter->second.size();
        }
        size_t branch_index = 0;
        auto branches = branch.initBranches(number_branches);

        if (downhill_iter != pip_downhill.end()) {
            const std::vector<PipId> &wire_pips = downhill_iter->second;
            for (size_t i = 0; i < wire_pips.size(); ++i) {
                PipId pip = wire_pips.at(i);
                NPNR_ASSERT(seen_pips.erase(pip) == 1);
                RouteBranchBuilder leaf_branch = emit_branch(pip, branches[branch_index++]);

                emit_net_segment(ctx->getPipDstWire(pip), leaf_branch);
            }
        }

        if (sink_iter != wire_sinks.end()) {
            for (const auto bel_pin : sink_iter->second) {
                auto leaf_branch = branches[branch_index++];
                emit_bel_pin(leaf_branch, bel_pin.bel, bel_pin.pin);
            }
        }
    }

    void emit_net(NetBuilder net_out, const NetInfo *ni)
    {
        reset_route_data();
        if (ni->name != id_GLOBAL_LOGIC1) // inverted Vcc is a special case is dealt with at the start
            find_inverted_pips(ni);
        const CellInfo *driver_cell = ni->driver.cell;
        if (driver_cell != nullptr && driver_cell->type == id_GND) {
            NPNR_ASSERT(ni->name == id_GLOBAL_LOGIC0);
            net_out.setType(PhysicalNetlist::PhysNetlist::NetType::GND);
        } else if (driver_cell != nullptr && driver_cell->type == id_VCC) {
            NPNR_ASSERT(ni->name == id_GLOBAL_LOGIC1);
            net_out.setType(PhysicalNetlist::PhysNetlist::NetType::VCC);
        }
        net_out.setName(id2str(ni->name));

        // Init bel pin sources and sinks
        dict<WireId, BelPin> root_wires;
        if (driver_cell != nullptr && driver_cell->bel != BelId()) {
            for (IdString phys_pin : ctx->getBelPinsForCellPin(driver_cell, ni->driver.port)) {
                BelPin driver_pin;
                driver_pin.bel = driver_cell->bel;
                driver_pin.pin = phys_pin;
                WireId driver_wire = ctx->getBelPinWire(driver_cell->bel, phys_pin);
                if (driver_wire == WireId())
                    continue;
                root_wires[driver_wire] = driver_pin;
            }
        }
        for (const auto &usr : ni->users) {
            for (IdString phys_pin : ctx->getBelPinsForCellPin(usr.cell, usr.port)) {
                // These skipped because they are written as part of logic0
                if (ni->name == id_GLOBAL_LOGIC1 && inv_one_sinks.count(std::make_pair(usr.cell->bel, phys_pin)))
                    continue;
                BelPin sink_pin;
                sink_pin.bel = usr.cell->bel;
                sink_pin.pin = phys_pin;
                WireId sink_wire = ctx->getBelPinWire(usr.cell->bel, phys_pin);
                if (sink_wire == WireId())
                    continue;
                wire_sinks[sink_wire].push_back(sink_pin);
            }
        }

        if (ni->name == id_GLOBAL_LOGIC0) {
            // Add these as extra sinks
            for (auto sink : inv_one_sinks) {
                BelPin bp;
                bp.bel = sink.first;
                bp.pin = sink.second;
                WireId sink_wire = ctx->getBelPinWire(sink.first, sink.second);
                if (sink_wire == WireId())
                    continue;
                wire_sinks[sink_wire].push_back(bp);
            }
        } else if (ni->name == id_GLOBAL_LOGIC1) {
            for (PipId pip : inv_one_roots) {
                // Logic-1 routing ends at the inverting PIP
                const auto &pip_data = chip_pip_info(ctx->chip_info, pip);
                NPNR_ASSERT(pip_data.type == PipDataPOD::SITE_INTERNAL);
                BelPin bp;
                bp.bel = BelId(pip.tile, pip_data.site_pip.bel);
                bp.pin = IdString(chip_bel_info(ctx->chip_info, bp.bel).pins[pip_data.site_pip.from_pin].name);
                wire_sinks[ctx->getPipSrcWire(pip)].push_back(bp);
            }
        }

        // Init the set of pips and binding strengths
        for (const auto &wire_pair : ni->wires) {
            WireId dst = wire_pair.first;
            PipId pip = wire_pair.second.pip;
            if (ni->name == id_GLOBAL_LOGIC1 && inv_one_pips.count(pip))
                continue;
            pip_place_strength[pip] = wire_pair.second.strength;
            if (pip != PipId()) {
                seen_pips.emplace(pip);
                WireId uphill_wire = ctx->getPipSrcWire(pip);
                NPNR_ASSERT(dst != uphill_wire);
                pip_downhill[uphill_wire].push_back(pip);
            } else {
                // No driving pip, should be a source bel pin
                NPNR_ASSERT(root_wires.count(dst));
            }
        }

        if (ni->name == id_GLOBAL_LOGIC0) {
            for (PipId pip : inv_one_pips) {
                seen_pips.emplace(pip);
                WireId uphill_wire = ctx->getPipSrcWire(pip);
                pip_downhill[uphill_wire].push_back(pip);
                pip_place_strength[pip] = STRENGTH_STRONG;
            }
            for (PipId pip : inv_one_roots) {
                seen_pips.insert(pip);
                WireId uphill_wire = ctx->getPipSrcWire(pip);
                pip_downhill[uphill_wire].push_back(pip);
                pip_place_strength[pip] = STRENGTH_STRONG;
                root_pips.push_back(pip);
            }
        }

        std::vector<WireId> roots_to_remove;
        for (const auto &root_pair : root_wires) {
            WireId root = root_pair.first;
            BelId root_bel = root_pair.second.bel;
            if (ctx->getBelType(root_bel) != id_GND && ctx->getBelType(root_bel) != id_VCC)
                continue; // not a synthetic bel
            roots_to_remove.push_back(root);
            find_non_synthetic_edges(root);
        }
        // These synthetic roots bel pins have been replaced by concrete root pips in find_non_synthetic_edges
        for (WireId root : roots_to_remove)
            root_wires.erase(root);

        auto sources = net_out.initSources(root_wires.size() + root_pips.size());
        auto source_iter = sources.begin();

        for (const auto &root_pair : root_wires) {
            // Emit route tree corresponding to bel pin sources
            WireId root_wire = root_pair.first;
            BelPin src_bel_pin = root_pair.second;

            RouteBranchBuilder source_branch = *source_iter++;
            emit_bel_pin(source_branch, src_bel_pin.bel, src_bel_pin.pin);

            emit_net_segment(root_wire, source_branch);
        }
        for (const PipId root : root_pips) {
            // Emit route tree corresponding to PIP sources for constant nets that can start from these
            RouteBranchBuilder source_branch = *source_iter++;

            NPNR_ASSERT(seen_pips.erase(root) == 1);
            WireId root_wire = ctx->getPipDstWire(root);
            source_branch = emit_branch(root, source_branch);
            emit_net_segment(root_wire, source_branch);
        }
        // Any pips that were not part of a tree starting from the source are
        // stubs.
        std::vector<PipId> remaining_pips;
        for (PipId pip : seen_pips) {
            const auto &pip_data = chip_pip_info(ctx->chip_info, pip);
            if (pip_data.flags & PipDataPOD::FLAG_SYNTHETIC)
                continue; // ignore synthetic PIPs
            remaining_pips.push_back(pip);
        }
        auto stubs = net_out.initStubs(remaining_pips.size());
        auto stub_iter = stubs.begin();
        for (PipId pip : remaining_pips) {
            emit_branch(pip, *stub_iter++);
        }
    }

    void run(const std::string &filename)
    {
        ::capnp::MallocMessageBuilder message;
        PhysicalNetlist::PhysNetlist::Builder phys_netlist = message.initRoot<PhysicalNetlist::PhysNetlist>();

        // TODO: configurable speed and temp grade
        std::string part;
        if (ctx->family == ArchFamily::XC7) {
            part = stringf("%s%s-1", IdString(ctx->chip_info->name).c_str(ctx),
                           IdString(ctx->package_info->name).c_str(ctx));
        } else if (ctx->family == ArchFamily::VERSAL) {
            part = stringf("%s-%s-1MP-e-S", IdString(ctx->chip_info->name).c_str(ctx),
                           IdString(ctx->package_info->name).c_str(ctx));
        } else {
            part = stringf("%s-%s-1-e", IdString(ctx->chip_info->name).c_str(ctx),
                           IdString(ctx->package_info->name).c_str(ctx));
        }

        invertible_pins = get_invertible_pins(ctx);
        find_inverted_pips(ctx->nets.at(id_GLOBAL_LOGIC1).get()); // need to run this first

        phys_netlist.setPart(part);

        // Work out which cells are included in the physical netlist
        pool<IdString> placed_cells;
        for (const auto &cell_pair : ctx->cells) {
            const CellInfo *cell = cell_pair.second.get();
            if (cell->bel == BelId())
                continue;
            if (cell->type == id_GND || cell->type == id_VCC)
                continue;
            NPNR_ASSERT(placed_cells.emplace(cell->name).second);
        }

        std::vector<const CellInfo *> pad_cells;
        dict<IdString, IdString> site2variant;

        auto placements = phys_netlist.initPlacements(placed_cells.size());
        auto placement_iter = placements.begin();
        for (auto cell_name : placed_cells) {
            const CellInfo *ci = ctx->cells.at(cell_name).get();
            const auto &bel_data = chip_bel_info(ctx->chip_info, ci->bel);
            NPNR_ASSERT(bel_data.site != -1);
            IdString site_name = ctx->site_name(ci->bel.tile, bel_data.site);
            IdString site_variant = ctx->site_variant_name(ci->bel.tile, bel_data.site, bel_data.site_variant);

            auto result = site2variant.emplace(site_name, site_variant);
            if (!result.second) {
                NPNR_ASSERT(result.first->second == site_variant);
            }
            placement_iter->setCellName(id2str(cell_name));
            if (ci->type == id_PAD) {
                placement_iter->setType(strs.get_index("<PORT>"));
                pad_cells.push_back(ci);
            } else {
                placement_iter->setType(id2str(ci->type));
            }
            placement_iter->setSite(id2str(site_name));
            placement_iter->setBel(id2str(IdString(bel_data.name)));
            placement_iter->setIsBelFixed(ci->belStrength >= STRENGTH_FIXED);
            placement_iter->setIsSiteFixed(ci->belStrength >= STRENGTH_FIXED);

            if (ci->type != id_PAD) {
                // Write cell-bel pin map
                size_t mapping_count = 0;
                for (const auto &entry : ci->cell_bel_pins) {
                    if (entry.first == id__TIED_0 || entry.first == id__TIED_1)
                        continue;
                    mapping_count += entry.second.size();
                }
                auto pins = placement_iter->initPinMap(mapping_count);
                auto pin_iter = pins.begin();
                for (const auto &entry : ci->cell_bel_pins) {
                    if (entry.first == id__TIED_0 || entry.first == id__TIED_1)
                        continue;
                    for (auto phys : entry.second) {
                        pin_iter->setCellPin(id2str(entry.first));
                        pin_iter->setBel(id2str(IdString(bel_data.name)));
                        pin_iter->setBelPin(id2str(phys));
                        ++pin_iter;
                    }
                }
            }

            ++placement_iter;
        }

        // TODO: are PADs always the only kind of physical cell we ever need?
        auto phys_cells = phys_netlist.initPhysCells(pad_cells.size());
        auto phys_cells_iter = phys_cells.begin();
        for (const CellInfo *pad : pad_cells) {
            auto phys_cell = *phys_cells_iter++;
            phys_cell.setCellName(id2str(pad->name));
            phys_cell.setPhysType(PhysicalNetlist::PhysNetlist::PhysCellType::PORT);
        }

        // Write nets
        auto nets = phys_netlist.initPhysNets(ctx->nets.size());
        auto net_iter = nets.begin();
        for (auto &net_pair : ctx->nets) {
            emit_net(*net_iter, net_pair.second.get());
            ++net_iter;
        }

        // Write site variant config
        auto site_instances = phys_netlist.initSiteInsts(site2variant.size());
        auto site_inst_iter = site_instances.begin();
        for (auto site_pair : site2variant) {
            site_inst_iter->setSite(id2str(site_pair.first));
            site_inst_iter->setType(id2str(site_pair.second));
            ++site_inst_iter;
        }

        // Write string pool
        auto str_list = phys_netlist.initStrList(strs.strings.size());
        for (size_t i = 0; i < strs.strings.size(); ++i) {
            str_list.set(i, strs.strings[i]);
        }

        write_message(message, filename);
    }
};

} // namespace

void Arch::write_logical(const std::string &filename)
{
    LogicalNetlistWriter writer(getCtx());
    writer.run(filename);
}

void Arch::write_physical(const std::string &filename)
{
    PhysicalNetlistWriter writer(getCtx());
    writer.run(filename);
}

NEXTPNR_NAMESPACE_END

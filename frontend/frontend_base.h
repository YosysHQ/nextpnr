/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  gatecat <gatecat@ds0.me>
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

/*
 * Generic Frontend Framework
 *
 * This is designed to make it possible to build frontends for parsing any format isomorphic to Yosys JSON [1]
 * with maximal inlining and minimal need for overhead such as runtime polymorphism or extra wrapper types.
 *
 * [1] https://yosyshq.net/yosys/cmd_write_json.html
 *
 * The frontend should implement a class referred to as FrontendType that defines the following type(def)s and
 * functions:
 *
 * Types:
 *   ModuleDataType: corresponds to a single entry in "modules"
 *   ModulePortDataType: corresponds to a single entry in "ports" of a module
 *   CellDataType: corresponds to a single entry in "cells"
 *   NetnameDataType: corresponds to a single entry in "netnames"
 *   BitVectorDataType: corresponds to a signal/constant bit vector (e.g. a "connections" field)
 *
 * Functions:
 *
 *   void foreach_module(Func) const;
 *       calls Func(const std::string &name, const ModuleDataType &mod);
 *       for each module in the netlist
 *
 *   void foreach_port(const ModuleDataType &mod, Func) const;
 *       calls Func(const std::string &name, const ModulePortDataType &port);
 *       for each port of mod
 *
 *   void foreach_cell(const ModuleDataType &mod, Func) const;
 *       calls Func(const std::string &name, const CellDataType &cell)
 *       for each cell of mod
 *
 *   void foreach_netname(const ModuleDataType &mod, Func) const;
 *       calls Func(const std::string &name, const NetnameDataType &cell);
 *       for each netname entry of mod
 *
 *   PortType get_port_dir(const ModulePortDataType &port) const;
 *       gets the PortType direction of a module port
 *
 *   int get_array_offset(const ModulePortDataType &port) const;
 *       gets the start bit number of a port or netname entry
 *
 *   bool is_array_upto(const ModulePortDataType &port) const;
 *       returns true if a port/net is an "upto" type port or netname entry
 *
 *   const BitVectorDataType &get_port_bits(const ModulePortDataType &port) const;
 *       gets the bit vector of a module port
 *
 *   const std::string& get_cell_type(const CellDataType &cell) const;
 *       gets the type of a cell
 *
 *   void foreach_attr(const {ModuleDataType|CellDataType|ModulePortDataType|NetnameDataType} &obj, Func) const;
 *       calls Func(const std::string &name, const Property &value);
 *       for each attribute on a module, cell, module port or net
 *
 *   void foreach_param(const CellDataType &obj, Func) const;
 *       calls Func(const std::string &name, const Property &value);
 *       for each parameter of a cell
 *
 *   void foreach_setting(const ModuleDataType &obj, Func) const;
 *       calls Func(const std::string &name, const Property &value);
 *       for each module-level setting
 *
 *   void foreach_port_dir(const CellDataType &cell, Func) const;
 *       calls Func(const std::string &name, PortType dir);
 *       for each port direction of a cell
 *
 *   void foreach_port_conn(const CellDataType &cell, Func) const;
 *       calls Func(const std::string &name, const BitVectorDataType &conn);
 *       for each port connection of a cell
 *
 *   const BitVectorDataType &get_net_bits(const NetnameDataType &net) const;
 *       gets the BitVector corresponding to the bits entry of a netname field
 *
 *   int get_vector_length(const BitVectorDataType &bits) const;
 *       gets the length of a BitVector
 *
 *   bool is_vector_bit_constant(const BitVectorDataType &bits, int i) const;
 *       returns true if bit <i> of bits is constant
 *
 *   char get_vector_bit_constval(const BitVectorDataType &bits, int i) const;
 *       returns a char [01xz] corresponding to the constant value of bit <i>
 *
 *   int get_vector_bit_signal(const BitVectorDataType &bits, int i) const;
 *       returns the signal number of vector bit <i>
 *
 */

#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

namespace {

// Used for hierarchy resolution
struct ModuleInfo
{
    bool is_top = false, is_blackbox = false, is_whitebox = false;
    inline bool is_box() const { return is_blackbox || is_whitebox; }
    pool<IdString> instantiated_celltypes;
};

template <typename FrontendType> struct GenericFrontend
{
    GenericFrontend(Context *ctx, const FrontendType &impl, bool split_io) : ctx(ctx), impl(impl), split_io(split_io) {}
    void operator()()
    {
        // Find which module is top
        find_top_module();
        HierModuleState m;
        m.is_toplevel = true;
        m.prefix = "";
        m.path = top;
        ctx->top_module = top;
        // Do the actual import, starting from the top level module
        import_module(m, top.str(ctx), top.str(ctx), mod_refs.at(top.str(ctx)));

        ctx->design_loaded = true;
    }

    Context *ctx;
    const FrontendType &impl;
    const bool split_io;
    using mod_dat_t = typename FrontendType::ModuleDataType;
    using mod_port_dat_t = typename FrontendType::ModulePortDataType;
    using cell_dat_t = typename FrontendType::CellDataType;
    using netname_dat_t = typename FrontendType::NetnameDataType;
    using bitvector_t = typename FrontendType::BitVectorDataType;

    dict<IdString, ModuleInfo> mods;
    std::unordered_map<std::string, const mod_dat_t> mod_refs;
    IdString top;

    // Process the list of modules and determine
    // the top module
    void find_top_module()
    {
        impl.foreach_module([&](const std::string &name, const mod_dat_t &mod) {
            IdString mod_id = ctx->id(name);
            auto &mi = mods[mod_id];
            mod_refs.emplace(name, mod);
            impl.foreach_attr(mod, [&](const std::string &name, const Property &value) {
                if (name == "top")
                    mi.is_top = (value.intval != 0);
                else if (name == "blackbox")
                    mi.is_blackbox = (value.intval != 0);
                else if (name == "whitebox")
                    mi.is_whitebox = (value.intval != 0);
            });
            impl.foreach_cell(mod, [&](const std::string &name, const cell_dat_t &cell) {
                mi.instantiated_celltypes.insert(ctx->id(impl.get_cell_type(cell)));
            });
        });
        // First of all, see if a top module has been manually specified
        if (ctx->settings.count(ctx->id("frontend/top"))) {
            IdString user_top = ctx->id(ctx->settings.at(ctx->id("frontend/top")).as_string());
            if (!mods.count(user_top))
                log_error("Top module '%s' not found!\n", ctx->nameOf(user_top));
            top = user_top;
            return;
        }
        // If not, look for a module with the top attribute set
        IdString top_by_attr;
        for (auto &mod : mods) {
            if (mod.second.is_top && !mod.second.is_box()) {
                if (top_by_attr != IdString())
                    log_error("Found multiple modules with (* top *) set (including %s and %s).\n",
                              ctx->nameOf(top_by_attr), ctx->nameOf(mod.first));
                top_by_attr = mod.first;
            }
        }
        if (top_by_attr != IdString()) {
            top = top_by_attr;
            return;
        }
        // Finally, attempt to autodetect the top module using hierarchy
        // (a module that is not a box and is not used as a cell by any other module)
        pool<IdString> candidate_top;
        for (auto &mod : mods)
            if (!mod.second.is_box())
                candidate_top.insert(mod.first);
        for (auto &mod : mods)
            for (auto &c : mod.second.instantiated_celltypes)
                candidate_top.erase(c);
        if (candidate_top.size() != 1) {
            if (candidate_top.size() == 0)
                log_info("No candidate top level modules.\n");
            else
                for (auto ctp : candidate_top)
                    log_info("Candidate top module: '%s'\n", ctx->nameOf(ctp));
            log_error("Failed to autodetect top module, please specify using --top.\n");
        }
        top = *(candidate_top.begin());
    }

    // Create a unique name (guaranteed collision free) for a net or a cell; based on
    // a base name and suffix. __unique__i will be be appended with increasing i
    // if a collision is found until no collision
    IdString unique_name(const std::string &base, const std::string &suffix, bool is_net)
    {
        IdString name;
        int incr = 0;
        do {
            std::string comb = base + suffix;
            if (incr > 0) {
                comb += "__unique__";
                comb += std::to_string(incr);
            }
            name = ctx->id(comb);
            incr++;
        } while (is_net ? (ctx->nets.count(name) || ctx->net_aliases.count(name)) : ctx->cells.count(name));
        return name;
    }

    // A flat index of map; designed to cope with merging nets where pointers to nets would go stale
    // A net's udata points into this index
    std::vector<NetInfo *> net_flatindex;
    std::vector<std::vector<int>> net_old_indices; // the other indices of a net in net_flatindex for merging

    // This structure contains some structures specific to the import of a module at
    // a certain point in the hierarchy
    struct HierModuleState
    {
        bool is_toplevel;
        std::string prefix;
        IdString parent_path, path;
        // Map from index in module to "flat" index of nets
        std::vector<int> index_to_net_flatindex;
        // Get a reference to index_to_net; resizing if
        // appropriate
        int &net_by_idx(int idx)
        {
            NPNR_ASSERT(idx >= 0);
            if (idx >= int(index_to_net_flatindex.size()))
                index_to_net_flatindex.resize(idx + 1, -1);
            return index_to_net_flatindex.at(idx);
        }
        dict<IdString, std::vector<int>> port_to_bus;
        // All of the names given to a net
        std::vector<std::vector<std::string>> net_names;
    };

    void import_module(HierModuleState &m, const std::string &name, const std::string &type, const mod_dat_t &data)
    {
        NPNR_ASSERT(!ctx->hierarchy.count(m.path));
        ctx->hierarchy[m.path].name = ctx->id(name);
        ctx->hierarchy[m.path].type = ctx->id(type);
        ctx->hierarchy[m.path].parent = m.parent_path;
        ctx->hierarchy[m.path].fullpath = m.path;

        std::vector<NetInfo *> index_to_net;
        if (!m.is_toplevel) {
            // Import port connections; for submodules only
            import_port_connections(m, data);
        } else {
            // Just create a list of ports for netname resolution
            impl.foreach_port(data,
                              [&](const std::string &name, const mod_port_dat_t &) { m.port_to_bus[ctx->id(name)]; });
            // Import module-level attributes
            impl.foreach_attr(
                    data, [&](const std::string &name, const Property &value) { ctx->attrs[ctx->id(name)] = value; });
            // Import settings
            impl.foreach_setting(data, [&](const std::string &name, const Property &value) {
                ctx->settings[ctx->id(name)] = value;
            });
        }
        import_module_netnames(m, data);
        import_module_cells(m, data);
        import_net_attrs(m, data);
        if (m.is_toplevel) {
            import_toplevel_ports(m, data);
            // Mark design as loaded through nextpnr
            ctx->settings[ctx->id("synth")] = 1;
            // Process nextpnr-specific attributes
            ctx->attributesToArchInfo();
        }
    }

    // Multiple labels might refer to the same net. Resolve conflicts for the primary name thus:
    //  - (toplevel) ports are always preferred
    //  - names with fewer $ are always prefered
    //  - between equal $ counts, fewer .s are prefered
    //  - ties are resolved alphabetically
    bool prefer_netlabel(HierModuleState &m, const std::string &a, const std::string &b)
    {
        if (m.port_to_bus.count(ctx->id(a)))
            return true;
        if (m.port_to_bus.count(ctx->id(b)))
            return false;

        if (b.empty())
            return true;
        long a_dollars = std::count(a.begin(), a.end(), '$'), b_dollars = std::count(b.begin(), b.end(), '$');
        if (a_dollars < b_dollars)
            return true;
        else if (a_dollars > b_dollars)
            return false;
        long a_dots = std::count(a.begin(), a.end(), '.'), b_dots = std::count(b.begin(), b.end(), '.');
        if (a_dots < b_dots)
            return true;
        else if (a_dots > b_dots)
            return false;
        return a < b;
    };

    // Get a net by index in modulestate (not flatindex); creating it if it doesn't already exist
    NetInfo *create_or_get_net(HierModuleState &m, int idx)
    {
        auto &midx = m.net_by_idx(idx);
        if (midx != -1) {
            return net_flatindex.at(midx);
        } else {
            std::string name;
            if (idx < int(m.net_names.size()) && !m.net_names.at(idx).empty()) {
                // Use the rule above to find the preferred name for a net
                name = m.net_names.at(idx).at(0);
                for (size_t j = 1; j < m.net_names.at(idx).size(); j++)
                    if (prefer_netlabel(m, m.net_names.at(idx).at(j), name))
                        name = m.net_names.at(idx).at(j);
            } else {
                name = "$frontend$" + std::to_string(idx);
            }
            NetInfo *net = ctx->createNet(unique_name(m.prefix, name, true));
            // Add to the flat index of nets
            net->udata = int(net_flatindex.size());
            net_flatindex.push_back(net);
            net_old_indices.emplace_back();
            // Add to the module-level index of netsd
            midx = net->udata;
            // Create aliases for all possible names
            if (idx < int(m.net_names.size()) && !m.net_names.at(idx).empty()) {
                for (const auto &name : m.net_names.at(idx)) {
                    IdString name_id = ctx->id(name);
                    net->aliases.push_back(name_id);
                    ctx->net_aliases[name_id] = net->name;
                }
            } else {
                net->aliases.push_back(net->name);
                ctx->net_aliases[net->name] = net->name;
            }
            return net;
        }
    }

    // Get the name of a vector bit given basename; settings and index
    std::string get_bit_name(const std::string &base, int index, int length, int offset = 0, bool upto = false)
    {
        std::string port = base;
        if (length == 1 && offset == 0)
            return port;
        int real_index;
        if (upto)
            real_index = offset + length - index - 1; // reversed ports like [0:7]
        else
            real_index = offset + index; // normal 'downto' ports like [7:0]
        port += '[';
        port += std::to_string(real_index);
        port += ']';
        return port;
    }

    // Import the netnames section of a module
    void import_module_netnames(HierModuleState &m, const mod_dat_t &data)
    {
        impl.foreach_netname(data, [&](const std::string &basename, const netname_dat_t &nn) {
            bool upto = impl.is_array_upto(nn);
            int offset = impl.get_array_offset(nn);
            const auto &bits = impl.get_net_bits(nn);
            int width = impl.get_vector_length(bits);
            for (int i = 0; i < width; i++) {
                if (impl.is_vector_bit_constant(bits, i))
                    continue;

                std::string bit_name = get_bit_name(basename, i, width, offset, upto);

                int net_bit = impl.get_vector_bit_signal(bits, i);
                int mapped_bit = m.net_by_idx(net_bit);
                if (mapped_bit == -1) {
                    // Net doesn't exist yet. Add the name here to the list of candidate names so we have that for when
                    // we create it later
                    if (net_bit >= int(m.net_names.size()))
                        m.net_names.resize(net_bit + 1);
                    m.net_names.at(net_bit).push_back(bit_name);
                } else {
                    // Net already exists; add this name as an alias
                    NetInfo *ni = net_flatindex.at(mapped_bit);
                    IdString alias_name = ctx->id(m.prefix + bit_name);
                    if (ctx->net_aliases.count(alias_name))
                        continue; // don't add duplicate aliases
                    ctx->net_aliases[alias_name] = ni->name;
                    ni->aliases.push_back(alias_name);
                }
            }
        });
    }

    void import_net_attrs(HierModuleState &m, const mod_dat_t &data)
    {
        impl.foreach_netname(data, [&](const std::string &basename, const netname_dat_t &nn) {
            const auto &bits = impl.get_net_bits(nn);
            int width = impl.get_vector_length(bits);
            for (int i = 0; i < width; i++) {
                if (impl.is_vector_bit_constant(bits, i))
                    continue;
                int net_bit = impl.get_vector_bit_signal(bits, i);
                int mapped_bit = m.net_by_idx(net_bit);
                if (mapped_bit != -1) {
                    NetInfo *ni = net_flatindex.at(mapped_bit);
                    impl.foreach_attr(nn, [&](const std::string &name, const Property &value) {
                        ni->attrs[ctx->id(name)] = value;
                    });
                }
            }
        });
    }

    // Create a new constant net; given a hint for what the name should be and its value
    NetInfo *create_constant_net(HierModuleState &m, const std::string &name_hint, char constval)
    {
        IdString name = unique_name(m.prefix, name_hint, true);
        NetInfo *ni = ctx->createNet(name);
        add_constant_driver(m, ni, constval);
        return ni;
    }

    // Import a leaf cell - (white|black)box
    void import_leaf_cell(HierModuleState &m, const std::string &name, const cell_dat_t &cd)
    {
        auto cell_type = impl.get_cell_type(cd);
        if (cell_type == "$scopeinfo" || cell_type == "$print" || cell_type == "$check")
            return;
        IdString inst_name = unique_name(m.prefix, name, false);
        ctx->hierarchy[m.path].leaf_cells_by_gname[inst_name] = ctx->id(name);
        ctx->hierarchy[m.path].leaf_cells[ctx->id(name)] = inst_name;
        CellInfo *ci = ctx->createCell(inst_name, ctx->id(cell_type));
        ci->hierpath = m.path;
        // Import port directions
        dict<IdString, PortType> port_dirs;
        impl.foreach_port_dir(cd, [&](const std::string &port, PortType dir) { port_dirs[ctx->id(port)] = dir; });
        // Import port connectivity
        impl.foreach_port_conn(cd, [&](const std::string &name, const bitvector_t &bits) {
            if (!port_dirs.count(ctx->id(name)))
                log_error("Failed to get direction for port '%s' of cell '%s'\n", name.c_str(), inst_name.c_str(ctx));
            PortType dir = port_dirs.at(ctx->id(name));
            int width = impl.get_vector_length(bits);
            for (int i = 0; i < width; i++) {
                std::string port_bit_name = get_bit_name(name, i, width);
                IdString port_bit_ids = ctx->id(port_bit_name);
                // Create cell port
                ci->ports[port_bit_ids].name = port_bit_ids;
                ci->ports[port_bit_ids].type = dir;
                // Resolve connectivity
                NetInfo *net;
                if (impl.is_vector_bit_constant(bits, i)) {
                    // Create a constant driver if one is needed
                    net = create_constant_net(m, inst_name.str(ctx) + "." + port_bit_name + "$const",
                                              impl.get_vector_bit_constval(bits, i));
                } else {
                    // Otherwise, lookup (creating if needed) the net with this index
                    net = create_or_get_net(m, impl.get_vector_bit_signal(bits, i));
                }
                NPNR_ASSERT(net != nullptr);

                // Check for multiple drivers
                if (dir == PORT_OUT && net->driver.cell != nullptr)
                    log_error("Net '%s' is multiply driven by cell ports %s.%s and %s.%s\n", ctx->nameOf(net),
                              ctx->nameOf(net->driver.cell), ctx->nameOf(net->driver.port), ctx->nameOf(inst_name),
                              port_bit_name.c_str());
                ci->connectPort(port_bit_ids, net);
            }
        });
        // Import attributes and parameters
        impl.foreach_attr(cd,
                          [&](const std::string &name, const Property &value) { ci->attrs[ctx->id(name)] = value; });
        impl.foreach_param(cd,
                           [&](const std::string &name, const Property &value) { ci->params[ctx->id(name)] = value; });
    }

    // Import a submodule cell
    void import_submodule_cell(HierModuleState &m, const std::string &name, const cell_dat_t &cd)
    {
        HierModuleState submod;
        submod.is_toplevel = false;
        // Create mapping from submodule port to nets (referenced by index in flatindex)
        impl.foreach_port_conn(cd, [&](const std::string &name, const bitvector_t &bits) {
            int width = impl.get_vector_length(bits);
            for (int i = 0; i < width; i++) {
                // Index of port net in flatindex
                int net_ref = -1;
                if (impl.is_vector_bit_constant(bits, i)) {
                    // Create a constant driver if one is needed
                    std::string port_bit_name = get_bit_name(name, i, width);
                    NetInfo *cnet = create_constant_net(m, name + "." + port_bit_name + "$const",
                                                        impl.get_vector_bit_constval(bits, i));
                    cnet->udata = int(net_flatindex.size());
                    net_flatindex.push_back(cnet);
                    net_old_indices.emplace_back();
                    net_ref = cnet->udata;
                } else {
                    // Otherwise, lookup (creating if needed) the net with given in-module index
                    net_ref = create_or_get_net(m, impl.get_vector_bit_signal(bits, i))->udata;
                }
                NPNR_ASSERT(net_ref != -1);
                submod.port_to_bus[ctx->id(name)].push_back(net_ref);
            }
        });
        // Create prefix for submodule
        submod.prefix = m.prefix;
        submod.prefix += name;
        submod.prefix += '.';
        submod.parent_path = m.path;
        submod.path = ctx->id(m.path.str(ctx) + "/" + name);
        ctx->hierarchy[m.path].hier_cells[ctx->id(name)] = submod.path;
        // Do the submodule import
        auto type = impl.get_cell_type(cd);
        import_module(submod, name, type, mod_refs.at(type));
    }

    // Import the cells section of a module
    void import_module_cells(HierModuleState &m, const mod_dat_t &data)
    {
        impl.foreach_cell(data, [&](const std::string &cellname, const cell_dat_t &cd) {
            IdString type = ctx->id(impl.get_cell_type(cd));
            if (mods.count(type) && !mods.at(type).is_box()) {
                // Module type is known; and not boxed. Import as a submodule by flattening hierarchy
                import_submodule_cell(m, cellname, cd);
            } else {
                // Module type is unknown or boxes. Import as a leaf cell (nextpnr CellInfo)
                import_leaf_cell(m, cellname, cd);
            }
        });
    }

    // Create a top level input/output buffer
    CellInfo *create_iobuf(NetInfo *net, PortType dir, const std::string &name)
    {
        // Skip IOBUF insertion if this is a design checkpoint (where they will already exist)
        if (ctx->settings.count(ctx->id("synth")))
            return nullptr;
        IdString name_id = ctx->id(name);
        if (ctx->cells.count(name_id))
            log_error("Cell '%s' of type '%s' with the same name as a top-level IO is not allowed.\n", name.c_str(),
                      ctx->cells.at(name_id)->type.c_str(ctx));
        CellInfo *iobuf = ctx->createCell(name_id, ctx->id("unknown_iob"));
        // Copy attributes from net to IOB
        for (auto &attr : net->attrs)
            iobuf->attrs[attr.first] = attr.second;
        // What we do now depends on port type
        if (dir == PORT_IN) {
            iobuf->type = ctx->id("$nextpnr_ibuf");
            iobuf->addOutput(ctx->id("O"));
            if (net->driver.cell != nullptr) {
                CellInfo *drv = net->driver.cell;
                if (drv->type != ctx->id("$nextpnr_iobuf"))
                    log_error("Net '%s' is multiply driven by cell port %s.%s and top level input '%s'.\n",
                              ctx->nameOf(net), ctx->nameOf(drv), ctx->nameOf(net->driver.port), name.c_str());
                // Special case: input, etc, directly drives inout
                // Use the input net of the inout instead
                net = drv->ports.at(ctx->id("I")).net;
            }
            NPNR_ASSERT(net->driver.cell == nullptr);
            // Connect IBUF output and net
            iobuf->connectPort(ctx->id("O"), net);
        } else if (dir == PORT_OUT) {
            iobuf->type = ctx->id("$nextpnr_obuf");
            iobuf->addInput(ctx->id("I"));
            // Connect IBUF input and net
            iobuf->connectPort(ctx->id("I"), net);
        } else if (dir == PORT_INOUT) {
            iobuf->type = ctx->id("$nextpnr_iobuf");

            if (split_io) {
                iobuf->addInput(ctx->id("I"));
                iobuf->addOutput(ctx->id("O"));
                // Need to bifurcate the net to avoid multiple drivers and split
                // the input/output parts of an inout
                // Create a new net connecting only the current net's driver and the IOBUF input
                // Then use the IOBUF output to drive all of the current net's users
                NetInfo *split_iobuf_i = ctx->createNet(unique_name("", "$" + name + "$iobuf_i", true));
                auto drv = net->driver;
                if (drv.cell != nullptr) {
                    drv.cell->disconnectPort(drv.port);
                    drv.cell->ports[drv.port].net = nullptr;
                    drv.cell->connectPort(drv.port, split_iobuf_i);
                }
                iobuf->connectPort(ctx->id("I"), split_iobuf_i);
                NPNR_ASSERT(net->driver.cell == nullptr);
                iobuf->connectPort(ctx->id("O"), net);
            } else {
                iobuf->addInout(ctx->id("IO"));
                iobuf->connectPort(ctx->id("IO"), net);
            }
        }

        PortInfo pinfo;
        pinfo.name = name_id;
        pinfo.net = net;
        pinfo.type = dir;
        ctx->ports[pinfo.name] = pinfo;
        ctx->port_cells[pinfo.name] = iobuf;

        return iobuf;
    }

    // Import ports of the top level module
    void import_toplevel_ports(HierModuleState &m, const mod_dat_t &data)
    {
        // For correct handling of inout ports driving other ports
        // first import non-inouts then import inouts so that they bifurcate correctly
        for (bool inout : {false, true}) {
            impl.foreach_port(data, [&](const std::string &portname, const mod_port_dat_t &pd) {
                const auto &port_bv = impl.get_port_bits(pd);
                int offset = impl.get_array_offset(pd);
                bool is_upto = impl.is_array_upto(pd);
                int width = impl.get_vector_length(port_bv);
                PortType dir = impl.get_port_dir(pd);
                if ((dir == PORT_INOUT) != inout)
                    return;
                for (int i = 0; i < width; i++) {
                    std::string pbit_name = get_bit_name(portname, i, width, offset, is_upto);
                    NetInfo *port_net = nullptr;
                    if (impl.is_vector_bit_constant(port_bv, i)) {
                        // Port bit is constant. Need to create a new constant net.
                        port_net =
                                create_constant_net(m, pbit_name + "$const", impl.get_vector_bit_constval(port_bv, i));
                    } else {
                        // Port bit is a signal. Need to create/get the associated net
                        port_net = create_or_get_net(m, impl.get_vector_bit_signal(port_bv, i));
                    }
                    create_iobuf(port_net, dir, pbit_name);
                }
            });
        }
    }

    // Add a constant-driving VCC or GND cell to make a net constant
    // (constval can be [01xz], x and z or no-ops)
    int const_autoidx = 0;
    void add_constant_driver(HierModuleState &m, NetInfo *net, char constval)
    {

        if (constval == 'x' || constval == 'z')
            return; // 'x' or 'z' is the same as undriven
        NPNR_ASSERT(constval == '0' || constval == '1');
        IdString cell_name = unique_name(
                m.prefix, net->name.str(ctx) + (constval == '1' ? "$VCC$" : "$GND$") + std::to_string(const_autoidx++),
                false);
        CellInfo *cc = ctx->createCell(cell_name, ctx->id(constval == '1' ? "VCC" : "GND"));
        cc->ports[ctx->id("Y")].name = ctx->id("Y");
        cc->ports[ctx->id("Y")].type = PORT_OUT;
        if (net->driver.cell != nullptr)
            log_error("Net '%s' is multiply driven by port %s.%s and constant '%c'\n", ctx->nameOf(net),
                      ctx->nameOf(net->driver.cell), ctx->nameOf(net->driver.port), constval);
        cc->connectPort(ctx->id("Y"), net);
    }

    // Merge two nets - e.g. if one net in a submodule bifurcates to two output bits and therefore two different
    // parent nets
    void merge_nets(NetInfo *base, NetInfo *mergee)
    {
        // Resolve drivers
        if (mergee->driver.cell != nullptr) {
            if (base->driver.cell != nullptr)
                log_error("Attempting to merge nets '%s' and '%s' due to port connectivity; but this would result in a "
                          "multiply driven net\n",
                          ctx->nameOf(base), ctx->nameOf(mergee));
            else {
                mergee->driver.cell->ports[mergee->driver.port].net = base;
                base->driver = mergee->driver;
            }
        }
        // Combine users
        for (auto &usr : mergee->users) {
            usr.cell->ports[usr.port].net = base;
            usr.cell->ports[usr.port].user_idx = base->users.add(usr);
        }
        // Point aliases to the new net
        for (IdString alias : mergee->aliases) {
            ctx->net_aliases[alias] = base->name;
            base->aliases.push_back(alias);
        }
        // Create a new alias from mergee's name to new base name
        ctx->net_aliases[mergee->name] = base->name;
        // Update flat index of nets
        for (auto old_idx : net_old_indices.at(mergee->udata)) {
            net_old_indices.at(base->udata).push_back(old_idx);
            net_flatindex.at(old_idx) = base;
        }
        net_old_indices.at(base->udata).push_back(mergee->udata);
        net_flatindex.at(mergee->udata) = base;
        net_old_indices.at(mergee->udata).clear();
        // Remove merged net from context
        ctx->nets.erase(mergee->name);
    }

    // Import connections between a submodule and its parent
    void import_port_connections(HierModuleState &m, const mod_dat_t &data)
    {
        impl.foreach_port(data, [&](const std::string &name, const mod_port_dat_t &port) {
            // CHECK: should disconnected module inputs really just be skipped; or is it better
            // to insert a ground driver?
            if (!m.port_to_bus.count(ctx->id(name)))
                return;
            auto &p2b = m.port_to_bus.at(ctx->id(name));
            // Get direction and vector of port bits
            PortType dir = impl.get_port_dir(port);
            const auto &bv = impl.get_port_bits(port);
            int bv_size = impl.get_vector_length(bv);
            // Iterate over bits of port; making connections
            for (int i = 0; i < std::min<int>(bv_size, p2b.size()); i++) {
                int conn_net = p2b.at(i);
                if (conn_net == -1)
                    continue;
                NetInfo *conn_ni = net_flatindex.at(conn_net);
                NPNR_ASSERT(conn_ni != nullptr);
                if (impl.is_vector_bit_constant(bv, i)) {
                    // It is a constant, we might need to insert a constant driver here to drive the corresponding
                    // net in the parent
                    char constval = impl.get_vector_bit_constval(bv, i);
                    // Inputs cannot be driving a constant back to the parent
                    if (dir == PORT_IN)
                        log_error("Input port %s%s[%d] cannot be driving a constant '%c'.\n", m.prefix.c_str(),
                                  name.c_str(), i, constval);
                    // Insert the constant driver
                    add_constant_driver(m, conn_ni, constval);
                } else {
                    // If not driving a constant; simply make the port bit net index in the submodule correspond
                    // to connected net in the parent module
                    int &submod_net = m.net_by_idx(impl.get_vector_bit_signal(bv, i));
                    if (submod_net == -1) {
                        // A net at this index doesn't yet exist
                        // We can simply set this index to point to the net in the parent
                        submod_net = conn_net;
                    } else {
                        // A net at this index already exists (this would usually be a submodule net
                        // connected to more than one I/O port)
                        merge_nets(net_flatindex.at(submod_net), net_flatindex.at(conn_net));
                    }
                }
            }
        });
    }
};
} // namespace

NEXTPNR_NAMESPACE_END

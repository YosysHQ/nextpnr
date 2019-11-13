/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  David Shah <dave@ds0.me>
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
 * [1] http://www.clifford.at/yosys/cmd_write_json.html
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
 *   void foreach_module(Func);
 *       calls Func(const std::string &name, const ModuleDataType &mod);
 *       for each module in the netlist
 *
 *   void foreach_port(const ModuleDataType &mod, Func);
 *       calls Func(const std::string &name, const ModulePortDataType &port);
 *       for each port of mod
 *
 *   void foreach_cell(const ModuleDataType &mod, Func);
 *       calls Func(const std::string &name, const CellDataType &cell);
 *       for each cell of mod
 *
 *   void foreach_netname(const ModuleDataType &mod, Func);
 *       calls Func(const std::string &name, const NetnameDataType &cell);
 *       for each netname entry of mod
 *
 *   PortType get_port_dir(const ModulePortDataType &port);
 *       gets the PortType direction of a module port
 *
 *   int get_port_offset(const ModulePortDataType &port);
 *       gets the start bit number of a port
 *
 *   bool is_port_upto(const ModulePortDataType &port);
 *       returns true if a port is an "upto" type port
 *
 *   const BitVectorDataType &get_port_bits(const ModulePortDataType &port);
 *       gets the bit vector of a module port
 *
 *   const std::string& get_cell_type(const CellDataType &cell);
 *       gets the type of a cell
 *
 *   void foreach_attr(const {ModuleDataType|CellDataType|ModulePortDataType|NetnameDataType} &obj, Func);
 *       calls Func(const std::string &name, const Property &value);
 *       for each attribute on a module, cell, module port or net
 *
 *   void foreach_param(const CellDataType &obj, Func);
 *       calls Func(const std::string &name, const Property &value);
 *       for each parameter of a cell
 *
 *   void foreach_port_dir(const CellDataType &cell, Func);
 *       calls Func(const std::string &name, PortType dir);
 *       for each port direction of a cell
 *
 *   void foreach_port_conn(const CellDataType &cell, Func);
 *       calls Func(const std::string &name, const BitVectorDataType &conn);
 *       for each port connection of a cell
 *
 *   const BitVectorDataType &get_net_bits(const NetnameDataType &net);
 *       gets the BitVector corresponding to the bits entry of a netname field
 *
 *   int get_vector_length(const BitVectorDataType &bits);
 *       gets the length of a BitVector
 *
 *   bool is_vector_bit_constant(const BitVectorDataType &bits, int i);
 *       returns true if bit <i> of bits is constant
 *
 *   char get_vector_bit_constval(const BitVectorDataType &bits, int i);
 *       returns a char [01xz] corresponding to the constant value of bit <i>
 *
 *   int get_vector_bit_signal(const BitVectorDataType &bits, int i);
 *       returns the signal number of vector bit <i>
 *
 */

#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
NEXTPNR_NAMESPACE_BEGIN

namespace {

template <typename FrontendType> struct GenericFrontend
{
    GenericFrontend(Context *ctx, const FrontendType &impl) : ctx(ctx), impl(impl) {}
    Context *ctx;
    const FrontendType &impl;
    using mod_dat_t = typename FrontendType::ModuleDataType;
    using mod_port_dat_t = typename FrontendType::ModulePortDataType;
    using cell_dat_t = typename FrontendType::CellDataType;
    using netname_dat_t = typename FrontendType::NetnameDataType;
    using bitvector_t = typename FrontendType::BitVectorDataType;

    // Used for hierarchy resolution
    struct ModuleInfo
    {
        const mod_dat_t *mod_data;
        bool is_top = false, is_blackbox = false, is_whitebox = false;
        inline bool is_box() const { return is_blackbox || is_whitebox; }
        std::unordered_set<IdString> instantiated_celltypes;
    };
    std::unordered_map<IdString, ModuleInfo> mods;
    IdString top;

    // Process the list of modules and determine
    // the top module
    void find_top_module()
    {
        impl.foreach_module([&](const std::string &name, const mod_dat_t &mod) {
            IdString mod_id = ctx->id(name);
            auto &mi = mods[mod_id];
            mi.mod_data = &mod;
            impl.foreach_attr(mod, [&](const std::string &name, const Property &value) {
                if (name == "top")
                    mi.is_top = (value.intval != 0);
                else if (name == "blackbox")
                    mi.is_blackbox = (value.intval != 0);
                else if (name == "whitebox")
                    mi.is_whitebox = (value.intval != 0);
            });
            impl.foreach_cell(mod, [&](const std::string &name, const cell_dat_t &cell) {
                mi.instantiated_cells.insert(ctx->id(impl.get_cell_type(cell)));
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
        std::unordered_set<IdString> candidate_top;
        for (auto &mod : mods)
            if (!mod.second.is_box())
                candidate_top.insert(mod.first);
        for (auto &mod : mods)
            for (auto &c : mod.second.instantiated_celltypes)
                candidate_top.erase(c);
        if (candidate_top.size() != 1)
            log_error("Failed to autodetect top module, please specify using --top.\n");
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
        } while (is_net ? ctx->nets.count(name) : ctx->cells.count(name));
        return name;
    }

    // A flat index of map; designed to cope with renaming
    // A net's udata points into this index
    std::vector<NetInfo *> net_flatindex;

    // This structure contains some structures specific to the import of a module at
    // a certain point in the hierarchy
    struct HierModuleState
    {
        bool is_toplevel;
        std::string prefix;
        // Map from index in module to "flat" index of nets
        std::vector<NetInfo *> index_to_net_flatindex;
        // Get a reference to index_to_net; resizing if
        // appropriate
        NetInfo *&net_by_idx(int idx)
        {
            NPNR_ASSERT(idx >= 0);
            if (idx >= int(index_to_net_flatindex.size()))
                index_to_net_flatindex.resize(idx + 1, nullptr);
            return index_to_net_flatindex.at(idx);
        }
        std::unordered_map<IdString, std::vector<NetInfo *>> port_to_bus;
    };

    void import_module(HierModuleState &m, mod_dat_t *data)
    {
        std::vector<NetInfo *> index_to_net;
        // Import port connections; for submodules only
        if (!m.is_toplevel) {
            import_port_connections(m, data);
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
        connect_port(ctx, net, cc, ctx->id("Y"));
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
            base->users.push_back(usr);
        }
        // Point aliases to the new net
        for (IdString alias : mergee->aliases) {
            ctx->net_aliases[alias] = base->name;
            base->aliases.push_back(alias);
        }
        // Create a new alias from mergee's name to new base name
        ctx->net_aliases[mergee->name] = base->name;
        // Update flat index of nets
        net_flatindex.at(mergee->udata) = base;
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
                NetInfo *conn_net = p2b.at(i);
                if (conn_net == nullptr)
                    continue;
                if (impl.is_vector_bit_constant(bv, i)) {
                    // It is a constant, we might need to insert a constant driver here to drive the corresponding
                    // net in the parent
                    char constval = impl.get_vector_bit_constval(bv, i);
                    // Inputs cannot be driving a constant back to the parent
                    if (dir == PORT_IN)
                        log_error("Input port %s%s[%d] cannot be driving a constant '%c'.\n", m.prefix.c_str(),
                                  port.c_str(), i, constval);
                    // Insert the constant driver
                    add_constant_driver(m, conn_net, constval);
                } else {
                    // If not driving a constant; simply make the port bit net index in the submodule correspond
                    // to connected net in the parent module
                }
            }
        });
    }
};
} // namespace

template <typename FrontendType> void run_frontend(Context *ctx, const FrontendType &impl) {}

NEXTPNR_NAMESPACE_END
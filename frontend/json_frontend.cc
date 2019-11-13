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

#include "json_frontend.h"
#include "frontend_base.h"
#include "json11.hpp"
#include "log.h"
#include "nextpnr.h"

#include <streambuf>

NEXTPNR_NAMESPACE_BEGIN

using namespace json11;

struct JsonFrontendImpl
{
    // See specification in frontend_base.h
    JsonFrontendImpl(Json &root) : root(root){};
    Json &root;
    typedef const Json &ModuleDataType;
    typedef const Json &ModulePortDataType;
    typedef const Json &CellDataType;
    typedef const Json &NetnameDataType;
    typedef const Json::array &BitVectorDataType;

    template <typename TFunc> void foreach_module(TFunc Func)
    {
        for (const auto &mod : root.object_items())
            Func(mod.first, mod.second);
    }

    template <typename TFunc> void foreach_port(const ModuleDataType &mod, TFunc Func)
    {
        const auto &ports = mod["ports"];
        if (ports.is_null())
            return;
        for (const auto &port : ports.object_items())
            Func(port.first, port.second);
    }

    template <typename TFunc> void foreach_cell(const ModuleDataType &mod, TFunc Func)
    {
        const auto &cells = mod["cells"];
        if (cells.is_null())
            return;
        for (const auto &cell : cells.object_items())
            Func(cell.first, cell.second);
    }

    template <typename TFunc> void foreach_netname(const ModuleDataType &mod, TFunc Func)
    {
        const auto &netnames = mod["netnames"];
        if (netnames.is_null())
            return;
        for (const auto &netname : netnames.object_items())
            Func(netname.first, netname.second);
    }

    PortType lookup_portdir(const std::string &dir)
    {
        if (dir == "input")
            return PORT_IN;
        else if (dir == "inout")
            return PORT_INOUT;
        else if (dir == "output")
            return PORT_OUT;
        else
            NPNR_ASSERT_FALSE("invalid json port direction");
    }

    PortType get_port_dir(const ModulePortDataType &port) { return lookup_portdir(port["direction"].string_value()); }

    int get_array_offset(const Json &obj)
    {
        auto offset = obj["offset"];
        return offset.is_null() ? 0 : offset.int_value();
    }

    bool is_array_upto(const Json &obj)
    {
        auto upto = obj["upto"];
        return upto.is_null() ? false : bool(upto.int_value());
    }

    const BitVectorDataType &get_port_bits(const ModulePortDataType &port) { return port["bits"].array_items(); }

    const std::string &get_cell_type(const CellDataType &cell) { return cell["type"].string_value(); }

    Property parse_property(const Json &val)
    {
        if (val.is_number())
            return Property(val.int_value(), 32);
        else
            return Property::from_string(val.string_value());
    }

    template <typename TFunc> void foreach_attr(const Json &obj, TFunc Func)
    {
        const auto &attrs = obj["attributes"];
        if (attrs.is_null())
            return;
        for (const auto &attr : attrs.object_items()) {
            Func(attr.first, parse_property(attr.second));
        }
    }

    template <typename TFunc> void foreach_param(const Json &obj, TFunc Func)
    {
        const auto &params = obj["parameters"];
        if (params.is_null())
            return;
        for (const auto &param : params.object_items()) {
            Func(param.first, parse_property(param.second));
        }
    }

    template <typename TFunc> void foreach_port_dir(const CellDataType &cell, TFunc Func)
    {
        for (const auto &pdir : cell["port_directions"].object_items())
            Func(pdir.first, lookup_portdir(pdir.second.string_value()));
    }

    template <typename TFunc> void foreach_port_conn(const CellDataType &cell, TFunc Func)
    {
        for (const auto &pconn : cell["connections"].object_items())
            Func(pconn.first, pconn.second);
    }

    template <typename TFunc> const BitVectorDataType &get_net_bits(const NetnameDataType &net)
    {
        return net["bits"].array_items();
    }

    int get_vector_length(const BitVectorDataType &bits) { return int(bits.size()); }

    bool is_vector_bit_constant(const BitVectorDataType &bits, int i)
    {
        NPNR_ASSERT(i < int(bits.size()));
        return bits[i].is_string();
    }

    char get_vector_bit_constval(const BitVectorDataType &bits, int i)
    {
        auto s = bits.at(i).string_value();
        NPNR_ASSERT(s.size() == 1);
        return s.at(0);
    }

    int get_vector_bit_signal(const BitVectorDataType &bits, int i)
    {
        NPNR_ASSERT(bits.at(i).is_number());
        return bits.at(i).int_value();
    }
};

bool parse_json(std::istream &in, const std::string &filename, Context *ctx)
{
    Json root;
    {
        if (!in)
            log_error("Failed to open JSON file '%s'.\n", filename.c_str());
        std::string json_str((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string error;
        root = Json::parse(json_str, error, JsonParse::COMMENTS);
        if (root.is_null())
            log_error("Failed to parse JSON file '%s': %s.\n", filename.c_str(), error.c_str());
    }
    run_frontend(ctx, JsonFrontendImpl(root));
    return true;
}

NEXTPNR_NAMESPACE_END
/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  SymbioticEDA
 *
 *  jsonparse.cc -- liberally copied from the yosys file of the same name by
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#include "jsonparse.h"
#include <assert.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <log.h>
#include <map>
#include <string>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

extern bool check_all_nets_driven(Context *ctx);

namespace JsonParser {

const bool json_debug = false;

typedef std::string string;

template <typename T> int GetSize(const T &obj) { return obj.size(); }

struct JsonNode
{
    char type; // S=String, N=Number, A=Array, D=Dict
    string data_string;
    int data_number;
    std::vector<JsonNode *> data_array;
    std::map<string, JsonNode *> data_dict;
    std::vector<string> data_dict_keys;

    JsonNode(std::istream &f, int &lineno)
    {
        type = 0;
        data_number = 0;

        while (1) {
            int ch = f.get();

            if (ch == EOF)
                log_error("Unexpected EOF in JSON file.\n");

            if (ch == '\n')
                lineno++;
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
                continue;

            if (ch == '\"') {
                type = 'S';

                while (1) {
                    ch = f.get();

                    if (ch == EOF)
                        log_error("Unexpected EOF in JSON string.\n");

                    if (ch == '\"')
                        break;

                    if (ch == '\\') {
                        int ch = f.get();

                        if (ch == EOF)
                            log_error("Unexpected EOF in JSON string.\n");
                    }

                    data_string += ch;
                }

                break;
            }

            if (('0' <= ch && ch <= '9') || ('-' == ch)) {
                type = 'N';
                if (ch == '-')
                    data_number = 0;
                else
                    data_number = ch - '0';
                data_string += ch;

                while (1) {
                    ch = f.get();

                    if (ch == EOF)
                        break;

                    if (ch == '.')
                        goto parse_real;

                    if (ch < '0' || '9' < ch) {
                        f.unget();
                        break;
                    }

                    data_number = data_number * 10 + (ch - '0');
                    data_string += ch;
                }

                if (data_string[0] == '-')
                    data_number = -data_number;
                data_string = "";
                break;

            parse_real:
                type = 'S';
                data_number = 0;
                data_string += ch;

                while (1) {
                    ch = f.get();

                    if (ch == EOF)
                        break;

                    if (ch < '0' || '9' < ch) {
                        f.unget();
                        break;
                    }

                    data_string += ch;
                }

                break;
            }

            if (ch == '[') {
                type = 'A';

                while (1) {
                    ch = f.get();

                    if (ch == EOF)
                        log_error("Unexpected EOF in JSON file.\n");

                    if (ch == '\n')
                        lineno++;
                    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',')
                        continue;

                    if (ch == ']')
                        break;

                    f.unget();
                    data_array.push_back(new JsonNode(f, lineno));
                }

                break;
            }

            if (ch == '{') {
                type = 'D';

                while (1) {
                    ch = f.get();

                    if (ch == EOF)
                        log_error("Unexpected EOF in JSON file.\n");

                    if (ch == '\n')
                        lineno++;
                    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',')
                        continue;

                    if (ch == '}')
                        break;

                    f.unget();
                    JsonNode key(f, lineno);

                    while (1) {
                        ch = f.get();

                        if (ch == EOF)
                            log_error("Unexpected EOF in JSON file.\n");

                        if (ch == '\n')
                            lineno++;
                        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ':')
                            continue;

                        f.unget();
                        break;
                    }

                    JsonNode *value = new JsonNode(f, lineno);

                    if (key.type != 'S')
                        log_error("Unexpected non-string key in JSON dict, line %d.\n", lineno);

                    data_dict[key.data_string] = value;
                    data_dict_keys.push_back(key.data_string);
                }

                break;
            }

            log_error("Unexpected character in JSON file, line %d: '%c'\n", lineno, ch);
        }
    }

    ~JsonNode()
    {
        for (auto it : data_array)
            delete it;
        for (auto &it : data_dict)
            delete it.second;
    }
};

void ground_net(Context *ctx, NetInfo *net)
{
    std::unique_ptr<CellInfo> cell = std::unique_ptr<CellInfo>(new CellInfo);
    PortInfo port_info;
    PortRef port_ref;

    cell->name = ctx->id(net->name.str(ctx) + ".GND");
    cell->type = ctx->id("GND");

    port_info.name = ctx->id(cell->name.str(ctx) + "[]");
    port_info.net = net;
    port_info.type = PORT_OUT;

    port_ref.cell = cell.get();
    port_ref.port = port_info.name;

    net->driver = port_ref;

    cell->ports[port_info.name] = port_info;

    ctx->cells[cell->name] = std::move(cell);
}

void vcc_net(Context *ctx, NetInfo *net)
{
    std::unique_ptr<CellInfo> cell = std::unique_ptr<CellInfo>(new CellInfo);
    PortInfo port_info;
    PortRef port_ref;

    cell->name = ctx->id(net->name.str(ctx) + ".VCC");
    cell->type = ctx->id("VCC");

    port_info.name = ctx->id(cell->name.str(ctx) + "[]");
    port_info.net = net;
    port_info.type = PORT_OUT;

    port_ref.cell = cell.get();
    port_ref.port = port_info.name;

    net->driver = port_ref;

    cell->ports[port_info.name] = port_info;

    ctx->cells[cell->name] = std::move(cell);
}

//
// is_blackbox
//
// Checks the JsonNode for an attributes dictionary, with a "blackbox" entry.
// An item is deemed to be a blackbox if this entry exists and if its
// value is not zero.  If the item is a black box, this routine will return
// true, false otherwise
bool is_blackbox(JsonNode *node)
{
    JsonNode *attr_node, *bbox_node;

    if (node->data_dict.count("attributes") == 0)
        return false;
    attr_node = node->data_dict.at("attributes");
    if (attr_node == NULL)
        return false;
    if (attr_node->type != 'D')
        return false;
    if (GetSize(attr_node->data_dict) == 0)
        return false;
    if (attr_node->data_dict.count("blackbox") == 0)
        return false;
    bbox_node = attr_node->data_dict.at("blackbox");
    if (bbox_node == NULL)
        return false;
    if (bbox_node->type != 'N')
        log_error("JSON module blackbox is not a number\n");
    if (bbox_node->data_number == 0)
        return false;
    return true;
}

void json_import_cell_params(Context *ctx, string &modname, CellInfo *cell, JsonNode *param_node,
                             std::unordered_map<IdString, std::string> *dest, int param_id)
{
    //
    JsonNode *param;
    IdString pId;
    //
    param = param_node->data_dict.at(param_node->data_dict_keys[param_id]);

    pId = ctx->id(param_node->data_dict_keys[param_id]);
    if (param->type == 'N') {
        (*dest)[pId] = std::to_string(param->data_number);
    } else if (param->type == 'S')
        (*dest)[pId] = param->data_string;
    else
        log_error("JSON parameter type of \"%s\' of cell \'%s\' not supported\n", pId.c_str(ctx),
                  cell->name.c_str(ctx));

    if (json_debug)
        log_info("    Added parameter \'%s\'=%s to cell \'%s\' "
                 "of module \'%s\'\n",
                 pId.c_str(ctx), cell->params[pId].c_str(), cell->name.c_str(ctx), modname.c_str());
}

static int const_net_idx = 0;

template <typename F>
void json_import_ports(Context *ctx, const string &modname, const std::vector<IdString> &netnames,
                       const string &obj_name, const string &port_name, JsonNode *dir_node, JsonNode *wire_group_node,
                       F visitor)
{
    // Examine a port of a cell or the design. For every bit of the port,
    // the connected net will be processed and `visitor` will be called
    // with (PortType dir, std::string name, NetInfo *net)
    assert(dir_node);

    if (json_debug)
        log_info("    Examining port %s, node %s\n", port_name.c_str(), obj_name.c_str());

    if (!wire_group_node)
        log_error("JSON no connection match "
                  "for port_direction \'%s\' of node \'%s\' "
                  "in module \'%s\'\n",
                  port_name.c_str(), obj_name.c_str(), modname.c_str());

    assert(wire_group_node);

    assert(dir_node->type == 'S');
    assert(wire_group_node->type == 'A');

    PortInfo port_info;

    port_info.name = ctx->id(port_name);
    if (dir_node->data_string.compare("input") == 0)
        port_info.type = PORT_IN;
    else if (dir_node->data_string.compare("output") == 0)
        port_info.type = PORT_OUT;
    else if (dir_node->data_string.compare("inout") == 0)
        port_info.type = PORT_INOUT;
    else
        log_error("JSON unknown port direction \'%s\' in node \'%s\' "
                  "of module \'%s\'\n",
                  dir_node->data_string.c_str(), obj_name.c_str(), modname.c_str());
    //
    // Find an update, or create a net to connect
    // to this port.
    //
    NetInfo *this_net = nullptr;
    bool is_bus;

    //
    // If this port references a bus, then there will be multiple nets
    // connected to it, all specified as part of an array.
    //
    is_bus = (wire_group_node->data_array.size() > 1);

    // Now loop through all of the connections to this port.
    if (wire_group_node->data_array.size() == 0) {
        //
        // There is/are no connections to this port.
        //
        // Create the port, but leave the net NULL

        visitor(port_info.type, port_info.name.str(ctx), nullptr);

        if (json_debug)
            log_info("      Port \'%s\' has no connection in \'%s\'\n", port_info.name.c_str(ctx), obj_name.c_str());

    } else
        for (int index = 0; index < int(wire_group_node->data_array.size()); index++) {
            //
            JsonNode *wire_node;
            PortInfo this_port;
            IdString net_id;
            //
            wire_node = wire_group_node->data_array[index];
            //
            // Pick a name for this port
            if (is_bus)
                this_port.name = ctx->id(port_info.name.str(ctx) + "[" + std::to_string(index) + "]");
            else
                this_port.name = port_info.name;
            this_port.type = port_info.type;

            if (wire_node->type == 'N') {
                int net_num;

                // A simple net, specified by a number
                net_num = wire_node->data_number;
                if (net_num < int(netnames.size()))
                    net_id = netnames.at(net_num);
                else
                    net_id = ctx->id(std::to_string(net_num));
                if (ctx->nets.count(net_id) == 0) {
                    // The net doesn't exist in the design (yet)
                    // Create in now

                    if (json_debug)
                        log_info("      Generating a new net, \'%d\'\n", net_num);

                    std::unique_ptr<NetInfo> net = std::unique_ptr<NetInfo>(new NetInfo());
                    net->name = net_id;
                    net->driver.cell = NULL;
                    net->driver.port = IdString();
                    ctx->nets[net_id] = std::move(net);

                    this_net = ctx->nets[net_id].get();
                } else {
                    //
                    // The net already exists within the design.
                    // We'll connect to it
                    //
                    this_net = ctx->nets[net_id].get();
                    if (json_debug)
                        log_info("      Reusing net \'%s\', id \'%s\', "
                                 "with driver \'%s\'\n",
                                 this_net->name.c_str(ctx), net_id.c_str(ctx),
                                 (this_net->driver.cell != NULL) ? this_net->driver.port.c_str(ctx) : "NULL");
                }

            } else if (wire_node->type == 'S') {
                // Strings are only used to drive wires for the fixed
                // values "0", "1", and "x".  Handle those constant
                // values here.
                //
                // Constants always get their own new net
                std::unique_ptr<NetInfo> net = std::unique_ptr<NetInfo>(new NetInfo());
                net->name = ctx->id("$const_" + std::to_string(const_net_idx++));

                if (wire_node->data_string.compare(string("0")) == 0) {

                    if (json_debug)
                        log_info("      Generating a constant "
                                 "zero net\n");
                    ground_net(ctx, net.get());

                } else if (wire_node->data_string.compare(string("1")) == 0) {

                    if (json_debug)
                        log_info("      Generating a constant "
                                 "one  net\n");
                    vcc_net(ctx, net.get());

                } else if (wire_node->data_string.compare(string("x")) == 0) {
                    ground_net(ctx, net.get());
                } else
                    log_error("      Unknown fixed type wire node "
                              "value, \'%s\'\n",
                              wire_node->data_string.c_str());
                IdString n = net->name;
                ctx->nets[net->name] = std::move(net);
                this_net = ctx->nets[n].get();
            }

            if (json_debug)
                log_info("    Inserting port \'%s\' into cell \'%s\'\n", this_port.name.c_str(ctx), obj_name.c_str());
            visitor(this_port.type, this_port.name.str(ctx), this_net);
        }
}

void json_import_cell(Context *ctx, string modname, const std::vector<IdString> &netnames, JsonNode *cell_node,
                      string cell_name)
{
    JsonNode *cell_type, *param_node, *attr_node;

    cell_type = cell_node->data_dict.at("type");
    if (cell_type == NULL)
        return;

    std::unique_ptr<CellInfo> cell = std::unique_ptr<CellInfo>(new CellInfo);

    cell->name = ctx->id(cell_name);
    assert(cell_type->type == 'S');
    cell->type = ctx->id(cell_type->data_string);
    // No BEL assignment here/yet

    if (json_debug)
        log_info("  Processing %s $ %s\n", modname.c_str(), cell->name.c_str(ctx));

    param_node = cell_node->data_dict.at("parameters");
    if (param_node->type != 'D')
        log_error("JSON parameter list of \'%s\' is not a data dictionary\n", cell->name.c_str(ctx));

    //
    // Loop through all parameters, adding them into the
    // design to annotate the cell
    //
    for (int paramid = 0; paramid < GetSize(param_node->data_dict_keys); paramid++) {

        json_import_cell_params(ctx, modname, cell.get(), param_node, &cell->params, paramid);
    }

    attr_node = cell_node->data_dict.at("attributes");
    if (attr_node->type != 'D')
        log_error("JSON attribute list of \'%s\' is not a data dictionary\n", cell->name.c_str(ctx));

    //
    // Loop through all attributes, adding them into the
    // design to annotate the cell
    //
    for (int attrid = 0; attrid < GetSize(attr_node->data_dict_keys); attrid++) {

        json_import_cell_params(ctx, modname, cell.get(), attr_node, &cell->attrs, attrid);
    }

    //
    // Now connect the ports of this module.  The ports are defined by
    // both the port directions node as well as the connections node.
    // Both should contain dictionaries having the same keys.
    //

    JsonNode *pdir_node = NULL;
    if (cell_node->data_dict.count("port_directions") > 0) {

        pdir_node = cell_node->data_dict.at("port_directions");
        if (pdir_node->type != 'D')
            log_error("JSON port_directions node of \'%s\' "
                      "in module \'%s\' is not a "
                      "dictionary\n",
                      cell->name.c_str(ctx), modname.c_str());

    } else if (cell_node->data_dict.count("ports") > 0) {
        pdir_node = cell_node->data_dict.at("ports");
        if (pdir_node->type != 'D')
            log_error("JSON ports node of \'%s\' "
                      "in module \'%s\' is not a "
                      "dictionary\n",
                      cell->name.c_str(ctx), modname.c_str());
    }

    JsonNode *connections = cell_node->data_dict.at("connections");
    if (connections->type != 'D')
        log_error("JSON connections node of \'%s\' "
                  "in module \'%s\' is not a "
                  "dictionary\n",
                  cell->name.c_str(ctx), modname.c_str());

    if (GetSize(pdir_node->data_dict_keys) != GetSize(connections->data_dict_keys))
        log_error("JSON number of connections doesnt "
                  "match number of ports in node \'%s\' "
                  "of module \'%s\'\n",
                  cell->name.c_str(ctx), modname.c_str());

    //
    // Loop through all of the ports of this logic element
    //
    for (int portid = 0; portid < GetSize(pdir_node->data_dict_keys); portid++) {
        //
        string port_name;
        JsonNode *dir_node, *wire_group_node;
        //

        port_name = pdir_node->data_dict_keys[portid];
        dir_node = pdir_node->data_dict.at(port_name);
        wire_group_node = connections->data_dict.at(port_name);

        json_import_ports(ctx, modname, netnames, cell->name.str(ctx), port_name, dir_node, wire_group_node,
                          [&cell, ctx](PortType type, const std::string &name, NetInfo *net) {
                              cell->ports[ctx->id(name)] = PortInfo{ctx->id(name), net, type};
                              PortRef pr;
                              pr.cell = cell.get();
                              pr.port = ctx->id(name);
                              if (net != nullptr) {
                                  if (type == PORT_IN || type == PORT_INOUT) {
                                      net->users.push_back(pr);
                                  } else if (type == PORT_OUT) {
                                      if (net->driver.cell != nullptr)
                                          log_error("multiple drivers on net '%s' (%s.%s and %s.%s)\n",
                                                    net->name.c_str(ctx), net->driver.cell->name.c_str(ctx),
                                                    net->driver.port.c_str(ctx), pr.cell->name.c_str(ctx),
                                                    pr.port.c_str(ctx));
                                      net->driver = pr;
                                  }
                              }
                          });
    }

    ctx->cells[cell->name] = std::move(cell);
    // check_all_nets_driven(ctx);
}

static void insert_iobuf(Context *ctx, NetInfo *net, PortType type, const string &name)
{
    // Instantiate a architecture-independent IO buffer connected to a given
    // net, of a given type, and named after the IO port.
    //
    // During packing, this generic IO buffer will be converted to an
    // architecure primitive.
    //
    std::unique_ptr<CellInfo> iobuf = std::unique_ptr<CellInfo>(new CellInfo());
    iobuf->name = ctx->id(name);
    std::copy(net->attrs.begin(), net->attrs.end(), std::inserter(iobuf->attrs, iobuf->attrs.begin()));
    if (type == PORT_IN) {
        if (ctx->verbose)
            log_info("processing input port %s\n", name.c_str());
        iobuf->type = ctx->id("$nextpnr_ibuf");
        iobuf->ports[ctx->id("O")] = PortInfo{ctx->id("O"), net, PORT_OUT};
        // Special case: input, etc, directly drives inout
        if (net->driver.cell != nullptr) {
            if (net->driver.cell->type != ctx->id("$nextpnr_iobuf"))
                log_error("Top-level input '%s' also driven by %s.%s.\n", name.c_str(),
                          net->driver.cell->name.c_str(ctx), net->driver.port.c_str(ctx));
            net = net->driver.cell->ports.at(ctx->id("I")).net;
        }
        assert(net->driver.cell == nullptr);
        net->driver.port = ctx->id("O");
        net->driver.cell = iobuf.get();
    } else if (type == PORT_OUT) {
        if (ctx->verbose)
            log_info("processing output port %s\n", name.c_str());
        iobuf->type = ctx->id("$nextpnr_obuf");
        iobuf->ports[ctx->id("I")] = PortInfo{ctx->id("I"), net, PORT_IN};
        PortRef ref;
        ref.cell = iobuf.get();
        ref.port = ctx->id("I");
        net->users.push_back(ref);
    } else if (type == PORT_INOUT) {
        if (ctx->verbose)
            log_info("processing inout port %s\n", name.c_str());
        iobuf->type = ctx->id("$nextpnr_iobuf");
        iobuf->ports[ctx->id("I")] = PortInfo{ctx->id("I"), nullptr, PORT_IN};

        // Split the input and output nets for bidir ports
        std::unique_ptr<NetInfo> net2 = std::unique_ptr<NetInfo>(new NetInfo());
        net2->name = ctx->id("$" + net->name.str(ctx) + "$iobuf_i");
        net2->driver = net->driver;
        if (net->driver.cell != nullptr) {
            net2->driver.cell->ports[net2->driver.port].net = net2.get();
            net->driver.cell = nullptr;
        }
        iobuf->ports[ctx->id("I")].net = net2.get();
        PortRef ref;
        ref.cell = iobuf.get();
        ref.port = ctx->id("I");
        net2->users.push_back(ref);
        ctx->nets[net2->name] = std::move(net2);

        iobuf->ports[ctx->id("O")] = PortInfo{ctx->id("O"), net, PORT_OUT};
        assert(net->driver.cell == nullptr);
        net->driver.port = ctx->id("O");
        net->driver.cell = iobuf.get();
    } else {
        assert(false);
    }
    ctx->cells[iobuf->name] = std::move(iobuf);
}

void json_import_toplevel_port(Context *ctx, const string &modname, const std::vector<IdString> &netnames,
                               const string &portname, JsonNode *node)
{
    JsonNode *dir_node = node->data_dict.at("direction");
    JsonNode *nets_node = node->data_dict.at("bits");
    json_import_ports(
            ctx, modname, netnames, "Top Level IO", portname, dir_node, nets_node,
            [ctx](PortType type, const std::string &name, NetInfo *net) { insert_iobuf(ctx, net, type, name); });
}

void json_import(Context *ctx, string modname, JsonNode *node)
{
    if (is_blackbox(node))
        return;

    log_info("Importing module %s\n", modname.c_str());

    // Multiple labels might refer to the same net. For now we resolve conflicts thus:
    //  - names with fewer $ are always prefered
    //  - between equal $ counts, fewer .s are prefered
    //  - ties are resolved alphabetically
    auto prefer_netlabel = [](const std::string &a, const std::string &b) {
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

    // Import netnames
    std::vector<std::string> netlabels;
    if (node->data_dict.count("netnames")) {
        JsonNode *cell_parent = node->data_dict.at("netnames");
        for (int nnid = 0; nnid < GetSize(cell_parent->data_dict_keys); nnid++) {
            JsonNode *here;

            here = cell_parent->data_dict.at(cell_parent->data_dict_keys[nnid]);
            std::string basename = cell_parent->data_dict_keys[nnid];
            if (here->data_dict.count("bits")) {
                JsonNode *bits = here->data_dict.at("bits");
                assert(bits->type == 'A');
                size_t num_bits = bits->data_array.size();
                for (size_t i = 0; i < num_bits; i++) {
                    int netid = bits->data_array.at(i)->data_number;
                    if (netid >= int(netlabels.size()))
                        netlabels.resize(netid + 1);
                    std::string name =
                            basename + (num_bits == 1 ? "" : std::string("[") + std::to_string(i) + std::string("]"));
                    if (prefer_netlabel(name, netlabels.at(netid)))
                        netlabels.at(netid) = name;
                }
            }
        }
    }
    std::vector<IdString> netids;
    std::transform(netlabels.begin(), netlabels.end(), std::back_inserter(netids),
                   [ctx](const std::string &s) { return ctx->id(s); });
    if (node->data_dict.count("cells")) {
        JsonNode *cell_parent = node->data_dict.at("cells");
        //
        //
        // Loop through all of the logic elements in a flattened design
        //
        //
        for (int cellid = 0; cellid < GetSize(cell_parent->data_dict_keys); cellid++) {
            JsonNode *here = cell_parent->data_dict.at(cell_parent->data_dict_keys[cellid]);
            json_import_cell(ctx, modname, netids, here, cell_parent->data_dict_keys[cellid]);
        }
    }

    if (node->data_dict.count("ports")) {
        JsonNode *ports_parent = node->data_dict.at("ports");

        // N.B. ports must be imported after cells for tristate behaviour
        // to be correct
        // Loop through all ports
        for (int portid = 0; portid < GetSize(ports_parent->data_dict_keys); portid++) {
            JsonNode *here;

            here = ports_parent->data_dict.at(ports_parent->data_dict_keys[portid]);
            json_import_toplevel_port(ctx, modname, netids, ports_parent->data_dict_keys[portid], here);
        }
    }
    check_all_nets_driven(ctx);
}
}; // End Namespace JsonParser

bool parse_json_file(std::istream &f, std::string &filename, Context *ctx)
{
    try {
        using namespace JsonParser;

        if (!f)
            log_error("failed to open JSON file.\n");

        int lineno = 1;

        JsonNode root(f, lineno);

        if (root.type != 'D')
            log_error("JSON root node is not a dictionary.\n");

        if (root.data_dict.count("modules") != 0) {
            JsonNode *modules = root.data_dict.at("modules");

            if (modules->type != 'D')
                log_error("JSON modules node is not a dictionary.\n");

            for (auto &it : modules->data_dict)
                json_import(ctx, it.first, it.second);
        }

        log_info("Checksum: 0x%08x\n", ctx->checksum());
        log_break();
        ctx->settings.emplace(ctx->id("input/json"), filename);
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  SymbioticEDA
 *
 *  jsonparse.cc -- liberally copied from the yosys file of the same name by
 *
 *  Copyright (C) 2012  Clifford Wolf <clifford@clifford.at>
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

#include <string>
#include <iostream>
#include <fstream>
#include <assert.h>
#include <log.h>
#include "design.h"
#include "chip.h"
#include "jsonparse.h"

extern	bool	check_all_nets_driven(Design *design);

namespace JsonParser {

	const	bool	json_debug = false;

	typedef	std::string string;

	template<typename T> int GetSize(const T &obj) { return obj.size(); }


struct JsonNode
{
	char			type; // S=String, N=Number, A=Array, D=Dict
	string			data_string;
	int			data_number;
	vector<JsonNode*>	data_array;
	dict<string, JsonNode*>	data_dict;
	vector<string>		data_dict_keys;

	JsonNode(std::istream &f)
	{
		type = 0;
		data_number = 0;

		while (1)
		{
			int ch = f.get();

			if (ch == EOF)
				log_error("Unexpected EOF in JSON file.\n");

			if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
				continue;

			if (ch == '\"')
			{
				type = 'S';

				while (1)
				{
					ch = f.get();

					if (ch == EOF)
						log_error("Unexpected EOF "
							"in JSON string.\n");

					if (ch == '\"')
						break;

					if (ch == '\\') {
						int ch = f.get();

						if (ch == EOF)
						    log_error("Unexpected EOF "
							"in JSON string.\n");
					}

					data_string += ch;
				}

				break;
			}

			if ('0' <= ch && ch <= '9')
			{
				type = 'N';
				data_number = ch - '0';
				data_string += ch;

				while (1)
				{
					ch = f.get();

					if (ch == EOF)
						break;

					if (ch == '.')
						goto parse_real;

					if (ch < '0' || '9' < ch) {
						f.unget();
						break;
					}

					data_number = data_number*10
								+ (ch - '0');
					data_string += ch;
				}

				data_string = "";
				break;

			parse_real:
				type = 'S';
				data_number = 0;
				data_string += ch;

				while (1)
				{
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

			if (ch == '[')
			{
				type = 'A';

				while (1)
				{
					ch = f.get();

					if (ch == EOF)
						log_error("Unexpected EOF "
							"in JSON file.\n");

					if (ch == ' ' || ch == '\t'
							|| ch == '\r'
							|| ch == '\n'
							|| ch == ',')
						continue;

					if (ch == ']')
						break;

					f.unget();
					data_array.push_back(new JsonNode(f));
				}

				break;
			}

			if (ch == '{')
			{
				type = 'D';

				while (1)
				{
					ch = f.get();

					if (ch == EOF)
						log_error("Unexpected EOF "
							"in JSON file.\n");

					if (ch == ' ' || ch == '\t'
							|| ch == '\r'
							|| ch == '\n'
							|| ch == ',')
						continue;

					if (ch == '}')
						break;

					f.unget();
					JsonNode key(f);

					while (1)
					{
						ch = f.get();

						if (ch == EOF)
						   log_error("Unexpected EOF "
							"in JSON file.\n");

						if (ch == ' ' || ch == '\t'
								|| ch == '\r'
								|| ch == '\n'
								|| ch == ':')
							continue;

						f.unget();
						break;
					}

					JsonNode *value = new JsonNode(f);

					if (key.type != 'S')
						log_error("Unexpected "
							"non-string key "
							"in JSON dict.\n");

					data_dict[key.data_string] = value;
					data_dict_keys.push_back(
							key.data_string);
				}

				break;
			}

			log_error("Unexpected character in JSON file: '%c'\n",
				ch);
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


NetInfo	*ground_net(NetInfo *net) {
	CellInfo	*cell = new CellInfo;
	PortInfo	port_info;
	PortRef		port_ref;

	cell->name = string(net->name + ".GND");
	cell->type = string("GND");

	port_info.name = cell->name + "[]";
	port_info.net  = net;
	port_info.type = PORT_OUT;

	port_ref.cell = cell;
	port_ref.port = port_info.name;

	net->driver = port_ref;

	cell->ports[port_info.name] = port_info;

	return net;
}

NetInfo	*vcc_net(NetInfo *net) {
	CellInfo	*cell = new CellInfo;
	PortInfo	port_info;
	PortRef		port_ref;

	cell->name = string(net->name + ".VCC");
	cell->type = string("VCC");

	port_info.name = cell->name + "[]";
	port_info.net  = net;
	port_info.type = PORT_OUT;

	port_ref.cell = cell;
	port_ref.port = port_info.name;

	net->driver = port_ref;

	cell->ports[port_info.name] = port_info;

	return net;
}

NetInfo	*floating_net(NetInfo *net) {
	PortInfo	port_info;
	PortRef		port_ref;

	port_info.name = net->name + ".floating";
	port_info.net  = net;
	port_info.type = PORT_OUT;

	port_ref.cell = NULL;
	port_ref.port = port_info.name;

	net->driver = port_ref;

	return net;
}

//
// is_blackbox
//
// Checks the JsonNode for an attributes dictionary, with a "blackbox" entry.
// An item is deemed to be a blackbox if this entry exists and if its
// value is not zero.  If the item is a black box, this routine will return
// true, false otherwise
bool	is_blackbox(JsonNode *node) {
	JsonNode	*attr_node, *bbox_node;

	if (node->data_dict.count("attributes")==0)
		return false;
	attr_node = node->data_dict.at("attributes");
	if (attr_node == NULL)
		return false;
	if (attr_node->type != 'D')
		return false;
	if (GetSize(attr_node->data_dict)==0)
		return false;
	if (attr_node->data_dict.count("blackbox") == 0)
		return false;
	bbox_node = attr_node->data_dict.at("blackbox");
	if (bbox_node == NULL)
		return false;
	if (bbox_node->type != 'N')
		log_error("JSON module blackbox is not a number\n");
	if (bbox_node->data_number==0)
		return false;
	return true;
}

void	json_import_cell_attributes(Design *design, string &modname,
		CellInfo *cell, JsonNode *param_node, int param_id) {
	//
	JsonNode	*param;
	IdString	pId;
	//
	param = param_node->data_dict.at(
		param_node->data_dict_keys[param_id]);

	pId = param_node->data_dict_keys[param_id];
	if (param->type == 'N') {
		cell->params[pId] = std::to_string(param_node->data_number);;
	} else if (param->type == 'S')
		cell->params[pId] = param->data_string;
	else
		log_error("JSON parameter type of \"%s\' of cell \'%s\' not supported\n",
			pId.c_str(),
			cell->name.c_str());

	if (json_debug) log_info("    Added parameter \'%s\'=%s to cell \'%s\' "
				"of module \'%s\'\n",
			pId.c_str(), cell->params[pId].c_str(),
			cell->name.c_str(), modname.c_str());
}

void	json_import_cell_ports(Design *design, string &modname, CellInfo *cell,
		string &port_name, JsonNode *dir_node,JsonNode *wire_group_node)
{
	// Examine and connect a single port of the given cell to its nets,
	// generating them as necessary

	assert(dir_node);

	if (json_debug) log_info("    Examining port %s, node %s\n", port_name.c_str(),
		cell->name.c_str());

	if (!wire_group_node)
		log_error("JSON no connection match "
			"for port_direction \'%s\' of node \'%s\' "
				"in module \'%s\'\n",
			port_name.c_str(), cell->name.c_str(), modname.c_str());

	assert(wire_group_node);

	assert(dir_node->type == 'S');
	assert(wire_group_node->type == 'A');

	PortInfo	port_info;

	port_info.name = port_name;
	if(dir_node->data_string.compare("input")==0)
		port_info.type = PORT_IN;
	else if(dir_node->data_string.compare("output")==0)
		port_info.type = PORT_OUT;
	else if(dir_node->data_string.compare("inout")==0)
		port_info.type = PORT_INOUT;
	else
		log_error("JSON unknown port direction \'%s\' in node \'%s\' "
				"of module \'%s\'\n",
			dir_node->data_string.c_str(),
			cell->name.c_str(),
			modname.c_str());
	//
	// Find an update, or create a net to connect
	// to this port.
	//
	NetInfo		*this_net;
	bool		is_bus;

	//
	// If this port references a bus, then there will be multiple nets
	// connected to it, all specified as part of an array.
	//
	is_bus = (wire_group_node->data_array.size()>1);

	// Now loop through all of the connections to this port.
	for(int index=0; index < wire_group_node->data_array.size(); index++) {
		//
		JsonNode	*wire_node;
		PortInfo	this_port;
		PortRef		port_ref;
		bool		const_input = false;
		IdString	net_id;
		//
		wire_node = wire_group_node->data_array[index];
		port_ref.cell = cell;

		//
		// Pick a name for this port
		if (is_bus)
			this_port.name = port_info.name + '['
						+ std::to_string(index) + ']';
		else
			this_port.name = port_info.name;
		this_port.type = port_info.type;

		port_ref.port = this_port.name;

		if (wire_node->type == 'N') {
			int		net_num;

			// A simple net, specified by a number
			net_num = wire_node->data_number;
			net_id = std::to_string(net_num);
			if (design->nets.count(net_id) == 0) {
				// The net doesn't exist in the design (yet)
				// Create in now

				if (json_debug) log_info("      Generating a new net, \'%d\'\n",
					net_num);

				this_net = new NetInfo;
				this_net->name = net_id;
				this_net->driver.cell = NULL;
				this_net->driver.port = "";
				design->nets[net_id] = this_net;
			} else {
				//
				// The net already exists within the design.
				// We'll connect to it
				// 
				this_net = design->nets[net_id];
				if (json_debug) log_info("      Reusing net \'%s\', id \'%s\', "
					"with driver \'%s\'\n",
					this_net->name.c_str(), net_id.c_str(),
					(this_net->driver.cell!=NULL)
						? this_net->driver.port.c_str()
						: "NULL");
			}

		} else if (wire_node->type == 'S') {
			// Strings are only used to drive wires for the fixed
			// values "0", "1", and "x".  Handle those constant
			// values here.
			//
			// Constants always get their own new net
			this_net = new NetInfo;
			this_net->name = net_id;
			const_input = (this_port.type == PORT_IN);

		    	if (wire_node->data_string.compare(string("0"))==0) {

				if (json_debug) log_info("      Generating a constant "
							"zero net\n");
				this_net = ground_net(this_net);

			} else if (wire_node->data_string.compare(string("1"))
						==0) {

				if (json_debug) log_info("      Generating a constant "
							"one  net\n");
				this_net = vcc_net(this_net);

			} else if (wire_node->data_string.compare(string("x"))
						==0) {

				this_net = floating_net(this_net);
				log_warning("      Floating wire node value, "
					"\'%s\' of port \'%s\' "
					"in cell \'%s\' of module \'%s\'\n",
					wire_node->data_string.c_str(),
					port_name.c_str(),
					cell->name.c_str(),
					modname.c_str());

			} else
				log_error("      Unknown fixed type wire node "
					"value, \'%s\'\n",
					wire_node->data_string.c_str());
		}

		if (json_debug) log_info("    Inserting port \'%s\' into cell \'%s\'\n",
			this_port.name.c_str(), cell->name.c_str());

		this_port.net = this_net;

		cell->ports[this_port.name] = this_port;

		if (this_port.type == PORT_OUT) {
			assert(this_net->driver.cell == NULL);
			this_net->driver = port_ref;
		} else
			this_net->users.push_back(port_ref);

		if (design->nets.count(this_net->name) == 0)
			design->nets[this_net->name] = this_net;
	}
}

void	json_import_cell(Design *design, string modname, JsonNode *cell_node,
		string cell_name) {
	JsonNode *cell_type, *param_node;

	cell_type = cell_node->data_dict.at("type");
	if (cell_type == NULL)
		return;

	CellInfo *cell =  new CellInfo;

	cell->name = cell_name;
	assert(cell_type->type == 'S');
	cell->type = cell_type->data_string;
	// No BEL assignment here/yet

	if (json_debug) log_info("  Processing %s $ %s\n",
			modname.c_str(), cell->name.c_str());

	param_node = cell_node->data_dict.at("parameters");
	if (param_node->type != 'D')
		log_error("JSON parameter list of \'%s\' is not a data dictionary\n", cell->name.c_str());

	//
	// Loop through all parameters, adding them into the
	// design to annotate the cell
	//
	for(int paramid = 0; paramid < GetSize(param_node->data_dict_keys);
			paramid++) {

		json_import_cell_attributes(design, modname, cell, param_node,
			paramid);
	}


	//
	// Now connect the ports of this module.  The ports are defined by
	// both the port directions node as well as the connections node.
	// Both should contain dictionaries having the same keys.
	//

	JsonNode *pdir_node
		= cell_node->data_dict.at("port_directions");
	if (pdir_node->type != 'D')
		log_error("JSON port_directions node of \'%s\' "
			"in module \'%s\' is not a "
				"dictionary\n", cell->name.c_str(),
			modname.c_str());

	JsonNode *connections
		= cell_node->data_dict.at("connections");
	if (connections->type != 'D')
		log_error("JSON connections node of \'%s\' "
			"in module \'%s\' is not a "
			"dictionary\n", cell->name.c_str(),
			modname.c_str());

	if (GetSize(pdir_node->data_dict_keys)
			!= GetSize(connections->data_dict_keys))
		log_error("JSON number of connections doesnt "
			"match number of ports in node \'%s\' "
			"of module \'%s\'\n",
			cell->name.c_str(),
			modname.c_str());

	//
	// Loop through all of the ports of this logic element
	//
	for(int portid=0; portid<GetSize(pdir_node->data_dict_keys);
			portid++) {
		//
		string		port_name;
		JsonNode	*dir_node, *wire_group_node;
		//

		port_name = pdir_node->data_dict_keys[portid];
		dir_node  = pdir_node->data_dict.at(port_name);
		wire_group_node = connections->data_dict.at(port_name);

		json_import_cell_ports(design, modname, cell,
			port_name, dir_node, wire_group_node);
	}

	design->cells[cell->name] = cell;
	// check_all_nets_driven(design);
}

void json_import(Design *design, string modname, JsonNode *node) {
	if (is_blackbox(node))
		return;

	log_info("Importing modname = %s\n", modname.c_str());

	if (node->data_dict.count("cells")) {
		JsonNode *cell_parent = node->data_dict.at("cells");
		//
		//
		// Loop through all of the logic elements in a flattened design
		//
		//
		for(int cellid=0; cellid < GetSize(cell_parent->data_dict_keys);
				cellid ++) {
			JsonNode *cell_type, *here, *param_node;
			
			here = cell_parent->data_dict.at(
				cell_parent->data_dict_keys[cellid]);
			json_import_cell(design, modname, here,
					cell_parent->data_dict_keys[cellid]);

		}
	}

	check_all_nets_driven(design);
}

struct JsonFrontend {
	// JsonFrontend() : Frontend("json", "read JSON file") { }
	JsonFrontend(void) { }
	virtual void help()
	{
	}
	virtual void execute(std::istream *&f, std::string filename,
			Design *design)
	{
		// log_header(design, "Executing JSON frontend.\n");

		JsonNode root(*f);

		if (root.type != 'D')
			log_error("JSON root node is not a dictionary.\n");

		if (root.data_dict.count("modules") != 0)
		{
			JsonNode *modules = root.data_dict.at("modules");

			if (modules->type != 'D')
				log_error("JSON modules node is not a dictionary.\n");

			for (auto &it : modules->data_dict)
				json_import(design, it.first, it.second);
		}
	}
}; // JsonFrontend;

}; // End Namespace JsonParser

void	parse_json_file(std::istream *&f, std::string filename, Design *design){
	auto *parser = new JsonParser::JsonFrontend();
	parser->execute(f, filename, design);
}

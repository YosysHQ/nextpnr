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
#include "common/design.h"
#include "ice40/chip.h"
#warning "CC files shouldnt be included"
#include "ice40/chip.cc"
// #include "ice40/chipdb-384.cc"
// #include "ice40/chipdb-1k.cc"
// #include "ice40/chipdb-5k.cc"
#include "ice40/chipdb-8k.cc"

namespace JsonParser {

void	log_error(const char fmt, ...) {
	va_list	args;

	std::string	sfmt = "ERROR: " + fmt;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

void	log_warning(const char fmt, ...) {
	va_list	args;

	std::string	sfmt = "WARNING: " + fmt;
	va_start(args, fmt);
	vfprintf(stderr, sfmt.c_str(), args);
	va_end(args);
}

void	log_info(const char fmt, ...) {
	va_list	args;

	std::string	sfmt = "INFO: " + fmt;
	va_start(args, fmt);
	vfprintf(stderr, sfmt.c_str(), args);
	va_end(args);
}

typedef	std::string string;

void	log_header(Design *d, const char *str) {
	std::cout << str;
		// log_header(design, "Executing JSON frontend.\n");
}

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

void json_import(Design *design, string modname, JsonNode *node)
{
	if (is_blackbox(node))
		return;

log_info("Processing modname = %s\n", modname.c_str());

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
			cell_type = here->data_dict.at("type");
			if (cell_type == NULL)
				continue;

			CellInfo *cell =  new CellInfo;

			cell->name = cell_parent->data_dict_keys[cellid];
			cell->bel  = design->chip.getBelByName(cell_type->data_string);

log_info("  Processing %s $ %s\n", modname.c_str(), cell->name.c_str());
			param_node = here->data_dict.at("parameters");
			if (param_node->type != 'D')
				log_error("JSON parameter list of \'%s\' is not a data dictionary\n", cell->name);

			//
			// Loop through all parameters, adding them into the
			// design to annotate the cell
			//
			for(int paramid = 0;
				paramid < GetSize(param_node->data_dict_keys);
				paramid++) {
				//
				JsonNode	*param;
				//
				param = param_node->data_dict.at(
					param_node->data_dict_keys[paramid]);

				IdString	pId;
				pId = param_node->data_dict_keys[paramid];
				if (param->type == 'N') {
					string	tmp;
					tmp = "" + param_node->data_number;
					cell->params[pId] = tmp;
				} else if (param->type == 'S')
					cell->params[pId] = param->data_string;
				else
					log_error("JSON parameter type of \"%s\' of cell \'%s\' not supported\n",
						pId.c_str(),
						cell->name.c_str());

log_info("    Added parameter \'%s\'=%s to cell \'%s\' of module \'%s\'\n",
	pId.c_str(), cell->params[pId].c_str(),
	cell->name.c_str(), modname.c_str());
			}

			JsonNode *pdir_node
				= here->data_dict.at("port_directions");
			if (pdir_node->type != 'D')
				log_error("JSON port_directions node of \'%s\' "
					"in module \'%s\' is not a "
					"dictionary\n", cell->name.c_str(),
					modname.c_str());

			JsonNode *connections
				= here->data_dict.at("connections");
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
			for(int portid=0;
				portid<GetSize(pdir_node->data_dict_keys);
				portid++) {
				string		key;
				JsonNode	*dir_node, *wire_node;

				key = pdir_node->data_dict_keys[portid];
				dir_node = pdir_node->data_dict.at(key);
				wire_node = connections->data_dict.at(key);

				assert(dir_node);

log_info("Examining port %s, node %s\n", key.c_str(),
	cell->name.c_str());

				if (!wire_node)
					log_error("JSON no connection match "
						"for port_direction \'%s\' "
						"of node \'%s\' "
						"in module \'%s\'\n",
						key, cell->name.c_str(),
						modname.c_str());

				assert(wire_node);

				assert(dir_node->type == 'S');
				assert(wire_node->type == 'A');

				PortInfo	port_info;
				port_info.name = cell->name + "$" + key;
				if(dir_node->data_string.compare("input")==0)
					port_info.type = PORT_IN;
				else if(dir_node->data_string.compare("output")==0)
					port_info.type = PORT_OUT;
				else if(dir_node->data_string.compare("inout")==0)
					port_info.type = PORT_INOUT;
				else
					log_error("JSON unknown port direction "
						"\'%s\' in node \'%s\' "
						"of module \'%s\'\n",
						dir_node->data_string.c_str(),
						cell->name.c_str(),
						modname.c_str());

				//
				// Find an update, or create a net to connect
				// to this port.
				//
				PortRef		port_ref;
				int		net_num;
				NetInfo		*this_net;
				IdString	net_id;

				net_num = wire_node->data_number;
				net_id = string(""+net_num);
				if (design->nets.count(net_id) == 0) {
					this_net = new NetInfo;
					this_net->name = "" + net_id;
					design->nets[net_id] = this_net;
				} else
					this_net = design->nets[net_id];

				port_ref.cell = cell;
				port_ref.port = port_info.name;

				if (port_info.type != PORT_IN)
					this_net->driver = port_ref;
				else
					this_net->users.push_back(port_ref);
			}

			design->cells[cell->name] = cell;
		}
	}
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
		log_header(design, "Executing JSON frontend.\n");

		JsonNode root(*f);

		if (root.type != 'D')
			log_error("JSON root node is not a dictionary.\n");

		if (root.data_dict.count("modules") != 0)
		{
			JsonNode *modules = root.data_dict.at("modules");

			if (modules->type != 'D')
				log_error("JSON modules node is not a dictionary.\n");

fprintf(stderr, "Looping\n");
			for (auto &it : modules->data_dict)
				json_import(design, it.first, it.second);
		}
	}
}; // JsonFrontend;

}; // End Namespace JsonParser

#warning "Main routine should be removed from jsonparse.cc before production"

int	main(int argc, char **argv) {
	JsonParser::JsonFrontend *parser = new JsonParser::JsonFrontend;
	ChipArgs	chip_args;
	chip_args.type = ChipArgs::LP384;

	Design	*design = new Design(chip_args);
	std::string	fname = "../../ice40/blinky.json";
	std::istream	*f = new std::ifstream(fname);
	parser->execute(f, fname, design);
}

int	num_wires_384 = 0;
WireInfoPOD	wire_data_384[] = { 0 };

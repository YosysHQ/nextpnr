#include <string>
#include <assert.h>
#include "design.h"
#include "log.h"

bool	check_all_nets_driven(Design *design) {
	const	bool	debug = false;

	log_info("Rule checker, Verifying pre-placed design\n");

	for(auto cell_entry : design->cells) {
		CellInfo	*cell = cell_entry.second;

		if (debug) log_info("  Examining cell \'%s\', of type \'%s\'\n",
			cell->name.c_str(),
			cell->type.c_str());
		for(auto port_entry : cell->ports) {
			PortInfo	&port = port_entry.second;

			if (debug) log_info("    Checking name of port \'%s\' "
					"against \'%s\'\n",
				port_entry.first.c_str(),
				port.name.c_str());
			assert(port.name.compare(port_entry.first)==0);
			assert(port.name.size() > 0);

			if (port.net == NULL) {
				if (debug) log_warning("    Port \'%s\' in cell \'%s\' is unconnected\n",
					port.name.c_str(), cell->name.c_str());
			} else {
				assert(port.net);
				if (debug) log_info("    Checking for a net named \'%s\'\n",
					port.net->name.c_str());
				assert(design->nets.count(port.net->name)>0);
			}
		}
	}

	for(auto net_entry : design->nets) {
		NetInfo	*net = net_entry.second;

		assert(net->name.compare(net_entry.first) == 0);
		if ((net->driver.cell != NULL)
			&&(net->driver.cell->type.compare("GND") != 0)
			&&(net->driver.cell->type.compare("VCC") != 0)) {

			if (debug) log_info("    Checking for a driver cell named \'%s\'\n",
				net->driver.cell->name.c_str());
			assert(design->cells.count(net->driver.cell->name) >0);
		}

		for(auto user : net->users) {
			if ((user.cell != NULL)
				&&(user.cell->type.compare("GND") != 0)
				&&(user.cell->type.compare("VCC") != 0)) {

			if (debug) log_info("    Checking for a user   cell named \'%s\'\n",
				user.cell->name.c_str());
			assert(design->cells.count(user.cell->name) >0);
		}

		}
	}

	if (debug) log_info("  Verified!\n");
	return true;
}


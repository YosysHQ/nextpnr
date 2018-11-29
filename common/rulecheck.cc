#include <assert.h>
#include <string>
#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

bool check_all_nets_driven(Context *ctx)
{
    const bool debug = false;

    log_info("Rule checker, verifying imported design\n");

    for (auto &cell_entry : ctx->cells) {
        CellInfo *cell = cell_entry.second.get();

        if (debug)
            log_info("  Examining cell \'%s\', of type \'%s\'\n", cell->name.c_str(ctx), cell->type.c_str(ctx));
        for (auto port_entry : cell->ports) {
            PortInfo &port = port_entry.second;

            if (debug)
                log_info("    Checking name of port \'%s\' "
                         "against \'%s\'\n",
                         port_entry.first.c_str(ctx), port.name.c_str(ctx));
            NPNR_ASSERT(port.name == port_entry.first);
            NPNR_ASSERT(!port.name.empty());

            if (port.net == NULL) {
                if (debug)
                    log_warning("    Port \'%s\' in cell \'%s\' is unconnected\n", port.name.c_str(ctx),
                                cell->name.c_str(ctx));
            } else {
                NPNR_ASSERT(port.net);
                if (debug)
                    log_info("    Checking for a net named \'%s\'\n", port.net->name.c_str(ctx));
                NPNR_ASSERT(ctx->nets.count(port.net->name) > 0);
            }
        }
    }

    for (auto &net_entry : ctx->nets) {
        NetInfo *net = net_entry.second.get();

        NPNR_ASSERT(net->name == net_entry.first);
        if ((net->driver.cell != NULL) && (net->driver.cell->type != ctx->id("GND")) &&
            (net->driver.cell->type != ctx->id("VCC"))) {

            if (debug)
                log_info("    Checking for a driver cell named \'%s\'\n", net->driver.cell->name.c_str(ctx));
            NPNR_ASSERT(ctx->cells.count(net->driver.cell->name) > 0);
        }

        for (auto user : net->users) {
            if ((user.cell != NULL) && (user.cell->type != ctx->id("GND")) && (user.cell->type != ctx->id("VCC"))) {

                if (debug)
                    log_info("    Checking for a user   cell named \'%s\'\n", user.cell->name.c_str(ctx));
                NPNR_ASSERT(ctx->cells.count(user.cell->name) > 0);
            }
        }
    }

    if (debug)
        log_info("  Verified!\n");
    return true;
}

NEXTPNR_NAMESPACE_END

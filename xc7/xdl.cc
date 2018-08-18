/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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
#include "xdl.h"
#include <cctype>
#include <vector>
#include "cells.h"
#include "log.h"
#include "util.h"

#include "torc/Physical.hpp"
using namespace torc::architecture::xilinx;
using namespace torc::physical;

NEXTPNR_NAMESPACE_BEGIN

void write_xdl(const Context *ctx, std::ostream &out)
{
    XdlExporter exporter(out);
    auto designPtr = Factory::newDesignPtr("name", torc_info->ddb->getDeviceName(), "clg484", "", "");

    std::unordered_map<int32_t,InstanceSharedPtr> site_to_instance;

    for (const auto& cell : ctx->cells) {
        const char* type;
        if (cell.second->type == id_SLICE_LUT6) type = "SLICEL";
        else if (cell.second->type == id_IOB33S) type = "IOB33S";
        else if (cell.second->type == id_BUFGCTRL) type = "BUFGCTRL";
        else log_error("Unsupported cell type '%s'.\n", cell.second->type.c_str(ctx));

        auto site_index = torc_info->bel_to_site_index[cell.second->bel.index];
        auto ret = site_to_instance.emplace(site_index, nullptr);
        InstanceSharedPtr instPtr;
        if (ret.second) {
            instPtr = Factory::newInstancePtr(cell.second->name.str(ctx), type, "", "");
            auto b = designPtr->addInstance(instPtr);
            assert(b);
            ret.first->second = instPtr;

            const auto& tile_info = torc_info->bel_to_tile_info(cell.second->bel.index);
            instPtr->setTile(tile_info.getName());
            instPtr->setSite(torc_info->bel_to_name(cell.second->bel.index));
        }
        else
            instPtr = ret.first->second;

        if (cell.second->type == id_SLICE_LUT6) {
            std::string config;
            switch (torc_info->bel_to_z[cell.second->bel.index]) {
                case 0: case 4: config += 'A'; break;
                case 1: case 5: config += 'B'; break;
                case 2: case 6: config += 'C'; break;
                case 3: case 7: config += 'D'; break;
                default: throw;
            }
            config += "6LUT";
            instPtr->setConfig(config, cell.second->name.str(ctx), "#LUT:O6=");
        }
        else if (cell.second->type == id_IOB33S) {
            if (get_net_or_empty(cell.second.get(), id_I)) {
                instPtr->setConfig("IUSED", "", "0");
                instPtr->setConfig("IBUF_LOW_PWR", "", "TRUE");
                instPtr->setConfig("ISTANDARD", "", "LVCMOS25");
            }
            else {
                instPtr->setConfig("OUSED", "", "0");
                instPtr->setConfig("OSTANDARD", "", "LVCMOS25");
                instPtr->setConfig("DRIVE", "", "12");
                instPtr->setConfig("SLEW", "", "SLOW");
            }
        }
        else if (cell.second->type == id_BUFGCTRL) {
        }
        else log_error("Unsupported cell type '%s'.\n", cell.second->type.c_str(ctx));
    }

    exporter(designPtr);

}

NEXTPNR_NAMESPACE_END

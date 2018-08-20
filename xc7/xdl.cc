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
    std::vector<std::pair<std::string,std::string>> lut_inputs;
    lut_inputs.reserve(6);

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
            std::string setting, value;
            std::string lut;
            switch (torc_info->bel_to_z[cell.second->bel.index]) {
                case 0: case 4: lut = 'A'; break;
                case 1: case 5: lut = 'B'; break;
                case 2: case 6: lut = 'C'; break;
                case 3: case 7: lut = 'D'; break;
                default: throw;
            }

            setting = lut + "6LUT";
            value = "#LUT:O6=";
            lut_inputs.clear();
            if (get_net_or_empty(cell.second.get(), id_I1)) lut_inputs.emplace_back(lut + "1", "~" + lut + "1");
            if (get_net_or_empty(cell.second.get(), id_I2)) lut_inputs.emplace_back(lut + "2", "~" + lut + "2");
            if (get_net_or_empty(cell.second.get(), id_I3)) lut_inputs.emplace_back(lut + "3", "~" + lut + "3");
            if (get_net_or_empty(cell.second.get(), id_I4)) lut_inputs.emplace_back(lut + "4", "~" + lut + "4");
            if (get_net_or_empty(cell.second.get(), id_I5)) lut_inputs.emplace_back(lut + "5", "~" + lut + "5");
            if (get_net_or_empty(cell.second.get(), id_I6)) lut_inputs.emplace_back(lut + "6", "~" + lut + "6");
            const auto& init = cell.second->params[ctx->id("INIT")];
            // Assume from Yosys that INIT masks of less than 32 bits are output as uint32_t
            if (lut_inputs.size() < 6) {
                auto init_as_uint = boost::lexical_cast<uint32_t>(init);
                NPNR_ASSERT(init_as_uint < (1ull << (1u << lut_inputs.size())));
                if (lut_inputs.empty())
                    value += init;
                else
                    for (unsigned o = 0; o < (1u << lut_inputs.size()); ++o) {
                        if ((init_as_uint >> o) & 0x1) continue;
                        if (o > 0) value += "+";
                        value += "(";
                        value += (o & 1) ? lut_inputs[0].first : lut_inputs[0].second;
                        for (unsigned i = 1; i < lut_inputs.size(); ++i) {
                            value += "*";
                            value += o & (1 << i) ? lut_inputs[i].first : lut_inputs[i].second;
                        }
                        value += ")";
                    }
            }
            // Otherwise as a bit string
            else {
                NPNR_ASSERT(init.size() == (1u << lut_inputs.size()));
                for (unsigned i = 0; i < (1u << lut_inputs.size()); ++i) {
                    if (init[i] == '0') continue;
                    if (i > 0) value += "+";
                    value += "(";
                    value += (i & 1) ? lut_inputs[0].first : lut_inputs[0].second;
                    for (unsigned i = 1; i < lut_inputs.size(); ++i) {
                        value += "*";
                        value += i & (1 << i) ? lut_inputs[i].first : lut_inputs[i].second;
                    }
                    value += ")";
                }
            }

            auto O = get_net_or_empty(cell.second.get(), id_O);
            if (O)
                instPtr->setConfig(setting, O->name.str(ctx), value);
            else
                instPtr->setConfig(setting, cell.second->name.str(ctx), value);

            auto OQ = get_net_or_empty(cell.second.get(), id_OQ);
            if (OQ) {
                setting = lut;
                setting += "FF";
                instPtr->setConfig(setting, OQ->name.c_str(ctx), "#FF");
            }
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

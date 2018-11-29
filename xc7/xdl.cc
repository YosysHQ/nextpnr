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
#include "xdl.h"
#include <cctype>
#include <vector>
#include "cells.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "torc/Physical.hpp"
using namespace torc::architecture::xilinx;
using namespace torc::physical;

NEXTPNR_NAMESPACE_BEGIN

void write_xdl(const Context *ctx, std::ostream &out)
{
    XdlExporter exporter(out);
    auto designPtr = Factory::newDesignPtr("name", torc_info->ddb->getDeviceName(), ctx->args.package, "-1", "");

    std::unordered_map<int32_t, InstanceSharedPtr> site_to_instance;
    std::vector<std::pair<std::string, std::string>> lut_inputs;
    lut_inputs.reserve(6);

    auto bel_to_lut = [](const BelId bel) {
        switch (torc_info->bel_to_loc[bel.index].z) {
        case 0:
        case 4:
            return "A";
            break;
        case 1:
        case 5:
            return "B";
            break;
        case 2:
        case 6:
            return "C";
            break;
        case 3:
        case 7:
            return "D";
            break;
        default:
            throw;
        }
    };

    for (const auto &cell : ctx->cells) {
        const char *type;
        if (cell.second->type == id_SLICE_LUT6)
            type = "SLICEL";
        else if (cell.second->type == id_IOB33 || cell.second->type == id_BUFGCTRL || cell.second->type == id_PS7 || cell.second->type == id_MMCME2_ADV)
            type = cell.second->type.c_str(ctx);
        else
            log_error("Unsupported cell type '%s'.\n", cell.second->type.c_str(ctx));

        auto site_index = torc_info->bel_to_site_index[cell.second->bel.index];
        auto ret = site_to_instance.emplace(site_index, nullptr);
        InstanceSharedPtr instPtr;
        if (ret.second) {
            instPtr = Factory::newInstancePtr(cell.second->name.str(ctx), type, "", "");
            auto b = designPtr->addInstance(instPtr);
            assert(b);
            ret.first->second = instPtr;

            const auto &tile_info = torc_info->bel_to_tile_info(cell.second->bel.index);
            instPtr->setTile(tile_info.getName());
            instPtr->setSite(torc_info->bel_to_name(cell.second->bel.index));
        } else
            instPtr = ret.first->second;

        if (cell.second->type == id_SLICE_LUT6) {
            std::string setting, name, value;
            const std::string lut = bel_to_lut(cell.second->bel);

            setting = lut + "6LUT";
            value = "#LUT:O6=";
            lut_inputs.clear();
            if (get_net_or_empty(cell.second.get(), id_I1))
                lut_inputs.emplace_back("A1", "~A1");
            if (get_net_or_empty(cell.second.get(), id_I2))
                lut_inputs.emplace_back("A2", "~A2");
            if (get_net_or_empty(cell.second.get(), id_I3))
                lut_inputs.emplace_back("A3", "~A3");
            if (get_net_or_empty(cell.second.get(), id_I4))
                lut_inputs.emplace_back("A4", "~A4");
            if (get_net_or_empty(cell.second.get(), id_I5))
                lut_inputs.emplace_back("A5", "~A5");
            if (get_net_or_empty(cell.second.get(), id_I6))
                lut_inputs.emplace_back("A6", "~A6");
            const auto &init = cell.second->params[ctx->id("INIT")];
            // Assume from Yosys that INIT masks of less than 32 bits are output as uint32_t
            if (lut_inputs.size() < 6) {
                auto init_as_uint = boost::lexical_cast<uint32_t>(init);
                NPNR_ASSERT(init_as_uint < (1ull << (1u << lut_inputs.size())));
                if (lut_inputs.empty())
                    value += init;
                else {
                    unsigned n = 0;
                    for (unsigned o = 0; o < (1u << lut_inputs.size()); ++o) {
                        if (!((init_as_uint >> o) & 1))
                            continue;
                        if (n++ > 0)
                            value += "+";
                        value += "(";
                        value += (o & 1) ? lut_inputs[0].first : lut_inputs[0].second;
                        for (unsigned i = 1; i < lut_inputs.size(); ++i) {
                            value += "*";
                            value += o & (1 << i) ? lut_inputs[i].first : lut_inputs[i].second;
                        }
                        value += ")";
                    }
                }
            }
            // Otherwise as a bit string
            else {
                NPNR_ASSERT(init.size() == (1u << lut_inputs.size()));
                unsigned n = 0;
                for (unsigned i = 0; i < init.size(); ++i) {
                    if (init[init.size() - 1 - i] == '0')
                        continue;
                    if (n++ > 0)
                        value += "+";
                    value += "(";
                    value += (i & 1) ? lut_inputs[0].first : lut_inputs[0].second;
                    for (unsigned j = 1; j < lut_inputs.size(); ++j) {
                        value += "*";
                        value += i & (1 << j) ? lut_inputs[j].first : lut_inputs[j].second;
                    }
                    value += ")";
                }
            }

            auto it = cell.second->params.find(ctx->id("LUT_NAME"));
            if (it != cell.second->params.end())
                name = it->second;
            else
                name = cell.second->name.str(ctx);
            boost::replace_all(name, ":", "\\:");
            instPtr->setConfig(setting, name, value);

            auto O = get_net_or_empty(cell.second.get(), id_O);
            if (O) {
                setting = lut;
                setting += "USED";
                instPtr->setConfig(setting, "", "0");
            }

            auto OQ = get_net_or_empty(cell.second.get(), id_OQ);
            if (OQ) {
                setting = lut;
                setting += "FF";
                name = OQ->name.str(ctx);
                boost::replace_all(name, ":", "\\:");
                instPtr->setConfig(setting, name, "#FF");
                instPtr->setConfig(setting + "MUX", "", "O6");
                instPtr->setConfig(setting + "INIT", "", "INIT" + cell.second->params.at(ctx->id("DFF_INIT")));
                assert(cell.second->params.at(ctx->id("SET_NORESET")) == "0"); // TODO
                instPtr->setConfig(setting + "SR", "", "SRLOW");

                NPNR_ASSERT(!cell.second->lcInfo.negClk); // TODO
                instPtr->setConfig("CLKINV", "", "CLK");

                instPtr->setConfig("SRUSEDMUX", "", "IN");
                instPtr->setConfig("CEUSEDMUX", "", "IN");
                instPtr->setConfig("SYNC_ATTR", "", "ASYNC");
            }
        } else if (cell.second->type == id_IOB33) {
            if (get_net_or_empty(cell.second.get(), id_I)) {
                instPtr->setConfig("IUSED", "", "0");
                instPtr->setConfig("IBUF_LOW_PWR", "", "TRUE");
                instPtr->setConfig("ISTANDARD", "", "LVCMOS33");
            } else {
                instPtr->setConfig("OUSED", "", "0");
                instPtr->setConfig("OSTANDARD", "", "LVCMOS33");
                instPtr->setConfig("DRIVE", "", "12");
                instPtr->setConfig("SLEW", "", "SLOW");
            }
        } else if (cell.second->type == id_BUFGCTRL) {
            auto it = cell.second->params.find(ctx->id("PRESELECT_I0"));
            instPtr->setConfig("PRESELECT_I0", "", it != cell.second->params.end() ? it->second : "FALSE");
            instPtr->setConfig("CE0INV", "", "CE0");
            instPtr->setConfig("S0INV", "", "S0");
            //instPtr->setConfig("IGNORE0INV", "", "IGNORE0");
            //instPtr->setConfig("CE1INV", "", "CE1");
            //instPtr->setConfig("S1INV", "", "S1");
            //instPtr->setConfig("IGNORE1INV", "", "IGNORE1");
        } else if (cell.second->type == id_PS7) {
        } else if (cell.second->type == id_MMCME2_ADV) {
            for (const auto& i : cell.second->params) {
                instPtr->setConfig(i.first.str(ctx), "", i.second);
            }
            if (!cell.second->params.count(ctx->id("CLKIN2_PERIOD")))
                instPtr->setConfig("CLKIN2_PERIOD", "", "0");
            for (const auto& i : { "CLKOUT1_DIVIDE", "CLKOUT2_DIVIDE", "CLKOUT3_DIVIDE", "CLKOUT4_DIVIDE", "CLKOUT5_DIVIDE", "CLKOUT6_DIVIDE" }) {
                if (!cell.second->params.count(ctx->id(i)))
                    instPtr->setConfig(i, "", "1");
            }
            for (const auto& i : { "CLKOUT1_PHASE", "CLKOUT2_PHASE", "CLKOUT3_PHASE", "CLKOUT4_PHASE", "CLKOUT5_PHASE", "CLKOUT6_PHASE" }) {
                if (!cell.second->params.count(ctx->id(i)))
                    instPtr->setConfig(i, "", "0.0");
            }
            for (const auto& i : { "CLKOUT1_DUTY_CYCLE", "CLKOUT2_DUTY_CYCLE", "CLKOUT3_DUTY_CYCLE", "CLKOUT4_DUTY_CYCLE", "CLKOUT5_DUTY_CYCLE", "CLKOUT6_DUTY_CYCLE" }) {
                if (!cell.second->params.count(ctx->id(i)))
                    instPtr->setConfig(i, "", "0.5");
            }
            if (!cell.second->params.count(ctx->id("REF_JITTER2")))
                instPtr->setConfig("REF_JITTER2", "", "0.01");
            if (!cell.second->params.count(ctx->id("SS_MOD_PERIOD")))
                instPtr->setConfig("SS_MOD_PERIOD", "", "10000");
        } else
            log_error("Unsupported cell type '%s'.\n", cell.second->type.c_str(ctx));
    }

    for (const auto &net : ctx->nets) {
        const auto &driver = net.second->driver;

        auto site_index = torc_info->bel_to_site_index[driver.cell->bel.index];
        auto instPtr = site_to_instance.at(site_index);

        auto netPtr = Factory::newNetPtr(net.second->name.str(ctx));

        auto pin_name = driver.port.str(ctx);
        // For all LUT based inputs and outputs (I1-I6,O,OQ,OMUX) then change the I/O into the LUT
        if (driver.cell->type == id_SLICE_LUT6 && (pin_name[0] == 'I' || pin_name[0] == 'O')) {
            const auto lut = bel_to_lut(driver.cell->bel);
            pin_name[0] = lut[0];
        }
        // e.g. Convert DDRARB[0] -> DDRARB0
        pin_name.erase(std::remove_if(pin_name.begin(), pin_name.end(), boost::is_any_of("[]")), pin_name.end());
        auto pinPtr = Factory::newInstancePinPtr(instPtr, pin_name);
        netPtr->addSource(pinPtr);

        for (const auto &user : net.second->users) {
            site_index = torc_info->bel_to_site_index[user.cell->bel.index];
            instPtr = site_to_instance.at(site_index);

            pin_name = user.port.str(ctx);
            // For all LUT based inputs and outputs (I1-I6,O,OQ,OMUX) then change the I/O into the LUT
            if (user.cell->type == id_SLICE_LUT6 && (pin_name[0] == 'I' || pin_name[0] == 'O')) {
                const auto lut = bel_to_lut(user.cell->bel);
                pin_name[0] = lut[0];
            }
            else {
                // e.g. Convert DDRARB[0] -> DDRARB0
                pin_name.erase(std::remove_if(pin_name.begin(), pin_name.end(), boost::is_any_of("[]")), pin_name.end());
            }
            pinPtr = Factory::newInstancePinPtr(instPtr, pin_name);
            netPtr->addSink(pinPtr);
        }

        auto b = designPtr->addNet(netPtr);
        assert(b);

        for (const auto &i : net.second->wires) {
            const auto &pip_map = i.second;
            if (pip_map.pip == PipId())
                continue;
            ExtendedWireInfo ewi_src(*torc_info->ddb, torc_info->pip_to_arc[pip_map.pip.index].getSourceTilewire());
            ExtendedWireInfo ewi_dst(*torc_info->ddb, torc_info->pip_to_arc[pip_map.pip.index].getSinkTilewire());
            auto p = Factory::newPip(ewi_src.mTileName, ewi_src.mWireName, ewi_dst.mWireName,
                                     ePipUnidirectionalBuffered);
            netPtr->addPip(p);
        }
    }

    exporter(designPtr);
}

NEXTPNR_NAMESPACE_END

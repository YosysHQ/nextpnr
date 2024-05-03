/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  Miodrag Milanovic <micko@yosyshq.com>
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

#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include <fstream>
#include <regex>

#include "json.hpp"

#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "ng_ultra.h"

#define HIMBAECHEL_CONSTIDS "uarch/ng-ultra/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct BitstreamJsonBackend
{
    Context *ctx;
    nlohmann::json json;
    NgUltraImpl *uarch;
    std::ostream &out;

    BitstreamJsonBackend(Context *ctx, NgUltraImpl *uarch, std::ostream &out) : ctx(ctx), uarch(uarch), out(out){};

    std::string update_name(std::string tile, std::string name)
    {
        if (boost::starts_with(tile,"FENCE[")) {
            char last = tile[tile.size()-2];
            switch(last)
            {
                case 'T':
                case 'B':
                case 'U':
                case 'L':
                    std::string loc = tile.substr(tile.find("[")+1, tile.find("x")-tile.find("["));
                    boost::replace_all(name, "1x", loc);
                    return name;
            }
        }
        if (boost::starts_with(tile,"TILE[")  &&  boost::algorithm::contains(name,".FE")) {
            std::string last = name.substr(name.rfind('.') + 1);
            if (last[0]=='D') {
                boost::replace_all(name, ".D", ".");
                boost::replace_all(name, ".FE", ".DFF");
                return name;
            }
            if (last=="L" || last=="R") {
                boost::replace_all(name, ".FE", ".DFF");
                return name;
            }
            if (last=="CK") {
                boost::replace_all(name, ".FE", ".DFF");
                return name;
            }
            if (last[0]=='L') {
                boost::replace_all(name, ".L", ".");
                boost::replace_all(name, ".FE", ".LUT");
                return name;
            }
            if (last[0]=='P') {
                boost::replace_all(name, ".PI", ".I");
                boost::replace_all(name, ".FE", ".LUT");
                return name;
            }
        }
        return name;
    }

    void add_net(std::set<std::string> &nets, std::string src_tile, std::string src_name, std::string dst_tile, std::string dst_name, IdString src_type, IdString dst_type)
    {
        if (src_type.in(ctx->id("CROSSBAR_INPUT_WIRE"), ctx->id("LUT_PERMUTATION_WIRE"), ctx->id("MUX_WIRE"), ctx->id("INTERCONNECT_INPUT"))) return;
        if (dst_type == ctx->id("MUX_WIRE"))
            dst_name = dst_name.substr(0, dst_name.rfind('.'));
        src_name = update_name(src_tile, src_name);
        dst_name = update_name(dst_tile, dst_name);

        nets.emplace(stringf("%s:%s->%s:%s", src_tile.c_str(), src_name.c_str(), dst_tile.c_str(), dst_name.c_str()));
    }
                
    std::string cleanup_name(std::string name)
    {
        std::replace(name.begin(), name.end(), '$', '_'); 
        return name;
    }

    void write_nets()
    {
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (ni->wires.size()==0) continue;
            auto& cfg = json["nets"][cleanup_name(ni->name.c_str(ctx))];
            std::set<std::string> nets;
            for (auto &w : ni->wires) {
                if (w.second.pip != PipId()) {
                    PipId pip = w.second.pip;
                    auto &pd = chip_pip_info(ctx->chip_info, pip);
                    const auto &extra_data = *reinterpret_cast<const NGUltraPipExtraDataPOD *>(pd.extra_data.get());
                    WireId swire = ctx->getPipSrcWire(pip);
                    IdString src = ctx->getWireName(swire)[1];
                    IdString src_type = ctx->getWireType(swire);

                    IdString src_orig = IdString(chip_tile_info(ctx->chip_info, pip.tile).wires[pd.src_wire].name);
                    IdString src_orig_type = IdString(chip_tile_info(ctx->chip_info, pip.tile).wires[pd.src_wire].wire_type);

                    WireId dwire = ctx->getPipDstWire(pip);
                    IdString dst = ctx->getWireName(dwire)[1];
                    IdString dst_type = ctx->getWireType(dwire);

                    std::string s_tile_name = uarch->tile_name(swire.tile);
                    std::string tile_name = uarch->tile_name(pip.tile);
                   
                    if (src_orig!=src)
                        add_net(nets, s_tile_name, src.c_str(ctx), tile_name, src_orig.c_str(ctx), src_type, src_orig_type);
                    if (!extra_data.name || (extra_data.type != PipExtra::PIP_EXTRA_BYPASS && extra_data.type != PipExtra::PIP_EXTRA_VIRTUAL && extra_data.type != PipExtra::PIP_EXTRA_MUX))
                        add_net(nets, tile_name, src_orig.c_str(ctx), tile_name, dst.c_str(ctx), src_orig_type, dst_type);
                } else if (ni->wires.size()==1) {
                    IdString src = ctx->getWireName(w.first)[1];
                    IdString src_type = ctx->getWireType(w.first);
                    std::string s_tile_name = uarch->tile_name(w.first.tile);
                    for (auto &u : ni->users){
                        std::string tile_name = uarch->tile_name(u.cell->bel.tile);
                        IdString bel_name = ctx->getBelName(u.cell->bel)[1];
                        add_net(nets, s_tile_name, src.c_str(ctx), tile_name, stringf("%s.%s", bel_name.c_str(ctx), u.port.c_str(ctx)), src_type, src_type);
                    }
                }
            }           
            cfg = nets;
        }

    }


    template <typename KeyType>
    std::string str_or_n_value(const dict<KeyType, Property> &ct, const KeyType &key, std::string def = "N")
    {
        auto found = ct.find(key);
        if (found == ct.end())
            return def;
        else {
            if (!found->second.is_string)
                log_error("Expecting string value but got integer %d.\n", int(found->second.intval));
            if (found->second.as_string().empty())
                return def;
            return found->second.as_string();
        }
    };


    nlohmann::json &get_cfg(CellInfo *cell, std::string type = "", std::string rename = "")
    {
        auto& cfg = json["instances"][cleanup_name(rename.empty() ? cell->name.c_str(ctx) : rename.c_str())];
        std::string tile_name = uarch->tile_name(cell->bel.tile);
        IdString idx = ctx->getBelName(cell->bel)[1];
        std::string belname = idx.c_str(ctx);
        cfg["location"] = tile_name + ":" + belname;
        cfg["type"] = type.empty() ? str_or_default(cell->params, ctx->id("type"), "") : type;
        return cfg;
    }

    nlohmann::json &get_cfg_fe(CellInfo *cell, std::string type, std::string replace, std::string postfix = "")
    {
        auto& cfg = json["instances"][cleanup_name(cell->name.c_str(ctx)) + postfix];
        std::string tile_name = uarch->tile_name(cell->bel.tile);
        IdString idx = ctx->getBelName(cell->bel)[1];
        std::string belname = idx.c_str(ctx);
        boost::replace_all(belname, ".FE", replace);
        cfg["location"] = tile_name + ":" + belname;
        cfg["type"] = type;
        return cfg;
    }

    void write_iop(CellInfo *cell) {
        auto& cfg = get_cfg(cell, "", str_or_default(cell->params, ctx->id("iobname"), ""));
        //cfg["config"]["alias_vhdl"] = str_or_default(cell->params, ctx->id("alias_vhdl"), "");
        //cfg["config"]["alias_vlog"] = str_or_default(cell->params, ctx->id("alias_vlog"), "");
        //cfg["config"]["differential"] = str_or_n_value(cell->params, ctx->id("differential"), "N");
        cfg["config"]["drive"] = str_or_default(cell->params, ctx->id("drive"), "2mA");
        //cfg["config"]["dynDrive"] = str_or_n_value(cell->params, ctx->id("dynDrive"), "N");
        //cfg["config"]["dynInput"] = str_or_n_value(cell->params, ctx->id("dynInput"), "N");
        //cfg["config"]["dynTerm"] = str_or_n_value(cell->params, ctx->id("dynTerm"), "N");
        //cfg["config"]["extra"] = int_or_default(cell->params, ctx->id("extra"), 2);
        //cfg["config"]["inputDelayLine"] = str_or_default(cell->params, ctx->id("inputDelayLine"), "");
        //cfg["config"]["inputDelayOn"] = str_or_n_value(cell->params, ctx->id("inputDelayOn"), "N");
        //cfg["config"]["inputSignalSlope"] = str_or_default(cell->params, ctx->id("inputSignalSlope"), "");
        cfg["config"]["location"] = str_or_default(cell->params, ctx->id("location"), "");
        //cfg["config"]["locked"] = bool_or_default(cell->params, ctx->id("locked"), false);
        //cfg["config"]["outputCapacity"] = str_or_default(cell->params, ctx->id("outputCapacity"), "");
        //cfg["config"]["outputDelayLine"] = str_or_default(cell->params, ctx->id("outputDelayLine"), "");
        //cfg["config"]["outputDelayOn"] = str_or_n_value(cell->params, ctx->id("outputDelayOn"), "N");
        //cfg["config"]["slewRate"] = str_or_default(cell->params, ctx->id("slewRate"), "");
        cfg["config"]["standard"] = str_or_default(cell->params, ctx->id("standard"), "LVCMOS");
        //cfg["config"]["termination"] = str_or_n_value(cell->params, ctx->id("termination"), "N");
        //cfg["config"]["terminationReference"] = str_or_n_value(cell->params, ctx->id("terminationReference"), "N");
        //cfg["config"]["turbo"] = str_or_n_value(cell->params, ctx->id("turbo"), "N");
        //cfg["config"]["weakTermination"] = str_or_n_value(cell->params, ctx->id("weakTermination"), "N");

        std::string tile_name = uarch->tile_name(cell->bel.tile);
        std::string bank = tile_name.substr(0, tile_name.rfind(':'));
        if (uarch->bank_voltage.count(bank)==0) {
            if (bank=="IOB0" || bank=="IOB1" || bank=="IOB6" || bank=="IOB7")
                uarch->bank_voltage[bank] = "3.3V";
            else
                uarch->bank_voltage[bank] = "1.8V";
        }
    }

    void write_dfr(CellInfo *cell) {
        auto& cfg = get_cfg(cell, "BFR");
        //cfg["config"]["data_inv"] = bool_or_default(cell->params, ctx->id("data_inv"), false);
        cfg["config"]["dff_edge"] = bool_or_default(cell->params, ctx->id("dff_edge"), false);
        //cfg["config"]["dff_init"] = bool_or_default(cell->params, ctx->id("dff_init"), false);
        //cfg["config"]["dff_load"] = bool_or_default(cell->params, ctx->id("dff_load"), false);
        cfg["config"]["dff_sync"] = bool_or_default(cell->params, ctx->id("dff_sync"), false);
        //cfg["config"]["dff_type"] = bool_or_default(cell->params, ctx->id("dff_type"), false);
        //cfg["config"]["location"] = str_or_default(cell->params, ctx->id("location"), "");
        cfg["config"]["mode"] = int_or_default(cell->params, ctx->id("mode"), 2);
        //cfg["config"]["path"] = int_or_default(cell->params, ctx->id("path"), 0);
        cfg["config"]["iobname"] = str_or_default(cell->params, ctx->id("iobname"), "");
    }

    void write_cy(CellInfo *cell) {
        auto& cfg = get_cfg(cell, "CY");
        cfg["config"]["add_carry"] = int_or_default(cell->params, ctx->id("add_carry"), 0);
        cfg["config"]["shifter"] = bool_or_default(cell->params, ctx->id("shifter"), false);
    }

    void write_fe(CellInfo *cell) {
        if (bool_or_default(cell->params, id_lut_used)) {
            auto& cfg = get_cfg_fe(cell, "LUT", ".LUT");
            Property init = get_or_default(cell->params, id_lut_table, Property()).extract(0, 16);
            std::string lut = init.str;
            std::reverse(lut.begin(), lut.end());
            cfg["config"]["lut_table"] = lut;
        }
        if (bool_or_default(cell->params, id_dff_used)) {
            std::string subtype = str_or_default(cell->params, ctx->id("type"), "DFF");
            auto& cfg = get_cfg_fe(cell, subtype, ".DFF", "_D");
            if (subtype =="DFF") {
                //cfg["config"]["dff_ctxt"] = int_or_default(cell->params, ctx->id("dff_ctxt"), 0);
                cfg["config"]["dff_edge"] = bool_or_default(cell->params, ctx->id("dff_edge"), false);
                cfg["config"]["dff_init"] = bool_or_default(cell->params, ctx->id("dff_init"), false);
                cfg["config"]["dff_load"] = bool_or_default(cell->params, ctx->id("dff_load"), false);
                cfg["config"]["dff_sync"] = bool_or_default(cell->params, ctx->id("dff_sync"), false);
                cfg["config"]["dff_type"] = bool_or_default(cell->params, ctx->id("dff_type"), false);
            }
        }
    }

    void write_iom(CellInfo *cell) {
        auto& cfg = get_cfg(cell, "IOM");
        cfg["config"]["pads_path"] = str_or_default(cell->params, ctx->id("pads_path"), ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;");
    }

    void write_gck(CellInfo *cell) {
        auto& cfg = get_cfg(cell, "GCK");
        cfg["config"]["inv_in"] = bool_or_default(cell->params, ctx->id("inv_in"), false);
        cfg["config"]["inv_out"] = bool_or_default(cell->params, ctx->id("inv_out"), false);
        cfg["config"]["std_mode"] = str_or_default(cell->params, ctx->id("std_mode"), "BYPASS");
    }

    void write_wfg(CellInfo *cell) {
        std::string subtype = str_or_default(cell->params, ctx->id("type"), "WFB");
        get_cfg(cell, subtype);
    }

    void write_interconnections()
    {
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (ni->wires.size()==0) continue;
            std::vector<std::string> nets;
            for (auto &w : ni->wires) {
                if (w.second.pip != PipId()) {
                    PipId pip = w.second.pip;
                    const auto &pip_data = chip_pip_info(ctx->chip_info, w.second.pip);
                    const auto &extra_data = *reinterpret_cast<const NGUltraPipExtraDataPOD *>(pip_data.extra_data.get());
                    if (!extra_data.name or extra_data.type != PipExtra::PIP_EXTRA_INTERCONNECT) continue;
                    auto &pd = chip_pip_info(ctx->chip_info, pip);
                    IdString src = IdString(chip_tile_info(ctx->chip_info, pip.tile).wires[pd.src_wire].name);
                    std::string tile_name = uarch->tile_name(pip.tile);
                    std::string src_name = src.c_str(ctx);
                    std::string type = "OTC";
                    if (src_name.find("UI1x") != std::string::npos) 
                        type = "ITC";
                    if (boost::starts_with(src_name,"SO1.")) type = "OTS";
                    if (boost::starts_with(src_name,"SI1.")) type = "ITS";
                    std::string name = cleanup_name(std::string(ni->name.c_str(ctx))+ "_" + type);
                    while (json["instances"].contains(name))
                        name += "_2";
                    auto& cfg = json["instances"][name];
                    cfg["type"] = type;
                    src_name = update_name(tile_name, src_name);
                    src_name = src_name.substr(0, src_name.size() - 2);
                    cfg["location"] = tile_name + ":" + src_name;
                }
            }
        }
    }

    void write_instances()
    {
        for (auto &cell : ctx->cells) {
            switch (cell.second->type.index) {
                case id_IOP.index : write_iop(cell.second.get()); break;
                case id_IOTP.index : write_iop(cell.second.get()); break;
                case id_BEYOND_FE.index : write_fe(cell.second.get()); break;
                case id_CY.index : write_cy(cell.second.get()); break;
                case id_WFG.index : write_wfg(cell.second.get()); break;
                case id_GCK.index : write_gck(cell.second.get()); break;
                case id_IOM.index : write_iom(cell.second.get()); break;
                case id_DDFR.index: write_dfr(cell.second.get()); break;
                case id_DFR.index: write_dfr(cell.second.get()); break;
                //case id_XLUT.index:
                //case id_RAM.index:
                //case id_RF.index:
                //case id_XRF.index:
                //case id_FIFO.index:
                //case id_XFIFO.index:
                //case id_CDC.index:
                //case id_XCDC.index:
                //case id_CRX.index:
                //case id_CTX.index:
                //case id_DSP.index:
                //case id_PLL.index:
                //case id_PMA.index:
                //case id_Service.index:
                //case id_SOCIF.index:
            }
        }
        write_interconnections();
    }
    
    void write_setup()
    {
        json["setup"]["variant"] = "NG-ULTRA";
        auto& cfg = json["setup"]["iobanks"];
        for (auto &bank : uarch->bank_voltage)
            cfg[bank.first] = bank.second;
    }

    void write_json()
    {
        write_nets();
        write_instances();
        write_setup();

        out << std::setw(4) << json << std::endl;
    }
};

} // namespace

void NgUltraImpl::write_bitstream_json(const std::string &filename)
{
    std::ofstream out(filename);
    if (!out)
        log_error("failed to open file %s for writing (%s)\n", filename.c_str(), strerror(errno));

    BitstreamJsonBackend be(ctx, this, out);
    be.write_json();
}

NEXTPNR_NAMESPACE_END

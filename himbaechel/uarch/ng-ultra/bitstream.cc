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
    NgUltraImpl *uarch;
    std::ostream &out;
    bool first_instance;

    BitstreamJsonBackend(Context *ctx, NgUltraImpl *uarch, std::ostream &out) : ctx(ctx), uarch(uarch), out(out){};

    std::string get_string(std::string str)
    {
        std::string newstr = "\"";
        for (char c : str) {
            if (c == '\\')
                newstr += c;
            newstr += c;
        }
        return newstr + "\"";
    }

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
        out << "\t\"nets\": {\n";
        bool first_net = true;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (ni->wires.size()==0) continue;
            out << (first_net ? "" : ",\n"); first_net = false;
            out << stringf("\t\t%s: [\n", get_string(cleanup_name(ni->name.c_str(ctx))).c_str());
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
            bool first = true;
            for (auto &str : nets) {
                out << (first ? "" : ",\n");
                out << stringf("\t\t\t%s",get_string(str).c_str());
                first = false;
            }
            out << "\n\t\t]";
        }
        out << "\n\t},\n";
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

    template <typename KeyType> std::string extract_bits_or_default(const dict<KeyType, Property> &ct, const KeyType &key, int bits, int def = 0)
    {
        Property extr = get_or_default(ct, key, Property()).extract(0, bits);
        std::string str = extr.str;
        std::reverse(str.begin(), str.end());
        return str;
    };

    std::vector<std::string> config;

    void open_instance(CellInfo *cell, std::string type = "", std::string rename = "")
    {
        out << stringf("%s", first_instance ? "" : ",\n"); first_instance = false;
        out << stringf("\t\t%s: {\n", get_string(cleanup_name(rename.empty() ? cell->name.c_str(ctx) : rename.c_str())).c_str());
        std::string tile_name = uarch->tile_name(cell->bel.tile);
        IdString idx = ctx->getBelName(cell->bel)[1];
        std::string belname = idx.c_str(ctx);
        config.clear();
        out << stringf("\t\t\t\"location\": %s,\n",get_string(tile_name + ":" + belname).c_str());
        out << stringf("\t\t\t\"type\": %s",get_string(type.empty() ? str_or_default(cell->params, ctx->id("type"), "") : type).c_str());
    }

    void open_instance_fe(CellInfo *cell, std::string type, std::string replace, std::string postfix = "")
    {
        out << stringf("%s", first_instance ? "" : ",\n"); first_instance = false;
        out << stringf("\t\t%s: {\n", get_string(cleanup_name(cell->name.c_str(ctx)) + postfix).c_str());
        std::string tile_name = uarch->tile_name(cell->bel.tile);
        IdString idx = ctx->getBelName(cell->bel)[1];
        std::string belname = idx.c_str(ctx);
        boost::replace_all(belname, ".FE", replace);
        config.clear();
        out << stringf("\t\t\t\"location\": %s,\n",get_string(tile_name + ":" + belname).c_str());
        out << stringf("\t\t\t\"type\": %s",get_string(type).c_str());
    }

    inline void add_config(std::string name, int val)
    {
        config.push_back(stringf("\t\t\t\t%s:%d", get_string(name).c_str(), val));
    }

    inline void add_config(std::string name, bool val)
    {
        config.push_back(stringf("\t\t\t\t%s:%s", get_string(name).c_str(), val ? "true" : "false"));
    }

    inline void add_config(std::string name, std::string val)
    {
        config.push_back(stringf("\t\t\t\t%s:%s", get_string(name).c_str(), get_string(val).c_str()));
    }

    void close_instance() {
        bool first = true;
        if (!config.empty()) out << ",\n\t\t\t\"config\": {\n";
        for (auto &str : config) {
            out << (first ? "" : ",\n");
            out << str.c_str();
            first = false;
        }
        if (!config.empty()) out << "\n\t\t\t}";
        out << "\n\t\t}";
        config.clear();
    }

    void write_iop(CellInfo *cell) {
        open_instance(cell, "", str_or_default(cell->params, ctx->id("iobname"), ""));      
        //add_config("alias_vhdl", str_or_default(cell->params, ctx->id("alias_vhdl"), ""));
        //add_config("alias_vlog", str_or_default(cell->params, ctx->id("alias_vlog"), ""));
        //add_config("differential", str_or_n_value(cell->params, ctx->id("differential"), "N"));
        add_config("drive", str_or_default(cell->params, ctx->id("drive"), "2mA"));
        //add_config("dynDrive", str_or_n_value(cell->params, ctx->id("dynDrive"), "N"));
        //add_config("dynInput", str_or_n_value(cell->params, ctx->id("dynInput"), "N"));
        //add_config("dynTerm", str_or_n_value(cell->params, ctx->id("dynTerm"), "N"));
        //add_config("extra", int_or_default(cell->params, ctx->id("extra"), 2));
        //add_config("inputDelayLine", str_or_default(cell->params, ctx->id("inputDelayLine"), ""));
        //add_config("inputDelayOn", str_or_n_value(cell->params, ctx->id("inputDelayOn"), "N"));
        //add_config("inputSignalSlope", str_or_default(cell->params, ctx->id("inputSignalSlope"), ""));
        add_config("location", str_or_default(cell->params, ctx->id("location"), ""));
        //add_config("locked", bool_or_default(cell->params, ctx->id("locked"), false));
        //add_config("outputCapacity", str_or_default(cell->params, ctx->id("outputCapacity"), ""));
        //add_config("outputDelayLine", str_or_default(cell->params, ctx->id("outputDelayLine"), ""));
        //add_config("outputDelayOn", str_or_n_value(cell->params, ctx->id("outputDelayOn"), "N"));
        //add_config("slewRate", str_or_default(cell->params, ctx->id("slewRate"), ""));
        add_config("standard", str_or_default(cell->params, ctx->id("standard"), "LVCMOS"));
        //add_config("termination", str_or_n_value(cell->params, ctx->id("termination"), "N"));
        //add_config("terminationReference", str_or_n_value(cell->params, ctx->id("terminationReference"), "N"));
        //add_config("turbo", str_or_n_value(cell->params, ctx->id("turbo"), "N"));
        //add_config("weakTermination", str_or_n_value(cell->params, ctx->id("weakTermination"), "N"));

        close_instance();
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
        open_instance(cell, "BFR");
        //add_config("data_inv", bool_or_default(cell->params, ctx->id("data_inv"), false));
        //add_config("dff_edge", bool_or_default(cell->params, ctx->id("dff_edge"), false));
        //add_config("dff_init", bool_or_default(cell->params, ctx->id("dff_init"), false));
        //add_config("dff_load", bool_or_default(cell->params, ctx->id("dff_load"), false));
        //add_config("dff_sync", bool_or_default(cell->params, ctx->id("dff_sync"), false));
        //add_config("dff_type", bool_or_default(cell->params, ctx->id("dff_type"), false));
        //add_config("location", str_or_default(cell->params, ctx->id("location"), ""));
        add_config("mode", int_or_default(cell->params, ctx->id("mode"), 2));
        //add_config("path", int_or_default(cell->params, ctx->id("path"), 0));
        add_config("iobname", str_or_default(cell->params, ctx->id("iobname"), ""));
        if (cell->params.count(ctx->id("data_inv"))) {
            add_config("data_inv", bool_or_default(cell->params, ctx->id("data_inv"), false));
        }
        close_instance();
    }

    void write_cy(CellInfo *cell) {
        open_instance(cell, "CY");
        add_config("add_carry", int_or_default(cell->params, ctx->id("add_carry"), 0));
        add_config("shifter", bool_or_default(cell->params, ctx->id("shifter"), false));
        close_instance();
    }

    void write_fe(CellInfo *cell) {
        if (bool_or_default(cell->params, id_lut_used)) {
            open_instance_fe(cell, "LUT", ".LUT");
            add_config("lut_table", extract_bits_or_default(cell->params, id_lut_table, 16));
            close_instance();
        }
        if (bool_or_default(cell->params, id_dff_used)) {
            std::string subtype = str_or_default(cell->params, ctx->id("type"), "DFF");
            open_instance_fe(cell, subtype, ".DFF", "_D");
            if (subtype =="DFF") {
                add_config("dff_ctxt", std::to_string(int_or_default(cell->params, ctx->id("dff_ctxt"), 0)));
                add_config("dff_edge", bool_or_default(cell->params, ctx->id("dff_edge"), false));
                add_config("dff_init", bool_or_default(cell->params, ctx->id("dff_init"), false));
                add_config("dff_load", bool_or_default(cell->params, ctx->id("dff_load"), false));
                add_config("dff_sync", bool_or_default(cell->params, ctx->id("dff_sync"), false));
                add_config("dff_type", bool_or_default(cell->params, ctx->id("dff_type"), false));
            }
            close_instance();
        }
    }

    void write_iom(CellInfo *cell) {
        open_instance(cell, "IOM");
        add_config("pads_path", str_or_default(cell->params, ctx->id("pads_path"), ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;"));
        close_instance();
    }

    void write_gck(CellInfo *cell) {
        open_instance(cell, "GCK");
        add_config("inv_in", bool_or_default(cell->params, ctx->id("inv_in"), false));
        add_config("inv_out", bool_or_default(cell->params, ctx->id("inv_out"), false));
        add_config("std_mode", str_or_default(cell->params, ctx->id("std_mode"), "BYPASS"));
        close_instance();
    }

    void write_wfg(CellInfo *cell) {
        std::string subtype = str_or_default(cell->params, ctx->id("type"), "WFB");
        open_instance(cell, subtype);
        close_instance();
    }

    void write_rf(CellInfo *cell) {
        int mode = int_or_default(cell->params, ctx->id("mode"), 0);
        switch(mode) {
            case 0 : open_instance(cell, "RF"); break;
            case 1 : open_instance(cell, "RFSP"); break;
            case 2 : open_instance(cell, "XHRF"); break;
            case 3 : open_instance(cell, "XWRF"); break;
            case 4 : open_instance(cell, "XPRF"); break;
            default:
                log_error("Unknown mode %d for cell '%s'.\n", mode, cell->name.c_str(ctx));
        }        
        add_config("context", str_or_default(cell->params, ctx->id("mem_ctxt"), ""));
        add_config("wck_edge", bool_or_default(cell->params, ctx->id("wck_edge"), false));
        close_instance();
    }

    void write_ram(CellInfo *cell) {
        open_instance(cell, "RAM");
        add_config("mcka_edge", bool_or_default(cell->params, ctx->id("mcka_edge"), false));
        add_config("mckb_edge", bool_or_default(cell->params, ctx->id("mckb_edge"), false));
        add_config("pcka_edge", bool_or_default(cell->params, ctx->id("pcka_edge"), false));
        add_config("pckb_edge", bool_or_default(cell->params, ctx->id("pckb_edge"), false));
        add_config("raw_config0", extract_bits_or_default(cell->params, ctx->id("raw_config0"), 4));
        add_config("raw_config1", extract_bits_or_default(cell->params, ctx->id("raw_config1"), 16));
        add_config("context", str_or_default(cell->params, ctx->id("mem_ctxt"), ""));
        close_instance();
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
                    src_name = update_name(tile_name, src_name);
                    src_name = src_name.substr(0, src_name.size() - 2);

                    std::string name = cleanup_name(std::string(ni->name.c_str(ctx))+ "_" + src_name.substr(4));
                    out << stringf(",\n\t\t%s: {\n", get_string(name).c_str());
                    out << stringf("\t\t\t\"location\": %s,\n",get_string(tile_name + ":" + src_name).c_str());
                    out << stringf("\t\t\t\"type\": %s\n\t\t}",get_string(type).c_str());
                }
            }
        }
    }

    void write_instances()
    {
        out << "\t\"instances\": {\n";
        first_instance = true;
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
                case id_RAM.index: write_ram(cell.second.get()); break;
                //case id_RF.index:
                case id_RF.index: write_rf(cell.second.get()); break;
                case id_XRF.index: write_rf(cell.second.get()); break;
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
        out << "\n\t},\n";
    }
  
    void write_setup()
    {
        out << "\t\"setup\": {\n";
        out << "\t\t\"variant\": \"NG-ULTRA\",\n";
        out << "\t\t\"iobanks\": {\n";
        bool first = true;
        for (auto &bank : uarch->bank_voltage) {
            out << (first ? "" : ",\n");
            out << stringf("\t\t\t%s:%s",get_string(bank.first).c_str(),get_string(bank.second).c_str());
            first = false;
        }
        out << "\n\t\t}\n\t}\n";
    }

    void write_json()
    {
        out << "{\n";
        write_nets();
        write_instances();
        write_setup();
        out << "}\n";
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

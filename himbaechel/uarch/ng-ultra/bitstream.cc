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
        if (src_type.in(ctx->id("LUT_PERMUTATION_WIRE"), ctx->id("MUX_WIRE"), ctx->id("INTERCONNECT_INPUT"))) return;
        if (boost::starts_with(src_type.c_str(ctx),"CROSSBAR_") && boost::ends_with(src_type.c_str(ctx),"INPUT_WIRE")) return;
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

    template <typename KeyType>
    std::string str_or_n_value_lower(const dict<KeyType, Property> &ct, const KeyType &key, std::string def = "N")
    {
        auto found = ct.find(key);
        if (found == ct.end())
            return def;
        else {
            if (!found->second.is_string)
                log_error("Expecting string value but got integer %d.\n", int(found->second.intval));
            if (found->second.as_string().empty())
                return def;
            std::string tmp = found->second.as_string();
            boost::algorithm::to_lower(tmp);
            return tmp;
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

    void open_instance(CellInfo *cell, std::string rename = "")
    {
        out << stringf("%s", first_instance ? "" : ",\n"); first_instance = false;
        out << stringf("\t\t%s: {\n", get_string(cleanup_name(rename.empty() ? cell->name.c_str(ctx) : rename.c_str())).c_str());
        std::string tile_name = uarch->tile_name(cell->bel.tile);
        IdString idx = ctx->getBelName(cell->bel)[1];
        std::string belname = idx.c_str(ctx);
        config.clear();
        out << stringf("\t\t\t\"location\": %s,\n",get_string(tile_name + ":" + belname).c_str());
        out << stringf("\t\t\t\"type\": %s",get_string(cell->type.c_str(ctx)).c_str());
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
        open_instance(cell, str_or_default(cell->params, ctx->id("iobname"), ""));
        add_config("location", str_or_default(cell->params, ctx->id("location"), ""));
        add_config("differential", str_or_n_value_lower(cell->params, ctx->id("differential"), "false"));
        add_config("slewRate", str_or_default(cell->params, ctx->id("slewRate"), "Medium"));
        add_config("turbo", str_or_n_value_lower(cell->params, ctx->id("turbo"), "false"));
        add_config("weakTermination", str_or_n_value(cell->params, ctx->id("weakTermination"), "PullUp"));
        add_config("inputDelayLine", str_or_default(cell->params, ctx->id("inputDelayLine"), "0"));
        add_config("outputDelayLine", str_or_default(cell->params, ctx->id("outputDelayLine"), "0"));
        add_config("inputSignalSlope", str_or_default(cell->params, ctx->id("inputSignalSlope"), "0"));
        add_config("outputCapacity", str_or_default(cell->params, ctx->id("outputCapacity"), "0"));
        add_config("standard", str_or_default(cell->params, ctx->id("standard"), "LVCMOS"));
        add_config("drive", str_or_default(cell->params, ctx->id("drive"), "2mA"));
        add_config("inputDelayOn", str_or_n_value_lower(cell->params, ctx->id("inputDelayOn"), "false"));
        add_config("outputDelayOn", str_or_n_value_lower(cell->params, ctx->id("outputDelayOn"), "false"));
        add_config("dynDrive", str_or_n_value_lower(cell->params, ctx->id("dynDrive"), "false"));
        add_config("dynInput", str_or_n_value_lower(cell->params, ctx->id("dynInput"), "false"));
        add_config("dynTerm", str_or_n_value_lower(cell->params, ctx->id("dynTerm"), "false"));
        if (cell->type.in(id_OTP, id_ITP, id_IOTP)) {
            add_config("termination", str_or_n_value(cell->params, ctx->id("termination"), "0"));
            add_config("terminationReference", str_or_n_value(cell->params, ctx->id("terminationReference"), "VT"));
        }
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
        open_instance(cell);
        add_config("data_inv", bool_or_default(cell->params, ctx->id("data_inv"), false));
        add_config("dff_edge", bool_or_default(cell->params, ctx->id("dff_edge"), false));
        add_config("dff_init", bool_or_default(cell->params, ctx->id("dff_init"), false));
        add_config("dff_load", bool_or_default(cell->params, ctx->id("dff_load"), false));
        add_config("dff_sync", bool_or_default(cell->params, ctx->id("dff_sync"), false));
        add_config("dff_type", bool_or_default(cell->params, ctx->id("dff_type"), false));
        add_config("mode", int_or_default(cell->params, ctx->id("mode"), 3));
        add_config("iobname", str_or_default(cell->params, ctx->id("iobname"), ""));
        close_instance();
    }

    void write_bfr(CellInfo *cell) {
        open_instance(cell);
        add_config("mode", int_or_default(cell->params, ctx->id("mode"), 2));
        add_config("iobname", str_or_default(cell->params, ctx->id("iobname"), ""));
        if (cell->params.count(ctx->id("data_inv"))) {
            add_config("data_inv", bool_or_default(cell->params, ctx->id("data_inv"), false));
        }
        close_instance();
    }

    void write_cy(CellInfo *cell) {
        open_instance(cell);
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

    void write_xlut(CellInfo *cell) {
        open_instance(cell);
        add_config("lut_table", extract_bits_or_default(cell->params, id_lut_table, 16));
        close_instance();
    }

    void write_iom(CellInfo *cell) {
        open_instance(cell);
        add_config("pads_path", str_or_default(cell->params, ctx->id("pads_path"), ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;"));
        close_instance();
    }

    void write_gck(CellInfo *cell) {
        open_instance(cell);
        add_config("inv_in", bool_or_default(cell->params, ctx->id("inv_in"), false));
        add_config("inv_out", bool_or_default(cell->params, ctx->id("inv_out"), false));
        add_config("std_mode", str_or_default(cell->params, ctx->id("std_mode"), "BYPASS"));
        close_instance();
    }

    void write_wfb(CellInfo *cell) {
        open_instance(cell);
        add_config("delay_on", bool_or_default(cell->params, ctx->id("delay_on"), false));
        add_config("delay", int_or_default(cell->params, ctx->id("delay"), 0));
        add_config("wfg_edge", bool_or_default(cell->params, ctx->id("wfg_edge"), false));
        close_instance();
    }

    void write_wfg(CellInfo *cell) {
        open_instance(cell);
        add_config("mode", int_or_default(cell->params, ctx->id("mode"), 0));
        add_config("delay_on", bool_or_default(cell->params, ctx->id("delay_on"), false));
        add_config("delay", int_or_default(cell->params, ctx->id("delay"), 0));
        add_config("wfg_edge", bool_or_default(cell->params, ctx->id("wfg_edge"), false));
        add_config("pattern", extract_bits_or_default(cell->params, ctx->id("pattern"), 16));
        add_config("pattern_end", int_or_default(cell->params, ctx->id("pattern_end"), 0));
        add_config("div_ratio", int_or_default(cell->params, ctx->id("div_ratio"), 0));
        add_config("div_phase", bool_or_default(cell->params, ctx->id("div_phase"), false));
        add_config("reset_on_pll_lock_n", bool_or_default(cell->params, ctx->id("reset_on_pll_lock_n"), false));
        add_config("reset_on_pll_locka_n", bool_or_default(cell->params, ctx->id("reset_on_pll_locka_n"), false));
        add_config("reset_on_cal_lock_n", bool_or_default(cell->params, ctx->id("reset_on_cal_lock_n"), false));        
        close_instance();
    }

    void write_pll(CellInfo *cell) {
        open_instance(cell);
        add_config("clk_outdiv1", int_or_default(cell->params, ctx->id("clk_outdiv1"), 0));
        add_config("clk_outdiv2", int_or_default(cell->params, ctx->id("clk_outdiv2"), 0));
        add_config("clk_outdiv3", int_or_default(cell->params, ctx->id("clk_outdiv3"), 0));
        add_config("clk_outdiv4", int_or_default(cell->params, ctx->id("clk_outdiv4"), 0));
        add_config("clk_outdivd1", int_or_default(cell->params, ctx->id("clk_outdivd1"), 0));
        add_config("clk_outdivd2", int_or_default(cell->params, ctx->id("clk_outdivd2"), 0));
        add_config("clk_outdivd3", int_or_default(cell->params, ctx->id("clk_outdivd3"), 0));
        add_config("clk_outdivd4", int_or_default(cell->params, ctx->id("clk_outdivd4"), 0));
        add_config("clk_outdivd5", int_or_default(cell->params, ctx->id("clk_outdivd5"), 0));
        add_config("use_cal", bool_or_default(cell->params, ctx->id("use_cal"), false));
        add_config("clk_cal_sel", int_or_default(cell->params, ctx->id("clk_cal_sel"), 0));
        add_config("pll_odf", int_or_default(cell->params, ctx->id("pll_odf"), 0));
        add_config("pll_lpf_res", int_or_default(cell->params, ctx->id("pll_lpf_res"), 0));
        add_config("pll_lpf_cap", int_or_default(cell->params, ctx->id("pll_lpf_cap"), 0));
        add_config("cal_div", int_or_default(cell->params, ctx->id("cal_div"), 0));
        add_config("cal_delay", int_or_default(cell->params, ctx->id("cal_delay"), 0));
        add_config("use_pll", bool_or_default(cell->params, ctx->id("use_pll"), true));
        add_config("ref_intdiv", int_or_default(cell->params, ctx->id("ref_intdiv"), 0));
        add_config("ref_osc_on", bool_or_default(cell->params, ctx->id("ref_osc_on"), false));
        add_config("pll_cpump", int_or_default(cell->params, ctx->id("pll_cpump"), 0));
        add_config("pll_lock", int_or_default(cell->params, ctx->id("pll_lock"), 0));
        add_config("ext_fbk_on", bool_or_default(cell->params, ctx->id("ext_fbk_on"), false));
        add_config("fbk_intdiv", int_or_default(cell->params, ctx->id("fbk_intdiv"), 0));
        add_config("fbk_delay_on", bool_or_default(cell->params, ctx->id("fbk_delay_on"), false));
        add_config("fbk_delay", int_or_default(cell->params, ctx->id("fbk_delay"), 0));
        close_instance();
    }

    void write_rfb(CellInfo *cell) {
        open_instance(cell);
        std::string context = str_or_default(cell->params, ctx->id("mem_ctxt"), "");
        if (!context.empty()) add_config("mem_ctxt", context);
        add_config("wck_edge", bool_or_default(cell->params, ctx->id("wck_edge"), false));
        close_instance();
    }

    void write_ram(CellInfo *cell) {
        open_instance(cell);
        add_config("mcka_edge", bool_or_default(cell->params, ctx->id("mcka_edge"), false));
        add_config("mckb_edge", bool_or_default(cell->params, ctx->id("mckb_edge"), false));
        add_config("pcka_edge", bool_or_default(cell->params, ctx->id("pcka_edge"), false));
        add_config("pckb_edge", bool_or_default(cell->params, ctx->id("pckb_edge"), false));
        add_config("raw_config0", extract_bits_or_default(cell->params, ctx->id("raw_config0"), 4));
        add_config("raw_config1", extract_bits_or_default(cell->params, ctx->id("raw_config1"), 16));
        std::string context = str_or_default(cell->params, ctx->id("mem_ctxt"), "");
        if (!context.empty()) add_config("mem_ctxt", context);
        close_instance();
    }

    void write_dsp(CellInfo *cell) {
        open_instance(cell);
        add_config("raw_config0", extract_bits_or_default(cell->params, ctx->id("raw_config0"), 27));
        add_config("raw_config1", extract_bits_or_default(cell->params, ctx->id("raw_config1"), 24));
        add_config("raw_config2", extract_bits_or_default(cell->params, ctx->id("raw_config2"), 14));
        add_config("raw_config3", extract_bits_or_default(cell->params, ctx->id("raw_config3"), 3));
        close_instance();
    }

    void write_cdc(CellInfo *cell) {
        open_instance(cell);
        if (cell->type.in(id_DDE, id_TDE, id_CDC, id_XCDC)) {
            add_config("ck0_edge", bool_or_default(cell->params, ctx->id("ck0_edge"), false));
            add_config("ck1_edge", bool_or_default(cell->params, ctx->id("ck1_edge"), false));
            add_config("ack_sel", bool_or_default(cell->params, ctx->id("ack_sel"), false));
            add_config("bck_sel", bool_or_default(cell->params, ctx->id("bck_sel"), false));
            add_config("use_adest_arst", bool_or_default(cell->params, ctx->id("use_adest_arst"), false));
            add_config("use_bdest_arst", bool_or_default(cell->params, ctx->id("use_bdest_arst"), false));
            if (cell->type != id_DDE) {
                add_config("use_asrc_arst", bool_or_default(cell->params, ctx->id("use_asrc_arst"), false));
                add_config("use_bsrc_arst", bool_or_default(cell->params, ctx->id("use_bsrc_arst"), false));
            }
            if (cell->type == id_XCDC) {
                add_config("cck_sel", bool_or_default(cell->params, ctx->id("cck_sel"), false));
                add_config("dck_sel", bool_or_default(cell->params, ctx->id("dck_sel"), false));
                add_config("use_csrc_arst", bool_or_default(cell->params, ctx->id("use_csrc_arst"), false));
                add_config("use_dsrc_arst", bool_or_default(cell->params, ctx->id("use_dsrc_arst"), false));
                add_config("use_cdest_arst", bool_or_default(cell->params, ctx->id("use_cdest_arst"), false));
                add_config("use_ddest_arst", bool_or_default(cell->params, ctx->id("use_ddest_arst"), false));
                add_config("link_BA", bool_or_default(cell->params, ctx->id("link_BA"), false));
                add_config("link_CB", bool_or_default(cell->params, ctx->id("link_CB"), false));
                add_config("link_DC", bool_or_default(cell->params, ctx->id("link_DC"), false));
            }
        }
        close_instance();
    }

    void write_fifo(CellInfo *cell) {
        open_instance(cell);
        add_config("rck_edge", bool_or_default(cell->params, ctx->id("rck_edge"), false));
        add_config("wck_edge", bool_or_default(cell->params, ctx->id("wck_edge"), false));
        if (cell->type != id_FIFO) {
            add_config("use_read_arst", bool_or_default(cell->params, ctx->id("use_read_arst"), false));
            add_config("use_write_arst", bool_or_default(cell->params, ctx->id("use_write_arst"), false));
        }
        add_config("read_addr_inv", extract_bits_or_default(cell->params, ctx->id("read_addr_inv"), 7));
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
                case id_BEYOND_FE.index: write_fe(cell.second.get()); break;
                case id_IOP.index:
                case id_IP.index:
                case id_OP.index:
                case id_IOTP.index:
                case id_ITP.index:
                case id_OTP.index: write_iop(cell.second.get()); break;
                case id_CY.index: write_cy(cell.second.get()); break;
                case id_WFB.index: write_wfb(cell.second.get()); break;
                case id_WFG.index: write_wfg(cell.second.get()); break;
                case id_GCK.index: write_gck(cell.second.get()); break;
                case id_IOM.index: write_iom(cell.second.get()); break;
                case id_BFR.index: write_bfr(cell.second.get()); break;
                //case id_DDFR.index:
                case id_DFR.index: write_dfr(cell.second.get()); break;
                case id_RAM.index: write_ram(cell.second.get()); break;
                case id_RF.index:
                case id_RFSP.index:
                case id_XHRF.index:
                case id_XWRF.index:
                case id_XPRF.index: write_rfb(cell.second.get()); break;
                case id_XLUT.index: write_xlut(cell.second.get()); break;
                case id_FIFO.index: // mode 0
                case id_XHFIFO.index: // mode 1
                case id_XWFIFO.index: write_fifo(cell.second.get()); break; // mode 2
                case id_DDE.index: // mode 0
                case id_TDE.index: // mode 1
                case id_CDC.index: // mode 2
                case id_BGC.index: // mode 3
                case id_GBC.index: // mode 4
                case id_XCDC.index: write_cdc(cell.second.get()); break; // mode 5
                case id_DSP.index: write_dsp(cell.second.get()); break;
                case id_PLL.index: write_pll(cell.second.get()); break;
                //case id_CRX.index:
                //case id_CTX.index:
                //case id_PMA.index:
                //case id_Service.index:
                //case id_SOCIF.index:
                default:
                    log_error("Unhandled cell %s of type %s\n", cell.second.get()->name.c_str(ctx), cell.second->type.c_str(ctx));
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

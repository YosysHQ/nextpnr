/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019-2023  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2023  Hans Baier <hansfbaier@gmail.com>
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
#include "pins.h"
#include "util.h"

#include "xilinx.h"

#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN
namespace {
struct FasmBackend
{
    Context *ctx;
    XilinxImpl *uarch;
    std::ostream &out;
    std::vector<std::string> fasm_ctx;
    dict<int, std::vector<PipId>> pips_by_tile;

    dict<IdString, pool<IdString>> invertible_pins;

    FasmBackend(Context *ctx, XilinxImpl *uarch, std::ostream &out) : ctx(ctx), uarch(uarch), out(out){};

    void push(const std::string &x) { fasm_ctx.push_back(x); }

    void pop() { fasm_ctx.pop_back(); }

    void pop(int N)
    {
        for (int i = 0; i < N; i++)
            fasm_ctx.pop_back();
    }
    bool last_was_blank = true;
    void blank()
    {
        if (!last_was_blank)
            out << std::endl;
        last_was_blank = true;
    }

    void write_prefix()
    {
        for (auto &x : fasm_ctx)
            out << x << ".";
        last_was_blank = false;
    }

    void write_bit(const std::string &name, bool value = true)
    {
        if (value) {
            write_prefix();
            out << name << std::endl;
        }
    }

    void write_vector(const std::string &name, const std::vector<bool> &value, bool invert = false)
    {
        write_prefix();
        out << name << " = " << int(value.size()) << "'b";
        for (auto bit : boost::adaptors::reverse(value))
            out << ((bit ^ invert) ? '1' : '0');
        out << std::endl;
    }

    void write_int_vector(const std::string &name, uint64_t value, int width, bool invert = false)
    {
        std::vector<bool> bits(width, false);
        for (int i = 0; i < width; i++)
            bits[i] = (value & (1ULL << i)) != 0;
        write_vector(name, bits, invert);
    }

    struct PseudoPipKey
    {
        IdString tileType;
        IdString dest;
        IdString source;

        bool operator==(const PseudoPipKey &b) const
        {
            return std::tie(this->tileType, this->dest, this->source) == std::tie(b.tileType, b.dest, b.source);
        }

        unsigned int hash() const { return mkhash(mkhash(tileType.hash(), source.hash()), dest.hash()); }
    };

    dict<PseudoPipKey, std::vector<std::string>> pp_config;
    void get_pseudo_pip_data()
    {
        /*
         * Create the mapping from pseudo pip tile type, dest wire, and source wire, to
         * the config bits set when that pseudo pip is used
         */
        for (std::string s : {"L", "R"})
            for (std::string s2 : {"", "_TBYTESRC", "_TBYTETERM", "_SING"})
                for (std::string i :
                     (s2 == "_SING") ? std::vector<std::string>{"", "0", "1"} : std::vector<std::string>{"0", "1"}) {
                    pp_config[{ctx->id(s + "IOI3" + s2), ctx->id(s + "IOI_OLOGIC" + i + "_OQ"),
                               ctx->id("IOI_OLOGIC" + i + "_D1")}] = {"OLOGIC_Y" + i + ".OMUX.D1",
                                                                      "OLOGIC_Y" + i + ".OQUSED",
                                                                      "OLOGIC_Y" + i + ".OSERDES.DATA_RATE_TQ.BUF"};
                    pp_config[{ctx->id(s + "IOI3" + s2), ctx->id("IOI_ILOGIC" + i + "_O"),
                               ctx->id(s + "IOI_ILOGIC" + i + "_D")}] = {"IDELAY_Y" + i + ".IDELAY_TYPE_FIXED",
                                                                         "ILOGIC_Y" + i + ".ZINV_D"};
                    pp_config[{ctx->id(s + "IOI3" + s2), ctx->id("IOI_ILOGIC" + i + "_O"),
                               ctx->id(s + "IOI_ILOGIC" + i + "_DDLY")}] = {"ILOGIC_Y" + i + ".IDELMUXE3.P0",
                                                                            "ILOGIC_Y" + i + ".ZINV_D"};
                    pp_config[{ctx->id(s + "IOI3" + s2), ctx->id(s + "IOI_OLOGIC" + i + "_TQ"),
                               ctx->id("IOI_OLOGIC" + i + "_T1")}] = {"OLOGIC_Y" + i + ".ZINV_T1"};
                    if (i == "0") {
                        pp_config[{ctx->id(s + "IOB33" + s2), id_IOB_O_IN1, id_IOB_O_OUT0}] = {};
                        pp_config[{ctx->id(s + "IOB33" + s2), id_IOB_O_OUT0, id_IOB_O0}] = {};
                        pp_config[{ctx->id(s + "IOB33" + s2), id_IOB_T_IN1, id_IOB_T_OUT0}] = {};
                        pp_config[{ctx->id(s + "IOB33" + s2), id_IOB_T_OUT0, id_IOB_T0}] = {};
                        pp_config[{ctx->id(s + "IOB33" + s2), id_IOB_DIFFI_IN0, id_IOB_PADOUT1}] = {};
                    }
                }

        for (std::string s2 : {"", "_TBYTESRC", "_TBYTETERM", "_SING"})
            for (std::string i : (s2 == "_SING") ? std::vector<std::string>{"0"} : std::vector<std::string>{"0", "1"}) {
                pp_config[{ctx->id("RIOI" + s2), ctx->id("RIOI_OLOGIC" + i + "_OQ"),
                           ctx->id("IOI_OLOGIC" + i + "_D1")}] = {"OLOGIC_Y" + i + ".OMUX.D1",
                                                                  "OLOGIC_Y" + i + ".OQUSED",
                                                                  "OLOGIC_Y" + i + ".OSERDES.DATA_RATE_TQ.BUF"};
                pp_config[{ctx->id("RIOI" + s2), ctx->id("RIOI_OLOGIC" + i + "_OFB"),
                           ctx->id("RIOI_OLOGIC" + i + "_OQ")}] = {};
                pp_config[{ctx->id("RIOI" + s2), ctx->id("RIOI_O" + i), ctx->id("RIOI_ODELAY" + i + "_DATAOUT")}] = {};
                pp_config[{ctx->id("RIOI" + s2), ctx->id("RIOI_OLOGIC" + i + "_OFB"),
                           ctx->id("IOI_OLOGIC" + i + "_D1")}] = {"OLOGIC_Y" + i + ".OMUX.D1",
                                                                  "OLOGIC_Y" + i + ".OSERDES.DATA_RATE_TQ.BUF"};
                pp_config[{ctx->id("RIOI" + s2), ctx->id("IOI_ILOGIC" + i + "_O"), ctx->id("RIOI_ILOGIC" + i + "_D")}] =
                        {"ILOGIC_Y" + i + ".ZINV_D"};
                pp_config[{ctx->id("RIOI" + s2), ctx->id("IOI_ILOGIC" + i + "_O"),
                           ctx->id("RIOI_ILOGIC" + i + "_DDLY")}] = {"ILOGIC_Y" + i + ".IDELMUXE3.P0",
                                                                     "ILOGIC_Y" + i + ".ZINV_D"};
                pp_config[{ctx->id("RIOI" + s2), ctx->id("RIOI_OLOGIC" + i + "_TQ"),
                           ctx->id("IOI_OLOGIC" + i + "_T1")}] = {"OLOGIC_Y" + i + ".ZINV_T1"};
                pp_config[{ctx->id("RIOI" + s2), ctx->id("RIOI_OLOGIC" + i + "_OFB"),
                           ctx->id("RIOI_ODELAY" + i + "_ODATAIN")}] = {"OLOGIC_Y" + i + ".ZINV_ODATAIN"};
                if (i == "0") {
                    pp_config[{ctx->id("RIOB18" + s2), id_IOB_O_IN1, id_IOB_O_OUT0}] = {};
                    pp_config[{ctx->id("RIOB18" + s2), id_IOB_O_OUT0, id_IOB_O0}] = {};
                    pp_config[{ctx->id("RIOB18" + s2), id_IOB_T_IN1, id_IOB_T_OUT0}] = {};
                    pp_config[{ctx->id("RIOB18" + s2), id_IOB_T_OUT0, id_IOB_T0}] = {};
                    pp_config[{ctx->id("RIOB18" + s2), id_IOB_DIFFI_IN0, id_IOB_PADOUT1}] = {};
                }
            }

        for (std::string s1 : {"TOP", "BOT"}) {
            for (std::string s2 : {"L", "R"}) {
                for (int i = 0; i < 12; i++) {
                    std::string ii = std::to_string(i);
                    std::string hck = s2 + ii;
                    std::string buf = std::string((s2 == "R") ? "X1Y" : "X0Y") + ii;
                    pp_config[{ctx->id("CLK_HROW_" + s1 + "_R"), ctx->id("CLK_HROW_CK_HCLK_OUT_" + hck),
                               ctx->id("CLK_HROW_CK_MUX_OUT_" + hck)}] = {"BUFHCE.BUFHCE_" + buf + ".IN_USE",
                                                                          "BUFHCE.BUFHCE_" + buf + ".ZINV_CE"};
                }
            }

            for (int i = 0; i < 16; i++) {
                std::string ii = std::to_string(i);
                pp_config[{ctx->id("CLK_BUFG_" + s1 + "_R"), ctx->id("CLK_BUFG_BUFGCTRL" + ii + "_O"),
                           ctx->id("CLK_BUFG_BUFGCTRL" + ii + "_I0")}] = {
                        "BUFGCTRL.BUFGCTRL_X0Y" + ii + ".IN_USE", "BUFGCTRL.BUFGCTRL_X0Y" + ii + ".IS_IGNORE1_INVERTED",
                        "BUFGCTRL.BUFGCTRL_X0Y" + ii + ".ZINV_CE0", "BUFGCTRL.BUFGCTRL_X0Y" + ii + ".ZINV_S0"};
                pp_config[{ctx->id("CLK_BUFG_" + s1 + "_R"), ctx->id("CLK_BUFG_BUFGCTRL" + ii + "_O"),
                           ctx->id("CLK_BUFG_BUFGCTRL" + ii + "_I1")}] = {
                        "BUFGCTRL.BUFGCTRL_X0Y" + ii + ".IN_USE", "BUFGCTRL.BUFGCTRL_X0Y" + ii + ".IS_IGNORE0_INVERTED",
                        "BUFGCTRL.BUFGCTRL_X0Y" + ii + ".ZINV_CE1", "BUFGCTRL.BUFGCTRL_X0Y" + ii + ".ZINV_S1"};
            }
        }

        int rclk_y_to_i[4] = {2, 3, 0, 1};
        for (int y = 0; y < 4; y++) {
            std::string yy = std::to_string(y);
            std::string ii = std::to_string(rclk_y_to_i[y]);
            pp_config[{id_HCLK_IOI3, ctx->id("HCLK_IOI_RCLK_OUT" + ii), ctx->id("HCLK_IOI_RCLK_BEFORE_DIV" + ii)}] = {
                    "BUFR_Y" + yy + ".IN_USE", "BUFR_Y" + yy + ".BUFR_DIVIDE.BYPASS"};
            pp_config[{id_HCLK_IOI, ctx->id("HCLK_IOI_RCLK_OUT" + ii), ctx->id("HCLK_IOI_RCLK_BEFORE_DIV" + ii)}] = {
                    "BUFR_Y" + yy + ".IN_USE", "BUFR_Y" + yy + ".BUFR_DIVIDE.BYPASS"};
        }

        // FIXME: shouldn't these be in the X-RAY ppips database?
        for (char c : {'L', 'R'}) {
            for (int i = 0; i < 24; i++) {
                pp_config[{ctx->idf("INT_INTERFACE_%c", c), ctx->idf("INT_INTERFACE_LOGIC_OUTS_%c%d", c, i),
                           ctx->idf("INT_INTERFACE_LOGIC_OUTS_%c_B%d", c, i)}];
            }
        }
    }

    void write_pip(PipId pip, NetInfo *net)
    {

        pips_by_tile[pip.tile].push_back(pip);

        auto dst_intent = ctx->getWireType(ctx->getPipDstWire(pip));
        if (dst_intent == id_PSEUDO_GND || dst_intent == id_PSEUDO_VCC)
            return;

        auto &pd = chip_pip_info(ctx->chip_info, pip);
        const auto &extra_data = *reinterpret_cast<const XlnxPipExtraDataPOD *>(pd.extra_data.get());
        unsigned pip_type = pd.flags;

        if (pip_type != PIP_TILE_ROUTING && pip_type != PIP_SITE_INTERNAL)
            return;

        IdString src = IdString(chip_tile_info(ctx->chip_info, pip.tile).wires[pd.src_wire].name);
        IdString dst = IdString(chip_tile_info(ctx->chip_info, pip.tile).wires[pd.dst_wire].name);

        // handle certain site internal pips:
        // this is necessary, because in tristate outputs, the
        // ZINV_T1 bit needs to be set, because in the OLOGIC tiles the
        // tristate control signals are inverted if this bit is not set
        // this only applies to router1, because router2 does not generate
        // site internal pips here.
        if (pip_type == PIP_SITE_INTERNAL) {
            if (src.str(ctx) == "T1" && dst.str(ctx) == "T1INV_OUT") {
                auto srcwire_uphill_iter = ctx->getPipsUphill(ctx->getPipSrcWire(pip));
                auto uphill = srcwire_uphill_iter.begin();
                if (uphill != srcwire_uphill_iter.end()) {
                    // source wire should be like: LIOI3_X0Y73/IOI_OLOGIC1_T1
                    auto loc = ctx->getWireName(ctx->getPipSrcWire(*uphill)).str(ctx);
                    boost::replace_all(loc, "/", ".");
                    boost::erase_all(loc, "_T1");
                    boost::replace_all(loc, "IOI_OLOGIC", "OLOGIC_Y");
                    // the replacements transformed it into : LIOI3_X0Y73.OLOGIC_Y1
                    out << loc << "."
                        << "ZINV_T1" << std::endl;
                }
            }
            return;
        }

        // handle tile routing pips
        IdString tile_type = IdString(chip_tile_info(ctx->chip_info, pip.tile).type_name);
        PseudoPipKey ppk{tile_type, dst, src};

        if (pp_config.count(ppk)) {
            auto &pp = pp_config.at(ppk);
            std::string tile_name = uarch->tile_name(pip.tile);
            for (auto c : pp) {
                if (boost::starts_with(tile_name, "RIOI3_SING") || boost::starts_with(tile_name, "LIOI3_SING") ||
                    boost::starts_with(tile_name, "RIOI_SING")) {
                    // Need to flip for top HCLK
                    bool is_top_sing = pip.tile < uarch->hclk_for_ioi(pip.tile);
                    if (is_top_sing) {
                        auto y0pos = c.find("Y0");
                        if (y0pos != std::string::npos)
                            c.replace(y0pos, 2, "Y1");
                    }
                }
                out << tile_name << "." << c << std::endl;
            }
            if (!pp.empty())
                last_was_blank = false;
        } else {
            if (extra_data.pip_config == 1)
                log_warning("Unprocessed route-thru %s.%s.%s\n!", tile_type.c_str(ctx), src.c_str(ctx), dst.c_str(ctx));

            std::string tile_name = uarch->tile_name(pip.tile);
            std::string dst_name = dst.str(ctx);
            std::string src_name = src.str(ctx);

            if (boost::starts_with(tile_name, "DSP_L") || boost::starts_with(tile_name, "DSP_R")) {
                // FIXME: PPIPs missing for DSPs
                return;
            }
            std::string orig_dst_name = dst_name;
            if (boost::starts_with(tile_name, "RIOI3_SING") || boost::starts_with(tile_name, "LIOI3_SING") ||
                boost::starts_with(tile_name, "RIOI_SING")) {
                // FIXME: PPIPs missing for SING IOI3s
                if ((boost::contains(src_name, "IMUX") || boost::contains(src_name, "CTRL0")) &&
                    !boost::contains(dst_name, "CLK"))
                    return;
                auto spos = src_name.find("_SING_");
                if (spos != std::string::npos)
                    src_name.erase(spos, 5);
                // Need to flip for top HCLK
                // TODO

                bool is_top_sing = pip.tile < uarch->hclk_for_ioi(pip.tile);
                if (is_top_sing) {
                    auto us0pos = dst_name.find("_0");
                    if (us0pos != std::string::npos)
                        dst_name.replace(us0pos, 2, "_1");
                    auto ol0pos = dst_name.find("OLOGIC0");
                    if (ol0pos != std::string::npos) {
                        dst_name.replace(ol0pos, 7, "OLOGIC1");
                        us0pos = src_name.find("_0");
                        if (us0pos != std::string::npos)
                            src_name.replace(us0pos, 2, "_1");
                    }
                }

                NPNR_ASSERT_FALSE("unimplemented!");
            }
            if (boost::contains(tile_name, "IOI")) {
                if (boost::contains(dst_name, "OCLKB") && boost::contains(src_name, "IOI_OCLKM_"))
                    return; // missing, not sure if really a ppip?
            }

            out << tile_name << ".";
            out << dst_name << ".";
            out << src_name << std::endl;

            if (boost::contains(tile_name, "IOI") && boost::starts_with(dst_name, "IOI_OCLK_")) {
#if 0
                dst_name.insert(dst_name.find("OCLK") + 4, 1, 'M');
                orig_dst_name.insert(dst_name.find("OCLK") + 4, 1, 'M');

                WireId w = ctx->getWireByNameStr(tile_name + "/" + orig_dst_name);
                NPNR_ASSERT(w != WireId());
                if (ctx->getBoundWireNet(w) == nullptr) {
                    out << tile_name << ".";
                    out << dst_name << ".";
                    out << src_name << std::endl;
                }
#endif
                NPNR_ASSERT_FALSE("unimplemented!");
            }

            last_was_blank = false;
        }
    };

    // Get the set of input signals for a LUT-type cell
    std::vector<IdString> get_inputs(CellInfo *cell)
    {
        IdString type = ctx->id(str_or_default(cell->attrs, id_X_ORIG_TYPE, ""));
        if (type == id_LUT1)
            return {id_I0};
        else if (type == id_LUT2)
            return {id_I0, id_I1};
        else if (type == id_LUT3)
            return {id_I0, id_I1, id_I2};
        else if (type == id_LUT4)
            return {id_I0, id_I1, id_I2, id_I3};
        else if (type == id_LUT5)
            return {id_I0, id_I1, id_I2, id_I3, id_I4};
        else if (type == id_LUT6)
            return {id_I0, id_I1, id_I2, id_I3, id_I4, id_I5};
        else if (type == id_RAMD64E)
            return {id_RADR0, id_RADR1, id_RADR2, id_RADR3, id_RADR4, id_RADR5};
        else if (type == id_SRL16E)
            return {id_A0, id_A1, id_A2, id_A3};
        else if (type == id_SRLC32E)
            return {ctx->id("A[0]"), ctx->id("A[1]"), ctx->id("A[2]"), ctx->id("A[3]"), ctx->id("A[4]")};
        else if (type == id_RAMD32)
            return {id_RADR0, id_RADR1, id_RADR2, id_RADR3, id_RADR4};
        else
            NPNR_ASSERT_FALSE("unsupported LUT-type cell");
    }

    // Process LUT initialisation
    std::vector<bool> get_lut_init(CellInfo *lut6, CellInfo *lut5)
    {
        std::vector<bool> bits(64, false);

        std::vector<IdString> phys_inputs;
        for (int i = 1; i <= 6; i++)
            phys_inputs.push_back(ctx->id("A" + std::to_string(i)));

        for (int i = 0; i < 2; i++) {
            CellInfo *lut = (i == 1) ? lut5 : lut6;
            if (lut == nullptr)
                continue;
            auto lut_inputs = get_inputs(lut);
            dict<int, std::vector<std::string>> phys_to_log;
            dict<std::string, int> log_to_bit;
            for (int j = 0; j < int(lut_inputs.size()); j++)
                log_to_bit[lut_inputs[j].str(ctx)] = j;
            for (int j = 0; j < 6; j++) {
                // Get the LUT physical to logical mapping
                phys_to_log[j];
                if (!lut->attrs.count(ctx->idf("X_ORIG_PORT_%s", phys_inputs[j].c_str(ctx))))
                    continue;
                std::string orig = lut->attrs.at(ctx->idf("X_ORIG_PORT_%s", phys_inputs[j].c_str(ctx))).as_string();
                boost::split(phys_to_log[j], orig, boost::is_any_of(" "));
            }
            int lbound = 0, ubound = 64;
            // Fracturable LUTs
            if (lut5 && lut6) {
                lbound = (i == 1) ? 0 : 32;
                ubound = (i == 1) ? 32 : 64;
            }
            Property init = get_or_default(lut->params, id_INIT, Property()).extract(0, 64);
            for (int j = lbound; j < ubound; j++) {
                int log_index = 0;
                for (int k = 0; k < 6; k++) {
                    if ((j & (1 << k)) == 0)
                        continue;
                    for (auto &p2l : phys_to_log[k])
                        log_index |= (1 << log_to_bit[p2l]);
                }
                bits[j] = (init.str.at(log_index) == Property::S1);
            }
        }
        return bits;
    };

    // Return the name for a half-logic-tile
    std::string get_half_name(int half, bool is_m)
    {
        if (is_m)
            return half ? "SLICEL_X1" : "SLICEM_X0";
        else
            return half ? "SLICEL_X1" : "SLICEL_X0";
    }

    std::string get_bel_name(BelId bel) { return uarch->bel_name_in_site(bel).str(ctx); }

    void write_routing_bel(WireId dst_wire)
    {
        for (auto pip : ctx->getPipsUphill(dst_wire)) {
            if (ctx->getBoundPipNet(pip) != nullptr) {
                auto &pd = chip_pip_info(ctx->chip_info, pip);
                const auto &extra_data = *reinterpret_cast<const XlnxPipExtraDataPOD *>(pd.extra_data.get());
                std::string belname = IdString(extra_data.bel_name).str(ctx);
                std::string pinname = IdString(extra_data.pip_config).str(ctx);
                bool skip_pinname = false;
                // Ignore modes with no associated bit (X-ray omission??)
                if (belname == "WEMUX" && pinname == "WE")
                    continue;

                if (belname.substr(1) == "DI1MUX") {
                    belname = "DI1MUX";
                }

                if (belname.substr(1) == "CY0") {
                    if (pinname.substr(1) == "5")
                        skip_pinname = true;
                    else
                        continue;
                }

                write_prefix();
                out << belname;
                if (!skip_pinname)
                    out << "." << pinname;
                out << std::endl;
            }
        }
    }

    // Process flipflops in a half-tile
    void write_ffs_config(int tile, int half)
    {
        bool found_ff = false;
        bool negedge_ff = false;
        bool is_latch = false;
        bool is_sync = false;
        bool is_clkinv = false;
        bool is_srused = false;
        bool is_ceused = false;

#define SET_CHECK(dst, src)                                                                                            \
    do {                                                                                                               \
        if (found_ff)                                                                                                  \
            NPNR_ASSERT(dst == (src));                                                                                 \
        else                                                                                                           \
            dst = (src);                                                                                               \
    } while (0)

        std::string tname = uarch->tile_name(tile);

        const auto &lts = uarch->tile_status.at(tile).lts;
        if (!lts)
            return;

        push(tname);
        push(get_half_name(half, boost::contains(tname, "CLBLM")));

        for (int i = 0; i < 4; i++) {
            CellInfo *ff1 = lts->cells[(half << 6) | (i << 4) | BEL_FF];
            CellInfo *ff2 = lts->cells[(half << 6) | (i << 4) | BEL_FF2];
            for (int j = 0; j < 2; j++) {
                CellInfo *ff = (j == 1) ? ff2 : ff1;
                if (ff == nullptr)
                    continue;
                push(get_bel_name(ff->bel));
                bool zrst = false, zinit = false;
                zinit = (int_or_default(ff->params, id_INIT, 0) != 1);
                IdString srsig;
                std::string type = str_or_default(ff->attrs, id_X_ORIG_TYPE, "");
                if (type == "FDRE") {
                    zrst = true;
                    SET_CHECK(negedge_ff, false);
                    SET_CHECK(is_latch, false);
                    SET_CHECK(is_sync, true);
                } else if (type == "FDRE_1") {
                    zrst = true;
                    SET_CHECK(negedge_ff, true);
                    SET_CHECK(is_latch, false);
                    SET_CHECK(is_sync, true);
                } else if (type == "FDSE") {
                    zrst = false;
                    SET_CHECK(negedge_ff, false);
                    SET_CHECK(is_latch, false);
                    SET_CHECK(is_sync, true);
                } else if (type == "FDSE_1") {
                    zrst = false;
                    SET_CHECK(negedge_ff, true);
                    SET_CHECK(is_latch, false);
                    SET_CHECK(is_sync, true);
                } else if (type == "FDCE") {
                    zrst = true;
                    SET_CHECK(negedge_ff, false);
                    SET_CHECK(is_latch, false);
                    SET_CHECK(is_sync, false);
                } else if (type == "FDCE_1") {
                    zrst = true;
                    SET_CHECK(negedge_ff, true);
                    SET_CHECK(is_latch, false);
                    SET_CHECK(is_sync, false);
                } else if (type == "FDPE") {
                    zrst = false;
                    SET_CHECK(negedge_ff, false);
                    SET_CHECK(is_latch, false);
                    SET_CHECK(is_sync, false);
                } else if (type == "FDPE_1") {
                    zrst = false;
                    SET_CHECK(negedge_ff, true);
                    SET_CHECK(is_latch, false);
                    SET_CHECK(is_sync, false);
                } else {
                    log_error("unsupported FF type: '%s'\n", type.c_str());
                }

                write_bit("ZINI", zinit);
                write_bit("ZRST", zrst);

                pop();
                if (negedge_ff)
                    SET_CHECK(is_clkinv, true);
                else
                    SET_CHECK(is_clkinv, int_or_default(ff->params, id_IS_C_INVERTED) == 1);

                NetInfo *sr = ff->getPort(id_SR), *ce = ff->getPort(id_CE);

                SET_CHECK(is_srused, sr != nullptr && sr->name != ctx->id("$PACKER_GND_NET"));
                SET_CHECK(is_ceused, ce != nullptr && ce->name != ctx->id("$PACKER_VCC_NET"));

                // Input mux
                write_routing_bel(ctx->getBelPinWire(ff->bel, id_D));

                found_ff = true;
            }
        }
        write_bit("LATCH", is_latch);
        write_bit("FFSYNC", is_sync);
        write_bit("CLKINV", is_clkinv);
        write_bit("NOCLKINV", !is_clkinv);
        write_bit("SRUSEDMUX", is_srused);
        write_bit("CEUSEDMUX", is_ceused);
        pop(2);
    }

    // Get a named wire in the same site as a bel
    WireId get_site_wire(BelId site_bel, std::string name)
    {
        IdStringList bel_name = ctx->getBelName(site_bel);
        NPNR_ASSERT(bel_name.size() == 2);
        IdString tile_name = bel_name[0];
        const std::string &bel_name_str = bel_name[1].str(ctx);
        size_t sep_pos = bel_name_str.find('.');
        NPNR_ASSERT(sep_pos != std::string::npos);
        std::string site_name = bel_name_str.substr(0, sep_pos);
        IdString wire_name = ctx->idf("%s.%s", site_name.c_str(), name.c_str());
        WireId wire = ctx->getWireByName(IdStringList::concat(tile_name, wire_name));
        NPNR_ASSERT(wire != WireId());
        return wire;
    }

    // Process LUTs and associated functionality in a half
    void write_luts_config(int tile, int half)
    {
        bool wa7_used = false, wa8_used = false;

        std::string tname = uarch->tile_name(tile);
        bool is_mtile = boost::contains(tname, "CLBLM");
        bool is_slicem = is_mtile && (half == 0);

        const auto &lts = uarch->tile_status.at(tile).lts;
        if (!lts)
            return;

        push(tname);
        push(get_half_name(half, is_mtile));

        BelId bel_in_half =
                ctx->getBelByLocation(Loc(tile % ctx->chip_info->width, tile / ctx->chip_info->width, half << 6));

        for (int i = 0; i < 4; i++) {
            CellInfo *lut6 = lts->cells[(half << 6) | (i << 4) | BEL_6LUT];
            CellInfo *lut5 = lts->cells[(half << 6) | (i << 4) | BEL_5LUT];
            // Write LUT initialisation
            if (lut6 != nullptr || lut5 != nullptr) {
                std::string lutname = stringf("%cLUT", "ABCD"[i]);
                push(lutname);
                write_vector("INIT[63:0]", get_lut_init(lut6, lut5));

                // Write LUT mode config
                bool is_small = false, is_ram = false, is_srl = false;
                for (int j = 0; j < 2; j++) {
                    CellInfo *lut = (j == 1) ? lut5 : lut6;
                    if (lut == nullptr)
                        continue;
                    std::string type = str_or_default(lut->attrs, id_X_ORIG_TYPE);
                    if (type == "RAMD64E" || type == "RAMS64E") {
                        is_ram = true;
                    } else if (type == "RAMD32" || type == "RAMS32") {
                        is_ram = true;
                        is_small = true;
                    } else if (type == "SRL16E") {
                        is_srl = true;
                        is_small = true;
                    } else if (type == "SRLC32E") {
                        is_srl = true;
                    }
                    wa7_used |= (lut->getPort(id_WA7) != nullptr);
                    wa8_used |= (lut->getPort(id_WA8) != nullptr);
                }
                if (is_slicem && i != 3) {
                    write_routing_bel(get_site_wire(bel_in_half, stringf("%cDI1MUX_OUT", "ABCD"[i])));
                }
                write_bit("SMALL", is_small);
                write_bit("RAM", is_ram);
                write_bit("SRL", is_srl);
                pop();
            }
            write_routing_bel(get_site_wire(bel_in_half, stringf("%cMUX", "ABCD"[i])));
        }
        write_bit("WA7USED", wa7_used);
        write_bit("WA8USED", wa8_used);
        if (is_slicem)
            write_routing_bel(get_site_wire(bel_in_half, "WEMUX_OUT"));

        pop(2);
    }

    void write_carry_config(int tile, int half)
    {
        std::string tname = uarch->tile_name(tile);
        bool is_mtile = boost::contains(tname, "CLBLM");

        const auto &lts = uarch->tile_status.at(tile).lts;
        if (!lts)
            return;

        CellInfo *carry = lts->cells[half << 6 | BEL_CARRY4];
        if (carry == nullptr)
            return;

        push(tname);
        push(get_half_name(half, is_mtile));

        write_routing_bel(get_site_wire(carry->bel, "PRECYINIT_OUT"));
        if (carry->getPort(id_CIN) != nullptr)
            write_bit("PRECYINIT.CIN");
        push("CARRY4");
        for (char c : {'A', 'B', 'C', 'D'})
            write_routing_bel(get_site_wire(carry->bel, stringf("%cCY0_OUT", c)));
        pop(3);
    }

    void write_logic()
    {
        std::set<int> used_logic_tiles;
        for (auto &cell : ctx->cells) {
            if (uarch->is_logic_tile(cell.second->bel))
                used_logic_tiles.insert(cell.second->bel.tile);
        }
        for (int tile : used_logic_tiles) {
            write_luts_config(tile, 0);
            write_luts_config(tile, 1);
            write_ffs_config(tile, 0);
            write_ffs_config(tile, 1);
            write_carry_config(tile, 0);
            write_carry_config(tile, 1);
            blank();
        }
    }

    void write_routing()
    {
        get_pseudo_pip_data();
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            out << stringf("# routing for net %s", ni->name.c_str(ctx)) << std::endl;
            for (auto &w : ni->wires) {
                if (w.second.pip != PipId())
                    write_pip(w.second.pip, ni);
            }
            blank();
        }
    }

    struct BankIoConfig
    {
        bool stepdown = false;
        bool vref = false;
        bool tmds_33 = false;
        bool lvds_25 = false;
        bool only_diff = false;
    };

    dict<int, BankIoConfig> ioconfig_by_hclk;

    void write_io_config(CellInfo *pad)
    {
        NetInfo *pad_net = pad->getPort(id_PAD);
        NPNR_ASSERT(pad_net != nullptr);
        std::string iostandard = str_or_default(pad->attrs, id_IOSTANDARD, "LVCMOS33");
        std::string pulltype = str_or_default(pad->attrs, id_PULLTYPE, "NONE");
        std::string slew = str_or_default(pad->attrs, id_SLEW, "SLOW");

        Loc ioLoc = uarch->rel_site_loc(uarch->get_bel_site(pad->bel));
        bool is_output = false, is_input = false;
        if (pad_net->driver.cell != nullptr)
            is_output = true;
        for (auto &usr : pad_net->users)
            if (boost::contains(usr.cell->type.str(ctx), "INBUF"))
                is_input = true;
        std::string tile = uarch->tile_name(pad->bel.tile);
        push(tile);

        bool is_riob18 = boost::starts_with(tile, "RIOB18_");
        bool is_sing = boost::contains(tile, "_SING_");
        bool is_top_sing = pad->bel.tile < uarch->hclk_for_iob(pad->bel);
        bool is_stepdown = false;
        bool is_lvcmos = boost::starts_with(iostandard, "LVCMOS");
        bool is_low_volt_lvcmos = iostandard == "LVCMOS12" || iostandard == "LVCMOS15" || iostandard == "LVCMOS18";

        auto yLoc = is_sing ? (is_top_sing ? 1 : 0) : (1 - ioLoc.y);
        push("IOB_Y" + std::to_string(yLoc));

        bool has_diff_prefix = boost::starts_with(iostandard, "DIFF_");
        bool is_tmds33 = iostandard == "TMDS_33";
        bool is_lvds25 = iostandard == "LVDS_25";
        bool is_lvds = boost::starts_with(iostandard, "LVDS");
        bool only_diff = is_tmds33 || is_lvds;
        bool is_diff = only_diff || has_diff_prefix;
        if (has_diff_prefix)
            iostandard.erase(0, 5);
        bool is_sstl = iostandard == "SSTL12" || iostandard == "SSTL135" || iostandard == "SSTL15";

        int hclk = uarch->hclk_for_iob(pad->bel);

        if (only_diff)
            ioconfig_by_hclk[hclk].only_diff = true;
        if (is_tmds33)
            ioconfig_by_hclk[hclk].tmds_33 = true;
        if (is_lvds25)
            ioconfig_by_hclk[hclk].lvds_25 = true;

        if (is_output) {
            // DRIVE
            int default_drive = (is_riob18 && iostandard == "LVCMOS12") ? 8 : 12;
            int drive = int_or_default(pad->attrs, id_DRIVE, default_drive);

            if ((iostandard == "LVCMOS33" || iostandard == "LVTTL") && is_riob18)
                log_error("high performance banks (RIOB18) do not support IO standard %s\n", iostandard.c_str());

            if (iostandard == "SSTL135")
                write_bit("SSTL135.DRIVE.I_FIXED");
            else if (is_riob18) {
                if ((iostandard == "LVCMOS18" || iostandard == "LVCMOS15"))
                    write_bit("LVCMOS15_LVCMOS18.DRIVE.I12_I16_I2_I4_I6_I8");
                else if (iostandard == "LVCMOS12")
                    write_bit("LVCMOS12.DRIVE.I2_I4_I6_I8");
                else if (iostandard == "LVDS")
                    write_bit("LVDS.DRIVE.I_FIXED");
                else if (is_sstl) {
                    write_bit(iostandard + ".DRIVE.I_FIXED");
                }
            } else { // IOB33
                if (iostandard == "TMDS_33" && yLoc == 0) {
                    write_bit("TMDS_33.DRIVE.I_FIXED");
                    write_bit("TMDS_33.OUT");
                } else if (iostandard == "LVDS_25" && yLoc == 0) {
                    write_bit("LVDS_25.DRIVE.I_FIXED");
                    write_bit("LVDS_25.OUT");
                } else if ((iostandard == "LVCMOS15" && drive == 16) || iostandard == "SSTL15")
                    write_bit("LVCMOS15_SSTL15.DRIVE.I16_I_FIXED");
                else if (iostandard == "LVCMOS18" && (drive == 12 || drive == 8))
                    write_bit("LVCMOS18.DRIVE.I12_I8");
                else if ((iostandard == "LVCMOS33" && drive == 16) || (iostandard == "LVTTL" && drive == 16))
                    write_bit("LVCMOS33_LVTTL.DRIVE.I12_I16");
                else if ((iostandard == "LVCMOS33" && (drive == 8 || drive == 12)) ||
                         (iostandard == "LVTTL" && (drive == 8 || drive == 12)))
                    write_bit("LVCMOS33_LVTTL.DRIVE.I12_I8");
                else if ((iostandard == "LVCMOS33" && drive == 4) || (iostandard == "LVTTL" && drive == 4))
                    write_bit("LVCMOS33_LVTTL.DRIVE.I4");
                else if (drive == 8 && (iostandard == "LVCMOS12" || iostandard == "LVCMOS25"))
                    write_bit("LVCMOS12_LVCMOS25.DRIVE.I8");
                else if (drive == 4 &&
                         (iostandard == "LVCMOS15" || iostandard == "LVCMOS18" || iostandard == "LVCMOS25"))
                    write_bit("LVCMOS15_LVCMOS18_LVCMOS25.DRIVE.I4");
                else if (is_lvcmos || iostandard == "LVTTL")
                    write_bit(iostandard + ".DRIVE.I" + std::to_string(drive));
            }

            // SSTL output used
            if (is_riob18 && is_sstl)
                write_bit(iostandard + ".IN_USE");

            // SLEW
            if (is_riob18 && slew == "SLOW") {
                if (iostandard == "SSTL135")
                    write_bit("SSTL135.SLEW.SLOW");
                else if (iostandard == "SSTL15")
                    write_bit("SSTL15.SLEW.SLOW");
                else
                    write_bit("LVCMOS12_LVCMOS15_LVCMOS18.SLEW.SLOW");
            } else if (slew == "SLOW") {
                if (iostandard != "LVDS_25" && iostandard != "TMDS_33")
                    write_bit("LVCMOS12_LVCMOS15_LVCMOS18_LVCMOS25_LVCMOS33_LVTTL_SSTL135_SSTL15.SLEW.SLOW");
            } else if (is_riob18)
                write_bit(iostandard + ".SLEW.FAST");
            else if (iostandard == "SSTL135" || iostandard == "SSTL15")
                write_bit("SSTL135_SSTL15.SLEW.FAST");
            else
                write_bit("LVCMOS12_LVCMOS15_LVCMOS18_LVCMOS25_LVCMOS33_LVTTL.SLEW.FAST");
        }

        if (is_input) {
            if (!is_diff) {
                if (iostandard == "LVCMOS33" || iostandard == "LVTTL" || iostandard == "LVCMOS25") {
                    if (!is_riob18)
                        write_bit("LVCMOS25_LVCMOS33_LVTTL.IN");
                    else
                        log_error("high performance banks (RIOB18) do not support IO standard %s\n",
                                  iostandard.c_str());
                }

                if (is_sstl) {
                    ioconfig_by_hclk[hclk].vref = true;
                    if (!is_riob18)
                        write_bit("SSTL135_SSTL15.IN");

                    if (is_riob18) {
                        write_bit("SSTL12_SSTL135_SSTL15.IN");
                    }

                    if (!is_riob18 && pad->attrs.count(id_IN_TERM))
                        write_bit("IN_TERM." + pad->attrs.at(id_IN_TERM).as_string());
                }

                if (is_low_volt_lvcmos) {
                    write_bit("LVCMOS12_LVCMOS15_LVCMOS18.IN");
                }
            } else /* is_diff */ {
                if (is_riob18) {
                    // vivado generates these bits only for Y0 of a diff pair
                    if (yLoc == 0) {
                        write_bit("LVDS_SSTL12_SSTL135_SSTL15.IN_DIFF");
                        if (iostandard == "LVDS")
                            write_bit("LVDS.IN_USE");
                    }
                } else {
                    if (iostandard == "TDMS_33")
                        write_bit("TDMS_33.IN_DIFF");
                    else
                        write_bit("LVDS_25_SSTL135_SSTL15.IN_DIFF");
                }

                if (pad->attrs.count(id_IN_TERM))
                    write_bit("IN_TERM." + pad->attrs.at(id_IN_TERM).as_string());
            }

            // IN_ONLY
            if (!is_output) {
                if (is_riob18) {
                    // vivado also sets this bit for DIFF_SSTL
                    if (is_diff && (yLoc == 0))
                        write_bit("LVDS.IN_ONLY");
                    else
                        write_bit("LVCMOS12_LVCMOS15_LVCMOS18_SSTL12_SSTL135_SSTL15.IN_ONLY");
                } else
                    write_bit("LVCMOS12_LVCMOS15_LVCMOS18_LVCMOS25_LVCMOS33_LVDS_25_LVTTL_SSTL135_SSTL15_TMDS_33.IN_"
                              "ONLY");
            }
        }

        if (!is_riob18 && (is_low_volt_lvcmos || is_sstl)) {
            if (iostandard == "SSTL12") {
                log_error("SSTL12 is only available on high performance banks.");
            }
            write_bit("LVCMOS12_LVCMOS15_LVCMOS18_SSTL135_SSTL15.STEPDOWN");
            ioconfig_by_hclk[hclk].stepdown = true;
            is_stepdown = true;
        }

        if (is_riob18 && (is_input || is_output) && (boost::contains(iostandard, "SSTL") || iostandard == "LVDS")) {
            if (((yLoc == 0) && (iostandard == "LVDS")) || boost::contains(iostandard, "SSTL")) {
                // TODO: I get bit conflicts with this, it seems to work anyway. Test more.
                // write_bit("LVDS.IN_USE");
            }
        }

        if (is_input && is_output && !is_diff && yLoc == 1) {
            if (is_riob18 && boost::starts_with(iostandard, "SSTL"))
                write_bit("SSTL12_SSTL135_SSTL15.IN");
        }

        write_bit("PULLTYPE." + pulltype);
        pop(); // IOB_YN

        BelId inv;

        auto pad_bel_site = uarch->get_bel_site(pad->bel);

        if (is_riob18)
            inv = uarch->get_site_bel(pad_bel_site, ctx->id("IOB18S.O_ININV"));
        else
            inv = uarch->get_site_bel(pad_bel_site, ctx->id("IOB33S.O_ININV"));

        if (inv != BelId() && ctx->getBoundBelCell(inv) != nullptr)
            write_bit("OUT_DIFF");

        if (is_stepdown && !is_sing)
            write_bit("IOB_Y" + std::to_string(ioLoc.y) + ".LVCMOS12_LVCMOS15_LVCMOS18_SSTL135_SSTL15.STEPDOWN");

        pop(); // tile
    }

    void write_iol_config(CellInfo *ci)
    {
        std::string tile = uarch->tile_name(ci->bel.tile);
        push(tile);
        bool is_sing = boost::contains(tile, "_SING_");
        bool is_top_sing = ci->bel.tile < uarch->hclk_for_ioi(ci->bel.tile);

        auto site_key = uarch->get_bel_site(ci->bel);
        std::string site = uarch->get_site_name(site_key).str(ctx);
        std::string sitetype = site.substr(0, site.find('_'));
        Loc siteloc = uarch->rel_site_loc(site_key);
        push(stringf("%s_Y%d", sitetype.c_str(), is_sing ? (is_top_sing ? 1 : 0) : (1 - siteloc.y)));

        if (ci->type == id_ILOGICE3_IFF) {
            write_bit("IDDR.IN_USE");
            write_bit("IDDR_OR_ISERDES.IN_USE");
            write_bit("ISERDES.MODE.MASTER");
            write_bit("ISERDES.NUM_CE.N1");

            // Switch IDELMUXE3 to include the IDELAY element, if we have an IDELAYE2 driving D
            NetInfo *d = ci->getPort(id_D);
            if (d == nullptr || d->driver.cell == nullptr)
                log_error("%s '%s' has disconnected D input\n", ci->type.c_str(ctx), ctx->nameOf(ci));
            CellInfo *drv = d->driver.cell;
            if (boost::contains(drv->type.str(ctx), "IDELAYE2"))
                write_bit("IDELMUXE3.P0");
            else
                write_bit("IDELMUXE3.P1");

            // clock edge
            std::string edge = str_or_default(ci->params, id_DDR_CLK_EDGE, "OPPOSITE_EDGE");
            if (edge == "SAME_EDGE")
                write_bit("IFF.DDR_CLK_EDGE.SAME_EDGE");
            else if (edge == "OPPOSITE_EDGE")
                write_bit("IFF.DDR_CLK_EDGE.OPPOSITE_EDGE");
            else
                log_error("unsupported clock edge parameter for cell '%s' at %s: %s. Supported are: SAME_EDGE and "
                          "OPPOSITE_EDGE",
                          ci->name.c_str(ctx), site.c_str(), edge.c_str());

            std::string srtype = str_or_default(ci->params, id_SRTYPE, "SYNC");
            if (srtype == "SYNC")
                write_bit("IFF.SRTYPE.SYNC");
            else
                write_bit("IFF.SRTYPE.ASYNC");

            write_bit("IFF.ZINV_C", !bool_or_default(ci->params, id_IS_CLK_INVERTED, false));
            write_bit("ZINV_D", !bool_or_default(ci->params, id_IS_D_INVERTED, false));

            auto init = int_or_default(ci->params, id_INIT_Q1, 0);
            if (init == 0)
                write_bit("IFF.ZINIT_Q1");
            init = int_or_default(ci->params, id_INIT_Q2, 0);
            if (init == 0)
                write_bit("IFF.ZINIT_Q2");

            auto sr_name = str_or_default(ci->attrs, id_X_ORIG_PORT_SR, "R");
            if (sr_name == "R") {
                write_bit("IFF.ZSRVAL_Q1");
                write_bit("IFF.ZSRVAL_Q2");
            }
        } else if (ci->type.in(id_OLOGICE2_OUTFF, id_OLOGICE3_OUTFF)) {
            std::string edge = str_or_default(ci->params, id_DDR_CLK_EDGE, "OPPOSITE_EDGE");
            if (edge == "SAME_EDGE")
                write_bit("ODDR.DDR_CLK_EDGE.SAME_EDGE");

            write_bit("ODDR_TDDR.IN_USE");
            write_bit("OQUSED");
            write_bit("OSERDES.DATA_RATE_OQ.DDR");
            write_bit("OSERDES.DATA_RATE_TQ.BUF");

            std::string srtype = str_or_default(ci->params, id_SRTYPE, "SYNC");
            if (srtype == "SYNC")
                write_bit("OSERDES.SRTYPE.SYNC");

            for (std::string d : {"D1", "D2"})
                write_bit("IS_" + d + "_INVERTED",
                          bool_or_default(ci->params, ctx->id("IS_" + d + "_INVERTED"), false));

            auto init = int_or_default(ci->params, id_INIT, 1);
            if (init == 0)
                write_bit("ZINIT_OQ");

            write_bit("ODDR.SRUSED", ci->getPort(id_SR) != nullptr);
            auto sr_name = str_or_default(ci->attrs, id_X_ORIG_PORT_SR, "R");
            if (sr_name == "R")
                write_bit("ZSRVAL_OQ");

            auto clk_inv = bool_or_default(ci->params, id_IS_CLK_INVERTED);
            if (!clk_inv)
                write_bit("ZINV_CLK");
        } else if (ci->type == id_OSERDESE2_OSERDESE2) {
            write_bit("ODDR.DDR_CLK_EDGE.SAME_EDGE");
            write_bit("ODDR.SRUSED");
            write_bit("ODDR_TDDR.IN_USE");
            write_bit("OQUSED", ci->getPort(id_OQ) != nullptr);
            write_bit("ZINV_CLK", !bool_or_default(ci->params, id_IS_CLK_INVERTED, false));
            for (std::string t : {"T1", "T2", "T3", "T4"})
                write_bit("ZINV_" + t, (ci->getPort(ctx->id(t)) != nullptr || t == "T1") &&
                                               !bool_or_default(ci->params, ctx->id("IS_" + t + "_INVERTED"), false));
            for (std::string d : {"D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8"})
                write_bit("IS_" + d + "_INVERTED",
                          bool_or_default(ci->params, ctx->id("IS_" + d + "_INVERTED"), false));
            write_bit("ZINIT_OQ", !bool_or_default(ci->params, id_INIT_OQ, false));
            write_bit("ZINIT_TQ", !bool_or_default(ci->params, id_INIT_TQ, false));
            write_bit("ZSRVAL_OQ", !bool_or_default(ci->params, id_SRVAL_OQ, false));
            write_bit("ZSRVAL_TQ", !bool_or_default(ci->params, id_SRVAL_TQ, false));

            push("OSERDES");
            write_bit("IN_USE");
            std::string type = str_or_default(ci->params, id_DATA_RATE_OQ, "BUF");
            write_bit(std::string("DATA_RATE_OQ.") + ((ci->getPort(id_OQ) != nullptr) ? type : "BUF"));
            write_bit(std::string("DATA_RATE_TQ.") +
                      ((ci->getPort(id_TQ) != nullptr) ? str_or_default(ci->params, id_DATA_RATE_TQ, "BUF") : "BUF"));
            int width = int_or_default(ci->params, id_DATA_WIDTH, 8);
#if 0
            write_bit("DATA_WIDTH.W" + std::to_string(width));
            if (type == "DDR" && (width == 6 || width == 8)) {
                write_bit("DATA_WIDTH.DDR.W6_8");
                write_bit("DATA_WIDTH.SDR.W2_4_5_6");
            } else if (type == "SDR" && (width == 2 || width == 4 || width == 5 || width == 6)) {
                write_bit("DATA_WIDTH.SDR.W2_4_5_6");
            }
#else
            if (type == "DDR")
                write_bit("DATA_WIDTH.DDR.W" + std::to_string(width));
            else if (type == "SDR")
                write_bit("DATA_WIDTH.SDR.W" + std::to_string(width));
            else
                write_bit("DATA_WIDTH.W" + std::to_string(width));
#endif
            write_bit("SRTYPE.SYNC");
            write_bit("TSRTYPE.SYNC");
            pop();
        } else if (ci->type == id_ISERDESE2_ISERDESE2) {
            std::string data_rate = str_or_default(ci->params, id_DATA_RATE);
            write_bit("IDDR_OR_ISERDES.IN_USE");
            if (data_rate == "DDR")
                write_bit("IDDR.IN_USE");
            write_bit("IFF.DDR_CLK_EDGE.OPPOSITE_EDGE");
            write_bit("IFF.SRTYPE.SYNC");
            for (int i = 1; i <= 4; i++) {
                write_bit("IFF.ZINIT_Q" + std::to_string(i),
                          !bool_or_default(ci->params, ctx->idf("INIT_Q%d", i), false));
                write_bit("IFF.ZSRVAL_Q" + std::to_string(i),
                          !bool_or_default(ci->params, ctx->idf("SRVAL_Q%d", i), false));
            }
            write_bit("IFF.ZINV_C", !bool_or_default(ci->params, id_IS_CLK_INVERTED, false));
            write_bit("IFF.ZINV_OCLK", !bool_or_default(ci->params, id_IS_OCLK_INVERTED, false));

            std::string iobdelay = str_or_default(ci->params, id_IOBDELAY, "NONE");
            write_bit("IFFDELMUXE3.P0", (iobdelay == "IFD"));
            write_bit("ZINV_D", !bool_or_default(ci->params, id_IS_D_INVERTED, false) && (iobdelay != "IFD"));

            push("ISERDES");
            write_bit("IN_USE");
            int width = int_or_default(ci->params, id_DATA_WIDTH, 8);
            std::string mode = str_or_default(ci->params, id_INTERFACE_TYPE, "NETWORKING");
            std::string rate = str_or_default(ci->params, id_DATA_RATE, "DDR");
            write_bit(mode + "." + rate + ".W" + std::to_string(width));
            write_bit("MODE." + str_or_default(ci->params, id_SERDES_MODE, "MASTER"));
            write_bit("NUM_CE.N" + std::to_string(int_or_default(ci->params, id_NUM_CE, 1)));
            pop();
        } else if (ci->type == id_IDELAYE2_IDELAYE2) {
            write_bit("IN_USE");
            write_bit("CINVCTRL_SEL", str_or_default(ci->params, id_CINVCTRL_SEL, "FALSE") == "TRUE");
            write_bit("PIPE_SEL", str_or_default(ci->params, id_PIPE_SEL, "FALSE") == "TRUE");
            write_bit("HIGH_PERFORMANCE_MODE", str_or_default(ci->params, id_HIGH_PERFORMANCE_MODE, "FALSE") == "TRUE");
            write_bit("DELAY_SRC_" + str_or_default(ci->params, id_DELAY_SRC, "IDATAIN"));
            write_bit("IDELAY_TYPE_" + str_or_default(ci->params, id_IDELAY_TYPE, "FIXED"));
            write_int_vector("IDELAY_VALUE[4:0]", int_or_default(ci->params, id_IDELAY_VALUE, 0), 5, false);
            write_int_vector("ZIDELAY_VALUE[4:0]", int_or_default(ci->params, id_IDELAY_VALUE, 0), 5, true);
            write_bit("IS_DATAIN_INVERTED", bool_or_default(ci->params, id_IS_DATAIN_INVERTED, false));
            write_bit("IS_IDATAIN_INVERTED", bool_or_default(ci->params, id_IS_IDATAIN_INVERTED, false));
        } else if (ci->type == id_ODELAYE2_ODELAYE2) {
            write_bit("IN_USE");
            write_bit("CINVCTRL_SEL", str_or_default(ci->params, id_CINVCTRL_SEL, "FALSE") == "TRUE");
            write_bit("HIGH_PERFORMANCE_MODE", str_or_default(ci->params, id_HIGH_PERFORMANCE_MODE, "FALSE") == "TRUE");
            auto type = str_or_default(ci->params, id_ODELAY_TYPE, "FIXED");
            if (type != "FIXED")
                write_bit("ODELAY_TYPE_" + type);
            write_int_vector("ODELAY_VALUE[4:0]", int_or_default(ci->params, id_ODELAY_VALUE, 0), 5, false);
            write_int_vector("ZODELAY_VALUE[4:0]", int_or_default(ci->params, id_ODELAY_VALUE, 0), 5, true);
            write_bit("ZINV_ODATAIN", !bool_or_default(ci->params, id_IS_ODATAIN_INVERTED, false));
        } else {
            NPNR_ASSERT_FALSE("unsupported IOLOGIC");
        }
        pop(2);
    }

    void write_io()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type == id_PAD) {
                write_io_config(ci);
                blank();
            } else if (ci->type.in(id_ILOGICE3_IFF, id_OLOGICE2_OUTFF, id_OLOGICE3_OUTFF, id_OSERDESE2_OSERDESE2,
                                   id_ISERDESE2_ISERDESE2, id_IDELAYE2_IDELAYE2, id_ODELAYE2_ODELAYE2)) {
                write_iol_config(ci);
                blank();
            }
        }
        for (auto &hclk : ioconfig_by_hclk) {
            push(uarch->tile_name(hclk.first));
            write_bit("STEPDOWN", hclk.second.stepdown);
            write_bit("VREF.V_675_MV", hclk.second.vref);
            write_bit("ONLY_DIFF_IN_USE", hclk.second.only_diff);
            write_bit("TMDS_33_IN_USE", hclk.second.tmds_33);
            write_bit("LVDS_25_IN_USE", hclk.second.lvds_25);
            pop();
        }
    }

    std::vector<std::string> used_wires_starting_with(int tile, const std::string &prefix, bool is_source)
    {
        std::vector<std::string> wires;
        if (!pips_by_tile.count(tile))
            return wires;
        for (auto pip : pips_by_tile[tile]) {
            auto &pd = chip_pip_info(ctx->chip_info, pip);
            int wire_index = is_source ? pd.src_wire : pd.dst_wire;
            std::string wire = IdString(chip_wire_info(ctx->chip_info, WireId(pip.tile, wire_index)).name).str(ctx);
            if (boost::starts_with(wire, prefix))
                wires.push_back(wire);
        }
        return wires;
    }

    void write_clocking()
    {
        std::string name, type;

        std::set<std::string> all_gclk;
        dict<int, std::set<std::string>> hclk_by_row;

        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type == id_BUFGCTRL) {
                push(uarch->tile_name(ci->bel.tile));
                auto xy = uarch->rel_site_loc(uarch->get_bel_site(ci->bel));
                push(stringf("BUFGCTRL.BUFGCTRL_X%dY%d", xy.x, xy.y));
                write_bit("IN_USE");
                write_bit("INIT_OUT", bool_or_default(ci->params, id_INIT_OUT));
                write_bit("IS_IGNORE0_INVERTED", bool_or_default(ci->params, id_IS_IGNORE0_INVERTED));
                write_bit("IS_IGNORE1_INVERTED", bool_or_default(ci->params, id_IS_IGNORE1_INVERTED));
                write_bit("ZINV_CE0", !bool_or_default(ci->params, id_IS_CE0_INVERTED));
                write_bit("ZINV_CE1", !bool_or_default(ci->params, id_IS_CE1_INVERTED));
                write_bit("ZINV_S0", !bool_or_default(ci->params, id_IS_S0_INVERTED));
                write_bit("ZINV_S1", !bool_or_default(ci->params, id_IS_S1_INVERTED));
                pop(2);
            } else if (ci->type == id_PLLE2_ADV_PLLE2_ADV) {
                write_pll(ci);
            }
            blank();
        }

        for (int tile = 0; tile < ctx->chip_info->tile_insts.ssize(); tile++) {
            std::string name = uarch->tile_name(tile);
            std::string type = ctx->get_tile_type(tile).str(ctx);
            push(name);
            if (type == "HCLK_L" || type == "HCLK_R" || type == "HCLK_L_BOT_UTURN" || type == "HCLK_R_BOT_UTURN") {
                auto used_sources = used_wires_starting_with(tile, "HCLK_CK_", true);
                push("ENABLE_BUFFER");
                for (auto s : used_sources) {
                    if (boost::contains(s, "BUFHCLK")) {
                        write_bit(s);
                        hclk_by_row[tile / ctx->chip_info->width].insert(s.substr(s.find("BUFHCLK")));
                    }
                }
                pop();
            } else if (boost::starts_with(type, "CLK_HROW")) {
                auto used_gclk = used_wires_starting_with(tile, "CLK_HROW_R_CK_GCLK", true);
                auto used_ck_in = used_wires_starting_with(tile, "CLK_HROW_CK_IN", true);
                for (auto s : used_gclk) {
                    write_bit(s + "_ACTIVE");
                    all_gclk.insert(s.substr(s.find("GCLK")));
                }
                for (auto s : used_ck_in) {
                    if (boost::contains(s, "HROW_CK_INT"))
                        continue;
                    write_bit(s + "_ACTIVE");
                }
            } else if (boost::starts_with(type, "HCLK_CMT")) {
                auto used_ccio = used_wires_starting_with(tile, "HCLK_CMT_CCIO", true);
                for (auto s : used_ccio) {
                    write_bit(s + "_ACTIVE");
                    write_bit(s + "_USED");
                }
                auto used_hclk = used_wires_starting_with(tile, "HCLK_CMT_CK_", true);
                for (auto s : used_hclk) {
                    if (boost::contains(s, "BUFHCLK")) {
                        write_bit(s + "_USED");
                        hclk_by_row[tile / ctx->chip_info->width].insert(s.substr(s.find("BUFHCLK")));
                    }
                }
            }
            pop();
            blank();
        }

        for (int tile = 0; tile < ctx->chip_info->tile_insts.ssize(); tile++) {
            std::string name = uarch->tile_name(tile);
            std::string type = ctx->get_tile_type(tile).str(ctx);
            push(name);
            if (type == "CLK_BUFG_REBUF") {
                for (auto &gclk : all_gclk) {
                    write_bit(gclk + "_ENABLE_ABOVE");
                    write_bit(gclk + "_ENABLE_BELOW");
                }
            } else if (boost::starts_with(type, "HCLK_CMT")) {
                for (auto &hclk : hclk_by_row[tile / ctx->chip_info->width]) {
                    write_bit("HCLK_CMT_CK_" + hclk + "_USED");
                }
            }
            pop();
            blank();
        }
    }

    void write_bram_width(CellInfo *ci, const std::string &name, bool is_36, bool is_y1)
    {
        int width = int_or_default(ci->params, ctx->id(name), 0);
        if (width == 0)
            return;
        int actual_width = width;
        if (is_36) {
            if (width == 1)
                actual_width = 1;
            else
                actual_width = width / 2;
        }
        if (((is_36 && width == 72) || (is_y1 && actual_width == 36)) && name == "READ_WIDTH_A") {
            write_bit(name + "_18");
        }
        if (actual_width == 36) {
            write_bit("SDP_" + name.substr(0, name.length() - 2) + "_36");
            if (name.find("WRITE") == 0) {
                write_bit(name.substr(0, name.size() - 1) + "A_18");
                write_bit(name.substr(0, name.size() - 1) + "B_18");
            } else if (name.find("READ") == 0) {
                write_bit(name.substr(0, name.size() - 1) + "B_18");
            }
        } else {
            write_bit(name + "_" + std::to_string(actual_width));
        }
    }

    void write_bram_init(int half, CellInfo *ci, bool is_36)
    {
        for (std::string mode : {"", "P"}) {
            for (int i = 0; i < (mode == "P" ? 8 : 64); i++) {
                bool has_init = false;
                std::vector<bool> init_data(256, false);
                if (is_36) {
                    for (int j = 0; j < 2; j++) {
                        IdString param = ctx->idf("INIT%s_%02X", mode.c_str(), i * 2 + j);
                        if (ci->params.count(param)) {
                            auto &init0 = ci->params.at(param);
                            has_init = true;
                            for (int k = half; k < 256; k += 2) {
                                if (k >= int(init0.str.size()))
                                    break;
                                init_data[j * 128 + (k / 2)] = init0.str[k] == Property::S1;
                            }
                        }
                    }
                } else {
                    IdString param = ctx->idf("INIT%s_%02X", mode.c_str(), i);
                    if (ci->params.count(param)) {
                        auto &init = ci->params.at(param);
                        has_init = true;
                        for (int k = 0; k < 256; k++) {
                            if (k >= int(init.str.size()))
                                break;
                            init_data[k] = init.str[k] == Property::S1;
                        }
                    }
                }
                if (has_init)
                    write_vector(stringf("INIT%s_%02X[255:0]", mode.c_str(), i), init_data);
            }
        }
    }

    void write_bram_half(int tile, int half, CellInfo *ci)
    {
        push(uarch->tile_name(tile));
        push("RAMB18_Y" + std::to_string(half));
        if (ci != nullptr) {
            bool is_36 = ci->type == id_RAMB36E1_RAMB36E1;
            write_bit("IN_USE");
            write_bram_width(ci, "READ_WIDTH_A", is_36, half == 1);
            write_bram_width(ci, "READ_WIDTH_B", is_36, half == 1);
            write_bram_width(ci, "WRITE_WIDTH_A", is_36, half == 1);
            write_bram_width(ci, "WRITE_WIDTH_B", is_36, half == 1);
            write_bit("DOA_REG", bool_or_default(ci->params, id_DOA_REG, false));
            write_bit("DOB_REG", bool_or_default(ci->params, id_DOB_REG, false));
            for (auto &invpin : invertible_pins[ctx->id(ci->attrs[id_X_ORIG_TYPE].as_string())])
                write_bit("ZINV_" + invpin.str(ctx),
                          !bool_or_default(ci->params, ctx->id("IS_" + invpin.str(ctx) + "_INVERTED"), false));
            for (auto wrmode : {"WRITE_MODE_A", "WRITE_MODE_B"}) {
                std::string mode = str_or_default(ci->params, ctx->id(wrmode), "WRITE_FIRST");
                if (mode != "WRITE_FIRST")
                    write_bit(std::string(wrmode) + "_" + mode);
            }
            write_vector("ZINIT_A[17:0]", std::vector<bool>(18, true));
            write_vector("ZINIT_B[17:0]", std::vector<bool>(18, true));
            write_vector("ZSRVAL_A[17:0]", std::vector<bool>(18, true));
            write_vector("ZSRVAL_B[17:0]", std::vector<bool>(18, true));

            write_bram_init(half, ci, is_36);
        }
        pop();
        if (half == 0) {
            auto used_rdaddrcasc = used_wires_starting_with(tile, "BRAM_CASCOUT_ADDRARDADDR", false);
            auto used_wraddrcasc = used_wires_starting_with(tile, "BRAM_CASCOUT_ADDRBWRADDR", false);
            write_bit("CASCOUT_ARD_ACTIVE", !used_rdaddrcasc.empty());
            write_bit("CASCOUT_BWR_ACTIVE", !used_wraddrcasc.empty());
        }
        pop();
    }

    void write_bram()
    {
        for (int tile = 0; tile < ctx->chip_info->tile_insts.ssize(); tile++) {
            IdString type = ctx->get_tile_type(tile);
            if (type.in(id_BRAM_L, id_BRAM_R)) {
                CellInfo *l = nullptr, *u = nullptr;
                const auto &bts = uarch->tile_status[tile].bts;
                if (bts) {
                    if (bts->cells[BEL_RAM36] != nullptr) {
                        l = bts->cells[BEL_RAM36];
                        u = bts->cells[BEL_RAM36];
                    } else {
                        l = bts->cells[BEL_RAM18_L];
                        u = bts->cells[BEL_RAM18_U];
                    }
                }
                write_bram_half(tile, 0, l);
                write_bram_half(tile, 1, u);
                blank();
            }
        }
    }

    double float_or_default(CellInfo *ci, const std::string &name, double def)
    {
        IdString p = ctx->id(name);
        if (!ci->params.count(p))
            return def;
        auto &prop = ci->params.at(p);
        if (prop.is_string)
            return std::stod(prop.as_string());
        else
            return prop.as_int64();
    }

    void write_pll_clkout(const std::string &name, CellInfo *ci)
    {
        // FIXME: variable duty cycle
        int high = 1, low = 1, phasemux = 0, delaytime = 0, frac = 0;
        bool no_count = false, edge = false;
        double divide = float_or_default(ci, name + ((name == "CLKFBOUT") ? "_MULT" : "_DIVIDE"), 1);
        double phase = float_or_default(ci, name + "_PHASE", 1);
        if (divide <= 1) {
            no_count = true;
        } else {
            high = floor(divide / 2);
            low = int(floor(divide) - high);
            if (high != low)
                edge = true;
            if (name == "CLKOUT1" || name == "CLKFBOUT")
                frac = floor(divide * 8) - floor(divide) * 8;
            int phase_eights = floor((phase / 360) * divide * 8);
            phasemux = phase_eights % 8;
            delaytime = phase_eights / 8;
        }
        bool used = false;
        if (name == "DIVCLK" || name == "CLKFBOUT") {
            used = true;
        } else {
            used = ci->getPort(ctx->id(name)) != nullptr;
        }
        if (name == "DIVCLK") {
            write_int_vector("DIVCLK_DIVCLK_HIGH_TIME[5:0]", high, 6);
            write_int_vector("DIVCLK_DIVCLK_LOW_TIME[5:0]", low, 6);
            write_bit("DIVCLK_DIVCLK_EDGE[0]", edge);
            write_bit("DIVCLK_DIVCLK_NO_COUNT[0]", no_count);
        } else if (used) {
            write_bit(name + "_CLKOUT1_OUTPUT_ENABLE[0]");
            write_int_vector(name + "_CLKOUT1_HIGH_TIME[5:0]", high, 6);
            write_int_vector(name + "_CLKOUT1_LOW_TIME[5:0]", low, 6);
            write_int_vector(name + "_CLKOUT1_PHASE_MUX[2:0]", phasemux, 3);
            write_bit(name + "_CLKOUT2_EDGE[0]", edge);
            write_bit(name + "_CLKOUT2_NO_COUNT[0]", no_count);
            write_int_vector(name + "_CLKOUT2_DELAY_TIME[5:0]", delaytime, 6);
            if (frac != 0) {
                write_bit(name + "_CLKOUT2_FRAC_EN[0]", edge);
                write_int_vector(name + "_CLKOUT2_FRAC[2:0]", frac, 3);
            }
        }
    }

    void write_pll(CellInfo *ci)
    {
        push(uarch->tile_name(ci->bel.tile));
        push("PLLE2_ADV");
        write_bit("IN_USE");
        // FIXME: should be INV not ZINV (XRay error?)
        write_bit("ZINV_PWRDWN", bool_or_default(ci->params, id_IS_PWRDWN_INVERTED, false));
        write_bit("ZINV_RST", bool_or_default(ci->params, id_IS_RST_INVERTED, false));
        write_bit("INV_CLKINSEL", bool_or_default(ci->params, id_IS_CLKINSEL_INVERTED, false));
        write_pll_clkout("DIVCLK", ci);
        write_pll_clkout("CLKFBOUT", ci);
        write_pll_clkout("CLKOUT0", ci);
        write_pll_clkout("CLKOUT1", ci);
        write_pll_clkout("CLKOUT2", ci);
        write_pll_clkout("CLKOUT3", ci);
        write_pll_clkout("CLKOUT4", ci);
        write_pll_clkout("CLKOUT5", ci);

        std::string comp = str_or_default(ci->params, id_COMPENSATION, "INTERNAL");
        push("COMPENSATION");
        if (comp == "INTERNAL") {
            // write_bit("INTERNAL");
            write_bit("Z_ZHOLD_OR_CLKIN_BUF");
        } else {
            NPNR_ASSERT_FALSE("unsupported compensation type");
        }
        pop();

        // FIXME: should these be calculated somehow?
        write_int_vector("FILTREG1_RESERVED[11:0]", 0x8, 12);
        write_int_vector("LKTABLE[39:0]", 0xB5BE8FA401ULL, 40);
        write_bit("LOCKREG3_RESERVED[0]");
        write_int_vector("TABLE[9:0]", 0x3B4, 10);
        pop(2);
    }

    void write_dsp_cell(CellInfo *ci)
    {
        auto tile_name = uarch->tile_name(ci->bel.tile);
        auto tile_side = tile_name.at(4);
        push(tile_name);
        push("DSP48");
        auto xy = uarch->rel_site_loc(uarch->get_bel_site(ci->bel));
        auto dsp = stringf("DSP_%d", xy.y);
        push(dsp);

        auto write_bus_zinv = [&](std::string name, int width) {
            for (int i = 0; i < width; i++) {
                std::string b = stringf("[%d]", i);
                bool inv = (int_or_default(ci->params, ctx->id("IS_" + name + "_INVERTED"), 0) >> i) & 0x1;
                inv |= bool_or_default(ci->params, ctx->id("IS_" + name + b + "_INVERTED"), false);
                write_bit("ZIS_" + name + "_INVERTED" + b, !inv);
            }
        };

        // value 1 is equivalent to 2, according to UG479
        // but in real life, Vivado sets AREG_0 is 0,
        // no bit is 1, and AREG_2 is 2
        auto areg = int_or_default(ci->params, ctx->id("AREG"), 1);
        if (areg == 0 or areg == 2)
            write_bit("AREG_" + std::to_string(areg));

        auto ainput = str_or_default(ci->params, ctx->id("A_INPUT"), "DIRECT");
        if (ainput == "CASCADE")
            write_bit("A_INPUT[0]");

        // value 1 is equivalent to 2, according to UG479
        // but in real life, Vivado sets AREG_0 is 0,
        // no bit is 1, and AREG_2 is 2
        auto breg = int_or_default(ci->params, ctx->id("BREG"), 1);
        if (breg == 0 or breg == 2)
            write_bit("BREG_" + std::to_string(breg));

        auto binput = str_or_default(ci->params, ctx->id("B_INPUT"), "DIRECT");
        if (binput == "CASCADE")
            write_bit("B_INPUT[0]");

        auto use_dport = str_or_default(ci->params, ctx->id("USE_DPORT"), "FALSE");
        if (use_dport == "TRUE")
            write_bit("USE_DPORT[0]");

        auto use_simd = str_or_default(ci->params, ctx->id("USE_SIMD"), "ONE48");
        if (use_simd == "TWO24")
            write_bit("USE_SIMD_FOUR12_TWO24");
        if (use_simd == "FOUR12")
            write_bit("USE_SIMD_FOUR12");

        // PATTERN
        auto pattern_str = str_or_default(ci->params, ctx->id("PATTERN"), "");
        if (!boost::empty(pattern_str)) {
            const size_t pattern_size = 48;
            std::vector<bool> pattern_vector(pattern_size, true);
            size_t i = 0;
            for (auto it = pattern_str.crbegin(); it != pattern_str.crend() && i < pattern_size; ++i, ++it) {
                pattern_vector[i] = *it == '1';
            }
            write_vector("PATTERN[47:0]", pattern_vector);
        }

        auto autoreset_patdet = str_or_default(ci->params, ctx->id("AUTORESET_PATDET"), "NO_RESET");
        if (autoreset_patdet == "RESET_MATCH")
            write_bit("AUTORESET_PATDET_RESET");
        if (autoreset_patdet == "RESET_NOT_MATCH")
            write_bit("AUTORESET_PATDET_RESET_NOT_MATCH");

        // MASK
        auto mask_str = str_or_default(ci->params, ctx->id("MASK"), "001111111111111111111111111111111111111111111111");
        // Yosys gives us 48 bit, but prjxray only recognizes 46 bits
        // The most significant two bits seem to be zero, so let us just truncate them
        const size_t mask_size = 46;
        std::vector<bool> mask_vector(mask_size, true);
        size_t i = 0;
        for (auto it = mask_str.crbegin(); it != mask_str.crend() && i < mask_size; ++i, ++it) {
            mask_vector[i] = *it == '1';
        }
        write_vector("MASK[45:0]", mask_vector);

        auto sel_mask = str_or_default(ci->params, ctx->id("SEL_MASK"), "MASK");
        if (sel_mask == "C")
            write_bit("SEL_MASK_C");
        if (sel_mask == "ROUNDING_MODE1")
            write_bit("SEL_MASK_ROUNDING_MODE1");
        if (sel_mask == "ROUNDING_MODE2")
            write_bit("SEL_MASK_ROUNDING_MODE2");

        write_bit("ZADREG[0]", !bool_or_default(ci->params, ctx->id("ADREG"), true));
        write_bit("ZALUMODEREG[0]", !bool_or_default(ci->params, ctx->id("ALUMODEREG")));
        write_bit("ZAREG_2_ACASCREG_1", !bool_or_default(ci->params, ctx->id("ACASCREG")));
        write_bit("ZBREG_2_BCASCREG_1", !bool_or_default(ci->params, ctx->id("BCASCREG")));
        write_bit("ZCARRYINREG[0]", !bool_or_default(ci->params, ctx->id("CARRYINREG")));
        write_bit("ZCARRYINSELREG[0]", !bool_or_default(ci->params, ctx->id("CARRYINSELREG")));
        write_bit("ZCREG[0]", !bool_or_default(ci->params, ctx->id("CREG"), true));
        write_bit("ZDREG[0]", !bool_or_default(ci->params, ctx->id("DREG"), true));
        write_bit("ZINMODEREG[0]", !bool_or_default(ci->params, ctx->id("INMODEREG")));
        write_bus_zinv("ALUMODE", 4);
        write_bus_zinv("INMODE", 5);
        write_bus_zinv("OPMODE", 7);
        write_bit("ZMREG[0]", !bool_or_default(ci->params, ctx->id("MREG")));
        write_bit("ZOPMODEREG[0]", !bool_or_default(ci->params, ctx->id("OPMODEREG")));
        write_bit("ZPREG[0]", !bool_or_default(ci->params, ctx->id("PREG")));
        write_bit("USE_DPORT[0]", str_or_default(ci->params, ctx->id("USE_DPORT"), "FALSE") == "TRUE");
        write_bit("ZIS_CLK_INVERTED", !bool_or_default(ci->params, ctx->id("IS_CLK_INVERTED")));
        write_bit("ZIS_CARRYIN_INVERTED", !bool_or_default(ci->params, ctx->id("IS_CARRYIN_INVERTED")));
        pop(2);

        auto write_const_pins = [&](std::string const_net_name) {
            std::vector<std::string> pins;
            const auto attr_name = "DSP_" + const_net_name + "_PINS";
            const auto attr_value = str_or_default(ci->attrs, ctx->id(attr_name), "");
            boost::split(pins, attr_value, boost::is_any_of(" "));
            for (auto pin : pins) {
                if (boost::empty(pin))
                    continue;
                auto pin_basename = pin;
                boost::erase_all(pin_basename, "0123456789");
                auto inv = bool_or_default(ci->params, ctx->id("IS_" + pin_basename + "_INVERTED"), 0);
                auto net_name = inv ? (const_net_name == "GND" ? "VCC" : "GND") : const_net_name;
                write_bit(stringf("%s_%s.DSP_%s_%c", dsp.c_str(), pin.c_str(), net_name.c_str(), tile_side));
            }
        };

        write_const_pins("GND");
        write_const_pins("VCC");

        pop();
    }

    void write_ip()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type == id_DSP48E1_DSP48E1) {
                write_dsp_cell(ci);
                blank();
            }
        }
    }

    void write_fasm()
    {
        get_invertible_pins(ctx, invertible_pins);
        write_logic();
        write_io();
        write_routing();
        write_bram();
        write_clocking();
        write_ip();
    }
};

} // namespace

void XilinxImpl::write_fasm(const std::string &filename)
{
    std::ofstream out(filename);
    if (!out)
        log_error("failed to open file %s for writing (%s)\n", filename.c_str(), strerror(errno));

    FasmBackend be(this->ctx, this, out);
    be.write_fasm();
}

NEXTPNR_NAMESPACE_END

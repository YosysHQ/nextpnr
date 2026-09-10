/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "log.h"
#include "nextpnr.h"
#include "dsp.h"
#include <algorithm>
#include "timing.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN
namespace {

bool dsp_bool_param(const dict<IdString, Property> &params, IdString key, bool def = false)
{
    auto it = params.find(key);
    if (it == params.end())
        return def;
    if (!it->second.is_string)
        return it->second.as_bool();
    const std::string &value = it->second.as_string();
    if (value == "1" || value == "true" || value == "TRUE" || value == "on" || value == "reg" ||
        value == "registered")
        return true;
    if (value == "0" || value == "false" || value == "FALSE" || value == "off" || value == "bypass")
        return false;
    log_error("DSP parameter expects a boolean or register/bypass value, got '%s'.\n", value.c_str());
    return def;
}

bool dsp_reg_param(const dict<IdString, Property> &params, IdString key)
{
    return dsp_bool_param(params, key, false);
}

struct MistralBitgen
{
    MistralBitgen(Context *ctx) : ctx(ctx), cv(ctx->cyclonev) {};
    Context *ctx;
    CycloneV *cv;

    using rnode_t = CycloneV::rnode_t;
    using pnode_t = CycloneV::pnode_t;
    using pos_t = CycloneV::pos_t;
    using block_type_t = CycloneV::block_type_t;
    using port_type_t = CycloneV::port_type_t;

    rnode_t find_rnode(block_type_t bt, pos_t pos, port_type_t port, int bi = -1, int pi = -1) const
    {
        auto pn1 = CycloneV::pnode(bt, pos, port, bi, pi);
        auto rn1 = cv->pnode_to_rnode(pn1);
        if (rn1)
            return rn1;

        if (bt == CycloneV::GPIO) {
            auto pn2 = cv->p2p_to(pn1);
            if (!pn2) {
                auto pnv = cv->p2p_from(pn1);
                if (!pnv.empty())
                    pn2 = pnv[0];
            }
            auto pn3 = cv->hmc_get_bypass(pn2);
            auto rn2 = cv->pnode_to_rnode(pn3);
            return rn2;
        }

        return 0;
    }

    void options()
    {
        if (!ctx->setting<bool>("compress_rbf", false)) {
            cv->opt_b_set(CycloneV::COMPRESSION_DIS, true);
            cv->opt_r_set(CycloneV::OPT_B, 0xffffff40adffffffULL);
        } else
            cv->opt_r_set(CycloneV::OPT_B, 0xffffff402dffffffULL);
    }

    void write_routing()
    {
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            for (auto &wire : ni->wires) {
                PipId pip = wire.second.pip;
                if (pip == PipId())
                    continue;
                WireId src = ctx->getPipSrcWire(pip), dst = ctx->getPipDstWire(pip);
                // Only write out routes that are entirely in the Mistral domain. Everything else is dealt with
                // specially
                if (src.is_nextpnr_created() || dst.is_nextpnr_created())
                    continue;
                cv->rnode_link(src.node, dst.node);
            }
        }
    }

    void write_io_cell(CellInfo *ci, int x, int y, int bi)
    {
        bool is_output = (ci->type == id_MISTRAL_OB || (ci->type == id_MISTRAL_IO && ci->getPort(id_OE) != nullptr));
        auto pos = CycloneV::xy2pos(x, y);
        // TODO: configurable pull, IO standard, etc
        cv->bmux_b_set(CycloneV::GPIO, pos, CycloneV::USE_WEAK_PULLUP, bi, false);
        if (is_output) {
            cv->bmux_m_set(CycloneV::GPIO, pos, CycloneV::DRIVE_STRENGTH, bi, CycloneV::V3P3_LVTTL_16MA_LVCMOS_2MA);
            cv->bmux_m_set(CycloneV::GPIO, pos, CycloneV::IOCSR_STD, bi, CycloneV::DIS);

            // Output gpios must also bypass things in the associated dqs
            auto dqs = cv->p2p_to(CycloneV::pnode(CycloneV::GPIO, pos, CycloneV::PNONE, bi, -1));
            if (dqs) {
                cv->bmux_m_set(CycloneV::DQS16, CycloneV::pn2p(dqs), CycloneV::INPUT_REG4_SEL, CycloneV::pn2bi(dqs),
                               CycloneV::SEL_LOCKED_DPA);
                cv->bmux_r_set(CycloneV::DQS16, CycloneV::pn2p(dqs), CycloneV::RB_T9_SEL_EREG_CFF_DELAY,
                               CycloneV::pn2bi(dqs), 0x1f);
            }
        }
        // There seem to be two mirrored OEIN inversion bits for constant OE for inputs/outputs. This might be to
        // prevent a single bitflip from turning inputs to outputs and messing up other devices on the boards, notably
        // ECP5 does similar. OEIN.0 inverted for outputs; OEIN.1 for inputs
        cv->inv_set(find_rnode(CycloneV::GPIO, pos, CycloneV::OEIN, bi, 0), is_output);
        cv->inv_set(find_rnode(CycloneV::GPIO, pos, CycloneV::OEIN, bi, 1), !is_output);
    }

    void write_clkbuf_cell(CellInfo *ci, int x, int y, int bi)
    {
        (void)ci; // currently unused
        auto pos = CycloneV::xy2pos(x, y);
        cv->bmux_r_set(CycloneV::CMUXHG, pos, CycloneV::INPUT_SEL, bi, 0x1b); // hardcode to general routing
        cv->bmux_m_set(CycloneV::CMUXHG, pos, CycloneV::TESTSYN_ENOUT_SELECT, bi, CycloneV::PRE_SYNENB);
    }

    void write_m10k_cell(CellInfo *ci, int x, int y, int bi)
    {
        auto pos = CycloneV::xy2pos(x, y);

        // Notes:
        // DATA_FLOW_THRU is probably transparent reads.

        auto dbits = ci->params.at(id_CFG_DBITS).as_int64();

        cv->bmux_b_set(CycloneV::M10K, pos, CycloneV::A_DATA_FLOW_THRU, bi, 1);
        cv->bmux_n_set(CycloneV::M10K, pos, CycloneV::A_DATA_WIDTH, bi, dbits);
        cv->bmux_m_set(CycloneV::M10K, pos, CycloneV::A_FAST_WRITE, bi, dbits == 40 ? CycloneV::SLOW : CycloneV::FAST);
        cv->bmux_m_set(CycloneV::M10K, pos, CycloneV::A_OUTPUT_SEL, bi, CycloneV::ASYNC);
        cv->bmux_r_set(CycloneV::M10K, pos, CycloneV::A_SA_WREN_DELAY, bi, 1);
        cv->bmux_r_set(CycloneV::M10K, pos, CycloneV::A_SAEN_DELAY, bi, 2);
        cv->bmux_r_set(CycloneV::M10K, pos, CycloneV::A_WL_DELAY, bi, 2);
        cv->bmux_r_set(CycloneV::M10K, pos, CycloneV::A_WR_TIMER_PULSE, bi, 0x0b);

        cv->bmux_b_set(CycloneV::M10K, pos, CycloneV::B_DATA_FLOW_THRU, bi, 1);
        cv->bmux_n_set(CycloneV::M10K, pos, CycloneV::B_DATA_WIDTH, bi, dbits);
        cv->bmux_m_set(CycloneV::M10K, pos, CycloneV::B_FAST_WRITE, bi, dbits == 40 ? CycloneV::SLOW : CycloneV::FAST);
        cv->bmux_m_set(CycloneV::M10K, pos, CycloneV::B_OUTPUT_SEL, bi, CycloneV::ASYNC);
        cv->bmux_r_set(CycloneV::M10K, pos, CycloneV::B_SA_WREN_DELAY, bi, 1);
        cv->bmux_r_set(CycloneV::M10K, pos, CycloneV::B_SAEN_DELAY, bi, 2);
        cv->bmux_r_set(CycloneV::M10K, pos, CycloneV::B_WL_DELAY, bi, 2);
        cv->bmux_r_set(CycloneV::M10K, pos, CycloneV::B_WR_TIMER_PULSE, bi, 0x0b);

        cv->bmux_n_set(CycloneV::M10K, pos, CycloneV::TOP_CLK_SEL, bi, 1);
        cv->bmux_b_set(CycloneV::M10K, pos, CycloneV::TOP_W_INV, bi, dbits != 40);
        cv->bmux_n_set(CycloneV::M10K, pos, CycloneV::TOP_W_SEL, bi, dbits != 40);
        cv->bmux_b_set(CycloneV::M10K, pos, CycloneV::BOT_CLK_INV, bi, dbits != 40);
        cv->bmux_n_set(CycloneV::M10K, pos, CycloneV::BOT_W_SEL, bi, dbits != 40);

        cv->bmux_b_set(CycloneV::M10K, pos, CycloneV::TRUE_DUAL_PORT, bi, 0);

        cv->bmux_b_set(CycloneV::M10K, pos, CycloneV::DISABLE_UNUSED, bi, 0);

        auto permute_init = [](int64_t init) -> int64_t {
            const int permutation[40] = {0, 20, 10, 30, 1, 21, 11, 31, 2, 22, 12, 32, 3, 23, 13, 33, 4, 24, 14, 34,
                                         5, 25, 15, 35, 6, 26, 16, 36, 7, 27, 17, 37, 8, 28, 18, 38, 9, 29, 19, 39};

            int64_t output = 0;
            for (int bit = 0; bit < 40; bit++)
                output |= ((init >> permutation[bit]) & 1) << bit;
            return ~output; // RAM init is inverted.
        };

        Property init;
        if (ci->params.count(id_INIT) == 0) {
            init = Property{0, 10240};
        } else {
            init = ci->params.at(id_INIT);
        }
        for (int bi = 0; bi < 256; bi++)
            cv->bmux_r_set(CycloneV::M10K, pos, CycloneV::RAM, bi, permute_init(init.extract(bi * 40, 40).as_int64()));
    }

    void write_dsp_block(int x, int y)
    {
        auto pos = CycloneV::xy2pos(x, y);

        struct DspBinding
        {
            CellInfo *cell;
            std::vector<int> a_groups;
            std::vector<int> b_groups;
            std::vector<int> z_groups;
            std::vector<int> c_groups;
        };

        // A DSP tile has several logical BELs. Discover the bound cells from
        // the tile rather than relying on cell-map iteration order, then
        // configure the shared mode/sign controls once.
        std::vector<BelId> dsp_bels;
        for (BelId bel : ctx->getBelsByTile(x, y)) {
            if (ctx->getBelType(bel).in(id_MISTRAL_MUL9X9, id_MISTRAL_MUL18X18, id_MISTRAL_MUL27X27) &&
                ctx->getBoundBelCell(bel) != nullptr)
                dsp_bels.push_back(bel);
        }
        if (dsp_bels.empty())
            return;

        std::sort(dsp_bels.begin(), dsp_bels.end(), [](BelId a, BelId b) { return a.z < b.z; });
        IdString mode_type = ctx->getBelType(dsp_bels.front());
        std::vector<DspBinding> bindings;
        CellInfo *first = ctx->getBoundBelCell(dsp_bels.front());
        NPNR_ASSERT(first != nullptr);
        bool preadder = dsp_bool_param(first->params, id_PREADDER_EN);
        for (BelId bel : dsp_bels) {
            if (ctx->getBelType(bel) != mode_type)
                log_error("DSP tile %s contains incompatible multiplier modes (%s and %s).\n", ctx->nameOfBel(bel),
                          ctx->nameOf(mode_type), ctx->nameOf(ctx->getBelType(bel)));
            CellInfo *cell = ctx->getBoundBelCell(bel);
            if (cell == nullptr)
                continue;
            if (mode_type == id_MISTRAL_MUL9X9) {
                NPNR_ASSERT(bel.z >= 0 && bel.z < int(mistral_dsp_lanes.size()));
                const auto &lane = mistral_dsp_lanes.at(bel.z);
                bindings.push_back({cell,
                                    {lane.a_group},
                                    {preadder ? mistral_dsp_preadder_y_groups.at(bel.z) : lane.b_group},
                                    preadder ? std::vector<int>{mistral_dsp_preadder_z_groups.at(bel.z)}
                                              : std::vector<int>{},
                                    {}});
            } else if (mode_type == id_MISTRAL_MUL18X18) {
                bindings.push_back({cell,
                                    {mistral_dsp_18x18_a_groups.begin(), mistral_dsp_18x18_a_groups.end()},
                                    {mistral_dsp_18x18_b_groups.begin(), mistral_dsp_18x18_b_groups.end()},
                                    {},
                                    {mistral_dsp_18x18_c_groups.begin(), mistral_dsp_18x18_c_groups.end()}});
            } else if (mode_type == id_MISTRAL_MUL27X27) {
                bindings.push_back({cell,
                                    {mistral_dsp_27x27_a_groups.begin(), mistral_dsp_27x27_a_groups.end()},
                                    {mistral_dsp_27x27_b_groups.begin(), mistral_dsp_27x27_b_groups.end()},
                                    {},
                                    {}});
            }
        }
        if (bindings.empty())
            return;

        CycloneV::bmux_type_t mode;
        if (mode_type == id_MISTRAL_MUL9X9)
            mode = CycloneV::M9X9;
        else if (mode_type == id_MISTRAL_MUL18X18)
            mode = CycloneV::M18X18P36;
        else if (mode_type == id_MISTRAL_MUL27X27)
            mode = CycloneV::M27X27;
        else
            NPNR_ASSERT_FALSE("unreachable DSP mode");
        NPNR_ASSERT(cv->bmux_m_set(CycloneV::DSP, pos, CycloneV::MODE, 0, mode));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::AX_SIGNED, 0,
                                  dsp_bool_param(bindings.front().cell->params, id_A_SIGNED, true)));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::AY_SIGNED, 0,
                                  dsp_bool_param(bindings.front().cell->params, id_B_SIGNED, true)));

        auto set_reg = [&](CycloneV::bmux_type_t mux, IdString key) {
            NPNR_ASSERT(cv->bmux_m_set(CycloneV::DSP, pos, mux, 0,
                                      dsp_reg_param(first->params, key) ? CycloneV::REG : CycloneV::BYPASS));
        };
        set_reg(CycloneV::INREG_CTRL_AX, id_INREG_CTRL_AX);
        set_reg(CycloneV::INREG_CTRL_AY, id_INREG_CTRL_AY);
        set_reg(CycloneV::INREG_CTRL_AZ, id_INREG_CTRL_AZ);
        set_reg(CycloneV::INREG_CTRL_BX, id_INREG_CTRL_BX);
        set_reg(CycloneV::INREG_CTRL_BY, id_INREG_CTRL_BY);
        set_reg(CycloneV::INREG_CTRL_BZ, id_INREG_CTRL_BZ);
        set_reg(CycloneV::OREG_CTRL, id_OREG_CTRL);

        // DSP control inputs are active-high at the tile boundary. Keep the
        // packed PIN_0/PIN_1/PIN_INV state and translate it into the DSP's
        // inversion/force bits instead of leaving a disconnected control at
        // its silicon default.
        auto control_state = [&](IdString port, CellPinState default_state) {
            auto it = first->pin_data.find(port);
            if (it != first->pin_data.end() && it->second.state != PIN_SIG)
                return it->second.state;
            if (first->getPort(port) == nullptr)
                return default_state;
            return PIN_SIG;
        };
        auto control_inverted = [&](IdString port, CellPinState default_state) {
            CellPinState state = control_state(port, default_state);
            return state == PIN_0 || state == PIN_INV;
        };
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::ACC_INV, 0,
                                  control_inverted(id_ACCUMULATE, PIN_0)));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::PRELOAD_INV, 0,
                                  control_inverted(id_LOADCONST, PIN_0)));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::SUB_INV, 0,
                                  control_inverted(id_SUB, PIN_0)));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::DEC_INV, 0,
                                  control_inverted(id_NEGATE, PIN_0)));

        auto uses_fabric_pin = [&](IdString port, const char *pin) {
            if (first->getPort(port) == nullptr)
                return true;
            const auto &pins = first->pin_data.at(port).bel_pins;
            return !pins.empty() && pins.front() == ctx->id(pin);
        };
        // Unrouted fabric clear is low (unlike the arithmetic/data inputs).
        // Select it for constants and keep the unused second clear inactive.
        CellPinState aclr_state = control_state(id_ACLR, PIN_0);
        NPNR_ASSERT(cv->bmux_n_set(CycloneV::DSP, pos, CycloneV::ACLR0_SEL, 0,
                                  uses_fabric_pin(id_ACLR, "ACLR_FABRIC") ? 2 : 0));
        NPNR_ASSERT(cv->bmux_n_set(CycloneV::DSP, pos, CycloneV::ACLR1_SEL, 0, 3));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::ACLR0_INV, 0,
                                  aclr_state == PIN_1 || aclr_state == PIN_INV));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::ACLR1_INV, 0, false));

        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::PREADDER_EN, 0, preadder));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::PREADDER_SUB, 0,
                                  dsp_bool_param(first->params, id_PREADDER_SUB)));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::CASCADE_EN, 0,
                                  dsp_bool_param(first->params, id_CASCADE_EN)));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::CASCADE_1ST_EN, 0,
                                  dsp_bool_param(first->params, id_CASCADE_1ST_EN)));
        NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::CHAIN_OUTPUT_EN, 0,
                                  dsp_bool_param(first->params, id_CHAIN_OUTPUT_EN)));

        bool any_register = dsp_reg_param(first->params, id_INREG_CTRL_AX) ||
                            dsp_reg_param(first->params, id_INREG_CTRL_AY) ||
                            dsp_reg_param(first->params, id_INREG_CTRL_AZ) ||
                            dsp_reg_param(first->params, id_INREG_CTRL_BX) ||
                            dsp_reg_param(first->params, id_INREG_CTRL_BY) ||
                            dsp_reg_param(first->params, id_INREG_CTRL_BZ) || dsp_reg_param(first->params, id_OREG_CTRL);
        if (any_register) {
            if (first->getPort(id_CLK) == nullptr && first->get_pin_state(id_CLK) != PIN_0 &&
                first->get_pin_state(id_CLK) != PIN_1)
                log_error("DSP cell '%s' enables a register without a CLK port.\n", ctx->nameOf(first));
            CellPinState ena_state = control_state(id_ENA, PIN_1);
            bool ena_inverted = ena_state == PIN_0 || ena_state == PIN_INV;
            bool ena_forced = first->getPort(id_ENA) == nullptr && ena_state != PIN_0;
            NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::ENABLE0_INV, 0, ena_inverted));
            NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::ENABLE0_FORCE, 0,
                                      ena_forced));

            CellPinState clk_state = control_state(id_CLK, PIN_SIG);
            NPNR_ASSERT(cv->bmux_b_set(CycloneV::DSP, pos, CycloneV::CLK0_INV, 0,
                                      clk_state == PIN_1 || clk_state == PIN_INV));
        }

        NPNR_ASSERT(cv->bmux_n_set(CycloneV::DSP, pos, CycloneV::CLK0_SEL, 0,
                                  uses_fabric_pin(id_CLK, "CLK_FABRIC") ? 3 : 0));
        NPNR_ASSERT(cv->bmux_n_set(CycloneV::DSP, pos, CycloneV::CLK1_SEL, 0, 4));
        NPNR_ASSERT(cv->bmux_n_set(CycloneV::DSP, pos, CycloneV::CLK2_SEL, 0, 5));

        // Unrouted DSP inputs are 1; invert unused inputs to keep them at 0.
        for (int group = 0; group < 12; ++group) {
            unsigned inv = 0x1ff;
            for (const auto &binding : bindings) {
                auto apply = [&](const std::vector<int> &groups, char port_name) {
                    for (size_t slice = 0; slice < groups.size(); ++slice) {
                        if (groups.at(slice) != group)
                            continue;
                        for (int bit = 0; bit < 9; ++bit) {
                            IdString port = ctx->idf("%c[%d]", port_name, int(slice * 9 + bit));
                            auto state = binding.cell->get_pin_state(port);
                            bool invert = state == PIN_0 || state == PIN_INV ||
                                          (state == PIN_SIG && binding.cell->getPort(port) == nullptr);
                            if (!invert)
                                inv &= ~(1u << bit);
                        }
                    }
                };
                apply(binding.a_groups, 'A');
                apply(binding.b_groups, 'B');
                apply(binding.z_groups, 'Z');
                apply(binding.c_groups, 'C');
            }
            NPNR_ASSERT(cv->bmux_r_set(CycloneV::DSP, pos, CycloneV::DATA_INV, group, inv));
        }
    }

    void write_cells()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            Loc loc = ctx->getBelLocation(ci->bel);
            int bi = ctx->bel_data(ci->bel).block_index;
            if (ctx->is_io_cell(ci->type))
                write_io_cell(ci, loc.x, loc.y, bi);
            else if (ctx->is_clkbuf_cell(ci->type))
                write_clkbuf_cell(ci, loc.x, loc.y, bi);
            else if (ci->type == id_MISTRAL_M10K)
                write_m10k_cell(ci, loc.x, loc.y, bi);
        }
        for (auto dsp_pos : cv->dsp_get_pos())
            write_dsp_block(CycloneV::pos2x(dsp_pos), CycloneV::pos2y(dsp_pos));
    }

    bool write_alm(uint32_t lab, uint8_t alm)
    {
        auto &alm_data = ctx->labs.at(lab).alms.at(alm);
        auto block_type = ctx->labs.at(lab).is_mlab ? CycloneV::MLAB : CycloneV::LAB;

        std::array<CellInfo *, 2> luts{ctx->getBoundBelCell(alm_data.lut_bels[0]),
                                       ctx->getBoundBelCell(alm_data.lut_bels[1])};
        std::array<CellInfo *, 4> ffs{
                ctx->getBoundBelCell(alm_data.ff_bels[0]), ctx->getBoundBelCell(alm_data.ff_bels[1]),
                ctx->getBoundBelCell(alm_data.ff_bels[2]), ctx->getBoundBelCell(alm_data.ff_bels[3])};
        // Skip empty ALMs
        if (std::all_of(luts.begin(), luts.end(), [](CellInfo *c) { return !c; }) &&
            std::all_of(ffs.begin(), ffs.end(), [](CellInfo *c) { return !c; }))
            return false;

        bool is_lutram =
                (luts[0] && luts[0]->combInfo.mlab_group != -1) || (luts[1] && luts[1]->combInfo.mlab_group != -1);

        auto pos = alm_data.lut_bels[0].pos;
        if (is_lutram) {
            for (int i = 0; i < 10; i++) {
                // Many MLAB settings apply to the whole LAB, not just the ALM
                cv->bmux_m_set(block_type, pos, CycloneV::TMODE, i, CycloneV::RAM);
                cv->bmux_m_set(block_type, pos, CycloneV::BMODE, i, CycloneV::RAM);
                cv->bmux_n_set(block_type, pos, CycloneV::T_FEEDBACK_SEL, i, 1);
            }
            cv->bmux_r_set(block_type, pos, CycloneV::LUT_MASK, alm, 0xFFFFFFFFFFFFFFFFULL); // TODO: LUTRAM init
            cv->bmux_b_set(block_type, pos, CycloneV::BPKREG1, alm, true);
            cv->bmux_b_set(block_type, pos, CycloneV::TPKREG0, alm, true);
            cv->bmux_m_set(block_type, pos, CycloneV::MCRG_VOLTAGE, 0, CycloneV::VCCL);
            cv->bmux_b_set(block_type, pos, CycloneV::RAM_DIS, 0, false);
            cv->bmux_b_set(block_type, pos, CycloneV::WRITE_EN, 0, true);
            cv->bmux_n_set(block_type, pos, CycloneV::WRITE_PULSE_LENGTH, 0, 650); // picoseconds, presumably
            // TODO: understand how these enables really work
            cv->bmux_b_set(block_type, pos, CycloneV::EN2_EN, 0, false);
            cv->bmux_b_set(block_type, pos, CycloneV::SCLR_DIS, 0, true);
        } else {
            // Combinational mode - TODO: flop feedback and more modes...
            cv->bmux_m_set(block_type, pos, CycloneV::TMODE, alm, alm_data.l6_mode ? CycloneV::C_E : CycloneV::E_0);
            cv->bmux_m_set(block_type, pos, CycloneV::BMODE, alm, alm_data.l6_mode ? CycloneV::D_E : CycloneV::E_1);
            // LUT function
            cv->bmux_r_set(block_type, pos, CycloneV::LUT_MASK, alm, ctx->compute_lut_mask(lab, alm));
        }
        // DFF/LUT output selection
        const std::array<CycloneV::bmux_type_t, 6> mux_settings{CycloneV::TDFF0, CycloneV::TDFF1, CycloneV::TDFF1L,
                                                                CycloneV::BDFF0, CycloneV::BDFF1, CycloneV::BDFF1L};
        const std::array<CycloneV::port_type_t, 6> mux_port{CycloneV::FFT0, CycloneV::FFT1, CycloneV::FFT1L,
                                                            CycloneV::FFB0, CycloneV::FFB1, CycloneV::FFB1L};
        for (int i = 0; i < 6; i++) {
            if (ctx->wires_connected(alm_data.comb_out[i / 3], ctx->get_port(block_type, CycloneV::pos2x(pos),
                                                                             CycloneV::pos2y(pos), alm, mux_port[i])))
                cv->bmux_m_set(block_type, pos, mux_settings[i], alm, CycloneV::NLUT);
        }

        bool is_carry = (luts[0] && luts[0]->combInfo.is_carry) || (luts[1] && luts[1]->combInfo.is_carry);
        if (is_carry)
            cv->bmux_m_set(block_type, pos, CycloneV::ARITH_SEL, alm, CycloneV::ADDER);
        // The carry in/out enable bits
        if (is_carry && alm == 0 && !luts[0]->combInfo.carry_start)
            cv->bmux_b_set(block_type, pos, CycloneV::TTO_DIS, 0, true);
        if (is_carry && alm == 5)
            cv->bmux_b_set(block_type, pos, CycloneV::BTO_DIS, 0, true);
        // Flipflop configuration
        const std::array<CycloneV::bmux_type_t, 2> ef_sel{CycloneV::TEF_SEL, CycloneV::BEF_SEL};
        // This isn't a typo; the *PKREG* bits really are mirrored.
        const std::array<CycloneV::bmux_type_t, 4> pkreg{CycloneV::TPKREG1, CycloneV::TPKREG0, CycloneV::BPKREG1,
                                                         CycloneV::BPKREG0};

        const std::array<CycloneV::bmux_type_t, 2> clk_sel{CycloneV::TCLK_SEL, CycloneV::BCLK_SEL},
                clr_sel{CycloneV::TCLR_SEL, CycloneV::BCLR_SEL}, sclr_dis{CycloneV::TSCLR_DIS, CycloneV::BSCLR_DIS},
                sload_en{CycloneV::TSLOAD_EN, CycloneV::BSLOAD_EN};

        const std::array<CycloneV::bmux_type_t, 3> clk_choice{CycloneV::CLK0, CycloneV::CLK1, CycloneV::CLK2};

        const std::array<CycloneV::bmux_type_t, 3> clk_inv{CycloneV::CLK0_INV, CycloneV::CLK1_INV, CycloneV::CLK2_INV},
                en_en{CycloneV::EN0_EN, CycloneV::EN1_EN, CycloneV::EN2_EN},
                en_ninv{CycloneV::EN0_NINV, CycloneV::EN1_NINV, CycloneV::EN2_NINV};
        const std::array<CycloneV::bmux_type_t, 2> aclr_inv{CycloneV::ACLR0_INV, CycloneV::ACLR1_INV};

        for (int i = 0; i < 2; i++) {
            // EF selection mux
            if (ctx->wires_connected(ctx->getBelPinWire(alm_data.lut_bels[i], i ? id_F0 : id_F1), alm_data.sel_ef[i]))
                cv->bmux_m_set(block_type, pos, ef_sel[i], alm, CycloneV::bmux_type_t::F);
        }

        for (int i = 0; i < 4; i++) {
            CellInfo *ff = ffs[i];
            if (!ff)
                continue;
            // PKREG (input selection)
            if (ctx->wires_connected(alm_data.sel_ef[i / 2], alm_data.ff_in[i]))
                cv->bmux_b_set(block_type, pos, pkreg[i], alm, true);
            // Control set
            // CLK+ENA
            int ce_idx = alm_data.clk_ena_idx[i / 2];
            cv->bmux_m_set(block_type, pos, clk_sel[i / 2], alm, clk_choice[ce_idx]);
            if (ff->ffInfo.ctrlset.clk.inverted)
                cv->bmux_b_set(block_type, pos, clk_inv[ce_idx], 0, true);
            if (ff->getPort(id_ENA) != nullptr) { // not using ffInfo.ctrlset, this has a fake net always to
                                                  // ensure different constants don't collide
                cv->bmux_b_set(block_type, pos, en_en[ce_idx], 0, true);
                cv->bmux_b_set(block_type, pos, en_ninv[ce_idx], 0, ff->ffInfo.ctrlset.ena.inverted);
            } else {
                cv->bmux_b_set(block_type, pos, en_en[ce_idx], 0, false);
            }
            // ACLR
            int aclr_idx = alm_data.aclr_idx[i / 2];
            cv->bmux_b_set(block_type, pos, clr_sel[i / 2], alm, aclr_idx == 1);
            if (ff->ffInfo.ctrlset.aclr.inverted)
                cv->bmux_b_set(block_type, pos, aclr_inv[aclr_idx], 0, true);
            // SCLR
            if (ff->ffInfo.ctrlset.sclr.net != nullptr) {
                cv->bmux_b_set(block_type, pos, CycloneV::SCLR_INV, 0, ff->ffInfo.ctrlset.sclr.inverted);
                cv->bmux_b_set(block_type, pos, CycloneV::SCLR_DIS, 0, false);
            } else {
                cv->bmux_b_set(block_type, pos, sclr_dis[i / 2], alm, true);
            }
            // SLOAD
            if (ff->ffInfo.ctrlset.sload.net != nullptr) {
                cv->bmux_b_set(block_type, pos, sload_en[i / 2], alm, true);
                if (ff->ffInfo.ctrlset.sload.net->name == ctx->id("$PACKER_GND_NET")) {
                    // force-disabled LOAD (see workaround in assign_ff_info)
                    cv->bmux_b_set(block_type, pos, CycloneV::SLOAD_EN, 0, false);
                }
                cv->bmux_b_set(block_type, pos, CycloneV::SLOAD_INV, 0, ff->ffInfo.ctrlset.sload.inverted);
            }
        }
        if (is_lutram) {
            for (int i = 0; i < 2; i++) {
                CellInfo *lut = luts[i];
                if (!lut || lut->combInfo.mlab_group == -1)
                    continue;
                int ce_idx = alm_data.clk_ena_idx[1];
                cv->bmux_m_set(block_type, pos, clk_sel[1], alm, clk_choice[ce_idx]);
                if (lut->combInfo.wclk.inverted)
                    cv->bmux_b_set(block_type, pos, clk_inv[ce_idx], 0, true);
                if (lut->getPort(id_A1EN) != nullptr) {
                    cv->bmux_b_set(block_type, pos, en_en[ce_idx], 0, true);
                    cv->bmux_b_set(block_type, pos, en_ninv[ce_idx], 0, lut->combInfo.we.inverted);
                } else {
                    cv->bmux_b_set(block_type, pos, en_en[ce_idx], 0, false);
                }
                // TODO: understand what these are doing
                cv->bmux_b_set(block_type, pos, sclr_dis[0], alm, true);
                cv->bmux_b_set(block_type, pos, sclr_dis[1], alm, true);
            }
        }
        return true;
    }

    void write_ff_routing(uint32_t lab)
    {
        auto &lab_data = ctx->labs.at(lab);
        auto pos = lab_data.alms.at(0).lut_bels[0].pos;
        auto block_type = ctx->labs.at(lab).is_mlab ? CycloneV::MLAB : CycloneV::LAB;

        const std::array<CycloneV::bmux_type_t, 2> aclr_inp{CycloneV::ACLR0_SEL, CycloneV::ACLR1_SEL};
        for (int i = 0; i < 2; i++) {
            // Quartus seems to set unused ACLRs to ACLR0
            if (lab_data.aclr_used[i])
                cv->bmux_m_set(block_type, pos, aclr_inp[i], 0, (i == 1) ? CycloneV::DIN2 : CycloneV::DIN3);
            else if (i == 0)
                cv->bmux_m_set(block_type, pos, aclr_inp[i], 0, CycloneV::ACLR0);
        }
        for (int i = 0; i < 3; i++) {
            // Check for fabric->clock routing
            if (ctx->wires_connected(
                        ctx->get_port(block_type, CycloneV::pos2x(pos), CycloneV::pos2y(pos), -1, CycloneV::DATAIN, 0),
                        lab_data.clk_wires[i]))
                cv->bmux_m_set(block_type, pos, CycloneV::CLKA_SEL, 0, CycloneV::DIN0);
        }
    }

    void write_labs()
    {
        for (size_t lab = 0; lab < ctx->labs.size(); lab++) {
            bool used = false;
            for (uint8_t alm = 0; alm < 10; alm++)
                used |= write_alm(lab, alm);
            if (used)
                write_ff_routing(lab);
        }
    }

    void run()
    {
        cv->clear();
        options();
        write_routing();
        write_cells();
        write_labs();
        ctx->bitstream_configured = true;
    }
};
} // namespace

void Arch::build_bitstream()
{
    MistralBitgen gen(getCtx());
    gen.run();

    // This is a hack to run timing analysis yet again after the bitstream is
    // configured in Mistral, because the analogue simulator won't work until
    // it has a bitstream in the library.
    //
    // A better solution would be to move a lot of this bitstream code to
    // {un,}bind{Bel, Pip} and friends, but we're not there yet.
    log_info("Running signoff timing analysis...\n");

    timing_analysis(getCtx(), true, true, true, true, true);
}

NEXTPNR_NAMESPACE_END

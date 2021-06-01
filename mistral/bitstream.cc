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
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN
namespace {
struct MistralBitgen
{
    MistralBitgen(Context *ctx) : ctx(ctx), cv(ctx->cyclonev){};
    Context *ctx;
    CycloneV *cv;

    void init()
    {
        ctx->init_base_bitstream();
        // Default options
        cv->opt_b_set(CycloneV::ALLOW_DEVICE_WIDE_OUTPUT_ENABLE_DIS, true);
        if (!ctx->setting<bool>("compress_rbf", false))
            cv->opt_b_set(CycloneV::COMPRESSION_DIS, true);
        cv->opt_n_set(CycloneV::CRC_DIVIDE_ORDER, 8);
        cv->opt_b_set(CycloneV::CVP_CONF_DONE_EN, true);
        cv->opt_b_set(CycloneV::DEVICE_WIDE_RESET_EN, true);
        cv->opt_n_set(CycloneV::DRIVE_STRENGTH, 8);
        cv->opt_b_set(CycloneV::IOCSR_READY_FROM_CSR_DONE_EN, true);
        cv->opt_b_set(CycloneV::NCEO_DIS, true);
        cv->opt_b_set(CycloneV::OCT_DONE_DIS, true);
        cv->opt_r_set(CycloneV::OPT_A, 0x1dff);
        if (!ctx->setting<bool>("compress_rbf", false))
            cv->opt_r_set(CycloneV::OPT_B, 0xffffff40adffffffULL);
        else
            cv->opt_r_set(CycloneV::OPT_B, 0xffffff402dffffffULL);
        cv->opt_b_set(CycloneV::RELEASE_CLEARS_BEFORE_TRISTATES_DIS, true);
        cv->opt_b_set(CycloneV::RETRY_CONFIG_ON_ERROR_EN, true);
        cv->opt_r_set(CycloneV::START_UP_CLOCK, 0x3F);
        // Default inversion
        write_default_inv();
    }

    void write_default_inv()
    {
        // Some PNODEs are inverted by default. Set them up here.
        for (const auto &pn2r : cv->get_all_p2r()) {
            const auto &pn = pn2r.first;
            auto pt = CycloneV::pn2pt(pn);
            auto pi = CycloneV::pn2pi(pn);

            switch (CycloneV::pn2bt(pn)) {
            case CycloneV::HMC: {
                // HMC OE are inverted to set OE=0, i.e. unused pins floating
                // TODO: handle the case when we are using the HMC or HMC bypass
                std::string name(CycloneV::port_type_names[pt]);
                if (name.compare(0, 5, "IOINT") != 0 || name.compare(name.size() - 2, 2, "OE") != 0)
                    continue;
                cv->inv_set(pn2r.second, true);
                break;
            };
            // HPS IO - TODO: what about when we actually support the HPS primitives?
            case CycloneV::HPS_BOOT: {
                switch (pt) {
                case CycloneV::CSEL_EN:
                case CycloneV::BSEL_EN:
                case CycloneV::BOOT_FROM_FPGA_READY:
                case CycloneV::BOOT_FROM_FPGA_ON_FAILURE:
                    cv->inv_set(pn2r.second, true);
                    break;
                case CycloneV::CSEL:
                    if (pi < 2)
                        cv->inv_set(pn2r.second, true);
                    break;
                case CycloneV::BSEL:
                    if (pi < 3)
                        cv->inv_set(pn2r.second, true);
                    break;
                default:
                    break;
                };
                break;
            };
            case CycloneV::HPS_CROSS_TRIGGER: {
                if (pt == CycloneV::CLK_EN)
                    cv->inv_set(pn2r.second, true);
                break;
            };
            case CycloneV::HPS_TEST: {
                if (pt == CycloneV::CFG_DFX_BYPASS_ENABLE)
                    cv->inv_set(pn2r.second, true);
                break;
            };
            case CycloneV::GPIO: {
                // Ignore GPIO used by the design
                BelId bel = ctx->bel_by_block_idx(CycloneV::pn2x(pn), CycloneV::pn2y(pn), id_MISTRAL_IO,
                                                  CycloneV::pn2bi(pn));
                if (bel != BelId() && ctx->getBoundBelCell(bel) != nullptr)
                    continue;
                // Bonded IO invert OEIN.1 which disables the output buffer and floats the IO
                // Unbonded IO invert OEIN.0 which enables the output buffer, and {DATAIN.[0123]} to drive a constant
                // GND, presumably for power/EMI reasons
                bool is_bonded = cv->pin_find_pnode(pn) != nullptr;
                if (is_bonded && (pt != CycloneV::OEIN || pi != 1))
                    continue;
                if (!is_bonded && (pt != CycloneV::DATAIN) && (pt != CycloneV::OEIN || pi != 0))
                    continue;
                cv->inv_set(pn2r.second, true);
                break;
            };
            case CycloneV::FPLL: {
                if (pt == CycloneV::EXTSWITCH || (pt == CycloneV::CLKEN && pi < 2))
                    cv->inv_set(pn2r.second, true);
                break;
            };
            default:
                break;
            }
        }
    }

    void write_dqs()
    {
        for (auto pos : cv->dqs16_get_pos()) {
            int x = CycloneV::pos2x(pos), y = CycloneV::pos2y(pos);
            // DQS bypass for used output pins
            for (int z = 0; z < 16; z++) {
                int ioy = y + (z / 4) - 2;
                if (ioy < 0 || ioy >= int(cv->get_tile_sy()))
                    continue;
                BelId bel = ctx->bel_by_block_idx(x, ioy, id_MISTRAL_IO, z % 4);
                if (bel == BelId())
                    continue;
                CellInfo *ci = ctx->getBoundBelCell(bel);
                if (ci == nullptr || (ci->type != id_MISTRAL_IO && ci->type != id_MISTRAL_OB))
                    continue; // not an output
                cv->bmux_m_set(CycloneV::DQS16, pos, CycloneV::INPUT_REG4_SEL, z, CycloneV::SEL_LOCKED_DPA);
                cv->bmux_r_set(CycloneV::DQS16, pos, CycloneV::RB_T9_SEL_EREG_CFF_DELAY, z, 0x1f);
            }
        }
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
        bool is_output =
                (ci->type == id_MISTRAL_OB || (ci->type == id_MISTRAL_IO && get_net_or_empty(ci, id_OE) != nullptr));
        auto pos = CycloneV::xy2pos(x, y);
        // TODO: configurable pull, IO standard, etc
        cv->bmux_b_set(CycloneV::GPIO, pos, CycloneV::USE_WEAK_PULLUP, bi, false);
        if (is_output) {
            cv->bmux_m_set(CycloneV::GPIO, pos, CycloneV::DRIVE_STRENGTH, bi, CycloneV::V3P3_LVTTL_16MA_LVCMOS_2MA);
            cv->bmux_m_set(CycloneV::GPIO, pos, CycloneV::IOCSR_STD, bi, CycloneV::DIS);
        }
        // There seem to be two mirrored OEIN inversion bits for constant OE for inputs/outputs. This might be to
        // prevent a single bitflip from turning inputs to outputs and messing up other devices on the boards, notably
        // ECP5 does similar. OEIN.0 inverted for outputs; OEIN.1 for inputs
        cv->inv_set(cv->pnode_to_rnode(CycloneV::pnode(CycloneV::GPIO, pos, CycloneV::OEIN, bi, is_output ? 0 : 1)),
                    true);
    }

    void write_clkbuf_cell(CellInfo *ci, int x, int y, int bi)
    {
        (void)ci; // currently unused
        auto pos = CycloneV::xy2pos(x, y);
        cv->bmux_r_set(CycloneV::CMUXHG, pos, CycloneV::INPUT_SELECT, bi, 0x1b); // hardcode to general routing
        cv->bmux_m_set(CycloneV::CMUXHG, pos, CycloneV::TESTSYN_ENOUT_SELECT, bi, CycloneV::PRE_SYNENB);
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
        }
    }

    bool write_alm(uint32_t lab, uint8_t alm)
    {
        auto &alm_data = ctx->labs.at(lab).alms.at(alm);

        std::array<CellInfo *, 2> luts{ctx->getBoundBelCell(alm_data.lut_bels[0]),
                                       ctx->getBoundBelCell(alm_data.lut_bels[1])};
        std::array<CellInfo *, 4> ffs{
                ctx->getBoundBelCell(alm_data.ff_bels[0]), ctx->getBoundBelCell(alm_data.ff_bels[1]),
                ctx->getBoundBelCell(alm_data.ff_bels[2]), ctx->getBoundBelCell(alm_data.ff_bels[3])};
        // Skip empty ALMs
        if (std::all_of(luts.begin(), luts.end(), [](CellInfo *c) { return !c; }) &&
            std::all_of(ffs.begin(), ffs.end(), [](CellInfo *c) { return !c; }))
            return false;

        auto pos = alm_data.lut_bels[0].pos;
        // Combinational mode - TODO: flop feedback
        cv->bmux_m_set(CycloneV::LAB, pos, CycloneV::MODE, alm, alm_data.l6_mode ? CycloneV::L6 : CycloneV::L5);
        // LUT function
        cv->bmux_r_set(CycloneV::LAB, pos, CycloneV::LUT_MASK, alm, ctx->compute_lut_mask(lab, alm));
        // DFF/LUT output selection
        const std::array<CycloneV::bmux_type_t, 6> mux_settings{CycloneV::TDFF0, CycloneV::TDFF1, CycloneV::TDFF1L,
                                                                CycloneV::BDFF0, CycloneV::BDFF1, CycloneV::BDFF1L};
        const std::array<CycloneV::port_type_t, 6> mux_port{CycloneV::FFT0, CycloneV::FFT1, CycloneV::FFT1L,
                                                            CycloneV::FFB0, CycloneV::FFB1, CycloneV::FFB1L};
        for (int i = 0; i < 6; i++) {
            if (ctx->wires_connected(alm_data.comb_out[i / 3], ctx->get_port(CycloneV::LAB, CycloneV::pos2x(pos),
                                                                             CycloneV::pos2y(pos), alm, mux_port[i])))
                cv->bmux_m_set(CycloneV::LAB, pos, mux_settings[i], alm, CycloneV::NLUT);
        }

        bool is_carry = (luts[0] && luts[0]->combInfo.is_carry) || (luts[1] && luts[1]->combInfo.is_carry);
        if (is_carry)
            cv->bmux_m_set(CycloneV::LAB, pos, CycloneV::ARITH_SEL, alm, CycloneV::ADDER);
        // The carry in/out enable bits
        if (is_carry && alm == 0 && !luts[0]->combInfo.carry_start)
            cv->bmux_b_set(CycloneV::LAB, pos, CycloneV::TTO_DIS, 0, true);
        if (is_carry && alm == 5)
            cv->bmux_b_set(CycloneV::LAB, pos, CycloneV::BTO_DIS, 0, true);
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
                cv->bmux_m_set(CycloneV::LAB, pos, ef_sel[i], alm, CycloneV::bmux_type_t::F);
        }

        for (int i = 0; i < 4; i++) {
            CellInfo *ff = ffs[i];
            if (!ff)
                continue;
            // PKREG (input selection)
            if (ctx->wires_connected(alm_data.sel_ef[i / 2], alm_data.ff_in[i]))
                cv->bmux_b_set(CycloneV::LAB, pos, pkreg[i], alm, true);
            // Control set
            // CLK+ENA
            int ce_idx = alm_data.clk_ena_idx[i / 2];
            cv->bmux_m_set(CycloneV::LAB, pos, clk_sel[i / 2], alm, clk_choice[ce_idx]);
            if (ff->ffInfo.ctrlset.clk.inverted)
                cv->bmux_b_set(CycloneV::LAB, pos, clk_inv[ce_idx], 0, true);
            if (get_net_or_empty(ff, id_ENA) != nullptr) { // not using ffInfo.ctrlset, this has a fake net always to
                                                           // ensure different constants don't collide
                cv->bmux_b_set(CycloneV::LAB, pos, en_en[ce_idx], 0, true);
                cv->bmux_b_set(CycloneV::LAB, pos, en_ninv[ce_idx], 0, ff->ffInfo.ctrlset.ena.inverted);
            } else {
                cv->bmux_b_set(CycloneV::LAB, pos, en_en[ce_idx], 0, false);
            }
            // ACLR
            int aclr_idx = alm_data.aclr_idx[i / 2];
            cv->bmux_b_set(CycloneV::LAB, pos, clr_sel[i / 2], alm, aclr_idx == 1);
            if (ff->ffInfo.ctrlset.aclr.inverted)
                cv->bmux_b_set(CycloneV::LAB, pos, aclr_inv[aclr_idx], 0, true);
            // SCLR
            if (ff->ffInfo.ctrlset.sclr.net != nullptr) {
                cv->bmux_b_set(CycloneV::LAB, pos, CycloneV::SCLR_INV, 0, ff->ffInfo.ctrlset.sclr.inverted);
            } else {
                cv->bmux_b_set(CycloneV::LAB, pos, sclr_dis[i / 2], alm, true);
            }
            // SLOAD
            if (ff->ffInfo.ctrlset.sload.net != nullptr) {
                cv->bmux_b_set(CycloneV::LAB, pos, sload_en[i / 2], alm, true);
                cv->bmux_b_set(CycloneV::LAB, pos, CycloneV::SLOAD_INV, 0, ff->ffInfo.ctrlset.sload.inverted);
            }
        }
        return true;
    }

    void write_ff_routing(uint32_t lab)
    {
        auto &lab_data = ctx->labs.at(lab);
        auto pos = lab_data.alms.at(0).lut_bels[0].pos;

        const std::array<CycloneV::bmux_type_t, 2> aclr_inp{CycloneV::ACLR0_SEL, CycloneV::ACLR1_SEL};
        for (int i = 0; i < 2; i++) {
            // Quartus seems to set unused ACLRs to CLKI2...
            if (!lab_data.aclr_used[i])
                cv->bmux_m_set(CycloneV::LAB, pos, aclr_inp[i], 0, CycloneV::CLKI2);
            else
                cv->bmux_m_set(CycloneV::LAB, pos, aclr_inp[i], 0, (i == 1) ? CycloneV::GIN0 : CycloneV::GIN1);
        }
        for (int i = 0; i < 3; i++) {
            // Check for fabric->clock routing
            if (ctx->wires_connected(ctx->get_port(CycloneV::LAB, CycloneV::pos2x(pos), CycloneV::pos2y(pos), -1,
                                                   CycloneV::DATAIN, 0),
                                     lab_data.clk_wires[i]))
                cv->bmux_m_set(CycloneV::LAB, pos, CycloneV::CLKA_SEL, 0, CycloneV::GIN2);
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
        init();
        write_routing();
        write_dqs();
        write_cells();
        write_labs();
    }
};
} // namespace

void Arch::build_bitstream()
{
    MistralBitgen gen(getCtx());
    gen.run();
}

NEXTPNR_NAMESPACE_END

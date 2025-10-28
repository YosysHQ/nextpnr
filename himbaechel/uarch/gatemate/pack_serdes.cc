/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

struct DefaultParam
{
    IdString name;
    int width;
    int value;
};

static const DefaultParam serdes_defaults[] = {
        {id_RX_BUF_RESET_TIME, 5, 3},
        {id_RX_PCS_RESET_TIME, 5, 3},
        {id_RX_RESET_TIMER_PRESC, 5, 0},
        {id_RX_RESET_DONE_GATE, 1, 0},
        {id_RX_CDR_RESET_TIME, 5, 3},
        {id_RX_EQA_RESET_TIME, 5, 3},
        {id_RX_PMA_RESET_TIME, 5, 3},
        {id_RX_WAIT_CDR_LOCK, 1, 1},
        {id_RX_CALIB_EN, 1, 0},
        {id_RX_CALIB_DONE, 1, 1}, // read-only but set
        {id_RX_CALIB_OVR, 1, 0},
        {id_RX_CALIB_VAL, 4, 0},
        // { id_RX_CALIB_CAL, 4, 0 },
        {id_RX_RTERM_VCMSEL, 3, 4},
        {id_RX_RTERM_PD, 1, 0},
        {id_RX_EQA_CKP_LF, 8, 0xA3},
        {id_RX_EQA_CKP_HF, 8, 0xA3},
        {id_RX_EQA_CKP_OFFSET, 8, 0x01},
        {id_RX_EN_EQA, 1, 0},
        {id_RX_EQA_LOCK_CFG, 4, 0},
        // { id_RX_EQA_LOCKED, 1, 0 },
        {id_RX_TH_MON1, 5, 8},
        // { id_RX_EN_EQA_EXT_VALUE[0], 1, 0 }, // handled in code
        {id_RX_TH_MON2, 5, 8},
        // { id_RX_EN_EQA_EXT_VALUE[1], 1, 0 }, // handled in code
        {id_RX_TAPW, 5, 8},
        // { id_RX_EN_EQA_EXT_VALUE[2], 1, 0 }, // handled in code
        {id_RX_AFE_OFFSET, 5, 8},
        // { id_RX_EN_EQA_EXT_VALUE[3], 1, 0 }, // handled in code
        {id_RX_EQA_TAPW, 5, 8}, // read-only but set
        // { id_RX_TH_MON, 5, 0 },
        // { id_RX_OFFSET, 4, 0 },
        {id_RX_EQA_CONFIG, 16, 0x01C0},
        {id_RX_AFE_PEAK, 5, 16},
        {id_RX_AFE_GAIN, 4, 8},
        {id_RX_AFE_VCMSEL, 3, 4},
        {id_RX_CDR_CKP, 8, 0xF8},
        {id_RX_CDR_CKI, 8, 0},
        {id_RX_CDR_TRANS_TH, 7, 0x08},
        {id_RX_CDR_LOCK_CFG, 8, 0xD5},
        // { id_RX_CDR_LOCKED, 1, 0 },
        // { id_RX_CDR_FREQ_ACC_VAL, 15, 0 },
        // { id_RX_CDR_PHASE_ACC_VAL, 16, 0 },
        {id_RX_CDR_FREQ_ACC, 15, 0},
        {id_RX_CDR_PHASE_ACC, 16, 0},
        {id_RX_CDR_SET_ACC_CONFIG, 2, 0},
        {id_RX_CDR_FORCE_LOCK, 1, 0},
        {id_RX_ALIGN_MCOMMA_VALUE, 10, 0x283},
        {id_RX_MCOMMA_ALIGN_OVR, 1, 0},
        {id_RX_MCOMMA_ALIGN, 1, 0},
        {id_RX_ALIGN_PCOMMA_VALUE, 10, 0x17C},
        {id_RX_PCOMMA_ALIGN_OVR, 1, 0},
        {id_RX_PCOMMA_ALIGN, 1, 0},
        {id_RX_ALIGN_COMMA_WORD, 2, 0},
        {id_RX_ALIGN_COMMA_ENABLE, 10, 0x3FF},
        {id_RX_SLIDE_MODE, 2, 0},
        {id_RX_COMMA_DETECT_EN_OVR, 1, 0},
        {id_RX_COMMA_DETECT_EN, 1, 0},
        {id_RX_SLIDE, 2, 0},
        {id_RX_EYE_MEAS_EN, 1, 0},
        {id_RX_EYE_MEAS_CFG, 15, 0},
        {id_RX_MON_PH_OFFSET, 6, 0},
        // { id_RX_EYE_MEAS_CORRECT_11S, 16, 0 },
        // { id_RX_EYE_MEAS_WRONG_11S, 16, 0 },
        // { id_RX_EYE_MEAS_CORRECT_00S, 16, 0 },
        // { id_RX_EYE_MEAS_WRONG_00S, 16, 0 },
        // { id_RX_EYE_MEAS_CORRECT_001S, 16, 0 },
        // { id_RX_EYE_MEAS_WRONG_001S, 16, 0 },
        // { id_RX_EYE_MEAS_CORRECT_110S, 16, 0 },
        // { id_RX_EYE_MEAS_WRONG_110S, 16, 0 },
        {id_RX_EI_BIAS, 4, 0},
        {id_RX_EI_BW_SEL, 4, 4},
        {id_RX_EN_EI_DETECTOR_OVR, 1, 0},
        {id_RX_EN_EI_DETECTOR, 1, 0},
        // { id_RX_EI_EN, 1, 0 },
        // { id_RX_PRBS_ERR_CNT, 15, 0 },
        // { id_RX_PRBS_LOCKED, 1, 0 },
        {id_RX_DATA_SEL, 1, 0},
        // { id_RX_DATA[15:1], 15, 0 },
        // { id_RX_DATA[31:16], 16, 0 },
        // { id_RX_DATA[47:32], 16, 0 },
        // { id_RX_DATA[63:48], 16, 0 },
        // { id_RX_DATA[79:64], 16, 0 },
        {id_RX_BUF_BYPASS, 1, 0},
        {id_RX_CLKCOR_USE, 1, 0},
        {id_RX_CLKCOR_MIN_LAT, 6, 32},
        {id_RX_CLKCOR_MAX_LAT, 6, 39},
        {id_RX_CLKCOR_SEQ_1_0, 10, 0x1F7},
        {id_RX_CLKCOR_SEQ_1_1, 10, 0x1F7},
        {id_RX_CLKCOR_SEQ_1_2, 10, 0x1F7},
        {id_RX_CLKCOR_SEQ_1_3, 10, 0x1F7},
        {id_RX_PMA_LOOPBACK, 1, 0},
        {id_RX_PCS_LOOPBACK, 1, 0},
        {id_RX_DATAPATH_SEL, 2, 3},
        {id_RX_PRBS_OVR, 1, 0},
        {id_RX_PRBS_SEL, 3, 0},
        {id_RX_LOOPBACK_OVR, 1, 0},
        {id_RX_PRBS_CNT_RESET, 1, 0},
        {id_RX_POWER_DOWN_OVR, 1, 0},
        {id_RX_POWER_DOWN_N, 1, 0},
        // { id_RX_PRESENT, 1, 0 },
        // { id_RX_DETECT_DONE, 1, 0 },
        // { id_RX_BUF_ERR, 1, 0 },
        {id_RX_RESET_OVR, 1, 0},
        {id_RX_RESET, 1, 0},
        {id_RX_PMA_RESET_OVR, 1, 0},
        {id_RX_PMA_RESET, 1, 0},
        {id_RX_EQA_RESET_OVR, 1, 0},
        {id_RX_EQA_RESET, 1, 0},
        {id_RX_CDR_RESET_OVR, 1, 0},
        {id_RX_CDR_RESET, 1, 0},
        {id_RX_PCS_RESET_OVR, 1, 0},
        {id_RX_PCS_RESET, 1, 0},
        {id_RX_BUF_RESET_OVR, 1, 0},
        {id_RX_BUF_RESET, 1, 0},
        {id_RX_POLARITY_OVR, 1, 0},
        {id_RX_POLARITY, 1, 0},
        {id_RX_8B10B_EN_OVR, 1, 0},
        {id_RX_8B10B_EN, 1, 0},
        {id_RX_8B10B_BYPASS, 8, 0},
        // { id_RX_BYTE_IS_ALIGNED, 1, 0 },
        {id_RX_BYTE_REALIGN, 1, 0},
        // { id_RX_RESET_DONE, 1, 0 },
        {id_RX_DBG_EN, 1, 0},
        {id_RX_DBG_SEL, 4, 0},
        {id_RX_DBG_MODE, 1, 0},
        {id_RX_DBG_SRAM_DELAY, 6, 0x05},
        {id_RX_DBG_ADDR, 10, 0},
        {id_RX_DBG_RE, 1, 0},
        {id_RX_DBG_WE, 1, 0},
        {id_RX_DBG_DATA, 20, 0},
        {id_TX_SEL_PRE, 5, 0},
        {id_TX_SEL_POST, 5, 0},
        {id_TX_AMP, 5, 15},
        {id_TX_BRANCH_EN_PRE, 5, 0},
        {id_TX_BRANCH_EN_MAIN, 6, 0x3F},
        {id_TX_BRANCH_EN_POST, 5, 0},
        {id_TX_TAIL_CASCODE, 3, 4},
        {id_TX_DC_ENABLE, 7, 63},
        {id_TX_DC_OFFSET, 5, 0},
        {id_TX_CM_RAISE, 5, 0},
        {id_TX_CM_THRESHOLD_0, 5, 14},
        {id_TX_CM_THRESHOLD_1, 5, 16},
        {id_TX_SEL_PRE_EI, 5, 0},
        {id_TX_SEL_POST_EI, 5, 0},
        {id_TX_AMP_EI, 5, 15},
        {id_TX_BRANCH_EN_PRE_EI, 5, 0},
        {id_TX_BRANCH_EN_MAIN_EI, 6, 0x3F},
        {id_TX_BRANCH_EN_POST_EI, 5, 0},
        {id_TX_TAIL_CASCODE_EI, 3, 4},
        {id_TX_DC_ENABLE_EI, 7, 63},
        {id_TX_DC_OFFSET_EI, 5, 0},
        {id_TX_CM_RAISE_EI, 5, 0},
        {id_TX_CM_THRESHOLD_0_EI, 5, 14},
        {id_TX_CM_THRESHOLD_1_EI, 5, 16},
        {id_TX_SEL_PRE_RXDET, 5, 0},
        {id_TX_SEL_POST_RXDET, 5, 0},
        {id_TX_AMP_RXDET, 5, 15},
        {id_TX_BRANCH_EN_PRE_RXDET, 5, 0},
        {id_TX_BRANCH_EN_MAIN_RXDET, 6, 0x3F},
        {id_TX_BRANCH_EN_POST_RXDET, 5, 0},
        {id_TX_TAIL_CASCODE_RXDET, 3, 4},
        {id_TX_DC_ENABLE_RXDET, 7, 0},
        {id_TX_DC_OFFSET_RXDET, 5, 0},
        {id_TX_CM_RAISE_RXDET, 5, 0},
        {id_TX_CM_THRESHOLD_0_RXDET, 5, 14},
        {id_TX_CM_THRESHOLD_1_RXDET, 5, 16},
        {id_TX_CALIB_EN, 1, 0},
        {id_TX_CALIB_DONE, 1, 1}, // read-only but set
        {id_TX_CALIB_OVR, 1, 0},
        {id_TX_CALIB_VAL, 4, 0},
        // { id_TX_CALIB_CAL, 4, 0 },
        {id_TX_CM_REG_KI, 8, 0x80},
        {id_TX_CM_SAR_EN, 1, 0},
        {id_TX_CM_REG_EN, 1, 1},
        // { id_TX_CM_SAR_RESULT_0, 5, 0 },
        // { id_TX_CM_SAR_RESULT_1, 5, 0 },
        {id_TX_PMA_RESET_TIME, 5, 3},
        {id_TX_PCS_RESET_TIME, 5, 3},
        {id_TX_PCS_RESET_OVR, 1, 0},
        {id_TX_PCS_RESET, 1, 0},
        {id_TX_PMA_RESET_OVR, 1, 0},
        {id_TX_PMA_RESET, 1, 0},
        {id_TX_RESET_OVR, 1, 0},
        {id_TX_RESET, 1, 0},
        {id_TX_PMA_LOOPBACK, 2, 0},
        {id_TX_PCS_LOOPBACK, 1, 0},
        {id_TX_DATAPATH_SEL, 2, 3},
        {id_TX_PRBS_OVR, 1, 0},
        {id_TX_PRBS_SEL, 3, 0},
        {id_TX_PRBS_FORCE_ERR, 1, 0},
        {id_TX_LOOPBACK_OVR, 1, 0},
        {id_TX_POWER_DOWN_OVR, 1, 0},
        {id_TX_POWER_DOWN_N, 1, 0},
        {id_TX_ELEC_IDLE_OVR, 1, 0},
        {id_TX_ELEC_IDLE, 1, 0},
        {id_TX_DETECT_RX_OVR, 1, 0},
        {id_TX_DETECT_RX, 1, 0},
        {id_TX_POLARITY_OVR, 1, 0},
        {id_TX_POLARITY, 1, 0},
        {id_TX_8B10B_EN_OVR, 1, 0},
        {id_TX_8B10B_EN, 1, 0},
        {id_TX_DATA_OVR, 1, 0},
        {id_TX_DATA_CNT, 3, 0},
        {id_TX_DATA_VALID, 1, 0},
        // { id_TX_BUF_ERR, 1, 0 },
        // { id_TX_RESET_DONE, 1, 0 },
        {id_TX_DATA, 16, 0},
        {id_PLL_EN_ADPLL_CTRL, 1, 0},
        {id_PLL_CONFIG_SEL, 1, 0},
        {id_PLL_SET_OP_LOCK, 1, 0},
        {id_PLL_ENFORCE_LOCK, 1, 0},
        {id_PLL_DISABLE_LOCK, 1, 0},
        {id_PLL_LOCK_WINDOW, 1, 1},
        {id_PLL_FAST_LOCK, 1, 1},
        {id_PLL_SYNC_BYPASS, 1, 0},
        {id_PLL_PFD_SELECT, 1, 0},
        {id_PLL_REF_BYPASS, 1, 0},
        {id_PLL_REF_SEL, 1, 0},
        {id_PLL_REF_RTERM, 1, 1},
        {id_PLL_FCNTRL, 6, 58},
        {id_PLL_MAIN_DIVSEL, 6, 27},
        {id_PLL_OUT_DIVSEL, 2, 0},
        {id_PLL_CI, 5, 3},
        {id_PLL_CP, 10, 80},
        {id_PLL_AO, 4, 0},
        {id_PLL_SCAP, 3, 0},
        {id_PLL_FILTER_SHIFT, 2, 2},
        {id_PLL_SAR_LIMIT, 3, 2},
        {id_PLL_FT, 11, 512},
        {id_PLL_OPEN_LOOP, 1, 0},
        {id_PLL_SCAP_AUTO_CAL, 1, 1},
        // { id_PLL_LOCKED, 1, 0 },
        // { id_PLL_CAP_FT_OF, 1, 0 },
        // { id_PLL_CAP_FT_UF, 1, 0 },
        // { id_PLL_CAP_FT, 10, 0 },
        // { id_PLL_CAP_STATE, 2, 0 },
        // { id_PLL_SYNC_VALUE, 8, 0 },
        {id_PLL_BISC_MODE, 3, 4},
        {id_PLL_BISC_TIMER_MAX, 4, 15},
        {id_PLL_BISC_OPT_DET_IND, 1, 0},
        {id_PLL_BISC_PFD_SEL, 1, 0},
        {id_PLL_BISC_DLY_DIR, 1, 0},
        {id_PLL_BISC_COR_DLY, 3, 1},
        {id_PLL_BISC_CAL_SIGN, 1, 0},
        {id_PLL_BISC_CAL_AUTO, 1, 1},
        {id_PLL_BISC_CP_MIN, 5, 4},
        {id_PLL_BISC_CP_MAX, 5, 18},
        {id_PLL_BISC_CP_START, 5, 12},
        {id_PLL_BISC_DLY_PFD_MON_REF, 5, 0},
        {id_PLL_BISC_DLY_PFD_MON_DIV, 5, 2},
        // { id_PLL_BISC_TIMER_DONE, 1, 0 },
        // { id_PLL_BISC_CP, 7, 0 },
        // { id_PLL_BISC_CO, 16, 0 },
        {id_SERDES_ENABLE, 1, 0},
        {id_SERDES_AUTO_INIT, 1, 0},
        {id_SERDES_TESTMODE, 1, 0},
};

void GateMatePacker::pack_serdes()
{
    log_info("Packing SERDES..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_SERDES))
            continue;
        ci.type = id_SERDES;
        ci.cluster = ci.name;

        for (int i = 0; i < 64; i++) {
            move_ram_o(&ci, ctx->idf("TX_DATA_I[%d]", i));
            move_ram_i(&ci, ctx->idf("RX_DATA_O[%d]", i));
        }
        for (int i = 0; i < 16; i++) {
            move_ram_o(&ci, ctx->idf("REGFILE_DI_I[%d]", i));
            move_ram_o(&ci, ctx->idf("REGFILE_MASK_I[%d]", i));
            move_ram_i(&ci, ctx->idf("REGFILE_DO_O[%d]", i));
        }
        for (int i = 0; i < 16; i++) {
            move_ram_o(&ci, ctx->idf("TX_8B10B_BYPASS_I[%d]", i));
            move_ram_o(&ci, ctx->idf("TX_CHAR_IS_K_I[%d]", i));
            move_ram_o(&ci, ctx->idf("TX_CHAR_DISPMODE_I[%d]", i));
            move_ram_o(&ci, ctx->idf("TX_CHAR_DISPVAL_I[%d]", i));
            move_ram_o(&ci, ctx->idf("RX_8B10B_BYPASS_I[%d]", i));
            move_ram_o(&ci, ctx->idf("REGFILE_ADDR_I[%d]", i));
            move_ram_i(&ci, ctx->idf("RX_NOT_IN_TABLE_O[%d]", i));
            move_ram_i(&ci, ctx->idf("RX_CHAR_IS_COMMA_O[%d]", i));
            move_ram_i(&ci, ctx->idf("RX_CHAR_IS_K_O[%d]", i));
            move_ram_i(&ci, ctx->idf("RX_DISP_ERR_O[%d]", i));
        }
        for (int i = 0; i < 3; i++) {
            move_ram_o(&ci, ctx->idf("TX_PRBS_SEL_I[%d]", i));
            move_ram_o(&ci, ctx->idf("LOOPBACK_I[%d]", i));
            move_ram_o(&ci, ctx->idf("RX_PRBS_SEL_I[%d]", i));
        }
        move_ram_o(&ci, id_TX_RESET_I);
        move_ram_o(&ci, id_TX_PCS_RESET_I);
        move_ram_o(&ci, id_TX_PMA_RESET_I);
        move_ram_o(&ci, id_PLL_RESET_I);
        move_ram_o(&ci, id_TX_POWER_DOWN_N_I);
        move_ram_o(&ci, id_TX_POLARITY_I);
        move_ram_o(&ci, id_TX_PRBS_FORCE_ERR_I);
        move_ram_o(&ci, id_TX_8B10B_EN_I);
        move_ram_o(&ci, id_TX_ELEC_IDLE_I);
        move_ram_o(&ci, id_TX_DETECT_RX_I);
        move_ram_o(&ci, id_TX_CLK_I);
        move_ram_o(&ci, id_RX_CLK_I);
        move_ram_o(&ci, id_RX_RESET_I);
        move_ram_o(&ci, id_RX_PMA_RESET_I);
        move_ram_o(&ci, id_RX_EQA_RESET_I);
        move_ram_o(&ci, id_RX_CDR_RESET_I);
        move_ram_o(&ci, id_RX_PCS_RESET_I);
        move_ram_o(&ci, id_RX_BUF_RESET_I);
        move_ram_o(&ci, id_RX_POWER_DOWN_N_I);
        move_ram_o(&ci, id_RX_POLARITY_I);
        move_ram_o(&ci, id_RX_PRBS_CNT_RESET_I);
        move_ram_o(&ci, id_RX_8B10B_EN_I);
        move_ram_o(&ci, id_RX_EN_EI_DETECTOR_I);
        move_ram_o(&ci, id_RX_COMMA_DETECT_EN_I);
        move_ram_o(&ci, id_RX_SLIDE_I);
        move_ram_o(&ci, id_RX_MCOMMA_ALIGN_I);
        move_ram_o(&ci, id_RX_PCOMMA_ALIGN_I);
        move_ram_o(&ci, id_REGFILE_CLK_I);
        move_ram_o(&ci, id_REGFILE_WE_I);
        move_ram_o(&ci, id_REGFILE_EN_I);
        move_ram_i(&ci, id_TX_DETECT_RX_DONE_O);
        move_ram_i(&ci, id_TX_DETECT_RX_PRESENT_O);
        move_ram_i(&ci, id_TX_BUF_ERR_O);
        move_ram_i(&ci, id_TX_RESET_DONE_O);
        move_ram_i(&ci, id_RX_PRBS_ERR_O);
        move_ram_i(&ci, id_RX_BUF_ERR_O);
        move_ram_i(&ci, id_RX_BYTE_IS_ALIGNED_O);
        move_ram_i(&ci, id_RX_BYTE_REALIGN_O);
        move_ram_i(&ci, id_RX_RESET_DONE_O);
        move_ram_i(&ci, id_RX_EI_EN_O);
        move_ram_i(&ci, id_RX_CLK_O);
        move_ram_i(&ci, id_PLL_CLK_O);
        move_ram_i(&ci, id_REGFILE_RDY_O);

        for (auto cfg : serdes_defaults)
            ci.params[cfg.name] = Property(int_or_default(ci.params, cfg.name, cfg.value), cfg.width);

        uint8_t rx_en_eqa_ext_value = int_or_default(ci.params, id_RX_EN_EQA_EXT_VALUE, 0);
        for (int i = 0; i < 4; i++)
            ci.params[ctx->idf("RX_EN_EQA_EXT_VALUE_%d", i)] = Property((rx_en_eqa_ext_value >> i) & 1, 1);
        ci.unsetParam(id_RX_EN_EQA_EXT_VALUE);
    }
}

NEXTPNR_NAMESPACE_END

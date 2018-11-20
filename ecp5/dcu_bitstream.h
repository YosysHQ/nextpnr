tg.config.add_word("DCU.CH0_AUTO_CALIB_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_AUTO_CALIB_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_AUTO_FACQ_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_AUTO_FACQ_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_BAND_THRESHOLD",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_BAND_THRESHOLD"), "0"), 6));
tg.config.add_word("DCU.CH0_CALIB_CK_MODE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_CALIB_CK_MODE"), "0"), 1));
tg.config.add_word("DCU.CH0_CC_MATCH_1",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_CC_MATCH_1"), "0"), 10));
tg.config.add_word("DCU.CH0_CC_MATCH_2",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_CC_MATCH_2"), "0"), 10));
tg.config.add_word("DCU.CH0_CC_MATCH_3",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_CC_MATCH_3"), "0"), 10));
tg.config.add_word("DCU.CH0_CC_MATCH_4",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_CC_MATCH_4"), "0"), 10));
tg.config.add_word("DCU.CH0_CDR_CNT4SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_CDR_CNT4SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_CDR_CNT8SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_CDR_CNT8SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_CTC_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_CTC_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH0_DCOATDCFG", parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOATDCFG"), "0"), 2));
tg.config.add_word("DCU.CH0_DCOATDDLY", parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOATDDLY"), "0"), 2));
tg.config.add_word("DCU.CH0_DCOBYPSATD",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOBYPSATD"), "0"), 1));
tg.config.add_word("DCU.CH0_DCOCALDIV", parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOCALDIV"), "0"), 3));
tg.config.add_word("DCU.CH0_DCOCTLGI", parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOCTLGI"), "0"), 3));
tg.config.add_word("DCU.CH0_DCODISBDAVOID",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCODISBDAVOID"), "0"), 1));
tg.config.add_word("DCU.CH0_DCOFLTDAC", parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOFLTDAC"), "0"), 2));
tg.config.add_word("DCU.CH0_DCOFTNRG", parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOFTNRG"), "0"), 3));
tg.config.add_word("DCU.CH0_DCOIOSTUNE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOIOSTUNE"), "0"), 3));
tg.config.add_word("DCU.CH0_DCOITUNE", parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOITUNE"), "0"), 2));
tg.config.add_word("DCU.CH0_DCOITUNE4LSB",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOITUNE4LSB"), "0"), 3));
tg.config.add_word("DCU.CH0_DCOIUPDNX2",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOIUPDNX2"), "0"), 1));
tg.config.add_word("DCU.CH0_DCONUOFLSB",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCONUOFLSB"), "0"), 3));
tg.config.add_word("DCU.CH0_DCOSCALEI", parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOSCALEI"), "0"), 2));
tg.config.add_word("DCU.CH0_DCOSTARTVAL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOSTARTVAL"), "0"), 3));
tg.config.add_word("DCU.CH0_DCOSTEP", parse_config_str(str_or_default(ci->params, ctx->id("CH0_DCOSTEP"), "0"), 2));
tg.config.add_word("DCU.CH0_DEC_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_DEC_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH0_ENABLE_CG_ALIGN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_ENABLE_CG_ALIGN"), "0"), 1));
tg.config.add_word("DCU.CH0_ENC_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_ENC_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH0_FF_RX_F_CLK_DIS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_FF_RX_F_CLK_DIS"), "0"), 1));
tg.config.add_word("DCU.CH0_FF_RX_H_CLK_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_FF_RX_H_CLK_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_FF_TX_F_CLK_DIS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_FF_TX_F_CLK_DIS"), "0"), 1));
tg.config.add_word("DCU.CH0_FF_TX_H_CLK_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_FF_TX_H_CLK_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_GE_AN_ENABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_GE_AN_ENABLE"), "0"), 1));
tg.config.add_word("DCU.CH0_INVERT_RX", parse_config_str(str_or_default(ci->params, ctx->id("CH0_INVERT_RX"), "0"), 1));
tg.config.add_word("DCU.CH0_INVERT_TX", parse_config_str(str_or_default(ci->params, ctx->id("CH0_INVERT_TX"), "0"), 1));
tg.config.add_word("DCU.CH0_LDR_CORE2TX_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_LDR_CORE2TX_SEL"), "0"), 1));
tg.config.add_word("DCU.CH0_LDR_RX2CORE_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_LDR_RX2CORE_SEL"), "0"), 1));
tg.config.add_word("DCU.CH0_LEQ_OFFSET_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_LEQ_OFFSET_SEL"), "0"), 1));
tg.config.add_word("DCU.CH0_LEQ_OFFSET_TRIM",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_LEQ_OFFSET_TRIM"), "0"), 3));
tg.config.add_word("DCU.CH0_LSM_DISABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_LSM_DISABLE"), "0"), 1));
tg.config.add_word("DCU.CH0_MATCH_2_ENABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_MATCH_2_ENABLE"), "0"), 1));
tg.config.add_word("DCU.CH0_MATCH_4_ENABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_MATCH_4_ENABLE"), "0"), 1));
tg.config.add_word("DCU.CH0_MIN_IPG_CNT",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_MIN_IPG_CNT"), "0"), 2));
tg.config.add_word("DCU.CH0_PCIE_EI_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_PCIE_EI_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_PCIE_MODE", parse_config_str(str_or_default(ci->params, ctx->id("CH0_PCIE_MODE"), "0"), 1));
tg.config.add_word("DCU.CH0_PCS_DET_TIME_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_PCS_DET_TIME_SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_PDEN_SEL", parse_config_str(str_or_default(ci->params, ctx->id("CH0_PDEN_SEL"), "0"), 1));
tg.config.add_word("DCU.CH0_PRBS_ENABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_PRBS_ENABLE"), "0"), 1));
tg.config.add_word("DCU.CH0_PRBS_LOCK", parse_config_str(str_or_default(ci->params, ctx->id("CH0_PRBS_LOCK"), "0"), 1));
tg.config.add_word("DCU.CH0_PRBS_SELECTION",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_PRBS_SELECTION"), "0"), 1));
tg.config.add_word("DCU.CH0_RATE_MODE_RX",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RATE_MODE_RX"), "0"), 1));
tg.config.add_word("DCU.CH0_RATE_MODE_TX",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RATE_MODE_TX"), "0"), 1));
tg.config.add_word("DCU.CH0_RCV_DCC_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RCV_DCC_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_REG_BAND_OFFSET",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_REG_BAND_OFFSET"), "0"), 4));
tg.config.add_word("DCU.CH0_REG_BAND_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_REG_BAND_SEL"), "0"), 6));
tg.config.add_word("DCU.CH0_REG_IDAC_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_REG_IDAC_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_REG_IDAC_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_REG_IDAC_SEL"), "0"), 10));
tg.config.add_word("DCU.CH0_REQ_EN", parse_config_str(str_or_default(ci->params, ctx->id("CH0_REQ_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_REQ_LVL_SET",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_REQ_LVL_SET"), "0"), 2));
tg.config.add_word("DCU.CH0_RIO_MODE", parse_config_str(str_or_default(ci->params, ctx->id("CH0_RIO_MODE"), "0"), 1));
tg.config.add_word("DCU.CH0_RLOS_SEL", parse_config_str(str_or_default(ci->params, ctx->id("CH0_RLOS_SEL"), "0"), 1));
tg.config.add_word("DCU.CH0_RPWDNB", parse_config_str(str_or_default(ci->params, ctx->id("CH0_RPWDNB"), "0"), 1));
tg.config.add_word("DCU.CH0_RTERM_RX", parse_config_str(str_or_default(ci->params, ctx->id("CH0_RTERM_RX"), "0"), 5));
tg.config.add_word("DCU.CH0_RTERM_TX", parse_config_str(str_or_default(ci->params, ctx->id("CH0_RTERM_TX"), "0"), 5));
tg.config.add_word("DCU.CH0_RXIN_CM", parse_config_str(str_or_default(ci->params, ctx->id("CH0_RXIN_CM"), "0"), 2));
tg.config.add_word("DCU.CH0_RXTERM_CM", parse_config_str(str_or_default(ci->params, ctx->id("CH0_RXTERM_CM"), "0"), 2));
tg.config.add_word("DCU.CH0_RX_DCO_CK_DIV",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_DCO_CK_DIV"), "0"), 3));
tg.config.add_word("DCU.CH0_RX_DIV11_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_DIV11_SEL"), "0"), 1));
tg.config.add_word("DCU.CH0_RX_GEAR_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_GEAR_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH0_RX_GEAR_MODE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_GEAR_MODE"), "0"), 1));
tg.config.add_word("DCU.CH0_RX_LOS_CEQ",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_LOS_CEQ"), "0"), 2));
tg.config.add_word("DCU.CH0_RX_LOS_EN", parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_LOS_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_RX_LOS_HYST_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_LOS_HYST_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_RX_LOS_LVL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_LOS_LVL"), "0"), 3));
tg.config.add_word("DCU.CH0_RX_RATE_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_RATE_SEL"), "0"), 4));
tg.config.add_word("DCU.CH0_RX_SB_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_RX_SB_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH0_SB_BYPASS", parse_config_str(str_or_default(ci->params, ctx->id("CH0_SB_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH0_SEL_SD_RX_CLK",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_SEL_SD_RX_CLK"), "0"), 1));
tg.config.add_word("DCU.CH0_TDRV_DAT_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_DAT_SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_POST_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_POST_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_TDRV_PRE_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_PRE_EN"), "0"), 1));
tg.config.add_word("DCU.CH0_TDRV_SLICE0_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE0_CUR"), "0"), 3));
tg.config.add_word("DCU.CH0_TDRV_SLICE0_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE0_SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE1_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE1_CUR"), "0"), 3));
tg.config.add_word("DCU.CH0_TDRV_SLICE1_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE1_SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE2_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE2_CUR"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE2_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE2_SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE3_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE3_CUR"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE3_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE3_SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE4_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE4_CUR"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE4_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE4_SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE5_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE5_CUR"), "0"), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE5_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TDRV_SLICE5_SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_TPWDNB", parse_config_str(str_or_default(ci->params, ctx->id("CH0_TPWDNB"), "0"), 1));
tg.config.add_word("DCU.CH0_TX_CM_SEL", parse_config_str(str_or_default(ci->params, ctx->id("CH0_TX_CM_SEL"), "0"), 2));
tg.config.add_word("DCU.CH0_TX_DIV11_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TX_DIV11_SEL"), "0"), 1));
tg.config.add_word("DCU.CH0_TX_GEAR_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TX_GEAR_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH0_TX_GEAR_MODE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TX_GEAR_MODE"), "0"), 1));
tg.config.add_word("DCU.CH0_TX_POST_SIGN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TX_POST_SIGN"), "0"), 1));
tg.config.add_word("DCU.CH0_TX_PRE_SIGN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_TX_PRE_SIGN"), "0"), 1));
tg.config.add_word("DCU.CH0_UC_MODE", parse_config_str(str_or_default(ci->params, ctx->id("CH0_UC_MODE"), "0"), 1));
tg.config.add_word("DCU.CH0_UDF_COMMA_A",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_UDF_COMMA_A"), "0"), 10));
tg.config.add_word("DCU.CH0_UDF_COMMA_B",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_UDF_COMMA_B"), "0"), 10));
tg.config.add_word("DCU.CH0_UDF_COMMA_MASK",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH0_UDF_COMMA_MASK"), "0"), 10));
tg.config.add_word("DCU.CH0_WA_BYPASS", parse_config_str(str_or_default(ci->params, ctx->id("CH0_WA_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH0_WA_MODE", parse_config_str(str_or_default(ci->params, ctx->id("CH0_WA_MODE"), "0"), 1));
tg.config.add_word("DCU.CH1_AUTO_CALIB_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_AUTO_CALIB_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_AUTO_FACQ_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_AUTO_FACQ_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_BAND_THRESHOLD",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_BAND_THRESHOLD"), "0"), 6));
tg.config.add_word("DCU.CH1_CALIB_CK_MODE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_CALIB_CK_MODE"), "0"), 1));
tg.config.add_word("DCU.CH1_CC_MATCH_1",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_CC_MATCH_1"), "0"), 10));
tg.config.add_word("DCU.CH1_CC_MATCH_2",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_CC_MATCH_2"), "0"), 10));
tg.config.add_word("DCU.CH1_CC_MATCH_3",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_CC_MATCH_3"), "0"), 10));
tg.config.add_word("DCU.CH1_CC_MATCH_4",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_CC_MATCH_4"), "0"), 10));
tg.config.add_word("DCU.CH1_CDR_CNT4SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_CDR_CNT4SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_CDR_CNT8SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_CDR_CNT8SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_CTC_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_CTC_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH1_DCOATDCFG", parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOATDCFG"), "0"), 2));
tg.config.add_word("DCU.CH1_DCOATDDLY", parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOATDDLY"), "0"), 2));
tg.config.add_word("DCU.CH1_DCOBYPSATD",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOBYPSATD"), "0"), 1));
tg.config.add_word("DCU.CH1_DCOCALDIV", parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOCALDIV"), "0"), 3));
tg.config.add_word("DCU.CH1_DCOCTLGI", parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOCTLGI"), "0"), 3));
tg.config.add_word("DCU.CH1_DCODISBDAVOID",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCODISBDAVOID"), "0"), 1));
tg.config.add_word("DCU.CH1_DCOFLTDAC", parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOFLTDAC"), "0"), 2));
tg.config.add_word("DCU.CH1_DCOFTNRG", parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOFTNRG"), "0"), 3));
tg.config.add_word("DCU.CH1_DCOIOSTUNE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOIOSTUNE"), "0"), 3));
tg.config.add_word("DCU.CH1_DCOITUNE", parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOITUNE"), "0"), 2));
tg.config.add_word("DCU.CH1_DCOITUNE4LSB",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOITUNE4LSB"), "0"), 3));
tg.config.add_word("DCU.CH1_DCOIUPDNX2",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOIUPDNX2"), "0"), 1));
tg.config.add_word("DCU.CH1_DCONUOFLSB",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCONUOFLSB"), "0"), 3));
tg.config.add_word("DCU.CH1_DCOSCALEI", parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOSCALEI"), "0"), 2));
tg.config.add_word("DCU.CH1_DCOSTARTVAL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOSTARTVAL"), "0"), 3));
tg.config.add_word("DCU.CH1_DCOSTEP", parse_config_str(str_or_default(ci->params, ctx->id("CH1_DCOSTEP"), "0"), 2));
tg.config.add_word("DCU.CH1_DEC_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_DEC_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH1_ENABLE_CG_ALIGN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_ENABLE_CG_ALIGN"), "0"), 1));
tg.config.add_word("DCU.CH1_ENC_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_ENC_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH1_FF_RX_F_CLK_DIS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_FF_RX_F_CLK_DIS"), "0"), 1));
tg.config.add_word("DCU.CH1_FF_RX_H_CLK_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_FF_RX_H_CLK_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_FF_TX_F_CLK_DIS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_FF_TX_F_CLK_DIS"), "0"), 1));
tg.config.add_word("DCU.CH1_FF_TX_H_CLK_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_FF_TX_H_CLK_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_GE_AN_ENABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_GE_AN_ENABLE"), "0"), 1));
tg.config.add_word("DCU.CH1_INVERT_RX", parse_config_str(str_or_default(ci->params, ctx->id("CH1_INVERT_RX"), "0"), 1));
tg.config.add_word("DCU.CH1_INVERT_TX", parse_config_str(str_or_default(ci->params, ctx->id("CH1_INVERT_TX"), "0"), 1));
tg.config.add_word("DCU.CH1_LDR_CORE2TX_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_LDR_CORE2TX_SEL"), "0"), 1));
tg.config.add_word("DCU.CH1_LDR_RX2CORE_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_LDR_RX2CORE_SEL"), "0"), 1));
tg.config.add_word("DCU.CH1_LEQ_OFFSET_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_LEQ_OFFSET_SEL"), "0"), 1));
tg.config.add_word("DCU.CH1_LEQ_OFFSET_TRIM",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_LEQ_OFFSET_TRIM"), "0"), 3));
tg.config.add_word("DCU.CH1_LSM_DISABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_LSM_DISABLE"), "0"), 1));
tg.config.add_word("DCU.CH1_MATCH_2_ENABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_MATCH_2_ENABLE"), "0"), 1));
tg.config.add_word("DCU.CH1_MATCH_4_ENABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_MATCH_4_ENABLE"), "0"), 1));
tg.config.add_word("DCU.CH1_MIN_IPG_CNT",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_MIN_IPG_CNT"), "0"), 2));
tg.config.add_word("DCU.CH1_PCIE_EI_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_PCIE_EI_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_PCIE_MODE", parse_config_str(str_or_default(ci->params, ctx->id("CH1_PCIE_MODE"), "0"), 1));
tg.config.add_word("DCU.CH1_PCS_DET_TIME_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_PCS_DET_TIME_SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_PDEN_SEL", parse_config_str(str_or_default(ci->params, ctx->id("CH1_PDEN_SEL"), "0"), 1));
tg.config.add_word("DCU.CH1_PRBS_ENABLE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_PRBS_ENABLE"), "0"), 1));
tg.config.add_word("DCU.CH1_PRBS_LOCK", parse_config_str(str_or_default(ci->params, ctx->id("CH1_PRBS_LOCK"), "0"), 1));
tg.config.add_word("DCU.CH1_PRBS_SELECTION",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_PRBS_SELECTION"), "0"), 1));
tg.config.add_word("DCU.CH1_RATE_MODE_RX",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RATE_MODE_RX"), "0"), 1));
tg.config.add_word("DCU.CH1_RATE_MODE_TX",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RATE_MODE_TX"), "0"), 1));
tg.config.add_word("DCU.CH1_RCV_DCC_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RCV_DCC_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_REG_BAND_OFFSET",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_REG_BAND_OFFSET"), "0"), 4));
tg.config.add_word("DCU.CH1_REG_BAND_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_REG_BAND_SEL"), "0"), 6));
tg.config.add_word("DCU.CH1_REG_IDAC_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_REG_IDAC_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_REG_IDAC_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_REG_IDAC_SEL"), "0"), 10));
tg.config.add_word("DCU.CH1_REQ_EN", parse_config_str(str_or_default(ci->params, ctx->id("CH1_REQ_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_REQ_LVL_SET",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_REQ_LVL_SET"), "0"), 2));
tg.config.add_word("DCU.CH1_RIO_MODE", parse_config_str(str_or_default(ci->params, ctx->id("CH1_RIO_MODE"), "0"), 1));
tg.config.add_word("DCU.CH1_RLOS_SEL", parse_config_str(str_or_default(ci->params, ctx->id("CH1_RLOS_SEL"), "0"), 1));
tg.config.add_word("DCU.CH1_RPWDNB", parse_config_str(str_or_default(ci->params, ctx->id("CH1_RPWDNB"), "0"), 1));
tg.config.add_word("DCU.CH1_RTERM_RX", parse_config_str(str_or_default(ci->params, ctx->id("CH1_RTERM_RX"), "0"), 5));
tg.config.add_word("DCU.CH1_RTERM_TX", parse_config_str(str_or_default(ci->params, ctx->id("CH1_RTERM_TX"), "0"), 5));
tg.config.add_word("DCU.CH1_RXIN_CM", parse_config_str(str_or_default(ci->params, ctx->id("CH1_RXIN_CM"), "0"), 2));
tg.config.add_word("DCU.CH1_RXTERM_CM", parse_config_str(str_or_default(ci->params, ctx->id("CH1_RXTERM_CM"), "0"), 2));
tg.config.add_word("DCU.CH1_RX_DCO_CK_DIV",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_DCO_CK_DIV"), "0"), 3));
tg.config.add_word("DCU.CH1_RX_DIV11_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_DIV11_SEL"), "0"), 1));
tg.config.add_word("DCU.CH1_RX_GEAR_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_GEAR_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH1_RX_GEAR_MODE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_GEAR_MODE"), "0"), 1));
tg.config.add_word("DCU.CH1_RX_LOS_CEQ",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_LOS_CEQ"), "0"), 2));
tg.config.add_word("DCU.CH1_RX_LOS_EN", parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_LOS_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_RX_LOS_HYST_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_LOS_HYST_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_RX_LOS_LVL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_LOS_LVL"), "0"), 3));
tg.config.add_word("DCU.CH1_RX_RATE_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_RATE_SEL"), "0"), 4));
tg.config.add_word("DCU.CH1_RX_SB_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_RX_SB_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH1_SB_BYPASS", parse_config_str(str_or_default(ci->params, ctx->id("CH1_SB_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH1_SEL_SD_RX_CLK",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_SEL_SD_RX_CLK"), "0"), 1));
tg.config.add_word("DCU.CH1_TDRV_DAT_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_DAT_SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_POST_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_POST_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_TDRV_PRE_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_PRE_EN"), "0"), 1));
tg.config.add_word("DCU.CH1_TDRV_SLICE0_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE0_CUR"), "0"), 3));
tg.config.add_word("DCU.CH1_TDRV_SLICE0_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE0_SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE1_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE1_CUR"), "0"), 3));
tg.config.add_word("DCU.CH1_TDRV_SLICE1_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE1_SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE2_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE2_CUR"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE2_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE2_SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE3_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE3_CUR"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE3_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE3_SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE4_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE4_CUR"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE4_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE4_SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE5_CUR",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE5_CUR"), "0"), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE5_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TDRV_SLICE5_SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_TPWDNB", parse_config_str(str_or_default(ci->params, ctx->id("CH1_TPWDNB"), "0"), 1));
tg.config.add_word("DCU.CH1_TX_CM_SEL", parse_config_str(str_or_default(ci->params, ctx->id("CH1_TX_CM_SEL"), "0"), 2));
tg.config.add_word("DCU.CH1_TX_DIV11_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TX_DIV11_SEL"), "0"), 1));
tg.config.add_word("DCU.CH1_TX_GEAR_BYPASS",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TX_GEAR_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH1_TX_GEAR_MODE",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TX_GEAR_MODE"), "0"), 1));
tg.config.add_word("DCU.CH1_TX_POST_SIGN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TX_POST_SIGN"), "0"), 1));
tg.config.add_word("DCU.CH1_TX_PRE_SIGN",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_TX_PRE_SIGN"), "0"), 1));
tg.config.add_word("DCU.CH1_UC_MODE", parse_config_str(str_or_default(ci->params, ctx->id("CH1_UC_MODE"), "0"), 1));
tg.config.add_word("DCU.CH1_UDF_COMMA_A",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_UDF_COMMA_A"), "0"), 10));
tg.config.add_word("DCU.CH1_UDF_COMMA_B",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_UDF_COMMA_B"), "0"), 10));
tg.config.add_word("DCU.CH1_UDF_COMMA_MASK",
                   parse_config_str(str_or_default(ci->params, ctx->id("CH1_UDF_COMMA_MASK"), "0"), 10));
tg.config.add_word("DCU.CH1_WA_BYPASS", parse_config_str(str_or_default(ci->params, ctx->id("CH1_WA_BYPASS"), "0"), 1));
tg.config.add_word("DCU.CH1_WA_MODE", parse_config_str(str_or_default(ci->params, ctx->id("CH1_WA_MODE"), "0"), 1));
tg.config.add_word("DCU.D_BITCLK_FROM_ND_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_BITCLK_FROM_ND_EN"), "0"), 1));
tg.config.add_word("DCU.D_BITCLK_LOCAL_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_BITCLK_LOCAL_EN"), "0"), 1));
tg.config.add_word("DCU.D_BITCLK_ND_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_BITCLK_ND_EN"), "0"), 1));
tg.config.add_word("DCU.D_BUS8BIT_SEL", parse_config_str(str_or_default(ci->params, ctx->id("D_BUS8BIT_SEL"), "0"), 1));
tg.config.add_word("DCU.D_CDR_LOL_SET", parse_config_str(str_or_default(ci->params, ctx->id("D_CDR_LOL_SET"), "0"), 2));
tg.config.add_word("DCU.D_CMUSETBIASI", parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETBIASI"), "0"), 2));
tg.config.add_word("DCU.D_CMUSETI4CPP", parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETI4CPP"), "0"), 4));
tg.config.add_word("DCU.D_CMUSETI4CPZ", parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETI4CPZ"), "0"), 4));
tg.config.add_word("DCU.D_CMUSETI4VCO", parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETI4VCO"), "0"), 2));
tg.config.add_word("DCU.D_CMUSETICP4P", parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETICP4P"), "0"), 2));
tg.config.add_word("DCU.D_CMUSETICP4Z", parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETICP4Z"), "0"), 3));
tg.config.add_word("DCU.D_CMUSETINITVCT",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETINITVCT"), "0"), 2));
tg.config.add_word("DCU.D_CMUSETISCL4VCO",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETISCL4VCO"), "0"), 3));
tg.config.add_word("DCU.D_CMUSETP1GM", parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETP1GM"), "0"), 3));
tg.config.add_word("DCU.D_CMUSETP2AGM", parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETP2AGM"), "0"), 3));
tg.config.add_word("DCU.D_CMUSETZGM", parse_config_str(str_or_default(ci->params, ctx->id("D_CMUSETZGM"), "0"), 3));
tg.config.add_word("DCU.D_DCO_CALIB_TIME_SEL",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_DCO_CALIB_TIME_SEL"), "0"), 2));
tg.config.add_word("DCU.D_HIGH_MARK", parse_config_str(str_or_default(ci->params, ctx->id("D_HIGH_MARK"), "0"), 4));
tg.config.add_word("DCU.D_IB_PWDNB", parse_config_str(str_or_default(ci->params, ctx->id("D_IB_PWDNB"), "0"), 1));
tg.config.add_word("DCU.D_ISETLOS", parse_config_str(str_or_default(ci->params, ctx->id("D_ISETLOS"), "0"), 8));
tg.config.add_word("DCU.D_LOW_MARK", parse_config_str(str_or_default(ci->params, ctx->id("D_LOW_MARK"), "0"), 4));
tg.config.add_word("DCU.D_MACROPDB", parse_config_str(str_or_default(ci->params, ctx->id("D_MACROPDB"), "0"), 1));
tg.config.add_word("DCU.D_PD_ISET", parse_config_str(str_or_default(ci->params, ctx->id("D_PD_ISET"), "0"), 2));
tg.config.add_word("DCU.D_PLL_LOL_SET", parse_config_str(str_or_default(ci->params, ctx->id("D_PLL_LOL_SET"), "0"), 2));
tg.config.add_word("DCU.D_REFCK_MODE", parse_config_str(str_or_default(ci->params, ctx->id("D_REFCK_MODE"), "0"), 3));
tg.config.add_word("DCU.D_REQ_ISET", parse_config_str(str_or_default(ci->params, ctx->id("D_REQ_ISET"), "0"), 3));
tg.config.add_word("DCU.D_RG_EN", parse_config_str(str_or_default(ci->params, ctx->id("D_RG_EN"), "0"), 1));
tg.config.add_word("DCU.D_RG_SET", parse_config_str(str_or_default(ci->params, ctx->id("D_RG_SET"), "0"), 2));
tg.config.add_word("DCU.D_SETICONST_AUX",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_SETICONST_AUX"), "0"), 2));
tg.config.add_word("DCU.D_SETICONST_CH",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_SETICONST_CH"), "0"), 2));
tg.config.add_word("DCU.D_SETIRPOLY_AUX",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_SETIRPOLY_AUX"), "0"), 2));
tg.config.add_word("DCU.D_SETIRPOLY_CH",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_SETIRPOLY_CH"), "0"), 2));
tg.config.add_word("DCU.D_SETPLLRC", parse_config_str(str_or_default(ci->params, ctx->id("D_SETPLLRC"), "0"), 6));
tg.config.add_word("DCU.D_SYNC_LOCAL_EN",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_SYNC_LOCAL_EN"), "0"), 1));
tg.config.add_word("DCU.D_SYNC_ND_EN", parse_config_str(str_or_default(ci->params, ctx->id("D_SYNC_ND_EN"), "0"), 1));
tg.config.add_word("DCU.D_TXPLL_PWDNB", parse_config_str(str_or_default(ci->params, ctx->id("D_TXPLL_PWDNB"), "0"), 1));
tg.config.add_word("DCU.D_TX_VCO_CK_DIV",
                   parse_config_str(str_or_default(ci->params, ctx->id("D_TX_VCO_CK_DIV"), "0"), 3));
tg.config.add_word("DCU.D_XGE_MODE", parse_config_str(str_or_default(ci->params, ctx->id("D_XGE_MODE"), "0"), 1));

tg.config.add_word("DCU.CH0_AUTO_CALIB_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_AUTO_CALIB_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_AUTO_FACQ_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_AUTO_FACQ_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_BAND_THRESHOLD",
                   parse_config_str(get_or_default(ci->params, id_CH0_BAND_THRESHOLD, Property(0)), 6));
tg.config.add_word("DCU.CH0_CALIB_CK_MODE",
                   parse_config_str(get_or_default(ci->params, id_CH0_CALIB_CK_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH0_CC_MATCH_1",
                   parse_config_str(get_or_default(ci->params, id_CH0_CC_MATCH_1, Property(0)), 10));
tg.config.add_word("DCU.CH0_CC_MATCH_2",
                   parse_config_str(get_or_default(ci->params, id_CH0_CC_MATCH_2, Property(0)), 10));
tg.config.add_word("DCU.CH0_CC_MATCH_3",
                   parse_config_str(get_or_default(ci->params, id_CH0_CC_MATCH_3, Property(0)), 10));
tg.config.add_word("DCU.CH0_CC_MATCH_4",
                   parse_config_str(get_or_default(ci->params, id_CH0_CC_MATCH_4, Property(0)), 10));
tg.config.add_word("DCU.CH0_CDR_CNT4SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_CDR_CNT4SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_CDR_CNT8SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_CDR_CNT8SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_CTC_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH0_CTC_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH0_DCOATDCFG", parse_config_str(get_or_default(ci->params, id_CH0_DCOATDCFG, Property(0)), 2));
tg.config.add_word("DCU.CH0_DCOATDDLY", parse_config_str(get_or_default(ci->params, id_CH0_DCOATDDLY, Property(0)), 2));
tg.config.add_word("DCU.CH0_DCOBYPSATD",
                   parse_config_str(get_or_default(ci->params, id_CH0_DCOBYPSATD, Property(0)), 1));
tg.config.add_word("DCU.CH0_DCOCALDIV", parse_config_str(get_or_default(ci->params, id_CH0_DCOCALDIV, Property(0)), 3));
tg.config.add_word("DCU.CH0_DCOCTLGI", parse_config_str(get_or_default(ci->params, id_CH0_DCOCTLGI, Property(0)), 3));
tg.config.add_word("DCU.CH0_DCODISBDAVOID",
                   parse_config_str(get_or_default(ci->params, id_CH0_DCODISBDAVOID, Property(0)), 1));
tg.config.add_word("DCU.CH0_DCOFLTDAC", parse_config_str(get_or_default(ci->params, id_CH0_DCOFLTDAC, Property(0)), 2));
tg.config.add_word("DCU.CH0_DCOFTNRG", parse_config_str(get_or_default(ci->params, id_CH0_DCOFTNRG, Property(0)), 3));
tg.config.add_word("DCU.CH0_DCOIOSTUNE",
                   parse_config_str(get_or_default(ci->params, id_CH0_DCOIOSTUNE, Property(0)), 3));
tg.config.add_word("DCU.CH0_DCOITUNE", parse_config_str(get_or_default(ci->params, id_CH0_DCOITUNE, Property(0)), 2));
tg.config.add_word("DCU.CH0_DCOITUNE4LSB",
                   parse_config_str(get_or_default(ci->params, id_CH0_DCOITUNE4LSB, Property(0)), 3));
tg.config.add_word("DCU.CH0_DCOIUPDNX2",
                   parse_config_str(get_or_default(ci->params, id_CH0_DCOIUPDNX2, Property(0)), 1));
tg.config.add_word("DCU.CH0_DCONUOFLSB",
                   parse_config_str(get_or_default(ci->params, id_CH0_DCONUOFLSB, Property(0)), 3));
tg.config.add_word("DCU.CH0_DCOSCALEI", parse_config_str(get_or_default(ci->params, id_CH0_DCOSCALEI, Property(0)), 2));
tg.config.add_word("DCU.CH0_DCOSTARTVAL",
                   parse_config_str(get_or_default(ci->params, id_CH0_DCOSTARTVAL, Property(0)), 3));
tg.config.add_word("DCU.CH0_DCOSTEP", parse_config_str(get_or_default(ci->params, id_CH0_DCOSTEP, Property(0)), 2));
tg.config.add_word("DCU.CH0_DEC_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH0_DEC_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH0_ENABLE_CG_ALIGN",
                   parse_config_str(get_or_default(ci->params, id_CH0_ENABLE_CG_ALIGN, Property(0)), 1));
tg.config.add_word("DCU.CH0_ENC_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH0_ENC_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH0_FF_RX_F_CLK_DIS",
                   parse_config_str(get_or_default(ci->params, id_CH0_FF_RX_F_CLK_DIS, Property(0)), 1));
tg.config.add_word("DCU.CH0_FF_RX_H_CLK_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_FF_RX_H_CLK_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_FF_TX_F_CLK_DIS",
                   parse_config_str(get_or_default(ci->params, id_CH0_FF_TX_F_CLK_DIS, Property(0)), 1));
tg.config.add_word("DCU.CH0_FF_TX_H_CLK_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_FF_TX_H_CLK_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_GE_AN_ENABLE",
                   parse_config_str(get_or_default(ci->params, id_CH0_GE_AN_ENABLE, Property(0)), 1));
tg.config.add_word("DCU.CH0_INVERT_RX", parse_config_str(get_or_default(ci->params, id_CH0_INVERT_RX, Property(0)), 1));
tg.config.add_word("DCU.CH0_INVERT_TX", parse_config_str(get_or_default(ci->params, id_CH0_INVERT_TX, Property(0)), 1));
tg.config.add_word("DCU.CH0_LDR_CORE2TX_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_LDR_CORE2TX_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH0_LDR_RX2CORE_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_LDR_RX2CORE_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH0_LEQ_OFFSET_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_LEQ_OFFSET_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH0_LEQ_OFFSET_TRIM",
                   parse_config_str(get_or_default(ci->params, id_CH0_LEQ_OFFSET_TRIM, Property(0)), 3));
tg.config.add_word("DCU.CH0_LSM_DISABLE",
                   parse_config_str(get_or_default(ci->params, id_CH0_LSM_DISABLE, Property(0)), 1));
tg.config.add_word("DCU.CH0_MATCH_2_ENABLE",
                   parse_config_str(get_or_default(ci->params, id_CH0_MATCH_2_ENABLE, Property(0)), 1));
tg.config.add_word("DCU.CH0_MATCH_4_ENABLE",
                   parse_config_str(get_or_default(ci->params, id_CH0_MATCH_4_ENABLE, Property(0)), 1));
tg.config.add_word("DCU.CH0_MIN_IPG_CNT",
                   parse_config_str(get_or_default(ci->params, id_CH0_MIN_IPG_CNT, Property(0)), 2));
tg.config.add_word("DCU.CH0_PCIE_EI_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_PCIE_EI_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_PCIE_MODE", parse_config_str(get_or_default(ci->params, id_CH0_PCIE_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH0_PCS_DET_TIME_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_PCS_DET_TIME_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_PDEN_SEL", parse_config_str(get_or_default(ci->params, id_CH0_PDEN_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH0_PRBS_ENABLE",
                   parse_config_str(get_or_default(ci->params, id_CH0_PRBS_ENABLE, Property(0)), 1));
tg.config.add_word("DCU.CH0_PRBS_LOCK", parse_config_str(get_or_default(ci->params, id_CH0_PRBS_LOCK, Property(0)), 1));
tg.config.add_word("DCU.CH0_PRBS_SELECTION",
                   parse_config_str(get_or_default(ci->params, id_CH0_PRBS_SELECTION, Property(0)), 1));
tg.config.add_word("DCU.CH0_RATE_MODE_RX",
                   parse_config_str(get_or_default(ci->params, id_CH0_RATE_MODE_RX, Property(0)), 1));
tg.config.add_word("DCU.CH0_RATE_MODE_TX",
                   parse_config_str(get_or_default(ci->params, id_CH0_RATE_MODE_TX, Property(0)), 1));
tg.config.add_word("DCU.CH0_RCV_DCC_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_RCV_DCC_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_REG_BAND_OFFSET",
                   parse_config_str(get_or_default(ci->params, id_CH0_REG_BAND_OFFSET, Property(0)), 4));
tg.config.add_word("DCU.CH0_REG_BAND_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_REG_BAND_SEL, Property(0)), 6));
tg.config.add_word("DCU.CH0_REG_IDAC_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_REG_IDAC_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_REG_IDAC_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_REG_IDAC_SEL, Property(0)), 10));
tg.config.add_word("DCU.CH0_REQ_EN", parse_config_str(get_or_default(ci->params, id_CH0_REQ_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_REQ_LVL_SET",
                   parse_config_str(get_or_default(ci->params, id_CH0_REQ_LVL_SET, Property(0)), 2));
tg.config.add_word("DCU.CH0_RIO_MODE", parse_config_str(get_or_default(ci->params, id_CH0_RIO_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH0_RLOS_SEL", parse_config_str(get_or_default(ci->params, id_CH0_RLOS_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH0_RPWDNB", parse_config_str(get_or_default(ci->params, id_CH0_RPWDNB, Property(0)), 1));
tg.config.add_word("DCU.CH0_RTERM_RX", parse_config_str(get_or_default(ci->params, id_CH0_RTERM_RX, Property(0)), 5));
tg.config.add_word("DCU.CH0_RTERM_TX", parse_config_str(get_or_default(ci->params, id_CH0_RTERM_TX, Property(0)), 5));
tg.config.add_word("DCU.CH0_RXIN_CM", parse_config_str(get_or_default(ci->params, id_CH0_RXIN_CM, Property(0)), 2));
tg.config.add_word("DCU.CH0_RXTERM_CM", parse_config_str(get_or_default(ci->params, id_CH0_RXTERM_CM, Property(0)), 2));
tg.config.add_word("DCU.CH0_RX_DCO_CK_DIV",
                   parse_config_str(get_or_default(ci->params, id_CH0_RX_DCO_CK_DIV, Property(0)), 3));
tg.config.add_word("DCU.CH0_RX_DIV11_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_RX_DIV11_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH0_RX_GEAR_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH0_RX_GEAR_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH0_RX_GEAR_MODE",
                   parse_config_str(get_or_default(ci->params, id_CH0_RX_GEAR_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH0_RX_LOS_CEQ",
                   parse_config_str(get_or_default(ci->params, id_CH0_RX_LOS_CEQ, Property(0)), 2));
tg.config.add_word("DCU.CH0_RX_LOS_EN", parse_config_str(get_or_default(ci->params, id_CH0_RX_LOS_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_RX_LOS_HYST_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_RX_LOS_HYST_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_RX_LOS_LVL",
                   parse_config_str(get_or_default(ci->params, id_CH0_RX_LOS_LVL, Property(0)), 3));
tg.config.add_word("DCU.CH0_RX_RATE_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_RX_RATE_SEL, Property(0)), 4));
tg.config.add_word("DCU.CH0_RX_SB_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH0_RX_SB_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH0_SB_BYPASS", parse_config_str(get_or_default(ci->params, id_CH0_SB_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH0_SEL_SD_RX_CLK",
                   parse_config_str(get_or_default(ci->params, id_CH0_SEL_SD_RX_CLK, Property(0)), 1));
tg.config.add_word("DCU.CH0_TDRV_DAT_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_DAT_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_POST_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_POST_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_TDRV_PRE_EN",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_PRE_EN, Property(0)), 1));
tg.config.add_word("DCU.CH0_TDRV_SLICE0_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE0_CUR, Property(0)), 3));
tg.config.add_word("DCU.CH0_TDRV_SLICE0_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE0_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE1_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE1_CUR, Property(0)), 3));
tg.config.add_word("DCU.CH0_TDRV_SLICE1_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE1_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE2_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE2_CUR, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE2_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE2_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE3_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE3_CUR, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE3_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE3_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE4_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE4_CUR, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE4_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE4_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE5_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE5_CUR, Property(0)), 2));
tg.config.add_word("DCU.CH0_TDRV_SLICE5_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_TDRV_SLICE5_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_TPWDNB", parse_config_str(get_or_default(ci->params, id_CH0_TPWDNB, Property(0)), 1));
tg.config.add_word("DCU.CH0_TX_CM_SEL", parse_config_str(get_or_default(ci->params, id_CH0_TX_CM_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH0_TX_DIV11_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH0_TX_DIV11_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH0_TX_GEAR_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH0_TX_GEAR_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH0_TX_GEAR_MODE",
                   parse_config_str(get_or_default(ci->params, id_CH0_TX_GEAR_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH0_TX_POST_SIGN",
                   parse_config_str(get_or_default(ci->params, id_CH0_TX_POST_SIGN, Property(0)), 1));
tg.config.add_word("DCU.CH0_TX_PRE_SIGN",
                   parse_config_str(get_or_default(ci->params, id_CH0_TX_PRE_SIGN, Property(0)), 1));
tg.config.add_word("DCU.CH0_UC_MODE", parse_config_str(get_or_default(ci->params, id_CH0_UC_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH0_UDF_COMMA_A",
                   parse_config_str(get_or_default(ci->params, id_CH0_UDF_COMMA_A, Property(0)), 10));
tg.config.add_word("DCU.CH0_UDF_COMMA_B",
                   parse_config_str(get_or_default(ci->params, id_CH0_UDF_COMMA_B, Property(0)), 10));
tg.config.add_word("DCU.CH0_UDF_COMMA_MASK",
                   parse_config_str(get_or_default(ci->params, id_CH0_UDF_COMMA_MASK, Property(0)), 10));
tg.config.add_word("DCU.CH0_WA_BYPASS", parse_config_str(get_or_default(ci->params, id_CH0_WA_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH0_WA_MODE", parse_config_str(get_or_default(ci->params, id_CH0_WA_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH1_AUTO_CALIB_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_AUTO_CALIB_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_AUTO_FACQ_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_AUTO_FACQ_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_BAND_THRESHOLD",
                   parse_config_str(get_or_default(ci->params, id_CH1_BAND_THRESHOLD, Property(0)), 6));
tg.config.add_word("DCU.CH1_CALIB_CK_MODE",
                   parse_config_str(get_or_default(ci->params, id_CH1_CALIB_CK_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH1_CC_MATCH_1",
                   parse_config_str(get_or_default(ci->params, id_CH1_CC_MATCH_1, Property(0)), 10));
tg.config.add_word("DCU.CH1_CC_MATCH_2",
                   parse_config_str(get_or_default(ci->params, id_CH1_CC_MATCH_2, Property(0)), 10));
tg.config.add_word("DCU.CH1_CC_MATCH_3",
                   parse_config_str(get_or_default(ci->params, id_CH1_CC_MATCH_3, Property(0)), 10));
tg.config.add_word("DCU.CH1_CC_MATCH_4",
                   parse_config_str(get_or_default(ci->params, id_CH1_CC_MATCH_4, Property(0)), 10));
tg.config.add_word("DCU.CH1_CDR_CNT4SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_CDR_CNT4SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_CDR_CNT8SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_CDR_CNT8SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_CTC_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH1_CTC_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH1_DCOATDCFG", parse_config_str(get_or_default(ci->params, id_CH1_DCOATDCFG, Property(0)), 2));
tg.config.add_word("DCU.CH1_DCOATDDLY", parse_config_str(get_or_default(ci->params, id_CH1_DCOATDDLY, Property(0)), 2));
tg.config.add_word("DCU.CH1_DCOBYPSATD",
                   parse_config_str(get_or_default(ci->params, id_CH1_DCOBYPSATD, Property(0)), 1));
tg.config.add_word("DCU.CH1_DCOCALDIV", parse_config_str(get_or_default(ci->params, id_CH1_DCOCALDIV, Property(0)), 3));
tg.config.add_word("DCU.CH1_DCOCTLGI", parse_config_str(get_or_default(ci->params, id_CH1_DCOCTLGI, Property(0)), 3));
tg.config.add_word("DCU.CH1_DCODISBDAVOID",
                   parse_config_str(get_or_default(ci->params, id_CH1_DCODISBDAVOID, Property(0)), 1));
tg.config.add_word("DCU.CH1_DCOFLTDAC", parse_config_str(get_or_default(ci->params, id_CH1_DCOFLTDAC, Property(0)), 2));
tg.config.add_word("DCU.CH1_DCOFTNRG", parse_config_str(get_or_default(ci->params, id_CH1_DCOFTNRG, Property(0)), 3));
tg.config.add_word("DCU.CH1_DCOIOSTUNE",
                   parse_config_str(get_or_default(ci->params, id_CH1_DCOIOSTUNE, Property(0)), 3));
tg.config.add_word("DCU.CH1_DCOITUNE", parse_config_str(get_or_default(ci->params, id_CH1_DCOITUNE, Property(0)), 2));
tg.config.add_word("DCU.CH1_DCOITUNE4LSB",
                   parse_config_str(get_or_default(ci->params, id_CH1_DCOITUNE4LSB, Property(0)), 3));
tg.config.add_word("DCU.CH1_DCOIUPDNX2",
                   parse_config_str(get_or_default(ci->params, id_CH1_DCOIUPDNX2, Property(0)), 1));
tg.config.add_word("DCU.CH1_DCONUOFLSB",
                   parse_config_str(get_or_default(ci->params, id_CH1_DCONUOFLSB, Property(0)), 3));
tg.config.add_word("DCU.CH1_DCOSCALEI", parse_config_str(get_or_default(ci->params, id_CH1_DCOSCALEI, Property(0)), 2));
tg.config.add_word("DCU.CH1_DCOSTARTVAL",
                   parse_config_str(get_or_default(ci->params, id_CH1_DCOSTARTVAL, Property(0)), 3));
tg.config.add_word("DCU.CH1_DCOSTEP", parse_config_str(get_or_default(ci->params, id_CH1_DCOSTEP, Property(0)), 2));
tg.config.add_word("DCU.CH1_DEC_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH1_DEC_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH1_ENABLE_CG_ALIGN",
                   parse_config_str(get_or_default(ci->params, id_CH1_ENABLE_CG_ALIGN, Property(0)), 1));
tg.config.add_word("DCU.CH1_ENC_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH1_ENC_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH1_FF_RX_F_CLK_DIS",
                   parse_config_str(get_or_default(ci->params, id_CH1_FF_RX_F_CLK_DIS, Property(0)), 1));
tg.config.add_word("DCU.CH1_FF_RX_H_CLK_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_FF_RX_H_CLK_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_FF_TX_F_CLK_DIS",
                   parse_config_str(get_or_default(ci->params, id_CH1_FF_TX_F_CLK_DIS, Property(0)), 1));
tg.config.add_word("DCU.CH1_FF_TX_H_CLK_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_FF_TX_H_CLK_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_GE_AN_ENABLE",
                   parse_config_str(get_or_default(ci->params, id_CH1_GE_AN_ENABLE, Property(0)), 1));
tg.config.add_word("DCU.CH1_INVERT_RX", parse_config_str(get_or_default(ci->params, id_CH1_INVERT_RX, Property(0)), 1));
tg.config.add_word("DCU.CH1_INVERT_TX", parse_config_str(get_or_default(ci->params, id_CH1_INVERT_TX, Property(0)), 1));
tg.config.add_word("DCU.CH1_LDR_CORE2TX_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_LDR_CORE2TX_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH1_LDR_RX2CORE_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_LDR_RX2CORE_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH1_LEQ_OFFSET_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_LEQ_OFFSET_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH1_LEQ_OFFSET_TRIM",
                   parse_config_str(get_or_default(ci->params, id_CH1_LEQ_OFFSET_TRIM, Property(0)), 3));
tg.config.add_word("DCU.CH1_LSM_DISABLE",
                   parse_config_str(get_or_default(ci->params, id_CH1_LSM_DISABLE, Property(0)), 1));
tg.config.add_word("DCU.CH1_MATCH_2_ENABLE",
                   parse_config_str(get_or_default(ci->params, id_CH1_MATCH_2_ENABLE, Property(0)), 1));
tg.config.add_word("DCU.CH1_MATCH_4_ENABLE",
                   parse_config_str(get_or_default(ci->params, id_CH1_MATCH_4_ENABLE, Property(0)), 1));
tg.config.add_word("DCU.CH1_MIN_IPG_CNT",
                   parse_config_str(get_or_default(ci->params, id_CH1_MIN_IPG_CNT, Property(0)), 2));
tg.config.add_word("DCU.CH1_PCIE_EI_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_PCIE_EI_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_PCIE_MODE", parse_config_str(get_or_default(ci->params, id_CH1_PCIE_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH1_PCS_DET_TIME_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_PCS_DET_TIME_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_PDEN_SEL", parse_config_str(get_or_default(ci->params, id_CH1_PDEN_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH1_PRBS_ENABLE",
                   parse_config_str(get_or_default(ci->params, id_CH1_PRBS_ENABLE, Property(0)), 1));
tg.config.add_word("DCU.CH1_PRBS_LOCK", parse_config_str(get_or_default(ci->params, id_CH1_PRBS_LOCK, Property(0)), 1));
tg.config.add_word("DCU.CH1_PRBS_SELECTION",
                   parse_config_str(get_or_default(ci->params, id_CH1_PRBS_SELECTION, Property(0)), 1));
tg.config.add_word("DCU.CH1_RATE_MODE_RX",
                   parse_config_str(get_or_default(ci->params, id_CH1_RATE_MODE_RX, Property(0)), 1));
tg.config.add_word("DCU.CH1_RATE_MODE_TX",
                   parse_config_str(get_or_default(ci->params, id_CH1_RATE_MODE_TX, Property(0)), 1));
tg.config.add_word("DCU.CH1_RCV_DCC_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_RCV_DCC_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_REG_BAND_OFFSET",
                   parse_config_str(get_or_default(ci->params, id_CH1_REG_BAND_OFFSET, Property(0)), 4));
tg.config.add_word("DCU.CH1_REG_BAND_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_REG_BAND_SEL, Property(0)), 6));
tg.config.add_word("DCU.CH1_REG_IDAC_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_REG_IDAC_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_REG_IDAC_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_REG_IDAC_SEL, Property(0)), 10));
tg.config.add_word("DCU.CH1_REQ_EN", parse_config_str(get_or_default(ci->params, id_CH1_REQ_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_REQ_LVL_SET",
                   parse_config_str(get_or_default(ci->params, id_CH1_REQ_LVL_SET, Property(0)), 2));
tg.config.add_word("DCU.CH1_RIO_MODE", parse_config_str(get_or_default(ci->params, id_CH1_RIO_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH1_RLOS_SEL", parse_config_str(get_or_default(ci->params, id_CH1_RLOS_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH1_RPWDNB", parse_config_str(get_or_default(ci->params, id_CH1_RPWDNB, Property(0)), 1));
tg.config.add_word("DCU.CH1_RTERM_RX", parse_config_str(get_or_default(ci->params, id_CH1_RTERM_RX, Property(0)), 5));
tg.config.add_word("DCU.CH1_RTERM_TX", parse_config_str(get_or_default(ci->params, id_CH1_RTERM_TX, Property(0)), 5));
tg.config.add_word("DCU.CH1_RXIN_CM", parse_config_str(get_or_default(ci->params, id_CH1_RXIN_CM, Property(0)), 2));
tg.config.add_word("DCU.CH1_RXTERM_CM", parse_config_str(get_or_default(ci->params, id_CH1_RXTERM_CM, Property(0)), 2));
tg.config.add_word("DCU.CH1_RX_DCO_CK_DIV",
                   parse_config_str(get_or_default(ci->params, id_CH1_RX_DCO_CK_DIV, Property(0)), 3));
tg.config.add_word("DCU.CH1_RX_DIV11_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_RX_DIV11_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH1_RX_GEAR_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH1_RX_GEAR_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH1_RX_GEAR_MODE",
                   parse_config_str(get_or_default(ci->params, id_CH1_RX_GEAR_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH1_RX_LOS_CEQ",
                   parse_config_str(get_or_default(ci->params, id_CH1_RX_LOS_CEQ, Property(0)), 2));
tg.config.add_word("DCU.CH1_RX_LOS_EN", parse_config_str(get_or_default(ci->params, id_CH1_RX_LOS_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_RX_LOS_HYST_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_RX_LOS_HYST_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_RX_LOS_LVL",
                   parse_config_str(get_or_default(ci->params, id_CH1_RX_LOS_LVL, Property(0)), 3));
tg.config.add_word("DCU.CH1_RX_RATE_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_RX_RATE_SEL, Property(0)), 4));
tg.config.add_word("DCU.CH1_RX_SB_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH1_RX_SB_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH1_SB_BYPASS", parse_config_str(get_or_default(ci->params, id_CH1_SB_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH1_SEL_SD_RX_CLK",
                   parse_config_str(get_or_default(ci->params, id_CH1_SEL_SD_RX_CLK, Property(0)), 1));
tg.config.add_word("DCU.CH1_TDRV_DAT_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_DAT_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_POST_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_POST_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_TDRV_PRE_EN",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_PRE_EN, Property(0)), 1));
tg.config.add_word("DCU.CH1_TDRV_SLICE0_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE0_CUR, Property(0)), 3));
tg.config.add_word("DCU.CH1_TDRV_SLICE0_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE0_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE1_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE1_CUR, Property(0)), 3));
tg.config.add_word("DCU.CH1_TDRV_SLICE1_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE1_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE2_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE2_CUR, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE2_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE2_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE3_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE3_CUR, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE3_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE3_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE4_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE4_CUR, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE4_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE4_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE5_CUR",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE5_CUR, Property(0)), 2));
tg.config.add_word("DCU.CH1_TDRV_SLICE5_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_TDRV_SLICE5_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_TPWDNB", parse_config_str(get_or_default(ci->params, id_CH1_TPWDNB, Property(0)), 1));
tg.config.add_word("DCU.CH1_TX_CM_SEL", parse_config_str(get_or_default(ci->params, id_CH1_TX_CM_SEL, Property(0)), 2));
tg.config.add_word("DCU.CH1_TX_DIV11_SEL",
                   parse_config_str(get_or_default(ci->params, id_CH1_TX_DIV11_SEL, Property(0)), 1));
tg.config.add_word("DCU.CH1_TX_GEAR_BYPASS",
                   parse_config_str(get_or_default(ci->params, id_CH1_TX_GEAR_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH1_TX_GEAR_MODE",
                   parse_config_str(get_or_default(ci->params, id_CH1_TX_GEAR_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH1_TX_POST_SIGN",
                   parse_config_str(get_or_default(ci->params, id_CH1_TX_POST_SIGN, Property(0)), 1));
tg.config.add_word("DCU.CH1_TX_PRE_SIGN",
                   parse_config_str(get_or_default(ci->params, id_CH1_TX_PRE_SIGN, Property(0)), 1));
tg.config.add_word("DCU.CH1_UC_MODE", parse_config_str(get_or_default(ci->params, id_CH1_UC_MODE, Property(0)), 1));
tg.config.add_word("DCU.CH1_UDF_COMMA_A",
                   parse_config_str(get_or_default(ci->params, id_CH1_UDF_COMMA_A, Property(0)), 10));
tg.config.add_word("DCU.CH1_UDF_COMMA_B",
                   parse_config_str(get_or_default(ci->params, id_CH1_UDF_COMMA_B, Property(0)), 10));
tg.config.add_word("DCU.CH1_UDF_COMMA_MASK",
                   parse_config_str(get_or_default(ci->params, id_CH1_UDF_COMMA_MASK, Property(0)), 10));
tg.config.add_word("DCU.CH1_WA_BYPASS", parse_config_str(get_or_default(ci->params, id_CH1_WA_BYPASS, Property(0)), 1));
tg.config.add_word("DCU.CH1_WA_MODE", parse_config_str(get_or_default(ci->params, id_CH1_WA_MODE, Property(0)), 1));
tg.config.add_word("DCU.D_BITCLK_FROM_ND_EN",
                   parse_config_str(get_or_default(ci->params, id_D_BITCLK_FROM_ND_EN, Property(0)), 1));
tg.config.add_word("DCU.D_BITCLK_LOCAL_EN",
                   parse_config_str(get_or_default(ci->params, id_D_BITCLK_LOCAL_EN, Property(0)), 1));
tg.config.add_word("DCU.D_BITCLK_ND_EN",
                   parse_config_str(get_or_default(ci->params, id_D_BITCLK_ND_EN, Property(0)), 1));
tg.config.add_word("DCU.D_BUS8BIT_SEL", parse_config_str(get_or_default(ci->params, id_D_BUS8BIT_SEL, Property(0)), 1));
tg.config.add_word("DCU.D_CDR_LOL_SET", parse_config_str(get_or_default(ci->params, id_D_CDR_LOL_SET, Property(0)), 2));
tg.config.add_word("DCU.D_CMUSETBIASI", parse_config_str(get_or_default(ci->params, id_D_CMUSETBIASI, Property(0)), 2));
tg.config.add_word("DCU.D_CMUSETI4CPP", parse_config_str(get_or_default(ci->params, id_D_CMUSETI4CPP, Property(0)), 4));
tg.config.add_word("DCU.D_CMUSETI4CPZ", parse_config_str(get_or_default(ci->params, id_D_CMUSETI4CPZ, Property(0)), 4));
tg.config.add_word("DCU.D_CMUSETI4VCO", parse_config_str(get_or_default(ci->params, id_D_CMUSETI4VCO, Property(0)), 2));
tg.config.add_word("DCU.D_CMUSETICP4P", parse_config_str(get_or_default(ci->params, id_D_CMUSETICP4P, Property(0)), 2));
tg.config.add_word("DCU.D_CMUSETICP4Z", parse_config_str(get_or_default(ci->params, id_D_CMUSETICP4Z, Property(0)), 3));
tg.config.add_word("DCU.D_CMUSETINITVCT",
                   parse_config_str(get_or_default(ci->params, id_D_CMUSETINITVCT, Property(0)), 2));
tg.config.add_word("DCU.D_CMUSETISCL4VCO",
                   parse_config_str(get_or_default(ci->params, id_D_CMUSETISCL4VCO, Property(0)), 3));
tg.config.add_word("DCU.D_CMUSETP1GM", parse_config_str(get_or_default(ci->params, id_D_CMUSETP1GM, Property(0)), 3));
tg.config.add_word("DCU.D_CMUSETP2AGM", parse_config_str(get_or_default(ci->params, id_D_CMUSETP2AGM, Property(0)), 3));
tg.config.add_word("DCU.D_CMUSETZGM", parse_config_str(get_or_default(ci->params, id_D_CMUSETZGM, Property(0)), 3));
tg.config.add_word("DCU.D_DCO_CALIB_TIME_SEL",
                   parse_config_str(get_or_default(ci->params, id_D_DCO_CALIB_TIME_SEL, Property(0)), 2));
tg.config.add_word("DCU.D_HIGH_MARK", parse_config_str(get_or_default(ci->params, id_D_HIGH_MARK, Property(0)), 4));
tg.config.add_word("DCU.D_IB_PWDNB", parse_config_str(get_or_default(ci->params, id_D_IB_PWDNB, Property(0)), 1));
tg.config.add_word("DCU.D_ISETLOS", parse_config_str(get_or_default(ci->params, id_D_ISETLOS, Property(0)), 8));
tg.config.add_word("DCU.D_LOW_MARK", parse_config_str(get_or_default(ci->params, id_D_LOW_MARK, Property(0)), 4));
tg.config.add_word("DCU.D_MACROPDB", parse_config_str(get_or_default(ci->params, id_D_MACROPDB, Property(0)), 1));
tg.config.add_word("DCU.D_PD_ISET", parse_config_str(get_or_default(ci->params, id_D_PD_ISET, Property(0)), 2));
tg.config.add_word("DCU.D_PLL_LOL_SET", parse_config_str(get_or_default(ci->params, id_D_PLL_LOL_SET, Property(0)), 2));
tg.config.add_word("DCU.D_REFCK_MODE", parse_config_str(get_or_default(ci->params, id_D_REFCK_MODE, Property(0)), 3));
tg.config.add_word("DCU.D_REQ_ISET", parse_config_str(get_or_default(ci->params, id_D_REQ_ISET, Property(0)), 3));
tg.config.add_word("DCU.D_RG_EN", parse_config_str(get_or_default(ci->params, id_D_RG_EN, Property(0)), 1));
tg.config.add_word("DCU.D_RG_SET", parse_config_str(get_or_default(ci->params, id_D_RG_SET, Property(0)), 2));
tg.config.add_word("DCU.D_SETICONST_AUX",
                   parse_config_str(get_or_default(ci->params, id_D_SETICONST_AUX, Property(0)), 2));
tg.config.add_word("DCU.D_SETICONST_CH",
                   parse_config_str(get_or_default(ci->params, id_D_SETICONST_CH, Property(0)), 2));
tg.config.add_word("DCU.D_SETIRPOLY_AUX",
                   parse_config_str(get_or_default(ci->params, id_D_SETIRPOLY_AUX, Property(0)), 2));
tg.config.add_word("DCU.D_SETIRPOLY_CH",
                   parse_config_str(get_or_default(ci->params, id_D_SETIRPOLY_CH, Property(0)), 2));
tg.config.add_word("DCU.D_SETPLLRC", parse_config_str(get_or_default(ci->params, id_D_SETPLLRC, Property(0)), 6));
tg.config.add_word("DCU.D_SYNC_LOCAL_EN",
                   parse_config_str(get_or_default(ci->params, id_D_SYNC_LOCAL_EN, Property(0)), 1));
tg.config.add_word("DCU.D_SYNC_ND_EN", parse_config_str(get_or_default(ci->params, id_D_SYNC_ND_EN, Property(0)), 1));
tg.config.add_word("DCU.D_TXPLL_PWDNB", parse_config_str(get_or_default(ci->params, id_D_TXPLL_PWDNB, Property(0)), 1));
tg.config.add_word("DCU.D_TX_VCO_CK_DIV",
                   parse_config_str(get_or_default(ci->params, id_D_TX_VCO_CK_DIV, Property(0)), 3));
tg.config.add_word("DCU.D_XGE_MODE", parse_config_str(get_or_default(ci->params, id_D_XGE_MODE, Property(0)), 1));

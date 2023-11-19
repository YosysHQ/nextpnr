# Mapping rules from prjxray to nextpnr

def get_bel_z_override(bel, default_z):
    s = bel.site
    t = s.tile
    bt = bel.bel_type()
    bn = bel.name()
    if t.tile_type() == "BRAM_L" or t.tile_type() == "BRAM_R":
        is_top18 = (s.primary.site_type() == "RAMB18E1")
        if bt == "RAMBFIFO36E1_RAMBFIFO36E1":
            return 0
        elif bt == "RAMB36E1_RAMB36E1":
            return 1
        elif bt == "FIFO36E1_FIFO36E1":
            return 2
        elif bt == "RAMB18E1_RAMB18E1":
            return (5 if is_top18 else 9)
        elif bt == "FIFO18E1_FIFO18E1":
            return 10
    if s.site_type() == "SLICEL" or s.site_type() == "SLICEM":
        is_upper_site = (s.rel_xy()[0] == 1)
        subslices = "ABCD"
        postfixes = ["6LUT", "5LUT", "FF", "5FF"]
        for i, pf in enumerate(postfixes):
            if len(bn) == len(pf) + 1 and bn[1:] == pf:
                return (64 if is_upper_site else 0) | (subslices.index(bn[0]) << 4) | i
        if bn == "F7AMUX":
            return (0x47 if is_upper_site else 0x07)
        elif bn == "F7BMUX":
            return (0x67 if is_upper_site else 0x27)
        elif bn == "F8MUX":
            return (0x48 if is_upper_site else 0x08)
        elif bn == "CARRY4":
            return (0x4F if is_upper_site else 0x0F)
        # Other bels (e.g. extra xc7 routing bels) can be ignored for nextpnr porpoises
        return -1
    return default_z

def get_bel_type_override(bt):
    if bt.endswith("6LUT") or bt == "LUT_OR_MEM6" or bt == "LUT6":
        return "SLICE_LUTX"
    elif bt.endswith("5LUT") or bt == "LUT_OR_MEM5" or bt == "LUT5":
        return "SLICE_LUTX"
    elif len(bt) == 4 and bt.endswith("FF2"):
        return "SLICE_FFX"
    elif len(bt) == 3 and bt.endswith("FF"):
        return "SLICE_FFX"
    elif bt == "FF_INIT" or bt == "REG_INIT":
        return "SLICE_FFX"

    iolParts = ["COMBUF_", "IDDR_", "IPFF_", "OPFF_", "OPTFF_", "TFF_"]
    for p in iolParts:
        if bt.startswith(p):
            return "IOL_" + p.replace("_", "")
    if bt.endswith("_VREF"):
        return "IOB_VREF"
    elif bt.endswith("_DIFFINBUF"):
        return "IOB_DIFFINBUF"
    elif bt.startswith("PSS_ALTO_CORE_PAD_"):
        return "PSS_PAD"
    elif bt.startswith("LAGUNA_RX_REG") or bt.startswith("LAGUNA_TX_REG"):
        return "LAGUNA_REGX"
    elif bt.startswith("BSCAN"):
        return "BSCAN"
    elif bt == "BUFGCTRL_BUFGCTRL":
        return "BUFGCTRL"
    elif bt == "RAMB18E2_U_RAMB18E2" or bt == "RAMB18E2_L_RAMB18E2":
        return "RAMB18E2_RAMB18E2"
    else:
        return bt

def include_pip(tile_type, p):
    is_xc7_logic = (tile_type in ("CLBLL_L", "CLBLL_R", "CLBLM_L", "CLBLM_R"))
    if p.is_route_thru() and p.src_wire().name().endswith("_CE_INT"):
        return False
    if p.is_route_thru() and is_xc7_logic:
        return False
    if p.is_route_thru() and "TFB" in p.dst_wire().name():
        return False
    if p.src_wire().name().startswith("CLK_BUFG_R_FBG_OUT"):
        return False
    if "CLK_HROW_CK_INT" in p.src_wire().name():
        return False
    if tile_type.startswith("HCLK_CMT") and "FREQ_REF" in p.dst_wire().name():
        return False
    if tile_type.startswith("CMT_TOP_L_LOWER"):
        return False
    if tile_type.startswith("CLK_HROW_TOP"):
        if "CK_BUFG_CASCO" in p.dst_wire().name() and "CK_BUFG_CASCIN" in p.src_wire().name():
            return False
    if tile_type.startswith("HCLK_IOI"):
        if "RCLK_BEFORE_DIV" in p.dst_wire().name() and "IMUX" in p.src_wire().name():
            return False
    if "IOI" in tile_type:
        if "CLKB" in p.dst_wire().name() and "IMUX22" in p.src_wire().name():
            return False
        if "OCLKB" in p.dst_wire().name() and "IOI_OCLK_" in p.src_wire().name():
            return False
        if "OCLKM" in p.dst_wire().name() and "IMUX31" in p.src_wire().name():
            return False
    if "CMT_TOP_R" in tile_type:
        if "PLLOUT_CLK_FREQ_BB_REBUFOUT" in p.dst_wire().name():
            return False
        if "MMCM_CLK_FREQ_BB" in p.dst_wire().name():
            return False
    return True
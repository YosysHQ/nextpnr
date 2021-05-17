/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
 *
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

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

BelBucketId Arch::getBelBucketForCellType(IdString cell_type) const
{
    if (family == ArchFamily::VERSAL) {
        // Versal only buckets
        switch (cell_type.index) {
        case ID_IBUFDS_DIFF_OUT:
        case ID_IBUFDS_DPHY:
        case ID_OBUFDS_DPHY:
        case ID_IBUFDSE3:
        case ID_OBUFTDS_COMP:
        case ID_IOBUFDSE3:
        case ID_IBUFDS_IBUFDISABLE:
        case ID_IOBUFDS_DIFF_OUT_DCIEN:
        case ID_IOBUFDS_DCIEN:
        case ID_IBUFDS:
        case ID_IOBUFDS_COMP:
        case ID_IBUFDS_DIFF_OUT_IBUFDISABLE:
        case ID_IOBUFDS:
        case ID_IOBUFDS_DIFF_OUT:
        case ID_OBUFDS_COMP:
        case ID_IBUFDS_DIFF_OUT_INTERMDISABLE:
        case ID_IOBUFDS_DIFF_OUT_INTERMDISABLE:
        case ID_IBUFDS_INTERMDISABLE:
        case ID_IOBUFDS_INTERMDISABLE:
        case ID_OBUFTDS:
        case ID_OBUFDS:
            return id_DIFF_INOUTBUF;
        case ID_IBUF:
        case ID_IBUFE3:
        case ID_IOBUF_DCIEN:
        case ID_IOBUFE3:
        case ID_IBUF_IBUFDISABLE:
        case ID_IOBUF:
        case ID_IOBUF_INTERMDISABLE:
        case ID_IBUF_INTERMDISABLE:
        case ID_OBUF:
        case ID_OBUFT:
            return id_INOUTBUF;
        case ID_URAM288E5_BASE:
        case ID_URAM288E5:
            return id_URAM288E5;
        case ID_BUFGCE:
        case ID_MBUFGCE:
            return id_BUFGCE;
        case ID_BUFGCTRL:
        case ID_MBUFGCTRL:
            return id_BUFGCTRL;
        case ID_BUFGCE_DIV:
        case ID_MBUFGCE_DIV:
            return id_BUFGCE_DIV;
        case ID_BUFG_GT:
        case ID_MBUFG_GT:
            return id_BUFG_GT;
        case ID_BUFG_PS:
        case ID_MBUFG_PS:
            return id_BUFG_PS;
        case ID_AIE_PL_M_AXIS32:
        case ID_AIE_PL_M_AXIS64:
            return id_AIE_PL_M_AXIS;
        case ID_AIE_PL_S_AXIS32:
        case ID_AIE_PL_S_AXIS64:
            return id_AIE_PL_S_AXIS;
        case ID_AIE_NOC_M_AXIS:
            return id_AIE_NOC_M_AXI;
        case ID_AIE_NOC_S_AXIS:
            return id_AIE_NOC_S_AXI;
        case ID_OBUFDS_GTE5_ADV:
        case ID_OBUFDS_GTE5:
            return id_GTY_OBUFDS;
        }
    }

    switch (cell_type.index) {
    case ID_LUT1:
    case ID_LUT2:
    case ID_LUT3:
    case ID_LUT4:
    case ID_LUT5:
    case ID_LUT6:
    case ID_LUTCY1:
    case ID_LUTCY2:
    case ID_RAMD32:
    case ID_RAMD32M64:
    case ID_RAMD64E:
    case ID_RAMD64E5:
    case ID_RAMS32:
    case ID_RAMS64E:
    case ID_RAMS64E1:
    case ID_RAMS64E5:
    case ID_SRL16E:
    case ID_SRLC16E:
    case ID_SRLC32E:
    case ID_CFGLUT5:
        return id_LUT;
    case ID_FDRE:
    case ID_FDSE:
    case ID_FDPE:
    case ID_FDCE:
    case ID_LDPE:
    case ID_LDCE:
    case ID_AND2B1L:
    case ID_OR2L:
        return id_FF;
    case ID_IBUF_ANALOG:
    case ID_INBUF:
        return id_INBUF;
    case ID_OBUF:
    case ID_OBUFT:
    case ID_OBUFT_DCIEN:
        return id_OUTBUF;
    case ID_PULLDOWN:
    case ID_PULLUP:
    case ID_KEEPER:
        return id_PULL;
    case ID_IDDRE1:
    case ID_ISERDESE3:
        return id_ISERDES;
    case ID_IDELAYCTRL:
    case ID_BITSLICE_CONTROL:
        return id_BITSLICE_CONTROL;
    case ID_OBUFTDS_DCIEN:
    case ID_OBUFDS:
    case ID_OBUFTDS:
        return id_DIFF_OUTBUF;
    default:
        return cell_type;
    }
}

BelBucketId Arch::getBelBucketForBel(BelId bel) const
{
    auto &bel_data = chip_bel_info(chip_info, bel);
    switch (bel_data.bel_type) {
    case ID_SLICEL_5LUT:
    case ID_SLICEL_6LUT:
    case ID_SLICEM_5LUT:
    case ID_SLICEM_6LUT:
        return id_LUT;
    case ID_SLICE_FF:
        return id_FF;
    case ID_HDIOB_INBUF_M:
    case ID_HDIOB_INBUF_S:
    case ID_HPIOB_INBUF_M:
    case ID_HPIOB_INBUF_S:
    case ID_HPIOB_INBUF_SNGL:
        return id_INBUF;
    case ID_HDIOB_OUTBUF_M:
    case ID_HDIOB_OUTBUF_S:
    case ID_HPIOB_OUTBUF_M:
    case ID_HPIOB_OUTBUF_S:
    case ID_HPIOB_OUTBUF_SNGL:
        return id_OUTBUF;
    case ID_HDIOB_PULL_M:
    case ID_HDIOB_PULL_S:
    case ID_HPIOB_PULL_M:
    case ID_HPIOB_PULL_S:
    case ID_HPIOB_PULL_SNGL:
    case ID_XPIOB_PULL_M:
    case ID_XPIOB_PULL_S:
        return id_PULL;
    case ID_ISERDESE3:
    case ID_IDDR_M:
    case ID_IDDR_S:
    case ID_COMP_IDDR_M:
    case ID_COMP_IDDR_S:
        return id_ISERDES;
    case ID_BITSLICE_CONTROL_BEL:
        return id_BITSLICE_CONTROL;
    case ID_HPIOBDIFFOUTBUF_DIFFOUTBUF:
        return id_DIFF_OUTBUF;
    case ID_URAM_URAM288:
        return id_URAM288E5;
    case ID_XPIOB_IOB_M:
    case ID_XPIOB_IOB_S:
    case ID_HDIOB_IOB_M:
    case ID_HDIOB_IOB_S:
        return id_INOUTBUF;
    case ID_XPIOB_DIFFRXTX:
    case ID_HDIOB_DIFFRX:
        return id_DIFF_INOUTBUF;
    case ID_BUFCE_BUFCE:
        return id_BUFGCE;
    case ID_BUFGCTRL_BUFGCTRL:
        return id_BUFGCTRL;
    case ID_BUFGCE_DIV_BUFGCE_DIV:
        return id_BUFGCE_DIV;
    case ID_BUFG_GT_BUFG_GT:
        return id_BUFG_GT;
    case ID_BUFCE_BUFG_PS:
        return id_BUFG_PS;
    default:
        if (bel_data.flags & BelDataPOD::FLAG_PAD)
            return id_PAD;
        else if (bel_data.placements.ssize() == 1)
            return IdString(bel_data.placements[0].cell_type);
        else
            return IdString(bel_data.bel_type);
    }
}

bool Arch::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    // Special path for certain bels
    auto &bel_data = chip_bel_info(chip_info, bel);
    switch (cell_type.index) {
    case ID_LUT1:
    case ID_LUT2:
    case ID_LUT3:
    case ID_LUT4:
    case ID_LUT5:
    case ID_LUTCY1:
        return (bel_data.bel_type == ID_SLICEL_5LUT || bel_data.bel_type == ID_SLICEM_5LUT ||
                bel_data.bel_type == ID_SLICEL_6LUT || bel_data.bel_type == ID_SLICEM_6LUT);
    case ID_LUT6:
    case ID_LUTCY2:
        return (bel_data.bel_type == ID_SLICEL_6LUT || bel_data.bel_type == ID_SLICEM_6LUT);
    case ID_RAMD32:
    case ID_RAMD32M64:
    case ID_RAMS32:
    case ID_SRL16E:
    case ID_SRLC16E:
        return (bel_data.bel_type == ID_SLICEM_5LUT || bel_data.bel_type == ID_SLICEM_6LUT);
    case ID_RAMD64E:
    case ID_RAMD64E5:
    case ID_RAMS64E:
    case ID_RAMS64E1:
    case ID_RAMS64E5:
    case ID_SRLC32E:
    case ID_CFGLUT5:
        return (bel_data.bel_type == ID_SLICEM_6LUT);
    case ID_FDRE:
    case ID_FDSE:
    case ID_FDPE:
    case ID_FDCE:
    case ID_LDPE:
    case ID_LDCE:
    case ID_AND2B1L:
    case ID_OR2L:
        return (bel_data.bel_type == ID_SLICE_FF);
    case ID_PAD:
        return (bel_data.flags & BelDataPOD::FLAG_PAD);
    case ID_VCC:
    case ID_GND:
        return IdString(bel_data.bel_type) == cell_type;
    }
    // Default lookup
    for (auto &plc : bel_data.placements)
        if (IdString(plc.cell_type) == cell_type)
            return true;
    return false;
}

NEXTPNR_NAMESPACE_END

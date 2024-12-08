#ifndef GOWIN_H
#define GOWIN_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
// Return true if a cell is a LUT
inline bool type_is_lut(IdString cell_type) { return cell_type.in(id_LUT1, id_LUT2, id_LUT3, id_LUT4); }
inline bool is_lut(const CellInfo *cell) { return type_is_lut(cell->type); }
// Return true if a cell is a DFF
inline bool type_is_dff(IdString cell_type)
{
    return cell_type.in(id_DFF, id_DFFE, id_DFFN, id_DFFNE, id_DFFS, id_DFFSE, id_DFFNS, id_DFFNSE, id_DFFR, id_DFFRE,
                        id_DFFNR, id_DFFNRE, id_DFFP, id_DFFPE, id_DFFNP, id_DFFNPE, id_DFFC, id_DFFCE, id_DFFNC,
                        id_DFFNCE);
}
inline bool is_dff(const CellInfo *cell) { return type_is_dff(cell->type); }
// Return true if a cell is a ALU
inline bool type_is_alu(IdString cell_type) { return cell_type == id_ALU; }
inline bool is_alu(const CellInfo *cell) { return type_is_alu(cell->type); }

// io
inline bool type_is_io(IdString cell_type) { return cell_type.in(id_IBUF, id_OBUF, id_IOBUF, id_TBUF); }
inline bool is_io(const CellInfo *cell) { return type_is_io(cell->type); }

inline bool type_is_diffio(IdString cell_type)
{
    return cell_type.in(id_ELVDS_IOBUF, id_ELVDS_IBUF, id_ELVDS_TBUF, id_ELVDS_OBUF, id_TLVDS_IOBUF, id_TLVDS_IBUF,
                        id_TLVDS_TBUF, id_TLVDS_OBUF);
}
inline bool is_diffio(const CellInfo *cell) { return type_is_diffio(cell->type); }

// IOLOGIC input and output separately

inline bool type_is_iologico(IdString cell_type)
{
    return cell_type.in(id_ODDR, id_ODDRC, id_OSER4, id_OSER8, id_OSER10, id_OVIDEO, id_IOLOGICO_EMPTY);
}
inline bool is_iologico(const CellInfo *cell) { return type_is_iologico(cell->type); }

inline bool type_is_iologici(IdString cell_type)
{
    return cell_type.in(id_IDDR, id_IDDRC, id_IDES4, id_IDES8, id_IDES10, id_IVIDEO, id_IOLOGICI_EMPTY);
}
inline bool is_iologici(const CellInfo *cell) { return type_is_iologici(cell->type); }

// Return true if a cell is a SSRAM
inline bool type_is_ssram(IdString cell_type) { return cell_type.in(id_RAM16SDP1, id_RAM16SDP2, id_RAM16SDP4); }
inline bool is_ssram(const CellInfo *cell) { return type_is_ssram(cell->type); }

// Return true if a cell is a BSRAM
inline bool type_is_bsram(IdString cell_type)
{
    return cell_type.in(id_SP, id_SPX9, id_pROM, id_pROMX9, id_ROM, id_SDP, id_SDPB, id_SDPX9B, id_DP, id_DPB,
                        id_DPX9B);
}
inline bool is_bsram(const CellInfo *cell) { return type_is_bsram(cell->type); }

// Return true if a cell is a DSP
inline bool type_is_dsp(IdString cell_type)
{
    return cell_type.in(id_PADD9, id_PADD18, id_MULT9X9, id_MULT18X18, id_MULT36X36, id_ALU54D, id_MULTALU18X18,
                        id_MULTALU36X18, id_MULTADDALU18X18);
}
inline bool is_dsp(const CellInfo *cell) { return type_is_dsp(cell->type); }

// Return true if a cell is CLKDIV
inline bool type_is_clkdiv(IdString cell_type) { return cell_type == id_CLKDIV; }
inline bool is_clkdiv(const CellInfo *cell) { return type_is_clkdiv(cell->type); }

// Return true if a cell is CLKDIV2
inline bool type_is_clkdiv2(IdString cell_type) { return cell_type == id_CLKDIV2; }
inline bool is_clkdiv2(const CellInfo *cell) { return type_is_clkdiv2(cell->type); }

// Return true for HCLK Cells
inline bool is_hclk(const CellInfo *cell) { return type_is_clkdiv2(cell->type) || type_is_clkdiv(cell->type); }

// Return true if a cell is a UserFlash
inline bool type_is_userflash(IdString cell_type)
{
    return cell_type.in(id_FLASH96K, id_FLASH256K, id_FLASH608K, id_FLASH128K, id_FLASH64K, id_FLASH64K, id_FLASH64KZ,
                        id_FLASH96KA);
}
inline bool is_userflash(const CellInfo *cell) { return type_is_userflash(cell->type); }

// Return true if a cell is a EMCU
inline bool type_is_emcu(IdString cell_type) { return cell_type == id_EMCU; }
inline bool is_emcu(const CellInfo *cell) { return type_is_emcu(cell->type); }

// ==========================================
// extra data in the chip db
// ==========================================
NPNR_PACKED_STRUCT(struct Pad_extra_data_POD {
    int32_t pll_tile;
    int32_t pll_bel;
    int32_t pll_type;
});

NPNR_PACKED_STRUCT(struct Tile_extra_data_POD {
    int32_t class_id;
    int16_t io16_x_off;
    int16_t io16_y_off;
});

NPNR_PACKED_STRUCT(struct Bottom_io_cnd_POD {
    int32_t wire_a_net;
    int32_t wire_b_net;
});

NPNR_PACKED_STRUCT(struct Bottom_io_POD {
    // simple OBUF
    static constexpr int8_t NORMAL = 0;
    // DDR
    static constexpr int8_t DDR = 1;
    RelSlice<Bottom_io_cnd_POD> conditions;
});

NPNR_PACKED_STRUCT(struct Spine_bel_POD {
    int32_t spine;
    int32_t bel_x;
    int32_t bel_y;
    int32_t bel_z;
});

NPNR_PACKED_STRUCT(struct Wire_bel_POD {
    int32_t pip_xy;
    int32_t pip_dst;
    int32_t pip_src;
    int32_t bel_x;
    int32_t bel_y;
    int32_t bel_z;
    int32_t side;
});

NPNR_PACKED_STRUCT(struct Constraint_POD {
    int32_t net;
    int32_t row;
    int32_t col;
    int32_t bel;
    int32_t iostd;
});

NPNR_PACKED_STRUCT(struct Extra_package_data_POD { RelSlice<Constraint_POD> cst; });

NPNR_PACKED_STRUCT(struct Extra_chip_data_POD {
    int32_t chip_flags;
    Bottom_io_POD bottom_io;
    RelSlice<IdString> diff_io_types;
    RelSlice<Spine_bel_POD> dqce_bels;
    RelSlice<Spine_bel_POD> dcs_bels;
    RelSlice<Wire_bel_POD> dhcen_bels;
    // chip flags
    static constexpr int32_t HAS_SP32 = 1;
    static constexpr int32_t NEED_SP_FIX = 2;
    static constexpr int32_t NEED_BSRAM_OUTREG_FIX = 4;
    static constexpr int32_t NEED_BLKSEL_FIX = 8;
    static constexpr int32_t HAS_BANDGAP = 16;
});

} // namespace

// Bels Z ranges. It is desirable that these numbers be synchronized with the chipdb generator
namespace BelZ {
enum
{
    LUT0_Z = 0,
    LUT7_Z = 14,
    MUX20_Z = 16,
    MUX21_Z = 18,
    MUX23_Z = 22,
    MUX27_Z = 29,
    ALU0_Z = 30, // :35, 6 ALU
    RAMW_Z = 36, // RAM16SDP4

    IOBA_Z = 50,
    IOBB_Z = 51, // +IOBC...IOBL

    IOLOGICA_Z = 70,
    IDES16_Z = 74,
    OSER16_Z = 75,

    BUFG_Z = 76, // : 81 reserve just in case
    BSRAM_Z = 100,

    OSC_Z = 274,
    PLL_Z = 275,
    GSR_Z = 276,
    VCC_Z = 277,
    VSS_Z = 278,
    BANDGAP_Z = 279,

    DQCE_Z = 280,  // : 286 reserve for 6 DQCEs
    DCS_Z = 286,   // : 288 reserve for 2 DCSs
    DHCEN_Z = 288, // : 298

    USERFLASH_Z = 298,

    EMCU_Z = 300,

    // The two least significant bits encode Z for 9-bit adders and
    // multipliers, if they are equal to 0, then we get Z of their common
    // 18-bit equivalent.
    DSP_Z = 509, // DSP

    DSP_0_Z = 511, // DSP macro 0
    PADD18_0_0_Z = 512,
    PADD9_0_0_Z = 512 + 1,
    PADD9_0_1_Z = 512 + 2,
    PADD18_0_1_Z = 516,
    PADD9_0_2_Z = 516 + 1,
    PADD9_0_3_Z = 516 + 2,

    MULT18X18_0_0_Z = 520,
    MULT9X9_0_0_Z = 520 + 1,
    MULT9X9_0_1_Z = 520 + 2,
    MULT18X18_0_1_Z = 524,
    MULT9X9_0_2_Z = 524 + 1,
    MULT9X9_0_3_Z = 524 + 2,

    ALU54D_0_Z = 524 + 3,
    MULTALU18X18_0_Z = 528,
    MULTALU36X18_0_Z = 528 + 1,
    MULTADDALU18X18_0_Z = 528 + 2,

    MULT36X36_Z = 528 + 3,

    DSP_1_Z = 543, // DSP macro 1
    PADD18_1_0_Z = 544,
    PADD9_1_0_Z = 544 + 1,
    PADD9_1_1_Z = 544 + 2,
    PADD18_1_1_Z = 548,
    PADD9_1_2_Z = 548 + 1,
    PADD9_1_3_Z = 548 + 2,

    MULT18X18_1_0_Z = 552,
    MULT9X9_1_0_Z = 552 + 1,
    MULT9X9_1_1_Z = 552 + 2,
    MULT18X18_1_1_Z = 556,
    MULT9X9_1_2_Z = 556 + 1,
    MULT9X9_1_3_Z = 556 + 2,

    ALU54D_1_Z = 556 + 3,
    MULTALU18X18_1_Z = 560,
    MULTALU36X18_1_Z = 560 + 1,
    MULTADDALU18X18_1_Z = 560 + 2,

    // HCLK Bels
    CLKDIV2_0_Z = 610,
    CLKDIV2_1_Z = 611,
    CLKDIV2_2_Z = 612,
    CLKDIV2_3_Z = 613,

    CLKDIV_0_Z = 620,
    CLKDIV_1_Z = 621,
    CLKDIV_2_Z = 622,
    CLKDIV_3_Z = 623
};
}

NEXTPNR_NAMESPACE_END
#endif

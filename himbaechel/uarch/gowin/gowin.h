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

inline bool type_is_diffio(IdString cell_type)
{
    return cell_type.in(id_ELVDS_IOBUF, id_ELVDS_IBUF, id_ELVDS_TBUF, id_ELVDS_OBUF, id_TLVDS_IOBUF, id_TLVDS_IBUF,
                        id_TLVDS_TBUF, id_TLVDS_OBUF);
}
inline bool is_diffio(const CellInfo *cell) { return type_is_diffio(cell->type); }

// IOLOGIC input and output separately

inline bool type_is_iologico(IdString cell_type)
{
    return cell_type.in(id_ODDR, id_ODDRC, id_OSER4, id_OSER8, id_OSER10, id_OVIDEO);
}
inline bool is_iologico(const CellInfo *cell) { return type_is_iologico(cell->type); }

inline bool type_is_iologici(IdString cell_type)
{
    return cell_type.in(id_IDDR, id_IDDRC, id_IDES4, id_IDES8, id_IDES10, id_IVIDEO);
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

// ==========================================
// extra data in the chip db
// ==========================================
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

NPNR_PACKED_STRUCT(struct Extra_chip_data_POD {
    int32_t chip_flags;
    Bottom_io_POD bottom_io;
    RelSlice<IdString> diff_io_types;
    // chip flags
    static constexpr int32_t HAS_SP32 = 0;
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
    VSS_Z = 278
};
}

NEXTPNR_NAMESPACE_END
#endif

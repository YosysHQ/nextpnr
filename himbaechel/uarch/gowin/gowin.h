#ifndef GOWIN_H
#define GOWIN_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

namespace BelFlags {
static constexpr uint32_t FLAG_SIMPLE_IO = 0x100;
}

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

// Return true if a cell is a SSRAM
inline bool type_is_ssram(IdString cell_type) { return cell_type.in(id_RAM16SDP1, id_RAM16SDP2, id_RAM16SDP4); }
inline bool is_ssram(const CellInfo *cell) { return type_is_ssram(cell->type); }

// extra data in the chip db
NPNR_PACKED_STRUCT(struct Bottom_io_cnd_POD {
    int32_t wire_a_net;
    int32_t wire_b_net;
});

NPNR_PACKED_STRUCT(struct Bottom_io_POD {
    // simple OBUF
    static constexpr int8_t NORMAL = 0;
    RelSlice<Bottom_io_cnd_POD> conditions;
});

NPNR_PACKED_STRUCT(struct Extra_chip_data_POD { Bottom_io_POD bottom_io; });

inline bool have_bottom_io_cnds(const ChipInfoPOD *chip)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(chip->extra_data.get());
    return extra->bottom_io.conditions.size() != 0;
}

inline IdString get_bottom_io_wire_a_net(const ChipInfoPOD *chip, int8_t condition)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(chip->extra_data.get());
    return IdString(extra->bottom_io.conditions[condition].wire_a_net);
}

inline IdString get_bottom_io_wire_b_net(const ChipInfoPOD *chip, int8_t condition)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(chip->extra_data.get());
    return IdString(extra->bottom_io.conditions[condition].wire_b_net);
}

inline bool getBelSimpleIO(const ChipInfoPOD *chip, BelId bel)
{
    return chip_bel_info(chip, bel).flags & BelFlags::FLAG_SIMPLE_IO;
}
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

    PLL_Z = 275,
    GSR_Z = 276,
    VCC_Z = 277,
    VSS_Z = 278
};
}

NEXTPNR_NAMESPACE_END
#endif

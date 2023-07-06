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

// Return true if a cell is a SSRAM
inline bool type_is_ssram(IdString cell_type) { return cell_type.in(id_RAM16SDP1, id_RAM16SDP2, id_RAM16SDP4); }
inline bool is_ssram(const CellInfo *cell) { return type_is_ssram(cell->type); }
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

    VCC_Z = 277,
    VSS_Z = 278
};
}

NEXTPNR_NAMESPACE_END
#endif

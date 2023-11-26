#ifndef GOWIN_UTILS_H
#define GOWIN_UTILS_H

#include "idstringlist.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

namespace BelFlags {
static constexpr uint32_t FLAG_SIMPLE_IO = 0x100;
}

struct GowinUtils
{
    Context *ctx;

    GowinUtils() {}

    void init(Context *ctx) { this->ctx = ctx; }

    // tile
    IdString get_tile_class(int x, int y);
    Loc get_tile_io16_offs(int x, int y);

    // pin functions: GCLKT_4, SSPI_CS, READY etc
    IdStringList get_pin_funcs(BelId bel);

    // Bels and pips
    bool is_simple_io_bel(BelId bel);
    Loc get_pair_iologic_bel(Loc loc);
    BelId get_io_bel_from_iologic(BelId bel);

    bool is_diff_io_supported(IdString type);
    bool have_bottom_io_cnds(void);
    IdString get_bottom_io_wire_a_net(int8_t condition);
    IdString get_bottom_io_wire_b_net(int8_t condition);

    // wires
    inline bool is_wire_type_default(IdString wire_type) { return wire_type == IdString(); }

    // chip dependent
    bool have_SP32(void);

    // make cell but do not include it in the list of chip cells.
    std::unique_ptr<CellInfo> create_cell(IdString name, IdString type);
};

NEXTPNR_NAMESPACE_END

#endif

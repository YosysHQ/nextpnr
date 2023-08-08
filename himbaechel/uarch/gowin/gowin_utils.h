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
};

NEXTPNR_NAMESPACE_END

#endif

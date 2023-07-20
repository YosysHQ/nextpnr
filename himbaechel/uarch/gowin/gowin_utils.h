#ifndef GOWIN_UTILS_H
#define GOWIN_UTILS_H

#include "idstringlist.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

struct GowinUtils
{
    Context *ctx;

    GowinUtils() {}

    void init(Context *ctx) { this->ctx = ctx; }

    // pin functions: GCLKT_4, SSPI_CS, READY etc
    IdStringList get_pin_funcs(BelId bel);
};

NEXTPNR_NAMESPACE_END

#endif

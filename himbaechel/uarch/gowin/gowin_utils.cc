#include "gowin_utils.h"

#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// pin functions: GCLKT_4, SSPI_CS, READY etc
IdStringList GowinUtils::get_pin_funcs(BelId bel)
{
    IdStringList bel_name = ctx->getBelName(bel);

    const PadInfoPOD *pins = ctx->package_info->pads.get();
    size_t len = ctx->package_info->pads.ssize();
    for (size_t i = 0; i < len; i++) {
        const PadInfoPOD *pin = &pins[i];
        if (IdString(pin->tile) == bel_name[0] && IdString(pin->bel) == bel_name[1]) {
            return IdStringList::parse(ctx, IdString(pin->pad_function).str(ctx));
        }
    }
    return IdStringList();
}

NEXTPNR_NAMESPACE_END

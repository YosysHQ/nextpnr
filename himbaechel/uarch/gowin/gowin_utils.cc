#include "log.h"
#include "nextpnr.h"
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_helpers.h"

#include "gowin.h"
#include "gowin_utils.h"

NEXTPNR_NAMESPACE_BEGIN

// tile extra data
IdString GowinUtils::get_tile_class(int x, int y)
{
    int tile = tile_by_xy(ctx->chip_info, x, y);
    const Tile_extra_data_POD *extra =
            reinterpret_cast<const Tile_extra_data_POD *>(chip_tile_info(ctx->chip_info, tile).extra_data.get());
    return IdString(extra->class_id);
}

// oser16/ides16 aux cell offsets
Loc GowinUtils::get_tile_io16_offs(int x, int y)
{
    int tile = tile_by_xy(ctx->chip_info, x, y);
    const Tile_extra_data_POD *extra =
            reinterpret_cast<const Tile_extra_data_POD *>(chip_tile_info(ctx->chip_info, tile).extra_data.get());
    return Loc(extra->io16_x_off, extra->io16_y_off, 0);
}

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

bool GowinUtils::is_simple_io_bel(BelId bel)
{
    return chip_bel_info(ctx->chip_info, bel).flags & BelFlags::FLAG_SIMPLE_IO;
}

Loc GowinUtils::get_pair_iologic_bel(Loc loc)
{
    int const z[] = {1, 0, 3, 2};
    loc.z = BelZ::IOLOGICA_Z + z[(loc.z - BelZ::IOLOGICA_Z)];
    return loc;
}

BelId GowinUtils::get_io_bel_from_iologic(BelId bel)
{
    Loc loc = ctx->getBelLocation(bel);
    loc.z = BelZ::IOBA_Z + ((loc.z - BelZ::IOLOGICA_Z) & 1);
    return ctx->getBelByLocation(loc);
}

bool GowinUtils::is_diff_io_supported(IdString type)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    for (auto &dtype : extra->diff_io_types) {
        if (IdString(dtype) == type) {
            return true;
        }
    }
    return false;
}

bool GowinUtils::have_bottom_io_cnds(void)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->bottom_io.conditions.size() != 0;
}

IdString GowinUtils::get_bottom_io_wire_a_net(int8_t condition)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return IdString(extra->bottom_io.conditions[condition].wire_a_net);
}

IdString GowinUtils::get_bottom_io_wire_b_net(int8_t condition)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return IdString(extra->bottom_io.conditions[condition].wire_b_net);
}

bool GowinUtils::have_SP32(void)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->chip_flags & Extra_chip_data_POD::HAS_SP32;
}

std::unique_ptr<CellInfo> GowinUtils::create_cell(IdString name, IdString type)
{
    NPNR_ASSERT(!ctx->cells.count(name));
    return std::make_unique<CellInfo>(ctx, name, type);
}

NEXTPNR_NAMESPACE_END

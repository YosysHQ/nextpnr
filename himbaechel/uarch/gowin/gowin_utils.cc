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
IdStringList GowinUtils::get_pin_funcs(BelId io_bel)
{
    IdStringList bel_name = ctx->getBelName(io_bel);

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

// PLL pads
BelId GowinUtils::get_pll_bel(BelId io_bel, IdString type)
{
    IdStringList bel_name = ctx->getBelName(io_bel);

    const PadInfoPOD *pins = ctx->package_info->pads.get();
    size_t len = ctx->package_info->pads.ssize();
    for (size_t i = 0; i < len; i++) {
        const PadInfoPOD *pin = &pins[i];
        if (IdString(pin->tile) == bel_name[0] && IdString(pin->bel) == bel_name[1]) {
            const Pad_extra_data_POD *extra = reinterpret_cast<const Pad_extra_data_POD *>(pin->extra_data.get());
            if (extra != nullptr && IdString(extra->pll_type) == type) {
                return ctx->getBelByName(IdStringList::concat(IdString(extra->pll_tile), IdString(extra->pll_bel)));
            }
        }
    }
    return BelId();
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

// DSP
Loc GowinUtils::get_dsp_next_9_in_chain(Loc from) const
{
    Loc res;
    res.y = from.y;
    if (get_dsp_18_idx(from.z) == 0) {
        res.x = from.x;
        res.z = from.z + 4;
        return res;
    }
    if (get_dsp_macro_num(from.z)) {
        // next DSP
        res.x = from.x + 9;
        res.z = from.z & (~0x24);
    } else {
        // next macro
        res.x = from.x;
        res.z = get_dsp_next_macro(from.z) & (~4);
    }
    return res;
}

Loc GowinUtils::get_dsp_next_macro_in_chain(Loc from) const
{
    Loc res;
    res.y = from.y;
    if (get_dsp_macro_num(from.z)) {
        // next DSP
        res.x = from.x + 9;
        res.z = from.z & (~0x20);
    } else {
        // next macro
        res.x = from.x;
        res.z = get_dsp_next_macro(from.z);
    }
    return res;
}

Loc GowinUtils::get_dsp_next_in_chain(Loc from, IdString dsp_type) const
{
    if (dsp_type.in(id_PADD9, id_PADD18, id_MULT9X9, id_MULT18X18)) {
        return get_dsp_next_9_in_chain(from);
    }
    if (dsp_type.in(id_ALU54D, id_MULTALU18X18, id_MULTALU36X18, id_MULTADDALU18X18)) {
        return get_dsp_next_macro_in_chain(from);
    }
    NPNR_ASSERT_FALSE("Unknown DSP cell type.");
}

CellInfo *GowinUtils::dsp_bus_src(const CellInfo *ci, const char *bus_prefix, int wire_num) const
{
    bool connected_to_const = false; // and disconnected too
    CellInfo *connected_to_cell = nullptr;

    for (int i = 0; i < wire_num; ++i) {
        const NetInfo *net = ci->getPort(ctx->idf("%s[%d]", bus_prefix, i));
        if (connected_to_cell == nullptr) {
            if (net == nullptr || net->driver.cell == nullptr || net->name == ctx->id("$PACKER_VCC") ||
                net->name == ctx->id("$PACKER_GND")) {
                connected_to_const = true;
                continue;
            } else {
                if (connected_to_const) {
                    log_error("The %s cell %s bus is connected simultaneously to constants and to another DSP.\n",
                              ctx->nameOf(ci), bus_prefix);
                }
            }
        }
        if (net == nullptr || !is_dsp(net->driver.cell)) {
            log_error("The %s cell %s bus is not connected to another DSP.\n", ctx->nameOf(ci), bus_prefix);
        }
        if (connected_to_cell != nullptr && net->driver.cell != connected_to_cell) {
            log_error("The %s cell %s bus is connected to different DSPs: %s and %s.\n", ctx->nameOf(ci), bus_prefix,
                      ctx->nameOf(connected_to_cell), ctx->nameOf(net->driver.cell));
        }
        connected_to_cell = net->driver.cell;
    }
    if (connected_to_const) {
        return nullptr;
    }
    return connected_to_cell;
}

CellInfo *GowinUtils::dsp_bus_dst(const CellInfo *ci, const char *bus_prefix, int wire_num) const
{
    bool disconnected = false; // and disconnected too
    CellInfo *connected_to_cell = nullptr;

    for (int i = 0; i < wire_num; ++i) {
        const NetInfo *net = ci->getPort(ctx->idf("%s[%d]", bus_prefix, i));
        if (connected_to_cell == nullptr) {
            if (net == nullptr || net->users.entries() == 0) {
                disconnected = true;
                continue;
            } else {
                if (disconnected) {
                    log_error("The %s cell %s bus is partially disconnected.\n", ctx->nameOf(ci), bus_prefix);
                }
            }
        }
        if (net->users.entries() > 1) {
            log_error("Net %s has >1 users.\n", ctx->nameOf(net));
        }

        CellInfo *dst = (*net->users.begin()).cell;
        if (net == nullptr || !is_dsp(dst)) {
            log_error("The %s cell %s bus is not connected to another DSP.\n", ctx->nameOf(ci), bus_prefix);
        }
        if (connected_to_cell != nullptr && dst != connected_to_cell) {
            log_error("The %s cell %s bus is connected to different DSPs: %s and %s.\n", ctx->nameOf(ci), bus_prefix,
                      ctx->nameOf(connected_to_cell), ctx->nameOf(dst));
        }
        connected_to_cell = dst;
    }
    if (disconnected) {
        return nullptr;
    }
    return connected_to_cell;
}

NEXTPNR_NAMESPACE_END

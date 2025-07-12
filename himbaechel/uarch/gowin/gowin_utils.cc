#include "log.h"
#include "nextpnr.h"
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_helpers.h"

#include <queue>
#include "gowin.h"
#include "gowin_utils.h"

NEXTPNR_NAMESPACE_BEGIN

// clock sources
bool GowinUtils::driver_is_clksrc(const PortRef &driver)
{
    // dedicated pins
    if (CellTypePort(driver) == CellTypePort(id_IBUF, id_O)) {

        NPNR_ASSERT(driver.cell->bel != BelId());
        IdStringList pin_func = get_pin_funcs(driver.cell->bel);
        for (size_t i = 0; i < pin_func.size(); ++i) {
            if (ctx->debug) {
                log_info("bel:%s, pin func: %zu:%s\n", ctx->nameOfBel(driver.cell->bel), i,
                         pin_func[i].str(ctx).c_str());
            }
            if (pin_func[i].str(ctx).rfind("GCLKT", 0) == 0) {
                if (ctx->debug) {
                    log_info("Clock pin:%s:%s\n", ctx->getBelName(driver.cell->bel).str(ctx).c_str(),
                             pin_func[i].c_str(ctx));
                }
                return true;
            }
        }
    }
    // PLL outputs
    if (driver.cell->type.in(id_rPLL, id_PLLVR)) {
        if (driver.port.in(id_CLKOUT, id_CLKOUTD, id_CLKOUTD3, id_CLKOUTP)) {
            if (ctx->debug) {
                if (driver.cell->bel != BelId()) {
                    log_info("PLL out bel:%s:%s\n", ctx->nameOfBel(driver.cell->bel), driver.port.c_str(ctx));
                } else {
                    log_info("PLL out:%s:%s\n", ctx->nameOf(driver.cell), driver.port.c_str(ctx));
                }
            }
            return true;
        }
    }
    // HCLK outputs
    if (driver.cell->type.in(id_CLKDIV, id_CLKDIV2)) {
        if (driver.port.in(id_CLKOUT)) {
            if (ctx->debug) {
                if (driver.cell->bel != BelId()) {
                    log_info("%s out bel:%s:%s:%s\n", driver.cell->type.c_str(ctx),
                             ctx->getBelName(driver.cell->bel).str(ctx).c_str(), driver.port.c_str(ctx),
                             ctx->nameOfWire(ctx->getBelPinWire(driver.cell->bel, driver.port)));
                } else {
                    log_info("%s out:%s:%s\n", driver.cell->type.c_str(ctx), ctx->nameOf(driver.cell),
                             driver.port.c_str(ctx));
                }
            }
            return true;
        }
    }
    // DLLDLY outputs
    if (driver.cell->type == id_DLLDLY) {
        if (driver.port.in(id_CLKOUT)) {
            if (ctx->debug) {
                if (driver.cell->bel != BelId()) {
                    log_info("%s out bel:%s:%s\n", driver.cell->type.c_str(ctx),
                             ctx->getBelName(driver.cell->bel).str(ctx).c_str(), driver.port.c_str(ctx));
                } else {
                    log_info("%s out:%s:%s\n", driver.cell->type.c_str(ctx), ctx->nameOf(driver.cell),
                             driver.port.c_str(ctx));
                }
            }
            return true;
        }
    }
    return false;
}

// Segments
int GowinUtils::get_segments_count(void) const
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->segments.ssize();
}

void GowinUtils::get_segment_region(int s_i, int &seg_idx, int &x, int &min_x, int &min_y, int &max_x, int &max_y) const
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    seg_idx = extra->segments[s_i].seg_idx;
    x = extra->segments[s_i].x;
    min_x = extra->segments[s_i].min_x;
    min_y = extra->segments[s_i].min_y;
    max_x = extra->segments[s_i].max_x;
    max_y = extra->segments[s_i].max_y;
}

void GowinUtils::get_segment_wires_loc(int s_i, Loc &top, Loc &bottom) const
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    top.x = bottom.x = extra->segments[s_i].x;
    top.y = extra->segments[s_i].top_row;
    bottom.y = extra->segments[s_i].bottom_row;
}

void GowinUtils::get_segment_wires(int s_i, WireId &top, WireId &bottom) const
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    Loc top_loc, bottom_loc;
    get_segment_wires_loc(s_i, top_loc, bottom_loc);

    IdString tile = ctx->idf("X%dY%d", top_loc.x, top_loc.y);
    IdStringList name = IdStringList::concat(tile, IdString(extra->segments[s_i].top_wire));
    top = ctx->getWireByName(name);
    tile = ctx->idf("X%dY%d", bottom_loc.x, bottom_loc.y);
    name = IdStringList::concat(tile, IdString(extra->segments[s_i].bottom_wire));
    bottom = ctx->getWireByName(name);
}

void GowinUtils::get_segment_top_gate_wires(int s_i, WireId &wire0, WireId &wire1) const
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    Loc top_loc, bottom_loc;
    get_segment_wires_loc(s_i, top_loc, bottom_loc);

    IdString tile = ctx->idf("X%dY%d", top_loc.x, top_loc.y);
    IdStringList name;
    wire0 = WireId();
    IdString wire_name = IdString(extra->segments[s_i].top_gate_wire[0]);
    if (wire_name != IdString()) {
        name = IdStringList::concat(tile, wire_name);
        wire0 = ctx->getWireByName(name);
    }
    wire1 = WireId();
    wire_name = IdString(extra->segments[s_i].top_gate_wire[1]);
    if (wire_name != IdString()) {
        name = IdStringList::concat(tile, wire_name);
        wire1 = ctx->getWireByName(name);
    }
}

void GowinUtils::get_segment_bottom_gate_wires(int s_i, WireId &wire0, WireId &wire1) const
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    Loc top_loc, bottom_loc;
    get_segment_wires_loc(s_i, top_loc, bottom_loc);

    IdString tile = ctx->idf("X%dY%d", bottom_loc.x, bottom_loc.y);
    IdStringList name;
    wire0 = WireId();
    IdString wire_name = IdString(extra->segments[s_i].bottom_gate_wire[0]);
    if (wire_name != IdString()) {
        name = IdStringList::concat(tile, wire_name);
        wire0 = ctx->getWireByName(name);
    }
    wire1 = WireId();
    wire_name = IdString(extra->segments[s_i].bottom_gate_wire[1]);
    if (wire_name != IdString()) {
        name = IdStringList::concat(tile, wire_name);
        wire1 = ctx->getWireByName(name);
    }
}

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

// oser16/ides16 aux cell offsets
bool GowinUtils::get_i3c_capable(int x, int y)
{
    int tile = tile_by_xy(ctx->chip_info, x, y);
    const Tile_extra_data_POD *extra =
            reinterpret_cast<const Tile_extra_data_POD *>(chip_tile_info(ctx->chip_info, tile).extra_data.get());
    return extra->tile_flags & Tile_extra_data_POD::TILE_I3C_CAPABLE_IO;
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

BelId GowinUtils::get_dqce_bel(IdString spine_name)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    for (auto &spine_bel : extra->dqce_bels) {
        if (IdString(spine_bel.spine) == spine_name) {
            return ctx->getBelByLocation(Loc(spine_bel.bel_x, spine_bel.bel_y, spine_bel.bel_z));
        }
    }
    return BelId();
}

BelId GowinUtils::get_dcs_bel(IdString spine_name)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    for (auto &spine_bel : extra->dcs_bels) {
        if (IdString(spine_bel.spine) == spine_name) {
            return ctx->getBelByLocation(Loc(spine_bel.bel_x, spine_bel.bel_y, spine_bel.bel_z));
        }
    }
    return BelId();
}

BelId GowinUtils::get_dlldly_bel(BelId io_bel)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    for (auto &io : extra->io_dlldly_bels) {
        if (IdStringList::parse(ctx, (IdString(io.io)).str(ctx)) == ctx->getBelName(io_bel)) {
            return ctx->getBelByName(IdStringList::parse(ctx, (IdString(io.dlldly)).str(ctx)));
        }
    }
    return BelId();
}

BelId GowinUtils::get_dhcen_bel(WireId hclkin_wire, IdString &side)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    for (auto &wire_bel : extra->dhcen_bels) {
        IdString dst = IdString(wire_bel.pip_dst);
        IdString src = IdString(wire_bel.pip_src);
        IdStringList pip = IdStringList::concat(IdStringList::concat(IdString(wire_bel.pip_xy), dst), src);
        WireId wire = ctx->getPipDstWire(ctx->getPipByName(pip));
        if (wire == hclkin_wire) {
            side = IdString(wire_bel.side);
            return ctx->getBelByLocation(Loc(wire_bel.bel_x, wire_bel.bel_y, wire_bel.bel_z));
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

bool GowinUtils::has_bottom_io_cnds(void)
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

bool GowinUtils::has_BANDGAP(void)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->chip_flags & Extra_chip_data_POD::HAS_BANDGAP;
}

bool GowinUtils::has_SP32(void)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->chip_flags & Extra_chip_data_POD::HAS_SP32;
}

bool GowinUtils::need_SP_fix(void)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->chip_flags & Extra_chip_data_POD::NEED_SP_FIX;
}

bool GowinUtils::need_BSRAM_OUTREG_fix(void)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->chip_flags & Extra_chip_data_POD::NEED_BSRAM_OUTREG_FIX;
}

bool GowinUtils::need_BLKSEL_fix(void)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->chip_flags & Extra_chip_data_POD::NEED_BLKSEL_FIX;
}

bool GowinUtils::has_PLL_HCLK(void)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->chip_flags & Extra_chip_data_POD::HAS_PLL_HCLK;
}

bool GowinUtils::has_CLKDIV_HCLK(void)
{
    const Extra_chip_data_POD *extra = reinterpret_cast<const Extra_chip_data_POD *>(ctx->chip_info->extra_data.get());
    return extra->chip_flags & Extra_chip_data_POD::HAS_CLKDIV_HCLK;
}

IdString GowinUtils::create_aux_name(IdString main_name, int idx, const char *str_suffix)
{
    return idx ? ctx->idf("%s%s%d", main_name.c_str(ctx), str_suffix, idx)
               : ctx->idf("%s%s", main_name.c_str(ctx), str_suffix);
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

// Use the upper CLKDIV as the id for a hclk section
IdStringList GowinUtils::get_hclk_id(BelId hclk_bel) const
{
    IdString bel_type = ctx->getBelType(hclk_bel);
    NPNR_ASSERT(hclk_bel != BelId() && bel_type.in(id_CLKDIV2, id_CLKDIV));
    Loc id_loc = Loc(ctx->getBelLocation(hclk_bel));
    if (bel_type == id_CLKDIV) {
        return get_hclk_id(get_clkdiv2_for_clkdiv(hclk_bel));
    } else if (id_loc.z == BelZ::CLKDIV2_0_Z || id_loc.z == BelZ::CLKDIV2_2_Z)
        return ctx->getBelName(hclk_bel);
    else
        return ctx->getBelName(ctx->getBelByLocation(Loc(id_loc.x, id_loc.y, id_loc.z - 1)));
    return IdStringList();
}

// Get the clkdiv in the same section as a clkdiv2
BelId GowinUtils::get_clkdiv_for_clkdiv2(BelId clkdiv2_bel) const
{
    NPNR_ASSERT(clkdiv2_bel != BelId() && (ctx->getBelType(clkdiv2_bel) == id_CLKDIV2));
    Loc clkdiv_loc = ctx->getBelLocation(clkdiv2_bel);
    clkdiv_loc.z = BelZ::CLKDIV_0_Z + (clkdiv_loc.z - BelZ::CLKDIV2_0_Z);
    return ctx->getBelByLocation(clkdiv_loc);
}

// Get the clkdiv2 in the same section as a clkdiv
BelId GowinUtils::get_clkdiv2_for_clkdiv(BelId clkdiv_bel) const
{
    NPNR_ASSERT(clkdiv_bel != BelId() && (ctx->getBelType(clkdiv_bel) == id_CLKDIV));
    Loc clkdiv_loc = ctx->getBelLocation(clkdiv_bel);
    Loc clkdiv2_loc = Loc(clkdiv_loc.x, clkdiv_loc.y, BelZ::CLKDIV2_0_Z + (clkdiv_loc.z - BelZ::CLKDIV_0_Z));
    return ctx->getBelByLocation(clkdiv2_loc);
}

// Get the clkdiv in the neighbouring section to a clkdiv
BelId GowinUtils::get_other_hclk_clkdiv(BelId clkdiv_bel) const
{
    NPNR_ASSERT(clkdiv_bel != BelId() && (ctx->getBelType(clkdiv_bel) == id_CLKDIV));
    Loc other_loc = Loc(ctx->getBelLocation(clkdiv_bel));
    int dz = BelZ::CLKDIV_1_Z - BelZ::CLKDIV_0_Z;
    if (other_loc.z == BelZ::CLKDIV_1_Z || other_loc.z == BelZ::CLKDIV_3_Z)
        dz = -dz;
    other_loc.z += dz;
    return ctx->getBelByLocation(other_loc);
}

// Get the clkdiv2 in the neighbouring section to a clkdiv2
BelId GowinUtils::get_other_hclk_clkdiv2(BelId clkdiv2_bel) const
{
    NPNR_ASSERT(clkdiv2_bel != BelId() && (ctx->getBelType(clkdiv2_bel) == id_CLKDIV2));
    Loc other_loc = Loc(ctx->getBelLocation(clkdiv2_bel));
    int dz = BelZ::CLKDIV2_1_Z - BelZ::CLKDIV2_0_Z;
    if (other_loc.z == BelZ::CLKDIV2_1_Z || other_loc.z == BelZ::CLKDIV2_3_Z)
        dz = -dz;
    other_loc.z += dz;
    return ctx->getBelByLocation(other_loc);
}

// Credit: https://cp-algorithms.com/graph/kuhn_maximum_bipartite_matching.html
std::vector<int> GowinUtils::kuhn_find_maximum_bipartite_matching(int n, int k, std::vector<std::vector<int>> &g)
{
    std::vector<int> mt;
    std::vector<bool> used(n);

    auto try_kuhn = [&](int v, auto &try_kuhn_ref) -> bool {
        if (used[v])
            return false;
        used[v] = true;
        for (int to : g[v]) {
            if (mt[to] == -1 || try_kuhn_ref(mt[to], try_kuhn_ref)) {
                mt[to] = v;
                return true;
            }
        }
        return false;
    };

    mt.assign(k, -1);
    for (int v = 0; v < n; ++v) {
        used.assign(n, false);
        try_kuhn(v, try_kuhn);
    }

    return mt;
}

// original implementation: nextpnr/machxo2/pack.cc
// Using a BFS, search for bels of a given type either upstream or downstream of another cell
void GowinUtils::find_connected_bels(const CellInfo *cell, IdString port, IdString dest_type, IdString dest_pin,
                                     int iter_limit, std::vector<BelId> &candidates)
{
    int iter = 0;
    std::queue<WireId> visit;
    pool<WireId> seen_wires;
    pool<BelId> seen_bels;

    BelId bel = cell->bel;
    if (bel == BelId())
        return;
    WireId start_wire = ctx->getBelPinWire(bel, port);
    NPNR_ASSERT(start_wire != WireId());
    PortType dir = ctx->getBelPinType(bel, port);

    visit.push(start_wire);

    while (!visit.empty() && (iter++ < iter_limit)) {
        WireId cursor = visit.front();
        visit.pop();
        // Check to see if we have reached a valid bel pin
        for (auto bp : ctx->getWireBelPins(cursor)) {
            if (ctx->getBelType(bp.bel) != dest_type)
                continue;
            if (dest_pin != IdString() && bp.pin != dest_pin)
                continue;
            if (seen_bels.count(bp.bel))
                continue;
            seen_bels.insert(bp.bel);
            candidates.push_back(bp.bel);
        }
        // Search in the appropriate direction up/downstream of the cursor
        if (dir == PORT_OUT) {
            for (PipId p : ctx->getPipsDownhill(cursor))
                if (ctx->checkPipAvail(p)) {
                    WireId dst = ctx->getPipDstWire(p);
                    if (seen_wires.count(dst))
                        continue;
                    seen_wires.insert(dst);
                    visit.push(dst);
                }
        } else {
            for (PipId p : ctx->getPipsUphill(cursor))
                if (ctx->checkPipAvail(p)) {
                    WireId src = ctx->getPipSrcWire(p);
                    if (seen_wires.count(src))
                        continue;
                    seen_wires.insert(src);
                    visit.push(src);
                }
        }
    }
}

NEXTPNR_NAMESPACE_END

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

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
 *
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <boost/algorithm/string.hpp>

#include "embed.h"
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "timing.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
static std::tuple<int, int, std::string> split_identifier_name(const std::string &name)
{
    size_t first_slash = name.find('/');
    NPNR_ASSERT(first_slash != std::string::npos);
    size_t second_slash = name.find('/', first_slash + 1);
    NPNR_ASSERT(second_slash != std::string::npos);
    return std::make_tuple(std::stoi(name.substr(1, first_slash)),
                           std::stoi(name.substr(first_slash + 2, second_slash - first_slash)),
                           name.substr(second_slash + 1));
};

} // namespace

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);

#include "constids.inc"

#undef X
}

// -----------------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
    // Parse device string
    if (boost::starts_with(args.device, "LIFCL")) {
        family = "LIFCL";
    } else {
        log_error("Unknown device string '%s' (expected device name like 'LIFCL-40-8SG72C')\n", args.device.c_str());
    }
    auto last_sep = args.device.rfind('-');
    if (last_sep == std::string::npos)
        log_error("Unknown device string '%s' (expected device name like 'LIFCL-40-8SG72C')\n", args.device.c_str());
    device = args.device.substr(0, last_sep);
    speed = args.device.substr(last_sep + 1, 1);
    auto package_end = args.device.find_last_of("0123456789");
    if (package_end == std::string::npos || package_end < last_sep)
        log_error("Unknown device string '%s' (expected device name like 'LIFCL-40-8SG72C')\n", args.device.c_str());
    package = args.device.substr(last_sep + 2, (package_end - (last_sep + 2)) + 1);
    rating = args.device.substr(package_end + 1);

    // Check for 'ES' part
    if (rating.size() > 1 && rating.substr(1) == "ES") {
        variant = "ES";
    } else {
        variant = "";
    }

    // Load database
    std::string chipdb = stringf("nexus/chipdb-%s.bin", family.c_str());
    auto db_ptr = reinterpret_cast<const RelPtr<DatabasePOD> *>(get_chipdb(chipdb));
    if (db_ptr == nullptr)
        log_error("Failed to load chipdb '%s'\n", chipdb.c_str());
    db = db_ptr->get();
    // Check database version and family
    if (db->version != bba_version) {
        log_error("Provided database version %d is %s than nextpnr version %d, please rebuild database/nextpnr.\n",
                  int(db->version), (db->version > bba_version) ? "newer" : "older", int(bba_version));
    }
    if (db->family.get() != family) {
        log_error("Database is for family '%s' but provided device is family '%s'.\n", db->family.get(),
                  family.c_str());
    }
    // Set up chip_info
    chip_info = nullptr;
    for (size_t i = 0; i < db->num_chips; i++) {
        auto &chip = db->chips[i];
        if (chip.device_name.get() == device) {
            chip_info = &chip;
            break;
        }
    }
    if (!chip_info)
        log_error("Unknown device '%s'.\n", device.c_str());
    // Set up bba IdStrings
    for (size_t i = 0; i < db->ids->num_bba_ids; i++) {
        IdString::initialize_add(this, db->ids->bba_id_strs[i].get(), uint32_t(i) + db->ids->num_file_ids);
    }
    // Set up validity structures
    tileStatus.resize(chip_info->num_tiles);
    for (size_t i = 0; i < chip_info->num_tiles; i++) {
        tileStatus[i].boundcells.resize(db->loctypes[chip_info->grid[i].loc_type].num_bels);
    }
    init_cell_pin_data();
    // Validate and set up package
    package_idx = -1;
    for (size_t i = 0; i < chip_info->num_packages; i++) {
        if (package == chip_info->packages[i].short_name.get()) {
            package_idx = i;
            break;
        }
    }
    if (package_idx == -1) {
        std::string all_packages = "";
        for (size_t i = 0; i < chip_info->num_packages; i++) {
            all_packages += " ";
            all_packages += chip_info->packages[i].short_name.get();
        }
        log_error("Unknown package '%s'. Available package options:%s\n", package.c_str(), all_packages.c_str());
    }
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const { return args.device; }
IdString Arch::archArgsToId(ArchArgs args) const { return id(args.device); }

// -----------------------------------------------------------------------

BelId Arch::getBelByName(IdString name) const
{
    int x, y;
    std::string belname;
    std::tie(x, y, belname) = split_identifier_name(name.str(this));
    NPNR_ASSERT(x >= 0 && x < chip_info->width);
    NPNR_ASSERT(y >= 0 && y < chip_info->height);
    auto &tile = db->loctypes[chip_info->grid[y * chip_info->width + x].loc_type];
    IdString bn = id(belname);
    for (size_t i = 0; i < tile.num_bels; i++) {
        if (tile.bels[i].name == bn.index) {
            BelId ret;
            ret.tile = y * chip_info->width + x;
            ret.index = i;
            return ret;
        }
    }
    return BelId();
}

BelRange Arch::getBelsByTile(int x, int y) const
{
    BelRange br;
    NPNR_ASSERT(x >= 0 && x < chip_info->width);
    NPNR_ASSERT(y >= 0 && y < chip_info->height);
    br.b.cursor_tile = y * chip_info->width + x;
    br.e.cursor_tile = y * chip_info->width + x;
    br.b.cursor_index = 0;
    br.e.cursor_index = db->loctypes[chip_info->grid[br.b.cursor_tile].loc_type].num_bels;
    br.b.chip = chip_info;
    br.b.db = db;
    br.e.chip = chip_info;
    br.e.db = db;
    if (br.e.cursor_index == -1)
        ++br.e.cursor_index;
    else
        ++br.e;
    return br;
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    // Binary search on wire IdString, by ID
    int num_bel_wires = bel_data(bel).num_ports;
    const BelWirePOD *bel_ports = bel_data(bel).ports.get();

    if (num_bel_wires < 7) {
        for (int i = 0; i < num_bel_wires; i++) {
            if (int(bel_ports[i].port) == pin.index) {
                return canonical_wire(bel.tile, bel_ports[i].wire_index);
            }
        }
    } else {
        int b = 0, e = num_bel_wires - 1;
        while (b <= e) {
            int i = (b + e) / 2;
            if (int(bel_ports[i].port) == pin.index) {
                return canonical_wire(bel.tile, bel_ports[i].wire_index);
            }
            if (int(bel_ports[i].port) > pin.index)
                e = i - 1;
            else
                b = i + 1;
        }
    }

    return WireId();
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    // Binary search on wire IdString, by ID
    int num_bel_wires = bel_data(bel).num_ports;
    const BelWirePOD *bel_ports = bel_data(bel).ports.get();

    if (num_bel_wires < 7) {
        for (int i = 0; i < num_bel_wires; i++) {
            if (int(bel_ports[i].port) == pin.index) {
                return PortType(bel_ports[i].type);
            }
        }
    } else {
        int b = 0, e = num_bel_wires - 1;
        while (b <= e) {
            int i = (b + e) / 2;
            if (int(bel_ports[i].port) == pin.index) {
                return PortType(bel_ports[i].type);
            }
            if (int(bel_ports[i].port) > pin.index)
                e = i - 1;
            else
                b = i + 1;
        }
    }

    NPNR_ASSERT_FALSE("unknown bel pin");
}

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    int num_bel_wires = bel_data(bel).num_ports;
    const BelWirePOD *bel_ports = bel_data(bel).ports.get();
    for (int i = 0; i < num_bel_wires; i++)
        ret.push_back(IdString(bel_ports[i].port));
    return ret;
}

std::vector<std::pair<IdString, std::string>> Arch::getBelAttrs(BelId bel) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    ret.emplace_back(id("INDEX"), stringf("%d", bel.index));

    ret.emplace_back(id("GRID_X"), stringf("%d", bel.tile % chip_info->width));
    ret.emplace_back(id("GRID_Y"), stringf("%d", bel.tile / chip_info->width));
    ret.emplace_back(id("BEL_Z"), stringf("%d", bel_data(bel).z));

    ret.emplace_back(id("BEL_TYPE"), nameOf(getBelType(bel)));

    return ret;
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdString name) const
{
    int x, y;
    std::string wirename;
    std::tie(x, y, wirename) = split_identifier_name(name.str(this));
    NPNR_ASSERT(x >= 0 && x < chip_info->width);
    NPNR_ASSERT(y >= 0 && y < chip_info->height);
    auto &tile = db->loctypes[chip_info->grid[y * chip_info->width + x].loc_type];
    IdString wn = id(wirename);
    for (size_t i = 0; i < tile.num_wires; i++) {
        if (tile.wires[i].name == wn.index) {
            WireId ret;
            ret.tile = y * chip_info->width + x;
            ret.index = i;
            return ret;
        }
    }
    return WireId();
}

IdString Arch::getWireType(WireId wire) const { return id("WIRE"); }

std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    ret.emplace_back(id("INDEX"), stringf("%d", wire.index));

    ret.emplace_back(id("GRID_X"), stringf("%d", wire.tile % chip_info->width));
    ret.emplace_back(id("GRID_Y"), stringf("%d", wire.tile / chip_info->width));
    ret.emplace_back(id("FLAGS"), stringf("%u", wire_data(wire).flags));

    return ret;
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    int x, y;
    std::string pipname;
    std::tie(x, y, pipname) = split_identifier_name(name.str(this));
    NPNR_ASSERT(x >= 0 && x < chip_info->width);
    NPNR_ASSERT(y >= 0 && y < chip_info->height);
    PipId ret;
    ret.tile = y * chip_info->width + x;
    auto sep_pos = pipname.find(':');
    ret.index = std::stoi(pipname.substr(0, sep_pos));
    return ret;
}

IdString Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());
    return id(stringf("X%d/Y%d/%d:%s->%s", pip.tile % chip_info->width, pip.tile / chip_info->width, pip.index,
                      nameOf(loc_data(pip).wires[pip_data(pip).from_wire].name),
                      nameOf(loc_data(pip).wires[pip_data(pip).to_wire].name)));
}

IdString Arch::getPipType(PipId pip) const { return IdString(); }

std::vector<std::pair<IdString, std::string>> Arch::getPipAttrs(PipId pip) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    ret.emplace_back(id("INDEX"), stringf("%d", pip.index));

    ret.emplace_back(id("GRID_X"), stringf("%d", pip.tile % chip_info->width));
    ret.emplace_back(id("GRID_Y"), stringf("%d", pip.tile / chip_info->width));

    ret.emplace_back(id("FROM_TILE_WIRE"), nameOf(loc_data(pip).wires[pip_data(pip).from_wire].name));
    ret.emplace_back(id("TO_TILE_WIRE"), nameOf(loc_data(pip).wires[pip_data(pip).to_wire].name));

    return ret;
}

// -----------------------------------------------------------------------

namespace {
const float bel_ofs_x = 0.7, bel_ofs_y = 0.0375;
const float bel_sp_x = 0.1, bel_sp_y = 0.1;
const float bel_width = 0.075, bel_height = 0.075;
} // namespace

std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const
{
    std::vector<GraphicElement> ret;

    switch (decal.type) {
    case DecalId::TYPE_BEL: {
        auto style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
        if (decal.index != -1) {
            int slice = (decal.index >> 3) & 0x3;
            int bel = decal.index & 0x7;
            float x1, x2, y1, y2;
            if (bel == BEL_RAMW) {
                x1 = bel_ofs_x;
                y1 = bel_ofs_y + 2 * bel_sp_y * slice;
                x2 = x1 + bel_sp_x + bel_width;
                y2 = y1 + bel_height;
            } else {
                x1 = bel_ofs_x + bel_sp_x * (bel >> 1);
                y1 = bel_ofs_y + 2 * bel_sp_y * slice + bel_sp_y * (bel & 0x1);
                if (slice >= 2)
                    y1 += bel_sp_y * 1.5;
                x2 = x1 + bel_width;
                y2 = y1 + bel_height;
            }
            ret.emplace_back(GraphicElement::TYPE_BOX, style, x1, y1, x2, y2, 1);
        }
        break;
    };
    default:
        break;
    }

    return ret;
}

DecalXY Arch::getBelDecal(BelId bel) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_BEL;
    if (tile_is(bel, LOC_LOGIC))
        decalxy.decal.index = bel_data(bel).z;
    else
        decalxy.decal.index = -1;
    decalxy.decal.active = (getBoundBelCell(bel) != nullptr);
    decalxy.x = bel.tile % chip_info->width;
    decalxy.y = bel.tile / chip_info->width;
    return decalxy;
}

DecalXY Arch::getWireDecal(WireId wire) const { return {}; }

DecalXY Arch::getPipDecal(PipId pip) const { return {}; };

DecalXY Arch::getGroupDecal(GroupId pip) const { return {}; };

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    return false;
}

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    return TMG_IGNORE;
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const { return {}; }

// -----------------------------------------------------------------------

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    int src_x = src.tile % chip_info->width, src_y = src.tile / chip_info->width;
    int dst_x = dst.tile % chip_info->width, dst_y = dst.tile / chip_info->width;
    int dist_x = std::abs(src_x - dst_x);
    int dist_y = std::abs(src_y - dst_y);
    return 100 * dist_x + 100 * dist_y;
}
delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    if (net_info->driver.cell == nullptr || net_info->driver.cell->bel == BelId() || sink.cell->bel == BelId())
        return 0;
    int src_x = net_info->driver.cell->bel.tile % chip_info->width,
        src_y = net_info->driver.cell->bel.tile / chip_info->width;

    int dst_x = sink.cell->bel.tile % chip_info->width, dst_y = sink.cell->bel.tile / chip_info->width;
    int dist_x = std::abs(src_x - dst_x);
    int dist_y = std::abs(src_y - dst_y);
    return 100 * dist_x + 100 * dist_y;
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

ArcBounds Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    ArcBounds bb;

    int src_x = src.tile % chip_info->width, src_y = src.tile / chip_info->width;
    int dst_x = dst.tile % chip_info->width, dst_y = dst.tile / chip_info->width;

    bb.x0 = src_x;
    bb.y0 = src_y;
    bb.x1 = src_x;
    bb.y1 = src_y;

    auto extend = [&](int x, int y) {
        bb.x0 = std::min(bb.x0, x);
        bb.x1 = std::max(bb.x1, x);
        bb.y0 = std::min(bb.y0, y);
        bb.y1 = std::max(bb.y1, y);
    };
    extend(dst_x, dst_y);

    return bb;
}

// -----------------------------------------------------------------------

bool Arch::place()
{
    std::string placer = str_or_default(settings, id("placer"), defaultPlacer);

    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.ioBufTypes.insert(id_SEIO33_CORE);
        cfg.ioBufTypes.insert(id_SEIO18_CORE);
        cfg.ioBufTypes.insert(id_OSC_CORE);
        cfg.criticalityExponent = 7;
        if (!placer_heap(getCtx(), cfg))
            return false;
    } else if (placer == "sa") {
        if (!placer1(getCtx(), Placer1Cfg(getCtx())))
            return false;
    } else {
        log_error("Nexus architecture does not support placer '%s'\n", placer.c_str());
    }
    getCtx()->attrs[getCtx()->id("step")] = std::string("place");
    archInfoToAttributes();
    return true;
}

bool Arch::route()
{
    assign_budget(getCtx(), true);

    route_globals();

    std::string router = str_or_default(settings, id("router"), defaultRouter);
    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("iCE40 architecture does not support router '%s'\n", router.c_str());
    }
    getCtx()->attrs[getCtx()->id("step")] = std::string("route");
    archInfoToAttributes();
    return result;
}

// -----------------------------------------------------------------------

CellPinMux Arch::get_cell_pinmux(const CellInfo *cell, IdString pin) const
{
    IdString param = id(stringf("%sMUX", pin.c_str(this)));
    auto fnd_param = cell->params.find(param);
    if (fnd_param == cell->params.end())
        return PINMUX_SIG;
    const std::string &pm = fnd_param->second.as_string();
    if (pm == "0")
        return PINMUX_0;
    else if (pm == "1")
        return PINMUX_1;
    else if (pm == "INV")
        return PINMUX_INV;
    else if (pm == pin.c_str(this))
        return PINMUX_SIG;
    else {
        log_error("Invalid %s setting '%s' for cell '%s'\n", nameOf(param), pm.c_str(), nameOf(cell));
        NPNR_ASSERT_FALSE("unreachable");
    }
}

void Arch::set_cell_pinmux(CellInfo *cell, IdString pin, CellPinMux state)
{
    IdString param = id(stringf("%sMUX", pin.c_str(this)));
    switch (state) {
    case PINMUX_SIG:
        cell->params.erase(param);
        break;
    case PINMUX_0:
        cell->params[param] = std::string("0");
        break;
    case PINMUX_1:
        cell->params[param] = std::string("1");
        break;
    case PINMUX_INV:
        cell->params[param] = std::string("INV");
        break;
    default:
        NPNR_ASSERT_FALSE("unreachable");
    }
}

// -----------------------------------------------------------------------

const PadInfoPOD *Arch::get_pkg_pin_data(const std::string &pin) const
{
    for (size_t i = 0; i < chip_info->num_pads; i++) {
        const PadInfoPOD *pad = &(chip_info->pads[i]);
        if (pin == pad->pins[package_idx].get())
            return pad;
    }
    return nullptr;
}

Loc Arch::get_pad_loc(const PadInfoPOD *pad) const
{
    Loc loc;
    switch (pad->side) {
    case PIO_LEFT:
        loc.x = 0;
        loc.y = pad->offset;
        break;
    case PIO_RIGHT:
        loc.x = chip_info->width - 1;
        loc.y = pad->offset;
        break;
    case PIO_TOP:
        loc.x = pad->offset;
        loc.y = 0;
        break;
    case PIO_BOTTOM:
        loc.x = pad->offset;
        loc.y = chip_info->height - 1;
    }
    loc.z = pad->pio_index;
    return loc;
}

BelId Arch::get_pad_pio_bel(const PadInfoPOD *pad) const
{
    if (pad == nullptr)
        return BelId();
    return getBelByLocation(get_pad_loc(pad));
}

const PadInfoPOD *Arch::get_bel_pad(BelId bel) const
{
    Loc loc = getBelLocation(bel);
    int side = -1, offset = -1;
    // Convert (x, y) to (side, offset)
    if (loc.x == 0) {
        side = PIO_LEFT;
        offset = loc.y;
    } else if (loc.x == (chip_info->width - 1)) {
        side = PIO_RIGHT;
        offset = loc.y;
    } else if (loc.y == 0) {
        side = PIO_TOP;
        offset = loc.x;
    } else if (loc.y == (chip_info->height - 1)) {
        side = PIO_BOTTOM;
        offset = loc.x;
    } else {
        return nullptr;
    }
    // Lookup in the list of pads
    for (size_t i = 0; i < chip_info->num_pads; i++) {
        const PadInfoPOD *pad = &(chip_info->pads[i]);
        if (pad->side == side && pad->offset == offset && pad->pio_index == loc.z)
            return pad;
    }
    return nullptr;
}

std::string Arch::get_pad_functions(const PadInfoPOD *pad) const
{
    std::string s;
    for (size_t i = 0; i < pad->num_funcs; i++) {
        if (!s.empty())
            s += '/';
        s += IdString(pad->func_strs[i]).str(this);
    }
    return s;
}

// -----------------------------------------------------------------------

#ifdef WITH_HEAP
const std::string Arch::defaultPlacer = "heap";
#else
const std::string Arch::defaultPlacer = "sa";
#endif

const std::vector<std::string> Arch::availablePlacers = {"sa",
#ifdef WITH_HEAP
                                                         "heap"
#endif

};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

NEXTPNR_NAMESPACE_END

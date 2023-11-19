/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
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

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <iostream>
#include <math.h>
#include "embed.h"
#include "gfx.h"
#include "machxo2_available.h"
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "timing.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);

#include "constids.inc"

#undef X
}

// ---------------------------------------------------------------

static void get_chip_info(std::string device, const ChipInfoPOD **chip_info, const PackageInfoPOD **package_info,
                          const SpeedGradePOD **speed_grade, const char **device_name, const char **package_name,
                          int *device_speed)
{
    std::stringstream ss(available_devices);
    std::string name;
    while (getline(ss, name, ';')) {
        std::string chipdb = stringf("machxo2/chipdb-%s.bin", name.c_str());
        auto db_ptr = reinterpret_cast<const RelPtr<ChipInfoPOD> *>(get_chipdb(chipdb));
        if (!db_ptr)
            continue; // chipdb not available
        for (auto &chip : db_ptr->get()->variants) {
            for (auto &pkg : chip.packages) {
                for (auto &speedgrade : chip.speeds) {
                    for (auto &rating : chip.suffixes) {
                        std::string devname = stringf("%s-%d%s%s", chip.name.get(), speedgrade.speed,
                                                      pkg.short_name.get(), rating.suffix.get());
                        if (device == devname) {
                            *chip_info = db_ptr->get();
                            *package_info = nullptr;
                            *package_name = pkg.name.get();
                            *device_name = chip.name.get();
                            *device_speed = speedgrade.speed;
                            *speed_grade = &(db_ptr->get()->speed_grades[speedgrade.index]);
                            for (auto &pi : db_ptr->get()->package_info) {
                                if (pkg.name.get() == pi.name.get()) {
                                    *package_info = &pi;
                                    break;
                                }
                            }
                            return;
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
    get_chip_info(args.device, &chip_info, &package_info, &speed_grade, &device_name, &package_name, &device_speed);
    if (chip_info == nullptr)
        log_error("Unsupported MachXO2 chip type.\n");
    if (chip_info->const_id_count != DB_CONST_ID_COUNT)
        log_error("Chip database 'bba' and nextpnr code are out of sync; please rebuild (or contact distribution "
                  "maintainer)!\n");

    if (!package_info)
        log_error("Unsupported package '%s' for '%s'.\n", package_name, getChipName().c_str());

    tile_status.resize(chip_info->num_tiles);
    for (int i = 0; i < chip_info->num_tiles; i++) {
        auto &ts = tile_status.at(i);
        auto &tile_data = chip_info->tile_info[i];
        ts.boundcells.resize(chip_info->tiles[i].bel_data.size(), nullptr);
        for (auto &name : tile_data.tile_names) {
            if (strcmp(chip_info->tiletype_names[name.type_idx].get(), "PLC") == 0) {
                // Is a logic tile
                ts.lts = new LogicTileStatus();
                break;
            }
        }
    }

    BaseArch::init_cell_types();
    BaseArch::init_bel_buckets();

    for (int i = 0; i < chip_info->width; i++)
        x_ids.push_back(idf("X%d", i));
    for (int i = 0; i < chip_info->height; i++)
        y_ids.push_back(idf("Y%d", i));

    for (int i = 0; i < chip_info->width; i++) {
        IdString x_id = idf("X%d", i);
        x_ids.push_back(x_id);
        id_to_x[x_id] = i;
    }
    for (int i = 0; i < chip_info->height; i++) {
        IdString y_id = idf("Y%d", i);
        y_ids.push_back(y_id);
        id_to_y[y_id] = i;
    }

    wire_tile_vecidx.resize(chip_info->num_tiles, -1);
    int n_wires = 0;
    for (auto e : getWires()) {
        if (e.index == 0) {
            wire_tile_vecidx.at(e.location.y * chip_info->width + e.location.x) = n_wires;
        }
        n_wires++;
    }
    wire2net.resize(n_wires, nullptr);
    wire_fanout.resize(n_wires, 0);

    pip_tile_vecidx.resize(chip_info->num_tiles, -1);
    int n_pips = 0;
    for (auto e : getPips()) {
        if (e.index == 0) {
            pip_tile_vecidx.at(e.location.y * chip_info->width + e.location.x) = n_pips;
        }
        n_pips++;
    }
    pip2net.resize(n_pips, nullptr);

    lutperm_allowed.resize(chip_info->width * chip_info->height * 4);
}

void Arch::list_devices()
{
    log("Supported devices: \n");
    std::stringstream ss(available_devices);
    std::string name;
    while (getline(ss, name, ';')) {
        std::string chipdb = stringf("machxo2/chipdb-%s.bin", name.c_str());
        auto db_ptr = reinterpret_cast<const RelPtr<ChipInfoPOD> *>(get_chipdb(chipdb));
        if (!db_ptr)
            continue; // chipdb not available
        for (auto &chip : db_ptr->get()->variants) {
            for (auto &pkg : chip.packages) {
                for (auto &speedgrade : chip.speeds) {
                    for (auto &rating : chip.suffixes) {
                        log("    %s-%d%s%s\n", chip.name.get(), speedgrade.speed, pkg.short_name.get(),
                            rating.suffix.get());
                    }
                }
            }
        }
    }
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const { return args.device; }
IdString Arch::archArgsToId(ArchArgs args) const { return id(args.device); }

// ---------------------------------------------------------------

BelId Arch::getBelByName(IdStringList name) const
{
    if (name.size() != 3)
        return BelId();
    BelId ret;
    Location loc;
    loc.x = id_to_x.at(name[0]);
    loc.y = id_to_y.at(name[1]);
    ret.location = loc;
    const TileTypePOD *loci = tile_info(ret);
    for (int i = 0; i < loci->bel_data.ssize(); i++) {
        if (std::strcmp(loci->bel_data[i].name.get(), name[2].c_str(this)) == 0) {
            ret.index = i;
            return ret;
        }
    }
    return BelId();
}

BelRange Arch::getBelsByTile(int x, int y) const
{
    BelRange br;

    br.b.cursor_tile = y * chip_info->width + x;
    br.e.cursor_tile = y * chip_info->width + x;
    br.b.cursor_index = 0;
    br.e.cursor_index = chip_info->tiles[y * chip_info->width + x].bel_data.ssize() - 1;
    br.b.chip = chip_info;
    br.e.chip = chip_info;
    if (br.e.cursor_index == -1)
        ++br.e.cursor_index;
    else
        ++br.e;
    return br;
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    WireId ret;

    NPNR_ASSERT(bel != BelId());

    for (auto &bw : tile_info(bel)->bel_data[bel.index].bel_wires)
        if (bw.port == pin.index) {
            ret.location.x = bw.rel_wire_loc.x;
            ret.location.y = bw.rel_wire_loc.y;
            ret.index = bw.wire_index;
            break;
        }

    return ret;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    for (auto &bw : tile_info(bel)->bel_data[bel.index].bel_wires)
        if (bw.port == pin.index)
            return PortType(bw.type);

    return PORT_INOUT;
}

// ---------------------------------------------------------------

WireId Arch::getWireByName(IdStringList name) const
{
    if (name.size() != 3)
        return WireId();
    WireId ret;
    Location loc;
    loc.x = id_to_x.at(name[0]);
    loc.y = id_to_y.at(name[1]);
    ret.location = loc;
    const TileTypePOD *loci = tile_info(ret);
    for (int i = 0; i < loci->wire_data.ssize(); i++) {
        if (std::strcmp(loci->wire_data[i].name.get(), name[2].c_str(this)) == 0) {
            ret.index = i;
            return ret;
        }
    }
    return WireId();
}

// ---------------------------------------------------------------

PipId Arch::getPipByName(IdStringList name) const
{
    if (name.size() != 3)
        return PipId();
    auto it = pip_by_name.find(name);
    if (it != pip_by_name.end())
        return it->second;

    PipId ret;
    Location loc;
    std::string basename;
    loc.x = id_to_x.at(name[0]);
    loc.y = id_to_y.at(name[1]);
    ret.location = loc;
    const TileTypePOD *loci = tile_info(ret);
    for (int i = 0; i < loci->pip_data.ssize(); i++) {
        PipId curr;
        curr.location = loc;
        curr.index = i;
        pip_by_name[getPipName(curr)] = curr;
    }
    if (pip_by_name.find(name) == pip_by_name.end())
        NPNR_ASSERT_FALSE_STR("no pip named " + name.str(getCtx()));
    return pip_by_name[name];
}

IdStringList Arch::getPipName(PipId pip) const
{
    auto &pip_data = tile_info(pip)->pip_data[pip.index];
    WireId src = getPipSrcWire(pip), dst = getPipDstWire(pip);
    std::string pip_name =
            stringf("%d_%d_%s->%d_%d_%s", pip_data.src.x, pip_data.src.y, get_wire_basename(src).c_str(this),
                    pip_data.dst.x, pip_data.dst.y, get_wire_basename(dst).c_str(this));

    std::array<IdString, 3> ids{x_ids.at(pip.location.x), y_ids.at(pip.location.y), id(pip_name)};
    return IdStringList(ids);
}

// ---------------------------------------------------------------

BelId Arch::get_package_pin_bel(const std::string &pin) const
{
    for (auto &ppin : package_info->pin_data) {
        if (ppin.name.get() == pin) {
            BelId bel;
            bel.location = ppin.abs_loc;
            bel.index = ppin.bel_index;
            return bel;
        }
    }
    return BelId();
}

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    NPNR_ASSERT(bel != BelId());

    for (auto &bw : tile_info(bel)->bel_data[bel.index].bel_wires) {
        IdString id;
        id.index = bw.port;
        ret.push_back(id);
    }

    return ret;
}

BelId Arch::getBelByLocation(Loc loc) const
{
    BelId ret;

    if (loc.x >= chip_info->width || loc.y >= chip_info->height)
        return BelId();

    ret.location.x = loc.x;
    ret.location.y = loc.y;

    const TileTypePOD *loci = tile_info(ret);
    for (int i = 0; i < loci->bel_data.ssize(); i++) {
        if (loci->bel_data[i].z == loc.z) {
            ret.index = i;
            return ret;
        }
    }

    return BelId();
}

// ---------------------------------------------------------------

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    int num_uh = tile_info(dst)->wire_data[dst.index].pips_uphill.size();
    if (num_uh < 6) {
        for (auto uh : getPipsUphill(dst)) {
            if (getPipSrcWire(uh) == src)
                return getPipDelay(uh).maxDelay();
        }
    }

    auto est_location = [&](WireId w) -> std::pair<int, int> {
        const auto &wire = tile_info(w)->wire_data[w.index];
        if (w == gsrclk_wire) {
            auto phys_wire = getPipSrcWire(*(getPipsUphill(w).begin()));
            return std::make_pair(int(phys_wire.location.x), int(phys_wire.location.y));
        } else if (wire.bel_pins.size() > 0) {
            return std::make_pair(wire.bel_pins[0].rel_bel_loc.x, wire.bel_pins[0].rel_bel_loc.y);
        } else if (wire.pips_downhill.size() > 0) {
            return std::make_pair(wire.pips_downhill[0].rel_loc.x, wire.pips_downhill[0].rel_loc.y);
        } else if (wire.pips_uphill.size() > 0) {
            return std::make_pair(wire.pips_uphill[0].rel_loc.x, wire.pips_uphill[0].rel_loc.y);
        } else {
            return std::make_pair(int(w.location.x), int(w.location.y));
        }
    };

    auto src_loc = est_location(src);
    std::pair<int, int> dst_loc;
    if (wire_loc_overrides.count(dst)) {
        dst_loc = wire_loc_overrides.at(dst);
    } else {
        dst_loc = est_location(dst);
    }

    int dx = abs(src_loc.first - dst_loc.first), dy = abs(src_loc.second - dst_loc.second);

    return (500 - 22 * device_speed) *
           (6 + std::max(dx - 5, 0) + std::max(dy - 5, 0) + 2 * (std::min(dx, 5) + std::min(dy, 5)));
}

BoundingBox Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    BoundingBox bb;

    bb.x0 = src.location.x;
    bb.y0 = src.location.y;
    bb.x1 = src.location.x;
    bb.y1 = src.location.y;

    auto extend = [&](int x, int y) {
        bb.x0 = std::min(bb.x0, x);
        bb.x1 = std::max(bb.x1, x);
        bb.y0 = std::min(bb.y0, y);
        bb.y1 = std::max(bb.y1, y);
    };

    auto est_location = [&](WireId w) -> std::pair<int, int> {
        const auto &wire = tile_info(w)->wire_data[w.index];
        if (w == gsrclk_wire) {
            auto phys_wire = getPipSrcWire(*(getPipsUphill(w).begin()));
            return std::make_pair(int(phys_wire.location.x), int(phys_wire.location.y));
        } else if (wire.bel_pins.size() > 0) {
            return std::make_pair(wire.bel_pins[0].rel_bel_loc.x, wire.bel_pins[0].rel_bel_loc.y);
        } else if (wire.pips_downhill.size() > 0) {
            return std::make_pair(wire.pips_downhill[0].rel_loc.x, wire.pips_downhill[0].rel_loc.y);
        } else if (wire.pips_uphill.size() > 0) {
            return std::make_pair(wire.pips_uphill[0].rel_loc.x, wire.pips_uphill[0].rel_loc.y);
        } else {
            return std::make_pair(int(w.location.x), int(w.location.y));
        }
    };

    auto src_loc = est_location(src);
    extend(src_loc.first, src_loc.second);
    if (wire_loc_overrides.count(src)) {
        extend(wire_loc_overrides.at(src).first, wire_loc_overrides.at(src).second);
    }
    std::pair<int, int> dst_loc;
    extend(dst.location.x, dst.location.y);
    if (wire_loc_overrides.count(dst)) {
        dst_loc = wire_loc_overrides.at(dst);
    } else {
        dst_loc = est_location(dst);
    }
    extend(dst_loc.first, dst_loc.second);
    return bb;
}

delay_t Arch::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    if ((src_pin == id_FCO && dst_pin == id_FCI) || dst_pin.in(id_FXA, id_FXB) || (src_pin == id_F && dst_pin == id_DI))
        return 0;
    auto driver_loc = getBelLocation(src_bel);
    auto sink_loc = getBelLocation(dst_bel);
    // Encourage use of direct interconnect
    //   exact LUT input doesn't matter as they can be permuted by the router...
    if (driver_loc.x == sink_loc.x && driver_loc.y == sink_loc.y) {
        if (dst_pin.in(id_A, id_B, id_C, id_D) && src_pin == id_Q) {
            int lut = (sink_loc.z >> lc_idx_shift), ff = (driver_loc.z >> lc_idx_shift);
            if (lut == ff)
                return 0;
        }
        if (dst_pin.in(id_A, id_B, id_C, id_D) && src_pin == id_F) {
            int l0 = (driver_loc.z >> lc_idx_shift);
            if (l0 != 1 && l0 != 6)
                return 0;
        }
    }

    int dx = abs(driver_loc.x - sink_loc.x), dy = abs(driver_loc.y - sink_loc.y);

    return (250 - 22 * device_speed) *
           (3 + std::max(dx - 5, 0) + std::max(dy - 5, 0) + 2 * (std::min(dx, 5) + std::min(dy, 5)));
}

delay_t Arch::getRipupDelayPenalty() const { return 400; }

// ---------------------------------------------------------------

bool Arch::place()
{
    std::string placer = str_or_default(settings, id_placer, defaultPlacer);

    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.criticalityExponent = 4;
        cfg.ioBufTypes.insert(id_TRELLIS_IO);

        cfg.cellGroups.emplace_back();
        cfg.cellGroups.back().insert(id_TRELLIS_COMB);
        cfg.cellGroups.back().insert(id_TRELLIS_FF);
        cfg.cellGroups.back().insert(id_TRELLIS_RAMW);
        cfg.placeAllAtOnce = true;

        cfg.beta = 0.75;

        if (!placer_heap(getCtx(), cfg))
            return false;
    } else if (placer == "sa") {
        if (!placer1(getCtx(), Placer1Cfg(getCtx())))
            return false;
    } else {
        log_error("MachXO2 architecture does not support placer '%s'\n", placer.c_str());
    }

    getCtx()->settings[id_place] = 1;

    archInfoToAttributes();
    return true;
}

bool Arch::route()
{
    std::string router = str_or_default(settings, id_router, defaultRouter);

    disable_router_lutperm = getCtx()->setting<bool>("arch.disable_router_lutperm", false);

    setup_wire_locations();

    assignArchInfo();

    route_globals();

    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("MachXO2 architecture does not support router '%s'\n", router.c_str());
    }
    getCtx()->settings[id_route] = 1;
    archInfoToAttributes();
    return result;
}

// -----------------------------------------------------------------------

std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const
{
    std::vector<GraphicElement> ret;
    if (decal.type == DecalId::TYPE_WIRE) {
        WireId wire;
        wire.index = decal.z;
        wire.location = decal.location;
        auto wire_type = getWireType(wire);
        int x = decal.location.x;
        int y = decal.location.y;
        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
        GfxTileWireId tilewire = GfxTileWireId(tile_info(wire)->wire_data[wire.index].tile_wire);
        gfxTileWire(ret, x, chip_info->height - y - 1, chip_info->width, chip_info->height, wire_type, tilewire, style);
    } else if (decal.type == DecalId::TYPE_PIP) {
        PipId pip;
        pip.index = decal.z;
        pip.location = decal.location;
        WireId src_wire = getPipSrcWire(pip);
        WireId dst_wire = getPipDstWire(pip);
        int x = decal.location.x;
        int y = decal.location.y;
        GfxTileWireId src_id = GfxTileWireId(tile_info(src_wire)->wire_data[src_wire.index].tile_wire);
        GfxTileWireId dst_id = GfxTileWireId(tile_info(dst_wire)->wire_data[dst_wire.index].tile_wire);
        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_HIDDEN;
        gfxTilePip(ret, x, chip_info->height - y - 1, chip_info->width, chip_info->height, src_wire,
                   getWireType(src_wire), src_id, dst_wire, getWireType(dst_wire), dst_id, style);
    } else if (decal.type == DecalId::TYPE_BEL) {
        BelId bel;
        bel.index = decal.z;
        bel.location = decal.location;
        auto bel_type = getBelType(bel);
        int x = decal.location.x;
        int y = decal.location.y;
        int z = tile_info(bel)->bel_data[bel.index].z;
        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
        gfxTileBel(ret, x, chip_info->height - y - 1, z, chip_info->width, chip_info->height, bel_type, style);
    }

    return ret;
}

DecalXY Arch::getBelDecal(BelId bel) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_BEL;
    decalxy.decal.location = bel.location;
    decalxy.decal.z = bel.index;
    decalxy.decal.active = getBoundBelCell(bel) != nullptr;
    return decalxy;
}

DecalXY Arch::getWireDecal(WireId wire) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_WIRE;
    decalxy.decal.location = wire.location;
    decalxy.decal.z = wire.index;
    decalxy.decal.active = getBoundWireNet(wire) != nullptr;
    return decalxy;
}

DecalXY Arch::getPipDecal(PipId pip) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_PIP;
    decalxy.decal.location = pip.location;
    decalxy.decal.z = pip.index;
    decalxy.decal.active = getBoundPipNet(pip) != nullptr;
    return decalxy;
};

DecalXY Arch::getGroupDecal(GroupId group) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_GROUP;
    decalxy.decal.location = group.location;
    decalxy.decal.z = group.type;
    decalxy.decal.active = true;
    return decalxy;
}

// -----------------------------------------------------------------------

bool Arch::get_delay_from_tmg_db(IdString tctype, IdString from, IdString to, DelayQuad &delay) const
{
    auto fnd_dk = celldelay_cache.find({tctype, from, to});
    if (fnd_dk != celldelay_cache.end()) {
        delay = fnd_dk->second.second;
        return fnd_dk->second.first;
    }
    for (auto &tc : speed_grade->cell_timings) {
        if (tc.cell_type == tctype.index) {
            for (auto &dly : tc.prop_delays) {
                if (dly.from_port == from.index && dly.to_port == to.index) {
                    delay = DelayQuad(dly.min_delay, dly.max_delay);
                    celldelay_cache[{tctype, from, to}] = std::make_pair(true, delay);
                    return true;
                }
            }
            celldelay_cache[{tctype, from, to}] = std::make_pair(false, DelayQuad());
            return false;
        }
    }
    NPNR_ASSERT_FALSE("failed to find timing cell in db");
}

void Arch::get_setuphold_from_tmg_db(IdString tctype, IdString clock, IdString port, DelayPair &setup,
                                     DelayPair &hold) const
{
    for (auto &tc : speed_grade->cell_timings) {
        if (tc.cell_type == tctype.index) {
            for (auto &sh : tc.setup_holds) {
                if (sh.clock_port == clock.index && sh.sig_port == port.index) {
                    setup.max_delay = sh.max_setup;
                    setup.min_delay = sh.min_setup;
                    hold.max_delay = sh.max_hold;
                    hold.min_delay = sh.min_hold;
                    return;
                }
            }
        }
    }
    NPNR_ASSERT_FALSE("failed to find timing cell in db");
}

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    // Data for -8 grade
    if (cell->type == id_TRELLIS_COMB) {
        bool has_carry = cell->combInfo.flags & ArchCellInfo::COMB_CARRY;
        IdString tmg_type = has_carry ? (((cell->constr_z >> Arch::lc_idx_shift) % 2) ? id_TRELLIS_COMB_CARRY1
                                                                                      : id_TRELLIS_COMB_CARRY0)
                                      : id_TRELLIS_COMB;
        if (fromPort.in(id_A, id_B, id_C, id_D, id_M, id_F1, id_FXA, id_FXB, id_FCI))
            return get_delay_from_tmg_db(tmg_type, fromPort, toPort, delay);
        else
            return false;
    } else if (cell->type == id_TRELLIS_FF) {
        return false;
    } else if (cell->type == id_TRELLIS_RAMW) {
        if ((fromPort == id_A0 && toPort == id_WADO0) || (fromPort == id_A1 && toPort == id_WDO0) ||
            (fromPort == id_B0 && toPort == id_WADO1) || (fromPort == id_B1 && toPort == id_WDO1) ||
            (fromPort == id_C0 && toPort == id_WADO2) || (fromPort == id_C1 && toPort == id_WDO2) ||
            (fromPort == id_D0 && toPort == id_WADO3) || (fromPort == id_D1 && toPort == id_WDO3)) {
            delay = DelayQuad(0);
            return true;
        }
        return false;
    } else if (cell->type == id_DCCA) {
        if (fromPort == id_CLKI && toPort == id_CLKO) {
            delay = DelayQuad(0);
            return true;
        }
        return false;
    } else if (cell->type == id_DP8KC) {
        return false;
    } else {
        return false;
    }
}

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    auto disconnected = [cell](IdString p) { return !cell->ports.count(p) || cell->ports.at(p).net == nullptr; };
    clockInfoCount = 0;
    if (cell->type == id_TRELLIS_COMB) {
        if (port == id_WCK)
            return TMG_CLOCK_INPUT;
        if (port.in(id_A, id_B, id_C, id_D, id_FCI, id_FXA, id_FXB, id_F1))
            return TMG_COMB_INPUT;
        if (port == id_F && disconnected(id_A) && disconnected(id_B) && disconnected(id_C) && disconnected(id_D) &&
            disconnected(id_FCI))
            return TMG_IGNORE; // LUT with no inputs is a constant
        if (port.in(id_F, id_FCO, id_OFX))
            return TMG_COMB_OUTPUT;
        if (port == id_M)
            return TMG_COMB_INPUT;
        if (port.in(id_WD, id_WAD0, id_WAD1, id_WAD2, id_WAD3, id_WRE)) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        }
        return TMG_IGNORE;
    } else if (cell->type == id_TRELLIS_FF) {
        bool using_m = (cell->ffInfo.flags & ArchCellInfo::FF_M_USED);
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        if (port == id_DI || (using_m && (port == id_M)) || port.in(id_CE, id_LSR)) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        }
        if (port == id_Q) {
            clockInfoCount = 1;
            return TMG_REGISTER_OUTPUT;
        }
        return TMG_IGNORE;
    } else if (cell->type == id_TRELLIS_RAMW) {
        if (port.in(id_A0, id_A1, id_B0, id_B1, id_C0, id_C1, id_D0, id_D1))
            return TMG_COMB_INPUT;
        if (port.in(id_WDO0, id_WDO1, id_WDO2, id_WDO3, id_WADO0, id_WADO1, id_WADO2, id_WADO3))
            return TMG_COMB_OUTPUT;
        return TMG_IGNORE;
    } else if (cell->type == id_TRELLIS_IO) {
        if (port.in(id_T, id_I))
            return TMG_ENDPOINT;
        if (port == id_O)
            return TMG_STARTPOINT;
        return TMG_IGNORE;
    } else if (cell->type == id_DCCA) {
        if (port == id_CLKI)
            return TMG_COMB_INPUT;
        if (port == id_CLKO)
            return TMG_COMB_OUTPUT;
        return TMG_IGNORE;
    } else if (cell->type == id_DP8KC) {
        if (port.in(id_CLKA, id_CLKB))
            return TMG_CLOCK_INPUT;
        std::string port_name = port.str(this);
        for (auto c : boost::adaptors::reverse(port_name)) {
            if (std::isdigit(c))
                continue;
            if (c == 'A' || c == 'B')
                clockInfoCount = 1;
            else
                NPNR_ASSERT_FALSE_STR("bad ram port");
            return (cell->ports.at(port).type == PORT_OUT) ? TMG_REGISTER_OUTPUT : TMG_REGISTER_INPUT;
        }
        NPNR_ASSERT_FALSE_STR("no timing type for RAM port '" + port.str(this) + "'");
    } else if (cell->type == id_EHXPLLJ) {
        return TMG_IGNORE;
    } else if (cell->type.in(id_SEDFA, id_GSR, id_JTAGF)) {
        return (cell->ports.at(port).type == PORT_OUT) ? TMG_STARTPOINT : TMG_ENDPOINT;
    } else if (cell->type.in(id_OSCH, id_OSCJ)) {
        if (port == id_OSC)
            return TMG_GEN_CLOCK;
        else
            return TMG_IGNORE;
    } else if (cell->type == id_CLKDIVC) {
        if (port == id_CLKI)
            return TMG_CLOCK_INPUT;
        else if (port.in(id_RST, id_ALIGNWD))
            return TMG_ENDPOINT;
        else if (port == id_CDIVX)
            return TMG_GEN_CLOCK;
        else
            NPNR_ASSERT_FALSE("bad clkdiv port");
    } else {
        log_error("cell type '%s' is unsupported (instantiated as '%s')\n", cell->type.c_str(this),
                  cell->name.c_str(this));
    }
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo info;
    info.setup = DelayPair(0);
    info.hold = DelayPair(0);
    info.clockToQ = DelayQuad(0);
    if (cell->type == id_TRELLIS_COMB) {
        if (port.in(id_WD, id_WAD0, id_WAD1, id_WAD2, id_WAD3, id_WRE)) {
            if (port == id_WD)
                port = id_WD0;
            info.edge = (cell->combInfo.flags & ArchCellInfo::COMB_RAM_WCKINV) ? FALLING_EDGE : RISING_EDGE;
            info.clock_port = id_WCK;
            get_setuphold_from_tmg_db(id_TRELLIS_SLICE, id_WCK, port, info.setup, info.hold);
        }
    } else if (cell->type == id_TRELLIS_FF) {
        bool using_m = (cell->ffInfo.flags & ArchCellInfo::FF_M_USED);
        if (port.in(id_DI, id_CE, id_LSR) || (using_m && port == id_M)) {
            if (port == id_DI)
                port = id_DI0;
            if (port == id_M)
                port = id_M0;
            info.edge = (cell->ffInfo.flags & ArchCellInfo::FF_CLKINV) ? FALLING_EDGE : RISING_EDGE;
            info.clock_port = id_CLK;
            get_setuphold_from_tmg_db(id_TRELLIS_SLICE, id_CLK, port, info.setup, info.hold);
        } else {
            NPNR_ASSERT(port == id_Q);
            port = id_Q0;
            info.edge = (cell->ffInfo.flags & ArchCellInfo::FF_CLKINV) ? FALLING_EDGE : RISING_EDGE;
            info.clock_port = id_CLK;
            bool is_path = get_delay_from_tmg_db(id_TRELLIS_SLICE, id_CLK, port, info.clockToQ);
            NPNR_ASSERT(is_path);
        }
    } else if (cell->type == id_DP8KC) {
        std::string port_name = port.str(this);
        IdString half_clock;
        for (auto c : boost::adaptors::reverse(port_name)) {
            if (std::isdigit(c))
                continue;
            if (c == 'A') {
                half_clock = id_CLKA;
                break;
            } else if (c == 'B') {
                half_clock = id_CLKB;
                break;
            } else
                NPNR_ASSERT_FALSE_STR("bad ram port " + port.str(this));
        }
        if (cell->ramInfo.is_pdp) {
            bool is_output = cell->ports.at(port).type == PORT_OUT;
            // In PDP mode, all read signals are in CLKB domain and write signals in CLKA domain
            if (is_output ||
                port.in(id_OCEB, id_CEB, id_ADB5, id_ADB6, id_ADB7, id_ADB8, id_ADB9, id_ADB10, id_ADB11, id_ADB12))
                info.clock_port = id_CLKB;
            else
                info.clock_port = id_CLKA;
        } else {
            info.clock_port = half_clock;
        }
        info.edge = (str_or_default(cell->params, info.clock_port == id_CLKB ? id_CLKBMUX : id_CLKAMUX, "CLK") == "INV")
                            ? FALLING_EDGE
                            : RISING_EDGE;
        if (cell->ports.at(port).type == PORT_OUT) {
            bool is_path = get_delay_from_tmg_db(cell->ramInfo.regmode_timing_id, half_clock, port, info.clockToQ);
            NPNR_ASSERT(is_path);
        } else {
            get_setuphold_from_tmg_db(cell->ramInfo.regmode_timing_id, half_clock, port, info.setup, info.hold);
        }
    }
    return info;
}

// ---------------------------------------------------------------

const std::string Arch::defaultPlacer = "heap";

const std::vector<std::string> Arch::availablePlacers = {"sa", "heap"};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

std::vector<std::pair<std::string, std::string>> Arch::get_tiles_at_loc(int row, int col)
{
    std::vector<std::pair<std::string, std::string>> ret;
    auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
    for (auto &tn : tileloc.tile_names) {
        ret.push_back(std::make_pair(tn.name.get(), chip_info->tiletype_names[tn.type_idx].get()));
    }
    return ret;
}

// -----------------------------------------------------------------------

GroupId Arch::getGroupByName(IdStringList name) const
{
    for (auto g : getGroups())
        if (getGroupName(g) == name)
            return g;
    return GroupId();
}

IdStringList Arch::getGroupName(GroupId group) const
{
    std::string suffix;

    switch (group.type) {
    case GroupId::TYPE_SWITCHBOX:
        suffix = "switchbox";
        break;
    default:
        return IdStringList();
    }

    std::array<IdString, 3> ids{x_ids.at(group.location.x), y_ids.at(group.location.y), id(suffix)};
    return IdStringList(ids);
}

std::vector<GroupId> Arch::getGroups() const
{
    std::vector<GroupId> ret;

    for (int y = 1; y < chip_info->height - 1; y++) {
        for (int x = 1; x < chip_info->width - 1; x++) {
            GroupId group;
            group.type = GroupId::TYPE_SWITCHBOX;
            group.location.x = x;
            group.location.y = y;
            ret.push_back(group);
        }
    }
    return ret;
}

std::vector<BelId> Arch::getGroupBels(GroupId group) const
{
    std::vector<BelId> ret;
    return ret;
}

std::vector<WireId> Arch::getGroupWires(GroupId group) const
{
    std::vector<WireId> ret;
    return ret;
}

std::vector<PipId> Arch::getGroupPips(GroupId group) const
{
    std::vector<PipId> ret;
    return ret;
}

std::vector<GroupId> Arch::getGroupGroups(GroupId group) const
{
    std::vector<GroupId> ret;
    return ret;
}

// -----------------------------------------------------------------------

std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const
{
    std::vector<std::pair<IdString, std::string>> ret;
    auto &wi = tile_info(wire)->wire_data[wire.index];

    ret.push_back(std::make_pair(id_TILE_WIRE_ID, stringf("%d", wi.tile_wire)));

    return ret;
}

// -----------------------------------------------------------------------
bool Arch::is_spine_row(int row) const
{
    for (auto &spine : chip_info->spines) {
        if (row == spine.row)
            return true;
    }
    return false;
}

NEXTPNR_NAMESPACE_END

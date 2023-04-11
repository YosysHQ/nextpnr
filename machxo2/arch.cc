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
                          const char **device_name, const char **package_name)
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
                for (auto &speedgrade : chip.speed_grades) {
                    for (auto &rating : chip.suffixes) {
                        std::string devname = stringf("%s-%d%s%s", chip.name.get(), speedgrade.speed,
                                                      pkg.short_name.get(), rating.suffix.get());
                        if (device == devname) {
                            *chip_info = db_ptr->get();
                            *package_info = nullptr;
                            *package_name = pkg.name.get();
                            *device_name = chip.name.get();
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
    get_chip_info(args.device, &chip_info, &package_info, &device_name, &package_name);
    if (chip_info == nullptr)
        log_error("Unsupported MachXO2 chip type.\n");
    if (chip_info->const_id_count != DB_CONST_ID_COUNT)
        log_error("Chip database 'bba' and nextpnr code are out of sync; please rebuild (or contact distribution "
                  "maintainer)!\n");

    if (!package_info)
        log_error("Unsupported package '%s' for '%s'.\n", package_name, getChipName().c_str());

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
                for (auto &speedgrade : chip.speed_grades) {
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

bool Arch::getBelGlobalBuf(BelId bel) const { return false; }

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    for (auto &bw : tile_info(bel)->bel_data[bel.index].bel_wires)
        if (bw.port == pin.index) {
            WireId ret;

            ret.location.x = bw.rel_wire_loc.x;
            ret.location.y = bw.rel_wire_loc.y;
            ret.index = bw.wire_index;

            return ret;
        }

    return WireId();
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    for (auto &bw : tile_info(bel)->bel_data[bel.index].bel_wires)
        if (bw.port == pin.index)
            return PortType(bw.type);

    return PORT_INOUT;
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

// ---------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const
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

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    // Taxicab distance multiplied by pipDelay (0.01) and fake wireDelay (0.01).
    // TODO: This function will not work well for entrance to global routing,
    // as the entrances are located physically far from the DCCAs.
    return (abs(dst.location.x - src.location.x) + abs(dst.location.y - src.location.y)) * (0.01 + 0.01);
}

delay_t Arch::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    NPNR_UNUSED(src_pin);
    NPNR_UNUSED(dst_pin);

    NPNR_ASSERT(src_bel != BelId());
    NPNR_ASSERT(dst_bel != BelId());

    // TODO: Same deal applies here as with estimateDelay.
    return (abs(dst_bel.location.x - src_bel.location.x) + abs(dst_bel.location.y - src_bel.location.y)) *
           (0.01 + 0.01);
}

BoundingBox Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    BoundingBox bb;
    bb.x0 = std::min(src.location.x, dst.location.x);
    bb.y0 = std::min(src.location.y, dst.location.y);
    bb.x1 = std::max(src.location.x, dst.location.x);
    bb.y1 = std::max(src.location.y, dst.location.y);
    return bb;
}

// ---------------------------------------------------------------

bool Arch::place()
{
    std::string placer = str_or_default(settings, id_placer, defaultPlacer);
    if (placer == "sa") {
        bool retVal = placer1(getCtx(), Placer1Cfg(getCtx()));
        getCtx()->settings[id_place] = 1;
        archInfoToAttributes();
        return retVal;
    } else if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.ioBufTypes.insert(id_TRELLIS_IO);
        bool retVal = placer_heap(getCtx(), cfg);
        getCtx()->settings[id_place] = 1;
        archInfoToAttributes();
        return retVal;
    } else {
        log_error("MachXO2 architecture does not support placer '%s'\n", placer.c_str());
    }
}

bool Arch::route()
{
    std::string router = str_or_default(settings, id_router, defaultRouter);
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

// ---------------------------------------------------------------

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    // FIXME: Same deal as isValidBelForCell.
    return true;
}

const std::string Arch::defaultPlacer = "heap";

const std::vector<std::string> Arch::availablePlacers = {"sa", "heap"};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

bool Arch::cells_compatible(const CellInfo **cells, int count) const { return false; }

std::vector<std::pair<std::string, std::string>> Arch::get_tiles_at_loc(int row, int col)
{
    std::vector<std::pair<std::string, std::string>> ret;
    auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
    for (auto &tn : tileloc.tile_names) {
        ret.push_back(std::make_pair(tn.name.get(), chip_info->tiletype_names[tn.type_idx].get()));
    }
    return ret;
}

NEXTPNR_NAMESPACE_END

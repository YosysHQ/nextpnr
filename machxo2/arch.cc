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

static const ChipInfoPOD *get_chip_info(ArchArgs::ArchArgsTypes chip)
{
    std::string chipdb;
    if (chip == ArchArgs::LCMXO2_256HC) {
        chipdb = "machxo2/chipdb-256.bin";
    } else if (chip == ArchArgs::LCMXO2_640HC) {
        chipdb = "machxo2/chipdb-640.bin";
    } else if (chip == ArchArgs::LCMXO2_1200HC) {
        chipdb = "machxo2/chipdb-1200.bin";
    } else if (chip == ArchArgs::LCMXO2_2000HC) {
        chipdb = "machxo2/chipdb-2000.bin";
    } else if (chip == ArchArgs::LCMXO2_4000HC) {
        chipdb = "machxo2/chipdb-4000.bin";
    } else if (chip == ArchArgs::LCMXO2_7000HC) {
        chipdb = "machxo2/chipdb-7000.bin";
    } else {
        log_error("Unknown chip\n");
    }

    auto ptr = reinterpret_cast<const RelPtr<ChipInfoPOD> *>(get_chipdb(chipdb));
    if (ptr == nullptr)
        return nullptr;
    return ptr->get();
}

// ---------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
    chip_info = get_chip_info(args.type);
    if (chip_info == nullptr)
        log_error("Unsupported MachXO2 chip type.\n");
    if (chip_info->const_id_count != DB_CONST_ID_COUNT)
        log_error("Chip database 'bba' and nextpnr code are out of sync; please rebuild (or contact distribution "
                  "maintainer)!\n");

    package_info = nullptr;
    for (int i = 0; i < chip_info->num_packages; i++) {
        if (args.package == chip_info->package_info[i].name.get()) {
            package_info = &(chip_info->package_info[i]);
            break;
        }
    }
    if (!package_info)
        log_error("Unsupported package '%s' for '%s'.\n", args.package.c_str(), getChipName().c_str());

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

bool Arch::is_available(ArchArgs::ArchArgsTypes chip) { return get_chip_info(chip) != nullptr; }

std::vector<std::string> Arch::get_supported_packages(ArchArgs::ArchArgsTypes chip)
{
    const ChipInfoPOD *chip_info = get_chip_info(chip);
    std::vector<std::string> pkgs;
    for (int i = 0; i < chip_info->num_packages; i++) {
        pkgs.push_back(chip_info->package_info[i].name.get());
    }
    return pkgs;
}

std::string Arch::getChipName() const
{
    if (args.type == ArchArgs::LCMXO2_256HC) {
        return "LCMXO2-256HC";
    } else if (args.type == ArchArgs::LCMXO2_640HC) {
        return "LCMXO2-640HC";
    } else if (args.type == ArchArgs::LCMXO2_1200HC) {
        return "LCMXO2-1200HC";
    } else if (args.type == ArchArgs::LCMXO2_2000HC) {
        return "LCMXO2-2000HC";
    } else if (args.type == ArchArgs::LCMXO2_4000HC) {
        return "LCMXO2-4000HC";
    } else if (args.type == ArchArgs::LCMXO2_7000HC) {
        return "LCMXO2-7000HC";
    } else {
        log_error("Unknown chip\n");
    }
}

std::string Arch::get_full_chip_name() const
{
    std::string name = getChipName();
    name += "-";
    switch (args.speed) {
    case ArchArgs::SPEED_1:
        name += "1";
        break;
    case ArchArgs::SPEED_2:
        name += "2";
        break;
    case ArchArgs::SPEED_3:
        name += "3";
        break;
    case ArchArgs::SPEED_4:
        name += "4";
        break;
    case ArchArgs::SPEED_5:
        name += "5";
        break;
    case ArchArgs::SPEED_6:
        name += "6";
        break;
    }
    name += args.package;
    return name;
}

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::LCMXO2_256HC) {
        return id_lcmxo2_256hc;
    } else if (args.type == ArchArgs::LCMXO2_640HC) {
        return id_lcmxo2_640hc;
    } else if (args.type == ArchArgs::LCMXO2_1200HC) {
        return id_lcmxo2_1200hc;
    } else if (args.type == ArchArgs::LCMXO2_2000HC) {
        return id_lcmxo2_2000hc;
    } else if (args.type == ArchArgs::LCMXO2_4000HC) {
        return id_lcmxo2_4000hc;
    } else if (args.type == ArchArgs::LCMXO2_7000HC) {
        return id_lcmxo2_7000hc;
    }

    return IdString();
}

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
    for (int i = 0; i < loci->num_bels; i++) {
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

    const TileTypePOD *tilei = tile_info(ret);
    for (int i = 0; i < tilei->num_bels; i++) {
        if (tilei->bel_data[i].z == loc.z) {
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
    br.e.cursor_index = chip_info->tiles[y * chip_info->width + x].num_bels - 1;
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

    int num_bel_wires = tile_info(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = &*tile_info(bel)->bel_data[bel.index].bel_wires;

    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin.index) {
            WireId ret;

            ret.location.x = bel_wires[i].rel_wire_loc.x;
            ret.location.y = bel_wires[i].rel_wire_loc.y;
            ret.index = bel_wires[i].wire_index;

            return ret;
        }

    return WireId();
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = tile_info(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = &*tile_info(bel)->bel_data[bel.index].bel_wires;

    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin.index)
            return PortType(bel_wires[i].dir);

    return PORT_INOUT;
}

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = tile_info(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = &*tile_info(bel)->bel_data[bel.index].bel_wires;

    for (int i = 0; i < num_bel_wires; i++) {
        IdString id(bel_wires[i].port);
        ret.push_back(id);
    }

    return ret;
}

// ---------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const
{
    for (int i = 0; i < package_info->num_pins; i++) {
        if (package_info->pin_data[i].name.get() == pin) {
            BelId bel;
            bel.location = package_info->pin_data[i].abs_loc;
            bel.index = package_info->pin_data[i].bel_index;
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
    for (int i = 0; i < loci->num_wires; i++) {
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
    for (int i = 0; i < loci->num_pips; i++) {
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
    auto &pip_data = tile_info(pip)->pips_data[pip.index];
    WireId src = getPipSrcWire(pip), dst = getPipDstWire(pip);
    const char *src_name = tile_info(src)->wire_data[src.index].name.get();
    const char *dst_name = tile_info(dst)->wire_data[dst.index].name.get();
    std::string pip_name =
            stringf("%d_%d_%s->%d_%d_%s", pip_data.src.x - pip.location.x, pip_data.src.y - pip.location.y, src_name,
                    pip_data.dst.x - pip.location.x, pip_data.dst.y - pip.location.y, dst_name);

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
        cfg.ioBufTypes.insert(id_FACADE_IO);
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

// ---------------------------------------------------------------

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    // FIXME: Same deal as isValidBelForCell.
    return true;
}

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

bool Arch::cells_compatible(const CellInfo **cells, int count) const { return false; }

std::vector<std::pair<std::string, std::string>> Arch::get_tiles_at_location(int row, int col)
{
    std::vector<std::pair<std::string, std::string>> ret;
    auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
    for (int i = 0; i < tileloc.num_tiles; i++) {
        ret.push_back(std::make_pair(tileloc.tile_names[i].name.get(),
                                     chip_info->tiletype_names[tileloc.tile_names[i].type_idx].get()));
    }
    return ret;
}

NEXTPNR_NAMESPACE_END

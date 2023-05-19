/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021 gatecat <gatecat@ds0.me>
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

#include "arch.h"
#include "archdefs.h"
#include "chipdb.h"
#include "log.h"
#include "nextpnr.h"

#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

static const ChipInfoPOD *get_chip_info(const RelPtr<ChipInfoPOD> *ptr) { return ptr->get(); }

Arch::Arch(ArchArgs args)
{
    try {
        blob_file.open(args.chipdb);
        if (args.chipdb.empty() || !blob_file.is_open())
            log_error("Unable to read chipdb %s\n", args.chipdb.c_str());
        const char *blob = reinterpret_cast<const char *>(blob_file.data());
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(blob));
    } catch (...) {
        log_error("Unable to read chipdb %s\n", args.chipdb.c_str());
    }
    // Check consistency of blob
    if (chip_info->magic != 0x00ca7ca7)
        log_error("chipdb %s does not look like a valid himbächel database!\n", args.chipdb.c_str());
    std::string blob_uarch(chip_info->uarch.get());
    if (blob_uarch != args.uarch)
        log_error("database device uarch '%s' does not match selected device uarch '%s'.\n", blob_uarch.c_str(),
                  args.uarch.c_str());
    // Load uarch
    uarch = HimbaechelArch::create(args.uarch, args.options);
    if (!uarch) {
        std::string available = HimbaechelArch::list();
        log_error("unable to load device uarch '%s', available options: %s\n", args.uarch.c_str(), available.c_str());
    }
    uarch->init_constids(this);
    // Setup constids from database
    for (int i = 0; i < chip_info->extra_constids->bba_ids.ssize(); i++) {
        IdString::initialize_add(this, chip_info->extra_constids->bba_ids[i].get(),
                                 i + chip_info->extra_constids->known_id_count);
    }
    init_tiles();
}

void Arch::init_tiles()
{
    for (int y = 0; y < chip_info->height; y++) {
        for (int x = 0; x < chip_info->width; x++) {
            int tile = y * chip_info->width + x;
            auto &inst = chip_info->tile_insts[tile];
            IdString name = idf("%sX%dY%d", IdString(inst.name_prefix).c_str(this), x, y);
            NPNR_ASSERT(int(tile_name.size()) == tile);
            tile_name.push_back(name);
            tile_name2idx[name] = tile;
        }
    }
}

void Arch::late_init()
{
    BaseArch::init_cell_types();
    BaseArch::init_bel_buckets();
}

BelId Arch::getBelByName(IdStringList name) const
{
    NPNR_ASSERT(name.size() == 2);
    int tile = tile_name2idx.at(name[0]);
    const auto &tdata = chip_tile_info(chip_info, tile);
    for (int bel = 0; bel < tdata.bels.ssize(); bel++) {
        if (IdString(tdata.bels[bel].name) == name[1])
            return BelId(tile, bel);
    }
    return BelId();
}

IdStringList Arch::getBelName(BelId bel) const
{
    return IdStringList::concat(tile_name.at(bel.tile), IdString(chip_bel_info(chip_info, bel).name));
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    // TODO: binary search
    auto &info = chip_bel_info(chip_info, bel);
    for (auto &bel_pin : info.pins) {
        if (IdString(bel_pin.name) == pin)
            return normalise_wire(bel.tile, bel_pin.wire);
    }
    return WireId();
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    auto &info = chip_bel_info(chip_info, bel);
    for (auto &bel_pin : info.pins) {
        if (IdString(bel_pin.name) == pin)
            return PortType(bel_pin.type);
    }
    NPNR_ASSERT_FALSE("bel pin not found");
}

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> result;
    auto &info = chip_bel_info(chip_info, bel);
    result.reserve(info.pins.size());
    for (auto &bel_pin : info.pins)
        result.emplace_back(bel_pin.name);
    return result;
}

bool Arch::pack()
{
    log_break();
    uarch->pack();
    getCtx()->assignArchInfo();
    getCtx()->settings[id("pack")] = 1;
    log_info("Checksum: 0x%08x\n", getCtx()->checksum());
    return true;
}

bool Arch::place()
{
    bool retVal = false;
    uarch->prePlace();
    std::string placer = str_or_default(settings, id("placer"), defaultPlacer);
    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        uarch->configurePlacerHeap(cfg);
        cfg.ioBufTypes.insert(id("GENERIC_IOB"));
        retVal = placer_heap(getCtx(), cfg);
    } else if (placer == "sa") {
        retVal = placer1(getCtx(), Placer1Cfg(getCtx()));
    } else {
        log_error("Himbächel architecture does not support placer '%s'\n", placer.c_str());
    }
    uarch->postPlace();
    getCtx()->settings[getCtx()->id("place")] = 1;
    archInfoToAttributes();
    return retVal;
}

bool Arch::route()
{
    uarch->preRoute();
    std::string router = str_or_default(settings, id("router"), defaultRouter);
    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("Himbächel architecture does not support router '%s'\n", router.c_str());
    }
    uarch->postRoute();
    getCtx()->settings[getCtx()->id("route")] = 1;
    archInfoToAttributes();
    return result;
}

void Arch::assignArchInfo()
{
    int cell_idx = 0, net_idx = 0;
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        ci->flat_index = cell_idx++;
        for (auto &port : ci->ports) {
            // Default 1:1 cell:bel mapping
            if (!ci->cell_bel_pins.count(port.first))
                ci->cell_bel_pins[port.first].push_back(port.first);
        }
    }
    for (auto &net : nets) {
        net.second->flat_index = net_idx++;
    }
}

WireId Arch::getWireByName(IdStringList name) const
{
    NPNR_ASSERT(name.size() == 2);
    int tile = tile_name2idx.at(name[0]);
    const auto &tdata = chip_tile_info(chip_info, tile);
    for (int wire = 0; wire < tdata.wires.ssize(); wire++) {
        if (IdString(tdata.wires[wire].name) == name[1])
            return WireId(tile, wire);
    }
    return WireId();
}

IdStringList Arch::getWireName(WireId wire) const
{
    return IdStringList::concat(tile_name.at(wire.tile), IdString(chip_wire_info(chip_info, wire).name));
}

PipId Arch::getPipByName(IdStringList name) const
{
    NPNR_ASSERT(name.size() == 3);
    int tile = tile_name2idx.at(name[0]);
    const auto &tdata = chip_tile_info(chip_info, tile);
    for (int pip = 0; pip < tdata.pips.ssize(); pip++) {
        if (IdString(tdata.wires[tdata.pips[pip].dst_wire].name) == name[1] &&
            IdString(tdata.wires[tdata.pips[pip].src_wire].name) == name[2])
            return PipId(tile, pip);
    }
    return PipId();
}

IdStringList Arch::getPipName(PipId pip) const
{
    auto &tdata = chip_tile_info(chip_info, pip.tile);
    auto &pdata = tdata.pips[pip.index];
    return IdStringList::concat(tile_name.at(pip.tile),
                                IdStringList::concat(IdString(tdata.wires[pdata.dst_wire].name),
                                                     IdString(tdata.wires[pdata.src_wire].name)));
}

IdString Arch::getPipType(PipId pip) const { return IdString(); }

std::string Arch::getChipName() const { return chip_info->name.get(); }

IdString Arch::archArgsToId(ArchArgs args) const
{
    // TODO
    return IdString();
}

void IdString::initialize_arch(const BaseCtx *ctx) {}

const std::string Arch::defaultPlacer = "heap";

const std::vector<std::string> Arch::availablePlacers = {"sa", "heap"};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

NEXTPNR_NAMESPACE_END

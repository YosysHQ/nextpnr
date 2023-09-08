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
    // Select speed grade
    if (args.speed.empty()) {
        if (chip_info->speed_grades.ssize() == 0) {
            // no timing information and no speed grade specified
            speed_grade = nullptr;
        } else if (chip_info->speed_grades.ssize() == 1) {
            // speed grade not specified but only one available; use it
            speed_grade = &(chip_info->speed_grades[0]);
        } else {
            std::string available_speeds = "";
            for (const auto &speed_data : chip_info->speed_grades) {
                if (!available_speeds.empty())
                    available_speeds += ", ";
                available_speeds += IdString(speed_data.name).c_str(this);
            }
            log_error("Speed grade must be specified using --speed (available options: %s).\n",
                      available_speeds.c_str());
        }
    } else {
        for (const auto &speed_data : chip_info->speed_grades) {
            if (IdString(speed_data.name) == id(args.speed)) {
                speed_grade = &speed_data;
                break;
            }
        }
        if (!speed_grade) {
            log_error("Speed grade '%s' not found in database.\n", args.speed.c_str());
        }
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
    set_fast_pip_delays(true);
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
    set_fast_pip_delays(false);
    return result;
}

void Arch::assignArchInfo()
{
    int cell_idx = 0, net_idx = 0;
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        ci->flat_index = cell_idx++;
        if (speed_grade && ci->timing_index == -1)
            ci->timing_index = get_cell_timing_idx(ci->type);
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

void Arch::set_fast_pip_delays(bool fast_mode)
{
    if (fast_mode && !fast_pip_delays) {
        // Have to rebuild these structures
        drive_res.clear();
        load_cap.clear();
        for (auto &net : nets) {
            for (auto &wire_pair : net.second->wires) {
                PipId pip = wire_pair.second.pip;
                if (pip == PipId())
                    continue;
                auto &pip_data = chip_pip_info(chip_info, pip);
                auto pip_tmg = get_pip_timing(pip_data);
                if (pip_tmg != nullptr) {
                    WireId src = getPipSrcWire(pip), dst = getPipDstWire(pip);
                    load_cap[src] += pip_tmg->in_cap.slow_max;
                    drive_res[dst] = (((pip_tmg->flags & 1) || !drive_res.count(src)) ? 0 : drive_res.at(src)) +
                                     pip_tmg->out_res.slow_max;
                }
            }
        }
    }
    fast_pip_delays = fast_mode;
}

// Helper for cell timing lookups
namespace {
template <typename Tres, typename Tgetter, typename Tkey>
int db_binary_search(const RelSlice<Tres> &list, Tgetter key_getter, Tkey key)
{
    if (list.ssize() < 7) {
        for (int i = 0; i < list.ssize(); i++) {
            if (key_getter(list[i]) == key) {
                return i;
            }
        }
    } else {
        int b = 0, e = list.ssize() - 1;
        while (b <= e) {
            int i = (b + e) / 2;
            if (key_getter(list[i]) == key) {
                return i;
            }
            if (key_getter(list[i]) > key)
                e = i - 1;
            else
                b = i + 1;
        }
    }
    return -1;
}
} // namespace

int Arch::get_cell_timing_idx(IdString type_variant) const
{
    return db_binary_search(
            speed_grade->cell_types, [](const CellTimingPOD &ct) { return ct.type_variant; }, type_variant.index);
}

bool Arch::lookup_cell_delay(int type_idx, IdString from_port, IdString to_port, DelayQuad &delay) const
{
    NPNR_ASSERT(type_idx != -1);
    const auto &ct = speed_grade->cell_types[type_idx];
    int to_pin_idx = db_binary_search(
            ct.pins, [](const CellPinTimingPOD &pd) { return pd.pin; }, to_port.index);
    if (to_pin_idx == -1)
        return false;
    const auto &tp = ct.pins[to_pin_idx];
    int arc_idx = db_binary_search(
            tp.comb_arcs, [](const CellPinCombArcPOD &arc) { return arc.input; }, from_port.index);
    if (arc_idx == -1)
        return false;
    delay = DelayQuad(tp.comb_arcs[arc_idx].delay.fast_min, tp.comb_arcs[arc_idx].delay.slow_max);
    return true;
}

const RelSlice<CellPinRegArcPOD> *Arch::lookup_cell_seq_timings(int type_idx, IdString port) const
{
    NPNR_ASSERT(type_idx != -1);
    const auto &ct = speed_grade->cell_types[type_idx];
    int pin_idx = db_binary_search(
            ct.pins, [](const CellPinTimingPOD &pd) { return pd.pin; }, port.index);
    if (pin_idx == -1)
        return nullptr;
    return &ct.pins[pin_idx].reg_arcs;
}

TimingPortClass Arch::lookup_port_tmg_type(int type_idx, IdString port, PortType dir) const
{

    NPNR_ASSERT(type_idx != -1);
    const auto &ct = speed_grade->cell_types[type_idx];
    int pin_idx = db_binary_search(
            ct.pins, [](const CellPinTimingPOD &pd) { return pd.pin; }, port.index);
    if (pin_idx == -1)
        return (dir == PORT_OUT) ? TMG_IGNORE : TMG_COMB_INPUT;
    auto &pin = ct.pins[pin_idx];

    if (dir == PORT_IN) {
        if (pin.flags & CellPinTimingPOD::FLAG_CLK)
            return TMG_CLOCK_INPUT;
        return pin.reg_arcs.ssize() > 0 ? TMG_REGISTER_INPUT : TMG_COMB_INPUT;
    } else {
        // If a clock-to-out entry exists, then this is a register output
        return pin.reg_arcs.ssize() > 0 ? TMG_REGISTER_OUTPUT : TMG_COMB_OUTPUT;
    }
}

// TODO: adding uarch overrides for these?
bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    if (cell->timing_index == -1)
        return false;
    return lookup_cell_delay(cell->timing_index, fromPort, toPort, delay);
}
TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    if (cell->timing_index == -1)
        return TMG_IGNORE;
    auto type = lookup_port_tmg_type(cell->timing_index, port, cell->ports.at(port).type);
    clockInfoCount = 0;
    if (type == TMG_REGISTER_INPUT || type == TMG_REGISTER_OUTPUT) {
        auto reg_arcs = lookup_cell_seq_timings(cell->timing_index, port);
        if (reg_arcs)
            clockInfoCount = reg_arcs->ssize();
    }
    return type;
}
TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo result;
    NPNR_ASSERT(cell->timing_index != -1);
    auto reg_arcs = lookup_cell_seq_timings(cell->timing_index, port);
    NPNR_ASSERT(reg_arcs);
    const auto &arc = (*reg_arcs)[index];

    result.clock_port = IdString(arc.clock);
    result.edge = ClockEdge(arc.edge);
    result.setup = DelayPair(arc.setup.fast_min, arc.setup.slow_max);
    result.hold = DelayPair(arc.hold.fast_min, arc.hold.slow_max);
    result.clockToQ = DelayQuad(arc.clk_q.fast_min, arc.clk_q.slow_max);

    return result;
}

NEXTPNR_NAMESPACE_END

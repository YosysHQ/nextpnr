/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include <regex>

NEXTPNR_NAMESPACE_BEGIN

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);

#include "constids.inc"

#undef X
}

static const ChipInfoPOD *get_chip_info(const RelPtr<ChipInfoPOD> *ptr) { return ptr->get(); }

Arch::Arch(ArchArgs args) : args(args)
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
    // Setup constids from database
    for (int i = 0; i < chip_info->extra_constids->bba_ids.ssize(); i++) {
        IdString::initialize_add(this, chip_info->extra_constids->bba_ids[i].get(),
                                 i + chip_info->extra_constids->known_id_count);
    }
    // Setup family
    std::string dev_name = IdString(chip_info->name).str(this);
    if (regex_search(dev_name, std::regex("^xc7")))
        family = ArchFamily::XC7;
    else if (regex_search(dev_name, std::regex("^xczu")))
        family = ArchFamily::XCUP;
    else if (regex_search(dev_name, std::regex("^xc[akv]u\\d+p")))
        family = ArchFamily::XCUP;
    else if (regex_search(dev_name, std::regex("^xc[kv]u")))
        family = ArchFamily::XCU;
    else if (regex_search(dev_name, std::regex("^xcv[cmpeh]")))
        family = ArchFamily::VERSAL;
    else
        log_error("Unable to determine family for device '%s'\n", dev_name.c_str());
    // Setup package
    if (args.package == "") {
        if (chip_info->packages.ssize() != 1) {
            log_info("Available packages:\n");
            for (const auto &pkg : chip_info->packages)
                log("        %s\n", nameOf(IdString(pkg.name)));
            log_error("--package must be specified.\n");
        } else {
            package_info = &(chip_info->packages[0]);
        }
    } else {
        for (const auto &pkg : chip_info->packages) {
            if (id(args.package) == IdString(pkg.name)) {
                package_info = &pkg;
                break;
            }
        }
        if (!package_info)
            log_error("Package '%s' is not supported\n", args.package.c_str());
    }
    // Setup name maps
    setup_byname();
    // Setup cell types
    init_cell_types();
    BaseArch::init_bel_buckets();
}

void Arch::late_init()
{
    tile_status.reserve(chip_info->tile_insts.size());
    for (int tile = 0; tile < chip_info->tile_insts.ssize(); tile++)
        tile_status.emplace_back(getCtx(), tile);
}

IdStringList Arch::getBelName(BelId bel) const
{
    auto &info = chip_bel_info(chip_info, bel);
    if (info.site == -1)
        return IdStringList(std::array<IdString, 2>{tile_name(bel.tile), IdString(info.name)});
    else if (info.site_variant == 0)
        return IdStringList(std::array<IdString, 2>{site_name(bel.tile, info.site), IdString(info.name)});
    else
        return IdStringList(std::array<IdString, 3>{site_name(bel.tile, info.site),
                                                    site_variant_name(bel.tile, info.site, info.site_variant),
                                                    IdString(info.name)});
}

std::string Arch::getChipName() const { return IdString(chip_info->name).str(this); }

IdString Arch::archArgsToId(ArchArgs args) const { return IdString(); }

void Arch::setup_byname()
{
    for (int i = 0; i < chip_info->tile_insts.ssize(); i++) {
        tile_by_name[tile_name(i)] = i;
        for (int j = 0; j < chip_info->tile_insts[i].site_insts.ssize(); j++)
            site_by_name[site_name(i, j)] = std::make_pair(i, j);
    }
}

bool Arch::parse_name_prefix(IdStringList name, unsigned postfix_len, int *tile, int *site, int *site_variant) const
{
    if (name.size() != (postfix_len + 1) && name.size() != (postfix_len + 2))
        return false;

    if (site_by_name.count(name[0]))
        std::tie(*tile, *site) = site_by_name.at(name[0]);
    else if (tile_by_name.count(name[0])) {
        *tile = tile_by_name.at(name[0]);
        *site = -1;
    } else
        return false;
    if (name.size() == (postfix_len + 2)) {
        // With site variant
        NPNR_ASSERT(*site != -1);
        auto &site_data = chip_tile_info(chip_info, *tile).sites[*site];
        for (int i = 0; i < site_data.variant_types.ssize(); i++) {
            if (IdString(site_data.variant_types[i]) == name[1]) {
                *site_variant = i;
                return true;
            }
        }
        return false;
    } else {
        *site_variant = 0;
        return true;
    }
}

BelId Arch::getBelByName(IdStringList name) const
{
    int tile = -1, site = -1, site_variant = -1;
    if (!parse_name_prefix(name, 1, &tile, &site, &site_variant))
        return BelId();
    auto &info = chip_tile_info(chip_info, tile);
    IdString bel_name = name[name.size() - 1];
    for (int i = 0; i < info.bels.ssize(); i++) {
        auto &b = info.bels[i];
        if (IdString(b.name) == bel_name && b.site == site && (site == -1 || b.site_variant == site_variant))
            return BelId(tile, i);
    }

    return BelId();
}

IdStringList Arch::getWireName(WireId wire) const
{
    auto &info = chip_wire_info(chip_info, wire);
    if (info.site == -1)
        return IdStringList(std::array<IdString, 2>{tile_name(wire.tile), IdString(info.name)});
    else if (info.site_variant == 0)
        return IdStringList(std::array<IdString, 2>{site_name(wire.tile, info.site), IdString(info.name)});
    else
        return IdStringList(std::array<IdString, 3>{site_name(wire.tile, info.site),
                                                    site_variant_name(wire.tile, info.site, info.site_variant),
                                                    IdString(info.name)});
}

WireId Arch::getWireByName(IdStringList name) const
{
    int tile = -1, site = -1, site_variant = -1;
    if (!parse_name_prefix(name, 1, &tile, &site, &site_variant))
        return WireId();
    auto &info = chip_tile_info(chip_info, tile);
    IdString wire_name = name[name.size() - 1];
    for (int i = 0; i < info.wires.ssize(); i++) {
        auto &w = info.wires[i];
        if (IdString(w.name) == wire_name && w.site == site && (site == -1 || w.site_variant == site_variant))
            return WireId(tile, i);
    }

    return WireId();
}

IdStringList Arch::getPipName(PipId pip) const
{
    auto &info = chip_pip_info(chip_info, pip);
    auto &src_info = chip_wire_info(chip_info, WireId(pip.tile, info.src_wire));
    auto &dst_info = chip_wire_info(chip_info, WireId(pip.tile, info.dst_wire));

    if (info.site == -1)
        return IdStringList(
                std::array<IdString, 3>{tile_name(pip.tile), IdString(dst_info.name), IdString(src_info.name)});
    else if (info.site_variant == 0)
        return IdStringList(std::array<IdString, 3>{site_name(pip.tile, info.site), IdString(dst_info.name),
                                                    IdString(src_info.name)});
    else
        return IdStringList(std::array<IdString, 4>{site_name(pip.tile, info.site),
                                                    site_variant_name(pip.tile, info.site, info.site_variant),
                                                    IdString(dst_info.name), IdString(src_info.name)});
}

IdString Arch::getPipType(PipId pip) const { return IdString(); }

PipId Arch::getPipByName(IdStringList name) const
{
    int tile = -1, site = -1, site_variant = -1;
    if (!parse_name_prefix(name, 2, &tile, &site, &site_variant))
        return PipId();
    auto &info = chip_tile_info(chip_info, tile);
    IdString dst_name = name[name.size() - 2], src_name = name[name.size() - 1];
    for (int i = 0; i < info.pips.ssize(); i++) {
        auto &p = info.pips[i];
        if (IdString(info.wires[p.src_wire].name) == src_name && IdString(info.wires[p.dst_wire].name) == dst_name &&
            p.site == site && (site == -1 || p.site_variant == site_variant))
            return PipId(tile, i);
    }

    return PipId();
}

bool Arch::getBelGlobalBuf(BelId bel) const { return false; }

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
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

BoundingBox Arch::getClusterBounds(ClusterId cluster) const
{
    CellInfo *root = getClusterRootCell(cluster);
    BoundingBox bb;
    bb.x0 = bb.y0 = std::numeric_limits<int>::max();
    bb.x1 = bb.y1 = std::numeric_limits<int>::min();
    for (auto cell : root->cluster_info.cluster_cells) {
        const auto &info = cell->cluster_info;
        bb.x0 = std::min(bb.x0, info.tile_dx);
        bb.y0 = std::min(bb.y0, info.tile_dy);
        bb.x1 = std::max(bb.x1, info.tile_dx);
        bb.y1 = std::max(bb.y1, info.tile_dy);
    }
    return bb;
}

bool Arch::getClusterPlacement(ClusterId cluster, BelId root_bel,
                               std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    placement.clear();
    CellInfo *root = getClusterRootCell(cluster);
    int root_tx, root_ty;
    tile_xy(chip_info, root_bel.tile, root_tx, root_ty);
    auto &root_bel_data = chip_bel_info(chip_info, root_bel);
    NPNR_ASSERT(root_bel_data.site != -1);
    auto &root_site = chip_tile_info(chip_info, root_bel.tile).sites[root_bel_data.site];
    for (auto &cell : root->cluster_info.cluster_cells) {
        const auto &info = cell->cluster_info;
        // TODO: chains that have gaps in tile coordinates e.g. long carries
        int x = root_tx + info.tile_dx, y = root_ty + info.tile_dy;
        if (x < 0 || x >= chip_info->width)
            return false;
        if (y < 0 || y >= chip_info->height)
            return false;
        int tile = tile_by_xy(chip_info, x, y);
        // TODO: speedup bel search
        auto &tile_data = chip_tile_info(chip_info, tile);
        for (int idx = 0; idx < tile_data.bels.ssize(); idx++) {
            auto &bel_data = tile_data.bels[idx];
            if (bel_data.site == -1)
                continue;
            auto &bel_site = tile_data.sites[bel_data.site];
            if ((bel_site.dx != (root_site.dx + info.site_dx)) || (bel_site.dy != (root_site.dy + info.site_dy)))
                continue;
            if (bel_data.place_idx != ((info.type == ClusterInfo::REL_PLACE_IDX)
                                               ? (root_bel_data.place_idx + info.place_idx)
                                               : info.place_idx))
                continue;
            BelId bel(tile, idx);
            if (!isValidBelForCellType(cell->type, bel))
                return false;
            placement.emplace_back(cell, bel);
            goto found;
        }
        if (0) {
        found:
            continue;
        }
        return false;
    }
    return true;
}

void Arch::init_cell_types()
{
    pool<IdString> all_cell_types;
    for (auto &tile_type : chip_info->tile_types) {
        for (auto &bel : tile_type.bels) {
            for (auto &plc : bel.placements)
                all_cell_types.emplace(plc.cell_type);
        }
    }
    std::copy(all_cell_types.begin(), all_cell_types.end(), std::back_inserter(cell_types));
    std::sort(cell_types.begin(), cell_types.end());
}

IdString Arch::tile_name(int32_t tile) const
{
    auto &info = chip_info->tile_insts[tile];
    return id(stringf("%s_X%dY%d", IdString(info.prefix).c_str(this), info.tile_x, info.tile_y));
}

IdString Arch::site_name(int32_t tile, int32_t site) const
{
    auto &info = chip_info->tile_insts[tile].site_insts[site];
    return id(stringf("%sX%dY%d", IdString(info.site_prefix).c_str(this), info.site_x, info.site_y));
}

IdString Arch::site_variant_name(int32_t tile, int32_t site, int32_t variant) const
{
    auto &info = chip_tile_info(chip_info, tile).sites[site];
    return IdString(info.variant_types[variant]);
}

ClockRegion Arch::get_clock_region(int32_t tile) const
{
    auto &inst = chip_info->tile_insts[tile];
    return ClockRegion(inst.clock_x, inst.clock_y);
}

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    int src_x = 0, src_y = 0, dst_x = 0, dst_y = 0;
    tile_xy(chip_info, src.tile, src_x, src_y);
    tile_xy(chip_info, dst.tile, dst_x, dst_y);
    return 100 + 25 * std::abs(dst_x - src_x) + 50 * std::abs(dst_y - src_y);
}

delay_t Arch::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    int src_x = 0, src_y = 0, dst_x = 0, dst_y = 0;
    tile_xy(chip_info, src_bel.tile, src_x, src_y);
    tile_xy(chip_info, dst_bel.tile, dst_x, dst_y);
    return 100 + 25 * std::abs(dst_x - src_x) + 50 * std::abs(dst_y - src_y);
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

BoundingBox Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    int src_x = 0, src_y = 0, dst_x = 0, dst_y = 0;
    tile_xy(chip_info, src.tile, src_x, src_y);
    tile_xy(chip_info, dst.tile, dst_x, dst_y);
    BoundingBox result;
    result.x0 = std::min(src_x, dst_x);
    result.x1 = std::max(src_x, dst_x);
    result.y0 = std::min(src_y, dst_y);
    result.y1 = std::max(src_y, dst_y);
    return result;
}

void Arch::find_specimen_bels()
{
    pool<IdString> cell_types;
    specimen_bels.clear();
    for (auto &cell : cells)
        cell_types.insert(cell.second->type);
    for (auto bel : getBels()) {
        if (specimen_bels.size() == cell_types.size())
            continue;
        for (IdString typ : cell_types)
            if (!specimen_bels.count(typ) && isValidBelForCellType(typ, bel))
                specimen_bels[typ] = bel;
    }
    for (auto &cell : cells)
        if (!specimen_bels.count(cell.second->type))
            log_error("No possible placements found for cell '%s' of type type '%s'\n", getCtx()->nameOf(cell.first),
                      getCtx()->nameOf(cell.second->type));
}

void Arch::update_cell_bel_pins(CellInfo *cell)
{
    cell->cell_bel_pins.clear();
    if (cell->type == id_PAD) {
        // Special case
        cell->cell_bel_pins[id_PAD].push_back(id_PAD);
        return;
    } else if (cell->type == id_VCC) {
        cell->cell_bel_pins[id_P].push_back(id_P);
        return;
    } else if (cell->type == id_GND) {
        cell->cell_bel_pins[id_G].push_back(id_G);
        return;
    }

    // For pins tied to constants, add a hidden logical pin that's ignored for netlist purposes but allows us to route
    // the constant
    auto is_invertible = [&](IdString phys_pin) {
        WireId wire = getBelPinWire(cell->bel, phys_pin);
        for (auto pip : getPipsUphill(wire)) {
            const auto &data = chip_pip_info(chip_info, pip);
            if ((data.flags & PipDataPOD::FLAG_CAN_INV) || (data.flags & PipDataPOD::FLAG_FIXED_INV))
                return true;
        }
        return false;
    };
    auto add_phys_const = [&](IdString phys_pin, bool value) {
        // Only add constant ties when we're creating concrete assignments
        if (cell->bel == BelId())
            return;
        // Invertible pins are always connected physically to LOGIC1 and the inversion bit set
        bool tie_value = value || is_invertible(phys_pin);
        IdString log_port_name = tie_value ? id__TIED_1 : id__TIED_0;
        if (!cell->ports.count(log_port_name)) {
            cell->addInput(log_port_name);
            connectPort(tie_value ? id_GLOBAL_LOGIC1 : id_GLOBAL_LOGIC0, cell->name, log_port_name);
        }
        cell->cell_bel_pins[log_port_name].push_back(phys_pin);
    };

    // If cell doesn't have a concrete bel yet, use a specimen location to provide a preliminary assignment
    BelId bel = (cell->bel != BelId()) ? cell->bel : specimen_bels.at(cell->type);
    auto &bel_data = chip_bel_info(chip_info, bel);

    // Fractured pure (i.e. not memory/SRL) LUTs are a special case as we need to pick a non-overlapping mapping, and
    // add a VCC tie
    if (getBelBucketForCellType(cell->type) == id_LUT && !cell->lutInfo.is_memory && !cell->lutInfo.is_srl &&
        cell->bel != BelId()) {
        auto lut_status = tile_status.at(cell->bel.tile).get_lut_status(cell->bel);
        if (lut_status.is_fractured) {
            // TODO: is Versal a special case due to not requiring A6=VCC any more?
            static const std::array<IdString, 6> phys_pins{id_A1, id_A2, id_A3, id_A4, id_A5, id_A6};
            LogicBelIdx bel_idx(bel_data.place_idx);
            for (auto &port : cell->ports) {
                if (port.first == id__TIED_0 || port.first == id__TIED_1)
                    continue;
                if (port.second.type == PORT_OUT) {
                    if (port.first == id_GE || port.first == id_PROP) {
                        // Versal carry LUT outputs
                        cell->cell_bel_pins[port.first].push_back(port.first);
                    } else {
                        // General LUT output
                        NPNR_ASSERT(port.first == id_O);
                        cell->cell_bel_pins[port.first].push_back((bel_idx.bel() == LogicBelIdx::LUT6) ? id_O6 : id_O5);
                    }
                } else {
                    const NetInfo *ni = port.second.net;
                    if (!ni)
                        cell->cell_bel_pins[port.first]; // TODO: floating LUT input?
                    else
                        cell->cell_bel_pins[port.first].push_back(phys_pins.at(lut_status.net2input.at(ni->name)));
                }
            }
            // LUT6 also has A6 tied high in the physical netlist only
            if (bel_idx.bel() == LogicBelIdx::LUT6)
                add_phys_const(id_A6, true);
            return;
        }
    }

    bool found = false;
    //#define DEBUG_PARAMS
    for (auto &plc : bel_data.placements) {
        if (IdString(plc.cell_type) != cell->type)
            continue;
        const PinMapPOD &pin_map = chip_info->pin_maps[plc.pin_map_idx];
        // Apply common pins
        for (auto &entry : pin_map.common_pins) {
            IdString log_pin(entry.log_pin);
            for (auto &phys : entry.phys_pins) {
                if (log_pin == id_VCC || log_pin == id_GND)
                    add_phys_const(IdString(phys), (log_pin == id_VCC));
                else
                    cell->cell_bel_pins[log_pin].emplace_back(phys);
            }
        }
        // Apply matching param-dependent pins
        for (auto &param_map : pin_map.param_pins) {
            bool matched = true;
            for (auto &match : param_map.param_matches) {
                IdString key(match.key);
                if (!cell->params.count(key)) {
#ifdef DEBUG_PARAMS
                    log_info("    %s missing %s=%s!\n", cell->name.c_str(this), key.c_str(this),
                             IdString(match.value).c_str(this));
#endif
                    matched = false;
                    break;
                }
                auto &param_entry = cell->params.at(key);
                std::string cell_value =
                        param_entry.is_string ? param_entry.as_string() : std::to_string(param_entry.as_int64());
#ifdef DEBUG_PARAMS
                log_info("    %s check %s ours=%s match=%s!\n", cell->name.c_str(this), key.c_str(this),
                         cell_value.c_str(), IdString(match.value).c_str(this));
#endif
                if (cell_value != IdString(match.value).str(this)) {
                    matched = false;
                    break;
                }
            }
            if (!matched)
                continue;
#ifdef DEBUG_PARAMS
            log_info("    %s matched!\n", cell->name.c_str(this));
#endif
            // Matched, apply param-dependent pins too
            for (auto &entry : param_map.pins) {
                IdString log_pin(entry.log_pin);
                for (auto &phys : entry.phys_pins) {
#ifdef DEBUG_PARAMS
                    log_info("    add mapping %s->%s\n", log_pin.c_str(this), IdString(phys).c_str(this));
#endif
                    if (log_pin == id_VCC || log_pin == id_GND)
                        add_phys_const(IdString(phys), (log_pin == id_VCC));
                    else
                        cell->cell_bel_pins[log_pin].emplace_back(phys);
                }
            }
        }
        found = true;
        break;
    }
    NPNR_ASSERT(found);
    // If we have previously created tied-0/tied-1 pins that are now redundant, add a null mapping
    if (cell->ports.count(id__TIED_0) && !cell->cell_bel_pins.count(id__TIED_0))
        cell->cell_bel_pins[id__TIED_0];
    if (cell->ports.count(id__TIED_1) && !cell->cell_bel_pins.count(id__TIED_1))
        cell->cell_bel_pins[id__TIED_1];
    if (cell->type == id_CARRY8) {
        // Some special cases for carries
        static const std::string x_pins = "ABCDEFGH";
        auto &ci_pins = cell->cell_bel_pins[id_CI];
        if (cell->carryInfo.ci_using_ax) {
            ci_pins.clear();
            ci_pins.push_back(id_AX);
        } else {
            NetInfo *ci = cell->getPort(id_CI);
            if (ci && (ci->name == id_GLOBAL_LOGIC0 || ci->name == id_GLOBAL_LOGIC1))
                ci_pins.clear();
        }
        for (int i = 0; i < 8; i++) {
            auto &di_pins = cell->cell_bel_pins[id(stringf("DI[%d]", i))];
            di_pins.clear();
            if (cell->carryInfo.di_using_x[i])
                di_pins.push_back(id(stringf("%cX", x_pins.at(i))));
            else
                di_pins.push_back(id(stringf("DI%d", i)));
        }
        if (str_or_default(cell->params, id_CARRY_TYPE, "SINGLE_CY8") == "SINGLE_CY8") {
            // CI_TOP not used
            const NetInfo *ci_top = cell->getPort(id_CI_TOP);
            NPNR_ASSERT(!ci_top || ci_top->name == id_GLOBAL_LOGIC0);
            if (cell->cell_bel_pins.count(id_CI_TOP))
                cell->cell_bel_pins[id_CI_TOP].clear();
        }
    }
    // Make sure we never have missing logical pins, even if the bel pins set is empty
    for (const auto &log : cell->ports)
        if (!cell->cell_bel_pins.count(log.first))
            cell->cell_bel_pins.emplace(log.first, {});
}

bool Arch::pack()
{
    apply_transforms();
    expand_macros();
    pack_io();
    pack_constants();
    pack_logic();
    pack_bram();
    assignArchInfo();
    return true;
}

bool Arch::place()
{
    std::string placer = str_or_default(settings, id("placer"), defaultPlacer);

    // Use specimen bels for a preliminary cell->bel pin mapping, as we need meaningful bel pins for placer delay
    // prediction
    find_specimen_bels();
    for (auto &cell : cells)
        update_cell_bel_pins(cell.second.get());
    preplace_globals();

    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.cellGroups.emplace_back();
        cfg.cellGroups.back().insert(id_LUT);
        cfg.cellGroups.back().insert(id_FF);
        cfg.cellGroups.back().insert(id_CARRY4);
        cfg.cellGroups.back().insert(id_CARRY8);
        cfg.beta = 0.5;
        cfg.criticalityExponent = 7;
        cfg.placeAllAtOnce = true;
        if (!placer_heap(getCtx(), cfg))
            return false;
    } else if (placer == "sa") {
        if (!placer1(getCtx(), Placer1Cfg(getCtx())))
            return false;
    } else {
        log_error("xilinx_interchange architecture does not support placer '%s'\n", placer.c_str());
    }

    getCtx()->attrs[getCtx()->id("step")] = std::string("place");
    archInfoToAttributes();
    return true;
}

bool Arch::route()
{
    for (auto &tile : tile_status)
        for (auto &site : tile.sites)
            if (site.logic)
                site.logic->update_lut_inputs();
    for (auto &cell : cells)
        update_cell_bel_pins(cell.second.get());
    assign_budget(getCtx(), true);

    route_globals();

    std::string router = str_or_default(settings, id("router"), defaultRouter);
    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        Router2Cfg cfg(getCtx());
        cfg.perf_profile = true;
        router2(getCtx(), cfg);
        result = true;
    } else {
        log_error("xilinx_interchange architecture does not support router '%s'\n", router.c_str());
    }
    getCtx()->attrs[getCtx()->id("step")] = std::string("route");
    archInfoToAttributes();
    return result;
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

const std::string Arch::defaultRouter = "router2";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

NEXTPNR_NAMESPACE_END

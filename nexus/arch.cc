/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  gatecat <gatecat@ds0.me>
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
    auto package_end = args.device.find_last_of("0123456789", args.device.substr(args.device.size() - 3) == "ES2"
                                                                      ? args.device.size() - 3
                                                                      : std::string::npos);
    if (package_end == std::string::npos || package_end < last_sep)
        log_error("Unknown device string '%s' (expected device name like 'LIFCL-40-8SG72C')\n", args.device.c_str());
    package = args.device.substr(last_sep + 2, (package_end - (last_sep + 2)) + 1);
    rating = args.device.substr(package_end + 1);

    // Check for 'ES' part
    if (rating.size() > 1 && rating.substr(1) == "ES") {
        variant = "ES";
    } else if (rating.size() > 1 && rating.substr(1) == "ES2") {
        // ES2 devices are production-equivalent from nextpnr's and bitstream point of view
        variant = "";
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
    for (auto &chip : db->chips) {
        if (chip.device_name.get() == device) {
            chip_info = &chip;
            break;
        }
    }
    if (!chip_info)
        log_error("Unknown device '%s'.\n", device.c_str());
    // Set up bba IdStrings
    for (size_t i = 0; i < db->ids->bba_id_strs.size(); i++) {
        IdString::initialize_add(this, db->ids->bba_id_strs[i].get(), uint32_t(i) + db->ids->num_file_ids);
    }
    // Set up validity structures
    tileStatus.resize(chip_info->grid.size());
    for (size_t i = 0; i < chip_info->grid.size(); i++) {
        tileStatus[i].boundcells.resize(db->loctypes[chip_info->grid[i].loc_type].bels.size());
    }
    // This structure is needed for a fast getBelByLocation because bels can have an offset
    for (size_t i = 0; i < chip_info->grid.size(); i++) {
        auto &loc = db->loctypes[chip_info->grid[i].loc_type];
        for (unsigned j = 0; j < loc.bels.size(); j++) {
            auto &bel = loc.bels[j];
            int rel_bel_tile;
            if (!rel_tile(i, bel.rel_x, bel.rel_y, rel_bel_tile))
                continue;
            auto &ts = tileStatus.at(rel_bel_tile);
            if (int(ts.bels_by_z.size()) <= bel.z)
                ts.bels_by_z.resize(bel.z + 1);
            ts.bels_by_z[bel.z].tile = i;
            ts.bels_by_z[bel.z].index = j;
        }
        auto &ts = tileStatus.at(i);
        ts.boundwires.resize(loc.wires.size());
        ts.boundpips.resize(loc.pips.size());
    }

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
    init_cell_pin_data();
    // Validate and set up package
    package_idx = -1;
    for (size_t i = 0; i < chip_info->packages.size(); i++) {
        if (package == chip_info->packages[i].short_name.get()) {
            package_idx = i;
            break;
        }
    }
    if (package_idx == -1) {
        std::string all_packages = "";
        for (auto &pkg : chip_info->packages) {
            all_packages += " ";
            all_packages += pkg.short_name.get();
        }
        log_error("Unknown package '%s'. Available package options:%s\n", package.c_str(), all_packages.c_str());
    }

    // Validate and set up speed grade

    // Convert speed to speed grade (TODO: low power back bias mode too)
    if (speed == "7")
        speed = "10";
    else if (speed == "8")
        speed = "11";
    else if (speed == "9")
        speed = "12";

    speed_grade = nullptr;
    for (auto &sg : db->speed_grades) {
        if (sg.name.get() == speed) {
            speed_grade = &sg;
            break;
        }
    }
    if (!speed_grade)
        log_error("Unknown speed grade '%s'.\n", speed.c_str());

    BaseArch::init_cell_types();
    BaseArch::init_bel_buckets();

    if (device == "LIFCL-17") {
        for (BelId bel : getBelsByTile(37, 10)) {
            // These pips currently don't work, due to routing differences between the variants that the DB format needs
            // some tweaks to accomodate properly
            if (getBelType(bel) != id_DCC)
                continue;
            WireId w = getBelPinWire(bel, id_CLKI);
            for (auto pip : getPipsUphill(w))
                disabled_pips.insert(pip);
        }
        // TODO: find a better solution to disable these
        WireId dcs_out =
                getWireByName(IdStringList(std::array<IdString, 3>{x_ids.at(37), y_ids.at(10), id_JDCSOUT_DCS_DCSIP}));
        for (auto dcs_pip : getPipsUphill(dcs_out))
            disabled_pips.insert(dcs_pip);
        NPNR_ASSERT(disabled_pips.size() == 6);
    }
}

void Arch::list_devices()
{
    std::vector<std::string> families{
            "LIFCL",
    };
    log("Supported devices: \n");
    for (auto fam : families) {
        std::string chipdb = stringf("nexus/chipdb-%s.bin", fam.c_str());
        auto db_ptr = reinterpret_cast<const RelPtr<DatabasePOD> *>(get_chipdb(chipdb));
        if (!db_ptr)
            continue; // chipdb not available
        // enumerate chips
        for (auto &chip : db_ptr->get()->chips) {
            // enumerate packages
            for (auto &pkg : chip.packages) {
                // enumerate suffices
                for (auto speedgrade : {"7", "8", "9"}) { // TODO: these might depend on family
                    for (auto rating : {"I", "C"}) {
                        for (auto suffix : {"", "ES", "ES2"}) {
                            log("    %s-%s%s%s%s\n", chip.device_name.get(), speedgrade, pkg.short_name.get(), rating,
                                suffix);
                        }
                    }
                }
            }
        }
    }
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const { return args.device; }
IdString Arch::archArgsToId(ArchArgs args) const { return id(args.device); }

// -----------------------------------------------------------------------

BelId Arch::getBelByName(IdStringList name) const
{
    if (name.size() != 3)
        return BelId();
    int x = id_to_x.at(name[0]);
    int y = id_to_y.at(name[1]);
    NPNR_ASSERT(x >= 0 && x < chip_info->width);
    NPNR_ASSERT(y >= 0 && y < chip_info->height);
    auto &tile = db->loctypes[chip_info->grid[y * chip_info->width + x].loc_type];
    for (size_t i = 0; i < tile.bels.size(); i++) {
        if (tile.bels[i].name == name[2].index) {
            BelId ret;
            ret.tile = y * chip_info->width + x;
            ret.index = i;
            return ret;
        }
    }
    return BelId();
}

std::vector<BelId> Arch::getBelsByTile(int x, int y) const
{
    std::vector<BelId> bels;
    for (auto bel : tileStatus.at(y * chip_info->width + x).bels_by_z)
        if (bel != BelId())
            bels.push_back(bel);
    return bels;
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    // Binary search on wire IdString, by ID
    int num_bel_wires = bel_data(bel).ports.size();
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
    int num_bel_wires = bel_data(bel).ports.size();
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
    for (auto &p : bel_data(bel).ports)
        ret.push_back(IdString(p.port));
    return ret;
}

std::vector<std::pair<IdString, std::string>> Arch::getBelAttrs(BelId bel) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    ret.emplace_back(id_INDEX, stringf("%d", bel.index));

    ret.emplace_back(id_GRID_X, stringf("%d", bel.tile % chip_info->width));
    ret.emplace_back(id_GRID_Y, stringf("%d", bel.tile / chip_info->width));
    ret.emplace_back(id_BEL_Z, stringf("%d", bel_data(bel).z));

    ret.emplace_back(id_BEL_TYPE, nameOf(getBelType(bel)));

    return ret;
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdStringList name) const
{
    if (name.size() != 3)
        return WireId();
    int x = id_to_x.at(name[0]);
    int y = id_to_y.at(name[1]);
    NPNR_ASSERT(x >= 0 && x < chip_info->width);
    NPNR_ASSERT(y >= 0 && y < chip_info->height);
    auto &tile = db->loctypes[chip_info->grid[y * chip_info->width + x].loc_type];
    for (size_t i = 0; i < tile.wires.size(); i++) {
        if (tile.wires[i].name == name[2].index) {
            WireId ret;
            ret.tile = y * chip_info->width + x;
            ret.index = i;
            return ret;
        }
    }
    return WireId();
}

std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    ret.emplace_back(id_INDEX, stringf("%d", wire.index));

    ret.emplace_back(id_GRID_X, stringf("%d", wire.tile % chip_info->width));
    ret.emplace_back(id_GRID_Y, stringf("%d", wire.tile / chip_info->width));
    ret.emplace_back(id_FLAGS, stringf("%u", wire_data(wire).flags));

    return ret;
}

IdString Arch::getWireType(WireId wire) const
{
    IdString basename(wire_data(wire).name);
    const std::string &basename_str = basename.str(this);
    // Interconnect - derive a type
    if ((basename_str[0] == 'H' || basename_str[0] == 'V') && basename_str[1] == '0')
        return id(basename_str.substr(0, 4));
    else
        return id_GENERAL;
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdStringList name) const
{
    if (name.size() != 5)
        return PipId();
    int x = id_to_x.at(name[0]);
    int y = id_to_y.at(name[1]);
    NPNR_ASSERT(x >= 0 && x < chip_info->width);
    NPNR_ASSERT(y >= 0 && y < chip_info->height);
    PipId ret;
    ret.tile = y * chip_info->width + x;
    ret.index = std::stoi(name[2].str(this));
    return ret;
}

IdStringList Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());
    std::array<IdString, 5> ids{x_ids.at(pip.tile % chip_info->width), y_ids.at(pip.tile / chip_info->width),
                                idf("%d", pip.index), IdString(loc_data(pip).wires[pip_data(pip).to_wire].name),
                                IdString(loc_data(pip).wires[pip_data(pip).from_wire].name)};
    return IdStringList(ids);
}

IdString Arch::getPipType(PipId pip) const { return IdString(); }

std::vector<std::pair<IdString, std::string>> Arch::getPipAttrs(PipId pip) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    ret.emplace_back(id_INDEX, stringf("%d", pip.index));

    ret.emplace_back(id_GRID_X, stringf("%d", pip.tile % chip_info->width));
    ret.emplace_back(id_GRID_Y, stringf("%d", pip.tile / chip_info->width));

    ret.emplace_back(id_FROM_TILE_WIRE, nameOf(IdString(loc_data(pip).wires[pip_data(pip).from_wire].name)));
    ret.emplace_back(id_TO_TILE_WIRE, nameOf(IdString(loc_data(pip).wires[pip_data(pip).to_wire].name)));

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

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    auto lookup_port = [&](IdString p) {
        auto fnd = cell->tmg_portmap.find(p);
        return fnd == cell->tmg_portmap.end() ? p : fnd->second;
    };
    if (cell->type == id_OXIDE_COMB) {
        if (cell->lutInfo.is_carry) {
            bool result = lookup_cell_delay(cell->tmg_index, lookup_port(fromPort), lookup_port(toPort), delay);
            // Because CCU2 = 2x OXIDE_COMB
            if (result && fromPort == id_FCI && toPort == id_FCO) {
                delay = DelayQuad(delay.minDelay() / 2, delay.maxDelay() / 2);
            }
            return result;
        } else {
            if (toPort.in(id_F, id_OFX))
                return lookup_cell_delay(cell->tmg_index, fromPort, toPort, delay);
        }
    } else if (is_dsp_cell(cell)) {
        if (fromPort == id_CLK)
            return false; // don't include delays that are actually clock-to-out here
        return lookup_cell_delay(cell->tmg_index, lookup_port(fromPort), lookup_port(toPort), delay);
    } else if (cell->type == id_DCS) {
        if (fromPort.in(id_SELFORCE, id_SEL)) {
            return false;
        }
        int index = get_cell_timing_idx(id_DCS, id_DCS);
        return lookup_cell_delay(index, fromPort, toPort, delay);
    } else if (cell->type == id_DCC) {
        if (fromPort == id_CLKI && toPort == id_CLKO) {
            // TODO: Use actual DCC delays
            delay.rise.min_delay = 1;
            delay.rise.max_delay = 1;
            delay.fall.min_delay = 1;
            delay.fall.max_delay = 1;
            return true;
        }
    }
    return false;
}

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    auto disconnected = [cell](IdString p) { return !cell->ports.count(p) || cell->ports.at(p).net == nullptr; };
    auto lookup_port = [&](IdString p) {
        auto fnd = cell->tmg_portmap.find(p);
        return fnd == cell->tmg_portmap.end() ? p : fnd->second;
    };
    clockInfoCount = 0;
    if (cell->type == id_OXIDE_COMB) {
        if (port.in(id_A, id_B, id_C, id_D, id_SEL, id_F1, id_FCI, id_WDI))
            return TMG_COMB_INPUT;
        if (port.in(id_F, id_OFX, id_FCO)) {
            if (disconnected(id_A) && disconnected(id_B) && disconnected(id_C) && disconnected(id_D) &&
                disconnected(id_FCI) && disconnected(id_SEL) && disconnected(id_WDI))
                return TMG_IGNORE;
            else
                return TMG_COMB_OUTPUT;
        }
    } else if (cell->type == id_OXIDE_FF) {
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        else if (port == id_Q) {
            clockInfoCount = 1;
            return TMG_REGISTER_OUTPUT;
        } else {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        }
    } else if (cell->type == id_RAMW) {
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        else if (port.in(id_WDO0, id_WDO1, id_WDO2, id_WDO3)) {
            clockInfoCount = 1;
            return TMG_REGISTER_OUTPUT;
        } else if (port.in(id_A0, id_A1, id_B0, id_B1, id_C0, id_C1, id_D0, id_D1)) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        }
    } else if (cell->type == id_OXIDE_EBR) {
        if (port.in(id_DWS0, id_DWS1, id_DWS2, id_DWS3, id_DWS4))
            return TMG_IGNORE;
        if (port.in(id_CLKA, id_CLKB))
            return TMG_CLOCK_INPUT;
        clockInfoCount = 1;
        return (cell->ports.at(port).type == PORT_IN) ? TMG_REGISTER_INPUT : TMG_REGISTER_OUTPUT;
    } else if (cell->type == id_LRAM_CORE) {
        if (port.in(id_OPCGLDCK, id_OPCGLOADCLK, id_SCANCLK, id_SCANRST, id_TBISTN, id_INITN, id_STDBYN, id_IGN,
                    id_DPS))
            return TMG_IGNORE;
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        clockInfoCount = 1;
        return (cell->ports.at(port).type == PORT_IN) ? TMG_REGISTER_INPUT : TMG_REGISTER_OUTPUT;
    } else if (cell->type.in(id_MULT18_CORE, id_MULT18X36_CORE, id_MULT36_CORE)) {
        return (cell->ports.at(port).type == PORT_IN) ? TMG_COMB_INPUT : TMG_COMB_OUTPUT;
    } else if (cell->type.in(id_PREADD9_CORE, id_REG18_CORE, id_MULT9_CORE)) {
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        auto type = lookup_port_type(cell->tmg_index, lookup_port(port), cell->ports.at(port).type, id_CLK);
        if (type == TMG_REGISTER_INPUT || type == TMG_REGISTER_OUTPUT)
            clockInfoCount = 1;
        return type;
    } else if (cell->type == id_DCC) {
        if (port == id_CLKI)
            return TMG_COMB_INPUT;
        else if (port == id_CLKO)
            return TMG_COMB_OUTPUT;
    } else if (cell->type == id_DCS) {
        // FIXME: Making inputs TMG_CLOCK_INPUT and the output TMG_CLOCK_GEN
        // yielded in error in the timing analyzer. For now keep those as
        // regular combinational ports.
        if (port.in(id_CLK0, id_CLK1))
            return TMG_COMB_INPUT;
        else if (port == id_DCSOUT) {
            return TMG_COMB_OUTPUT;
        }
        return TMG_IGNORE;
    }
    return TMG_IGNORE;
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    auto lookup_port = [&](IdString p) {
        auto fnd = cell->tmg_portmap.find(p);
        return fnd == cell->tmg_portmap.end() ? p : fnd->second;
    };
    TimingClockingInfo info;
    if (cell->type == id_OXIDE_FF) {
        info.edge = (cell->ffInfo.ctrlset.clkmux == ID_INV) ? FALLING_EDGE : RISING_EDGE;
        info.clock_port = id_CLK;
        if (port == id_Q)
            NPNR_ASSERT(lookup_cell_delay(cell->tmg_index, id_CLK, port, info.clockToQ));
        else
            lookup_cell_setuphold(cell->tmg_index, port, id_CLK, info.setup, info.hold);
    } else if (cell->type == id_RAMW) {
        info.edge = (cell->ffInfo.ctrlset.clkmux == ID_INV) ? FALLING_EDGE : RISING_EDGE;
        info.clock_port = id_CLK;
        if (port.in(id_WDO0, id_WDO1, id_WDO2, id_WDO3))
            NPNR_ASSERT(lookup_cell_delay(cell->tmg_index, id_CLK, port, info.clockToQ));
        else
            lookup_cell_setuphold(cell->tmg_index, port, id_CLK, info.setup, info.hold);
    } else if (cell->type == id_OXIDE_EBR) {
        if (cell->ports.at(port).type == PORT_IN) {
            lookup_cell_setuphold_clock(cell->tmg_index, lookup_port(port), info.clock_port, info.setup, info.hold);
        } else {
            lookup_cell_clock_out(cell->tmg_index, lookup_port(port), info.clock_port, info.clockToQ);
        }
        // Lookup edge based on inversion
        info.edge = (get_cell_pinmux(cell, info.clock_port) == PINMUX_INV) ? FALLING_EDGE : RISING_EDGE;
    } else if (cell->type == id_LRAM_CORE) {
        info.clock_port = id_CLK;
        if (cell->ports.at(port).type == PORT_IN) {
            lookup_cell_setuphold(cell->tmg_index, lookup_port(port), id_CLK, info.setup, info.hold);
        } else {
            NPNR_ASSERT(lookup_cell_delay(cell->tmg_index, id_CLK, lookup_port(port), info.clockToQ));
        }
        info.edge = (get_cell_pinmux(cell, info.clock_port) == PINMUX_INV) ? FALLING_EDGE : RISING_EDGE;
    } else if (cell->type.in(id_PREADD9_CORE, id_REG18_CORE, id_MULT9_CORE)) {
        info.clock_port = id_CLK;
        if (cell->ports.at(port).type == PORT_IN) {
            lookup_cell_setuphold(cell->tmg_index, lookup_port(port), id_CLK, info.setup, info.hold);
        } else {
            NPNR_ASSERT(lookup_cell_delay(cell->tmg_index, id_CLK, lookup_port(port), info.clockToQ));
        }
        info.edge = (get_cell_pinmux(cell, info.clock_port) == PINMUX_INV) ? FALLING_EDGE : RISING_EDGE;
    } else {
        NPNR_ASSERT_FALSE("missing clocking info");
    }
    return info;
}

// -----------------------------------------------------------------------

delay_t Arch::getRipupDelayPenalty() const { return 250; }

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    const auto &dst_data = wire_data(dst);
    if (src.tile == 0 && dst_data.name == ID_LOCAL_VCC)
        return 0;
    int src_x = src.tile % chip_info->width, src_y = src.tile / chip_info->width;
    int dst_x = dst.tile % chip_info->width, dst_y = dst.tile / chip_info->width;
    int dist_x = std::abs(src_x - dst_x);
    int dist_y = std::abs(src_y - dst_y);

    return estimate_delay_mult * (dist_x + dist_y) + 250;
}
delay_t Arch::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    NPNR_UNUSED(src_pin);
    if (dst_pin == id_FCI)
        return 0;
    int src_x = src_bel.tile % chip_info->width, src_y = src_bel.tile / chip_info->width;

    int dst_x = dst_bel.tile % chip_info->width, dst_y = dst_bel.tile / chip_info->width;
    int dist_x = std::abs(src_x - dst_x);
    int dist_y = std::abs(src_y - dst_y);
    return 100 * dist_x + 100 * dist_y + 250;
}

BoundingBox Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    BoundingBox bb;

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

    if (dsp_wires.count(src) || dsp_wires.count(dst)) {
        bb.x0 = std::max<int>(0, bb.x0 - 6);
        bb.x1 = std::min<int>(chip_info->width, bb.x1 + 6);
    }
    if (lram_wires.count(src) || lram_wires.count(dst)) {
        bb.y0 = std::max<int>(0, bb.y0 - 7);
        bb.y1 = std::min<int>(chip_info->width, bb.y1 + 7);
    }

    return bb;
}

// -----------------------------------------------------------------------

bool Arch::place()
{
    estimate_delay_mult = 75;
    if (getCtx()->settings.count(getCtx()->id("estimate-delay-mult")))
        estimate_delay_mult = getCtx()->setting<int>("estimate-delay-mult");

    std::string placer = str_or_default(settings, id_placer, defaultPlacer);

    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.ioBufTypes.insert(id_SEIO33_CORE);
        cfg.ioBufTypes.insert(id_SEIO18_CORE);
        cfg.ioBufTypes.insert(id_OSC_CORE);
        cfg.cellGroups.emplace_back();
        cfg.cellGroups.back().insert({id_OXIDE_COMB});
        cfg.cellGroups.back().insert({id_OXIDE_FF});

        cfg.beta = 0.5;
        cfg.criticalityExponent = 7;
        if (!placer_heap(getCtx(), cfg))
            return false;
    } else if (placer == "sa") {
        if (!placer1(getCtx(), Placer1Cfg(getCtx())))
            return false;
    } else {
        log_error("Nexus architecture does not support placer '%s'\n", placer.c_str());
    }

    post_place_opt();

    getCtx()->attrs[id_step] = std::string("place");
    archInfoToAttributes();
    return true;
}

void Arch::pre_routing()
{
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type.in(id_MULT9_CORE, id_PREADD9_CORE, id_MULT18_CORE, id_MULT18X36_CORE, id_MULT36_CORE,
                        id_REG18_CORE, id_ACC54_CORE)) {
            for (auto &port : ci->ports) {
                WireId wire = getBelPinWire(ci->bel, port.first);
                if (wire != WireId())
                    dsp_wires.insert(wire);
            }
        }
        if (ci->type == id_LRAM_CORE) {
            for (auto &port : ci->ports) {
                WireId wire = getBelPinWire(ci->bel, port.first);
                if (wire != WireId())
                    lram_wires.insert(wire);
            }
        }
    }
}
namespace {
float router2_base_cost(Context *ctx, WireId wire, PipId pip, float crit_weight)
{
    (void)crit_weight; // unused
    if (pip != PipId()) {
        auto &data = ctx->pip_data(pip);
        if (data.flags & PIP_ZERO_RR_COST)
            return 1e-12;
        if (data.flags & PIP_DRMUX_C)
            return 1e15;
    }
    return ctx->getDelayNS(ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(wire).maxDelay() +
                           ctx->getDelayEpsilon());
}
} // namespace

bool Arch::route()
{
    pre_routing();

    route_globals();

    std::string router = str_or_default(settings, id_router, defaultRouter);
    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        Router2Cfg cfg(getCtx());
        cfg.get_base_cost = router2_base_cost;
        router2(getCtx(), cfg);
        result = true;
    } else {
        log_error("Nexus architecture does not support router '%s'\n", router.c_str());
    }
    getCtx()->attrs[id_step] = std::string("route");
    archInfoToAttributes();
    return result;
}

// -----------------------------------------------------------------------

CellPinMux Arch::get_cell_pinmux(const CellInfo *cell, IdString pin) const
{
    IdString param = idf("%sMUX", pin.c_str(this));
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
    IdString param = idf("%sMUX", pin.c_str(this));
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
    for (auto &pad : chip_info->pads) {
        if (pin == pad.pins[package_idx].get())
            return &pad;
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
    if (pad == nullptr || pad->offset == -1)
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
    for (auto &pad : chip_info->pads) {
        if (pad.side == side && pad.offset == offset && pad.pio_index == loc.z)
            return &pad;
    }
    return nullptr;
}

std::string Arch::get_pad_functions(const PadInfoPOD *pad) const
{
    std::string s;
    for (auto f : pad->func_strs) {
        if (!s.empty())
            s += '/';
        s += IdString(f).str(this);
    }
    return s;
}

// -----------------------------------------------------------------------

// Helper for cell timing lookups
namespace {
template <typename Tres, typename Tgetter, typename Tkey>
int db_binary_search(const Tres *list, int count, Tgetter key_getter, Tkey key)
{
    if (count < 7) {
        for (int i = 0; i < count; i++) {
            if (key_getter(list[i]) == key) {
                return i;
            }
        }
    } else {
        int b = 0, e = count - 1;
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

bool Arch::is_dsp_cell(const CellInfo *cell) const
{
    return cell->type.in(id_MULT18_CORE, id_MULT18X36_CORE, id_MULT36_CORE, id_PREADD9_CORE, id_REG18_CORE,
                         id_MULT9_CORE);
}

int Arch::get_cell_timing_idx(IdString cell_type, IdString cell_variant) const
{
    return db_binary_search(
            speed_grade->cell_types.get(), speed_grade->cell_types.size(),
            [](const CellTimingPOD &ct) { return std::make_pair(ct.cell_type, ct.cell_variant); },
            std::make_pair(cell_type.index, cell_variant.index));
}

bool Arch::lookup_cell_delay(int type_idx, IdString from_port, IdString to_port, DelayQuad &delay) const
{
    NPNR_ASSERT(type_idx != -1);
    const auto &ct = speed_grade->cell_types[type_idx];
    int dly_idx = db_binary_search(
            ct.prop_delays.get(), ct.prop_delays.size(),
            [](const CellPropDelayPOD &pd) { return std::make_pair(pd.to_port, pd.from_port); },
            std::make_pair(to_port.index, from_port.index));
    if (dly_idx == -1)
        return false;
    delay = DelayQuad(ct.prop_delays[dly_idx].min_delay, ct.prop_delays[dly_idx].max_delay);
    return true;
}

void Arch::lookup_cell_setuphold(int type_idx, IdString from_port, IdString clock, DelayPair &setup,
                                 DelayPair &hold) const
{
    NPNR_ASSERT(type_idx != -1);
    const auto &ct = speed_grade->cell_types[type_idx];
    int dly_idx = db_binary_search(
            ct.setup_holds.get(), ct.setup_holds.size(),
            [](const CellSetupHoldPOD &sh) { return std::make_pair(sh.sig_port, sh.clock_port); },
            std::make_pair(from_port.index, clock.index));
    NPNR_ASSERT(dly_idx != -1);
    setup.min_delay = ct.setup_holds[dly_idx].min_setup;
    setup.max_delay = ct.setup_holds[dly_idx].max_setup;
    hold.min_delay = ct.setup_holds[dly_idx].min_hold;
    hold.max_delay = ct.setup_holds[dly_idx].max_hold;
}

void Arch::lookup_cell_setuphold_clock(int type_idx, IdString from_port, IdString &clock, DelayPair &setup,
                                       DelayPair &hold) const
{
    NPNR_ASSERT(type_idx != -1);
    const auto &ct = speed_grade->cell_types[type_idx];
    int dly_idx = db_binary_search(
            ct.setup_holds.get(), ct.setup_holds.size(), [](const CellSetupHoldPOD &sh) { return sh.sig_port; },
            from_port.index);
    NPNR_ASSERT(dly_idx != -1);
    clock = IdString(ct.setup_holds[dly_idx].clock_port);
    setup.min_delay = ct.setup_holds[dly_idx].min_setup;
    setup.max_delay = ct.setup_holds[dly_idx].max_setup;
    hold.min_delay = ct.setup_holds[dly_idx].min_hold;
    hold.max_delay = ct.setup_holds[dly_idx].max_hold;
}
void Arch::lookup_cell_clock_out(int type_idx, IdString to_port, IdString &clock, DelayQuad &delay) const
{
    NPNR_ASSERT(type_idx != -1);
    const auto &ct = speed_grade->cell_types[type_idx];
    int dly_idx = db_binary_search(
            ct.prop_delays.get(), ct.prop_delays.size(), [](const CellPropDelayPOD &pd) { return pd.to_port; },
            to_port.index);
    NPNR_ASSERT(dly_idx != -1);
    clock = IdString(ct.prop_delays[dly_idx].from_port);
    delay = DelayQuad(ct.prop_delays[dly_idx].min_delay, ct.prop_delays[dly_idx].max_delay);
}

TimingPortClass Arch::lookup_port_type(int type_idx, IdString port, PortType dir, IdString clock) const
{
    if (dir == PORT_IN) {
        NPNR_ASSERT(type_idx != -1);
        const auto &ct = speed_grade->cell_types[type_idx];
        // If a setup-hold entry exists, then this is a register input
        int sh_idx = db_binary_search(
                ct.setup_holds.get(), ct.setup_holds.size(),
                [](const CellSetupHoldPOD &sh) { return std::make_pair(sh.sig_port, sh.clock_port); },
                std::make_pair(port.index, clock.index));
        return (sh_idx != -1) ? TMG_REGISTER_INPUT : TMG_COMB_INPUT;
    } else {
        DelayQuad dly;
        // If a clock-to-out entry exists, then this is a register output
        return lookup_cell_delay(type_idx, clock, port, dly) ? TMG_REGISTER_OUTPUT : TMG_COMB_OUTPUT;
    }
}
// -----------------------------------------------------------------------

bool Arch::getClusterPlacement(ClusterId cluster, BelId root_bel,
                               std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    CellInfo *root_cell = cells.at(cluster).get();
    placement.clear();
    NPNR_ASSERT(root_bel != BelId());
    Loc root_loc = getBelLocation(root_bel);

    if (root_cell->constr_abs_z) {
        // Coerce root to absolute z constraint
        root_loc.z = root_cell->constr_z;
        root_bel = getBelByLocation(root_loc);
        if (root_bel == BelId() || !isValidBelForCellType(root_cell->type, root_bel))
            return false;
    }
    placement.emplace_back(root_cell, root_bel);

    for (auto child : root_cell->constr_children) {
        Loc child_loc;
        child_loc.x = root_loc.x + child->constr_x;
        child_loc.y = root_loc.y + child->constr_y;
        child_loc.z = child->constr_abs_z ? child->constr_z : (root_loc.z + child->constr_z);
        BelId child_bel = getBelByLocation(child_loc);
        if (child_bel == BelId() || !isValidBelForCellType(child->type, child_bel)) {
            // Special case for DSPs where the delta is sometimes different
            bool fixed = false;
            if (child->type == id_REG18_CORE && root_cell->is_9x9_18x18) {
                child_loc.x -= 1;
                child_loc.z += 2;
                child_bel = getBelByLocation(child_loc);
                if (child_bel != BelId() && isValidBelForCellType(child->type, child_bel))
                    fixed = true;
            }
            if (!fixed)
                return false;
        }
        placement.emplace_back(child, child_bel);
    }
    return true;
}

// -----------------------------------------------------------------------

const std::string Arch::defaultPlacer = "heap";

const std::vector<std::string> Arch::availablePlacers = {"sa", "heap"};

const std::string Arch::defaultRouter = "router2";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

NEXTPNR_NAMESPACE_END

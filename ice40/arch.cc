/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

#include <algorithm>
#include <cmath>
#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// -----------------------------------------------------------------------

IdString Arch::belTypeToId(BelType type) const
{
    if (type == TYPE_ICESTORM_LC)
        return id("ICESTORM_LC");
    if (type == TYPE_ICESTORM_RAM)
        return id("ICESTORM_RAM");
    if (type == TYPE_SB_IO)
        return id("SB_IO");
    if (type == TYPE_SB_GB)
        return id("SB_GB");
    return IdString();
}

BelType Arch::belTypeFromId(IdString type) const
{
    if (type == id("ICESTORM_LC"))
        return TYPE_ICESTORM_LC;
    if (type == id("ICESTORM_RAM"))
        return TYPE_ICESTORM_RAM;
    if (type == id("SB_IO"))
        return TYPE_SB_IO;
    if (type == id("SB_GB"))
        return TYPE_SB_GB;
    return TYPE_NONE;
}

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, PIN_##t);
#include "portpins.inc"
#undef X
}

IdString Arch::portPinToId(PortPin type) const
{
    IdString ret;
    if (type > 0 && type < PIN_MAXIDX)
        ret.index = type;
    return ret;
}

PortPin Arch::portPinFromId(IdString type) const
{
    if (type.index > 0 && type.index < PIN_MAXIDX)
        return PortPin(type.index);
    return PIN_NONE;
}

// -----------------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
#ifdef ICE40_HX1K_ONLY
    if (args.type == ArchArgs::HX1K) {
        chip_info =
                reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_1k)
                        ->get();
    } else {
        log_error("Unsupported iCE40 chip type.\n");
    }
#else
    if (args.type == ArchArgs::LP384) {
        chip_info =
                reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_384)
                        ->get();
    } else if (args.type == ArchArgs::LP1K || args.type == ArchArgs::HX1K) {
        chip_info =
                reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_1k)
                        ->get();
    } else if (args.type == ArchArgs::UP5K) {
        chip_info =
                reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_5k)
                        ->get();
    } else if (args.type == ArchArgs::LP8K || args.type == ArchArgs::HX8K) {
        chip_info =
                reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_8k)
                        ->get();
    } else {
        log_error("Unsupported iCE40 chip type.\n");
    }
#endif

    package_info = nullptr;
    for (int i = 0; i < chip_info->num_packages; i++) {
        if (chip_info->packages_data[i].name.get() == args.package) {
            package_info = &(chip_info->packages_data[i]);
            break;
        }
    }
    if (package_info == nullptr)
        log_error("Unsupported package '%s'.\n", args.package.c_str());

    bel_to_cell.resize(chip_info->num_bels);
    wire_to_net.resize(chip_info->num_wires);
    pip_to_net.resize(chip_info->num_pips);
    switches_locked.resize(chip_info->num_switches);
}

// -----------------------------------------------------------------------

std::string Arch::getChipName()
{
#ifdef ICE40_HX1K_ONLY
    if (args.type == ArchArgs::HX1K) {
        return "Lattice LP1K";
    } else {
        log_error("Unsupported iCE40 chip type.\n");
    }
#else
    if (args.type == ArchArgs::LP384) {
        return "Lattice LP384";
    } else if (args.type == ArchArgs::LP1K) {
        return "Lattice LP1K";
    } else if (args.type == ArchArgs::HX1K) {
        return "Lattice HX1K";
    } else if (args.type == ArchArgs::UP5K) {
        return "Lattice UP5K";
    } else if (args.type == ArchArgs::LP8K) {
        return "Lattice LP8K";
    } else if (args.type == ArchArgs::HX8K) {
        return "Lattice HX8K";
    } else {
        log_error("Unknown chip\n");
    }
#endif
}

// -----------------------------------------------------------------------

BelId Arch::getBelByName(IdString name) const
{
    BelId ret;

    if (bel_by_name.empty()) {
        for (int i = 0; i < chip_info->num_bels; i++)
            bel_by_name[id(chip_info->bel_data[i].name.get())] = i;
    }

    auto it = bel_by_name.find(name);
    if (it != bel_by_name.end())
        ret.index = it->second;

    return ret;
}

BelRange Arch::getBelsAtSameTile(BelId bel) const
{
    BelRange br;
    assert(bel != BelId());
    // This requires Bels at the same tile are consecutive
    int x = chip_info->bel_data[bel.index].x;
    int y = chip_info->bel_data[bel.index].y;
    int start = bel.index, end = bel.index;
    while (start >= 0 && chip_info->bel_data[start].x == x &&
           chip_info->bel_data[start].y == y)
        start--;
    start++;
    br.b.cursor = start;
    while (end < chip_info->num_bels && chip_info->bel_data[end].x == x &&
           chip_info->bel_data[end].y == y)
        end++;
    br.e.cursor = end;
    return br;
}

WireId Arch::getWireBelPin(BelId bel, PortPin pin) const
{
    WireId ret;

    assert(bel != BelId());

    int num_bel_wires = chip_info->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires =
            chip_info->bel_data[bel.index].bel_wires.get();

    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin) {
            ret.index = bel_wires[i].wire_index;
            break;
        }

    return ret;
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdString name) const
{
    WireId ret;

    if (wire_by_name.empty()) {
        for (int i = 0; i < chip_info->num_wires; i++)
            wire_by_name[id(chip_info->wire_data[i].name.get())] = i;
    }

    auto it = wire_by_name.find(name);
    if (it != wire_by_name.end())
        ret.index = it->second;

    return ret;
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    PipId ret;

    if (pip_by_name.empty()) {
        for (int i = 0; i < chip_info->num_pips; i++) {
            PipId pip;
            pip.index = i;
            pip_by_name[getPipName(pip)] = i;
        }
    }

    auto it = pip_by_name.find(name);
    if (it != pip_by_name.end())
        ret.index = it->second;

    return ret;
}

IdString Arch::getPipName(PipId pip) const
{
    assert(pip != PipId());

    int x = chip_info->pip_data[pip.index].x;
    int y = chip_info->pip_data[pip.index].y;

    std::string src_name =
            chip_info->wire_data[chip_info->pip_data[pip.index].src].name.get();
    std::replace(src_name.begin(), src_name.end(), '/', '.');

    std::string dst_name =
            chip_info->wire_data[chip_info->pip_data[pip.index].dst].name.get();
    std::replace(dst_name.begin(), dst_name.end(), '/', '.');

    return id("X" + std::to_string(x) + "/Y" + std::to_string(y) + "/" +
              src_name + ".->." + dst_name);
}

// -----------------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const
{
    for (int i = 0; i < package_info->num_pins; i++) {
        if (package_info->pins[i].name.get() == pin) {
            BelId id;
            id.index = package_info->pins[i].bel_index;
            return id;
        }
    }
    return BelId();
}

std::string Arch::getBelPackagePin(BelId bel) const
{
    for (int i = 0; i < package_info->num_pins; i++) {
        if (package_info->pins[i].bel_index == bel.index) {
            return std::string(package_info->pins[i].name.get());
        }
    }
    return "";
}
// -----------------------------------------------------------------------

bool Arch::estimatePosition(BelId bel, int &x, int &y) const
{
    assert(bel != BelId());
    x = chip_info->bel_data[bel.index].x;
    y = chip_info->bel_data[bel.index].y;

    return chip_info->bel_data[bel.index].type != TYPE_SB_GB;
}

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    assert(src != WireId());
    delay_t x1 = chip_info->wire_data[src.index].x;
    delay_t y1 = chip_info->wire_data[src.index].y;

    assert(dst != WireId());
    delay_t x2 = chip_info->wire_data[dst.index].x;
    delay_t y2 = chip_info->wire_data[dst.index].y;

    return fabsf(x1 - x2) + fabsf(y1 - y2);
}

// -----------------------------------------------------------------------

std::vector<GraphicElement> Arch::getFrameGraphics() const
{
    std::vector<GraphicElement> ret;

    for (int x = 0; x <= chip_info->width; x++)
        for (int y = 0; y <= chip_info->height; y++) {
            GraphicElement el;
            el.type = GraphicElement::G_LINE;
            el.x1 = x - 0.05, el.x2 = x + 0.05, el.y1 = y, el.y2 = y, el.z = 0;
            ret.push_back(el);
            el.x1 = x, el.x2 = x, el.y1 = y - 0.05, el.y2 = y + 0.05, el.z = 0;
            ret.push_back(el);
        }

    return ret;
}

std::vector<GraphicElement> Arch::getBelGraphics(BelId bel) const
{
    std::vector<GraphicElement> ret;

    auto bel_type = getBelType(bel);

    if (bel_type == TYPE_ICESTORM_LC) {
        GraphicElement el;
        el.type = GraphicElement::G_BOX;
        el.x1 = chip_info->bel_data[bel.index].x + 0.1;
        el.x2 = chip_info->bel_data[bel.index].x + 0.9;
        el.y1 = chip_info->bel_data[bel.index].y + 0.10 +
                (chip_info->bel_data[bel.index].z) * (0.8 / 8);
        el.y2 = chip_info->bel_data[bel.index].y + 0.18 +
                (chip_info->bel_data[bel.index].z) * (0.8 / 8);
        el.z = 0;
        ret.push_back(el);
    }

    if (bel_type == TYPE_SB_IO) {
        if (chip_info->bel_data[bel.index].x == 0 ||
            chip_info->bel_data[bel.index].x == chip_info->width - 1) {
            GraphicElement el;
            el.type = GraphicElement::G_BOX;
            el.x1 = chip_info->bel_data[bel.index].x + 0.1;
            el.x2 = chip_info->bel_data[bel.index].x + 0.9;
            if (chip_info->bel_data[bel.index].z == 0) {
                el.y1 = chip_info->bel_data[bel.index].y + 0.10;
                el.y2 = chip_info->bel_data[bel.index].y + 0.45;
            } else {
                el.y1 = chip_info->bel_data[bel.index].y + 0.55;
                el.y2 = chip_info->bel_data[bel.index].y + 0.90;
            }
            el.z = 0;
            ret.push_back(el);
        } else {
            GraphicElement el;
            el.type = GraphicElement::G_BOX;
            if (chip_info->bel_data[bel.index].z == 0) {
                el.x1 = chip_info->bel_data[bel.index].x + 0.10;
                el.x2 = chip_info->bel_data[bel.index].x + 0.45;
            } else {
                el.x1 = chip_info->bel_data[bel.index].x + 0.55;
                el.x2 = chip_info->bel_data[bel.index].x + 0.90;
            }
            el.y1 = chip_info->bel_data[bel.index].y + 0.1;
            el.y2 = chip_info->bel_data[bel.index].y + 0.9;
            el.z = 0;
            ret.push_back(el);
        }
    }

    if (bel_type == TYPE_ICESTORM_RAM) {
        GraphicElement el;
        el.type = GraphicElement::G_BOX;
        el.x1 = chip_info->bel_data[bel.index].x + 0.1;
        el.x2 = chip_info->bel_data[bel.index].x + 0.9;
        el.y1 = chip_info->bel_data[bel.index].y + 0.1;
        el.y2 = chip_info->bel_data[bel.index].y + 1.9;
        el.z = 0;
        ret.push_back(el);
    }

    return ret;
}

std::vector<GraphicElement> Arch::getWireGraphics(WireId wire) const
{
    std::vector<GraphicElement> ret;
    // FIXME
    return ret;
}

std::vector<GraphicElement> Arch::getPipGraphics(PipId pip) const
{
    std::vector<GraphicElement> ret;
    // FIXME
    return ret;
}

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort,
                        IdString toPort, delay_t &delay) const
{
    // TODO
    return false;
}

IdString Arch::getPortClock(const CellInfo *cell, IdString port) const
{
    // TODO
    return IdString();
}

bool Arch::isClockPort(const CellInfo *cell, IdString port) const
{
    // TODO
    return false;
}

NEXTPNR_NAMESPACE_END

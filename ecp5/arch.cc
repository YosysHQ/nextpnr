/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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
#include "util.h"
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
    if (type == TYPE_ICESTORM_PLL)
        return id("ICESTORM_PLL");
    if (type == TYPE_SB_WARMBOOT)
        return id("SB_WARMBOOT");
    if (type == TYPE_SB_MAC16)
        return id("SB_MAC16");
    if (type == TYPE_ICESTORM_HFOSC)
        return id("ICESTORM_HFOSC");
    if (type == TYPE_ICESTORM_LFOSC)
        return id("ICESTORM_LFOSC");
    if (type == TYPE_SB_I2C)
        return id("SB_I2C");
    if (type == TYPE_SB_SPI)
        return id("SB_SPI");
    if (type == TYPE_IO_I3C)
        return id("IO_I3C");
    if (type == TYPE_SB_LEDDA_IP)
        return id("SB_LEDDA_IP");
    if (type == TYPE_SB_RGBA_DRV)
        return id("SB_RGBA_DRV");
    if (type == TYPE_ICESTORM_SPRAM)
        return id("ICESTORM_SPRAM");
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
    if (type == id("ICESTORM_PLL"))
        return TYPE_ICESTORM_PLL;
    if (type == id("SB_WARMBOOT"))
        return TYPE_SB_WARMBOOT;
    if (type == id("SB_MAC16"))
        return TYPE_SB_MAC16;
    if (type == id("ICESTORM_HFOSC"))
        return TYPE_ICESTORM_HFOSC;
    if (type == id("ICESTORM_LFOSC"))
        return TYPE_ICESTORM_LFOSC;
    if (type == id("SB_I2C"))
        return TYPE_SB_I2C;
    if (type == id("SB_SPI"))
        return TYPE_SB_SPI;
    if (type == id("IO_I3C"))
        return TYPE_IO_I3C;
    if (type == id("SB_LEDDA_IP"))
        return TYPE_SB_LEDDA_IP;
    if (type == id("SB_RGBA_DRV"))
        return TYPE_SB_RGBA_DRV;
    if (type == id("ICESTORM_SPRAM"))
        return TYPE_ICESTORM_SPRAM;
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

static const ChipInfoPOD *get_chip_info(const RelPtr<ChipInfoPOD> *ptr) { return ptr->get(); }

#if defined(_MSC_VER)
void load_chipdb();
#endif

Arch::Arch(ArchArgs args) : args(args)
{
#if defined(_MSC_VER)
    load_chipdb();
#endif

#ifdef ICE40_HX1K_ONLY
    if (args.type == ArchArgs::HX1K) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_1k));
    } else {
        log_error("Unsupported iCE40 chip type.\n");
    }
#else
    if (args.type == ArchArgs::LP384) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_384));
    } else if (args.type == ArchArgs::LP1K || args.type == ArchArgs::HX1K) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_1k));
    } else if (args.type == ArchArgs::UP5K) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_5k));
    } else if (args.type == ArchArgs::LP8K || args.type == ArchArgs::HX8K) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_8k));
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

    // Initialise regularly used IDStrings for performance
    id_glb_buf_out = id("GLOBAL_BUFFER_OUTPUT");
    id_icestorm_lc = id("ICESTORM_LC");
    id_sb_io = id("SB_IO");
    id_sb_gb = id("SB_GB");
    id_cen = id("CEN");
    id_clk = id("CLK");
    id_sr = id("SR");
    id_i0 = id("I0");
    id_i1 = id("I1");
    id_i2 = id("I2");
    id_i3 = id("I3");
    id_dff_en = id("DFF_ENABLE");
    id_neg_clk = id("NEG_CLK");
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

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::LP384)
        return id("lp384");
    if (args.type == ArchArgs::LP1K)
        return id("lp1k");
    if (args.type == ArchArgs::HX1K)
        return id("hx1k");
    if (args.type == ArchArgs::UP5K)
        return id("up5k");
    if (args.type == ArchArgs::LP8K)
        return id("lp8k");
    if (args.type == ArchArgs::HX8K)
        return id("hx8k");
    return IdString();
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
    NPNR_ASSERT(bel != BelId());
    // This requires Bels at the same tile are consecutive
    int x = chip_info->bel_data[bel.index].x;
    int y = chip_info->bel_data[bel.index].y;
    int start = bel.index, end = bel.index;
    while (start >= 0 && chip_info->bel_data[start].x == x && chip_info->bel_data[start].y == y)
        start--;
    start++;
    br.b.cursor = start;
    while (end < chip_info->num_bels && chip_info->bel_data[end].x == x && chip_info->bel_data[end].y == y)
        end++;
    br.e.cursor = end;
    return br;
}

WireId Arch::getWireBelPin(BelId bel, PortPin pin) const
{
    WireId ret;

    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = chip_info->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = chip_info->bel_data[bel.index].bel_wires.get();

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
    NPNR_ASSERT(pip != PipId());

    int x = chip_info->pip_data[pip.index].x;
    int y = chip_info->pip_data[pip.index].y;

    std::string src_name = chip_info->wire_data[chip_info->pip_data[pip.index].src].name.get();
    std::replace(src_name.begin(), src_name.end(), '/', '.');

    std::string dst_name = chip_info->wire_data[chip_info->pip_data[pip.index].dst].name.get();
    std::replace(dst_name.begin(), dst_name.end(), '/', '.');

    return id("X" + std::to_string(x) + "/Y" + std::to_string(y) + "/" + src_name + ".->." + dst_name);
}

// -----------------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const { return BelId(); }

std::string Arch::getBelPackagePin(BelId bel) const { return ""; }
// -----------------------------------------------------------------------

void Arch::estimatePosition(BelId bel, int &x, int &y, bool &gb) const {}

delay_t Arch::estimateDelay(WireId src, WireId dst) const { return 1; }

// -----------------------------------------------------------------------

std::vector<GraphicElement> Arch::getFrameGraphics() const
{
    std::vector<GraphicElement> ret;

    return ret;
}

std::vector<GraphicElement> Arch::getBelGraphics(BelId bel) const
{
    std::vector<GraphicElement> ret;

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
};

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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
#include <cstring>
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "router1.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

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

// -----------------------------------------------------------------------

IdString Arch::belTypeToId(BelType type) const
{
    if (type == TYPE_TRELLIS_SLICE)
        return id("TRELLIS_SLICE");
    if (type == TYPE_TRELLIS_IO)
        return id("TRELLIS_IO");
    return IdString();
}

BelType Arch::belTypeFromId(IdString type) const
{
    if (type == id("TRELLIS_SLICE"))
        return TYPE_TRELLIS_SLICE;
    if (type == id("TRELLIS_IO"))
        return TYPE_TRELLIS_IO;
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

#define LFE5U_45F_ONLY

Arch::Arch(ArchArgs args) : args(args)
{
#if defined(_MSC_VER)
    load_chipdb();
#endif
#ifdef LFE5U_45F_ONLY
    if (args.type == ArchArgs::LFE5U_45F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_45k));
    } else {
        log_error("Unsupported ECP5 chip type.\n");
    }
#else
    if (args.type == ArchArgs::LFE5U_25F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_25k));
    } else if (args.type == ArchArgs::LFE5U_45F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_45k));
    } else if (args.type == ArchArgs::LFE5U_85F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_85k));
    } else {
        log_error("Unsupported ECP5 chip type.\n");
    }
#endif
}

// -----------------------------------------------------------------------

std::string Arch::getChipName()
{

    if (args.type == ArchArgs::LFE5U_25F) {
        return "LFE5U-25F";
    } else if (args.type == ArchArgs::LFE5U_45F) {
        return "LFE5U-45F";
    } else if (args.type == ArchArgs::LFE5U_85F) {
        return "LFE5U-85F";
    } else {
        log_error("Unknown chip\n");
    }
}

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::LFE5U_25F)
        return id("lfe5u_25f");
    if (args.type == ArchArgs::LFE5U_45F)
        return id("lfe5u_45f");
    if (args.type == ArchArgs::LFE5U_85F)
        return id("lfe5u_85f");
    return IdString();
}

// -----------------------------------------------------------------------

BelId ArchReadMethods::getBelByName(IdString name) const
{
    BelId ret;
    auto it = bel_by_name.find(name);
    if (it != bel_by_name.end())
        return it->second;

    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(parent_));
    ret.location = loc;
    const LocationTypePOD *loci = parent_->locInfo(ret);
    for (int i = 0; i < loci->num_bels; i++) {
        if (std::strcmp(loci->bel_data[i].name.get(), basename.c_str()) == 0) {
            ret.index = i;
            break;
        }
    }
    if (ret.index >= 0)
        bel_by_name[name] = ret;
    return ret;
}

BelRange Arch::getBelsAtSameTile(BelId bel) const
{
    BelRange br;
    NPNR_ASSERT(bel != BelId());
    br.b.cursor_tile = bel.location.y * chip_info->width + bel.location.x;
    br.e.cursor_tile = bel.location.y * chip_info->width + bel.location.x;
    br.b.cursor_index = 0;
    br.e.cursor_index = locInfo(bel)->num_bels;
    return br;
}

WireId ArchReadMethods::getWireBelPin(BelId bel, PortPin pin) const
{
    WireId ret;

    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = parent_->locInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = parent_->locInfo(bel)->bel_data[bel.index].bel_wires.get();
    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin) {
            ret.location = bel.location + bel_wires[i].rel_wire_loc;
            ret.index = bel_wires[i].wire_index;
            break;
        }

    return ret;
}

// -----------------------------------------------------------------------

WireId ArchReadMethods::getWireByName(IdString name) const
{
    WireId ret;
    auto it = wire_by_name.find(name);
    if (it != wire_by_name.end())
        return it->second;

    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(parent_));
    ret.location = loc;
    const LocationTypePOD *loci = parent_->locInfo(ret);
    for (int i = 0; i < loci->num_wires; i++) {
        if (std::strcmp(loci->wire_data[i].name.get(), basename.c_str()) == 0) {
            ret.index = i;
            ret.location = loc;
            break;
        }
    }
    if (ret.index >= 0)
        wire_by_name[name] = ret;
    else
        ret.location = Location();
    return ret;
}

// -----------------------------------------------------------------------

PipId ArchReadMethods::getPipByName(IdString name) const
{
    auto it = pip_by_name.find(name);
    if (it != pip_by_name.end())
        return it->second;

    PipId ret;
    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(parent_));
    const LocationTypePOD *loci = parent_->locInfo(ret);
    for (int i = 0; i < loci->num_pips; i++) {
        PipId curr;
        curr.location = loc;
        curr.index = i;
        pip_by_name[parent_->getPipName(curr)] = curr;
    }
    return pip_by_name[name];
}

IdString Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());

    int x = pip.location.x;
    int y = pip.location.y;

    std::string src_name = getWireName(getPipSrcWire(pip)).str(this);
    std::replace(src_name.begin(), src_name.end(), '/', '.');

    std::string dst_name = getWireName(getPipDstWire(pip)).str(this);
    std::replace(dst_name.begin(), dst_name.end(), '/', '.');

    return id("X" + std::to_string(x) + "/Y" + std::to_string(y) + "/" + src_name + ".->." + dst_name);
}

// -----------------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const { return BelId(); }

std::string Arch::getBelPackagePin(BelId bel) const { return ""; }
// -----------------------------------------------------------------------

void Arch::estimatePosition(BelId bel, int &x, int &y, bool &gb) const
{
    x = bel.location.x;
    y = bel.location.y;
    gb = false;
}

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    return abs(src.location.x - dst.location.x) + abs(src.location.y - dst.location.y);
}

// -----------------------------------------------------------------------

bool Arch::place() { return placer1(getCtx()); }

bool Arch::route() { return router1(getCtx()); }

// -----------------------------------------------------------------------

std::vector<GraphicElement> ArchReadMethods::getDecalGraphics(DecalId decalId) const
{
    std::vector<GraphicElement> ret;
    // FIXME
    return ret;
}

DecalXY Arch::getFrameDecal() const { return {}; }

DecalXY Arch::getBelDecal(BelId bel) const { return {}; }

DecalXY Arch::getWireDecal(WireId wire) const { return {}; }

DecalXY Arch::getPipDecal(PipId pip) const { return {}; };

DecalXY Arch::getGroupDecal(GroupId pip) const { return {}; };

// -----------------------------------------------------------------------

bool ArchReadMethods::isValidBelForCell(CellInfo *cell, BelId bel) const { return true; }

bool ArchReadMethods::isBelLocationValid(BelId bel) const { return true; }

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, delay_t &delay) const
{
    return false;
}

IdString Arch::getPortClock(const CellInfo *cell, IdString port) const { return IdString(); }

bool Arch::isClockPort(const CellInfo *cell, IdString port) const { return false; }

bool ArchReadMethods::checkWireAvail(WireId wire) const
{
    NPNR_ASSERT(wire != WireId());
    return wire_to_net.find(wire) == wire_to_net.end() || wire_to_net.at(wire) == IdString();
}

bool ArchReadMethods::checkPipAvail(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());
    return pip_to_net.find(pip) == pip_to_net.end() || pip_to_net.at(pip) == IdString();
}

bool ArchReadMethods::checkBelAvail(BelId bel) const
{
    NPNR_ASSERT(bel != BelId());
    return bel_to_cell.find(bel) == bel_to_cell.end() || bel_to_cell.at(bel) == IdString();
}

IdString ArchReadMethods::getConflictingBelCell(BelId bel) const
{
    NPNR_ASSERT(bel != BelId());
    if (bel_to_cell.find(bel) == bel_to_cell.end())
        return IdString();
    else
        return bel_to_cell.at(bel);
}

IdString ArchReadMethods::getConflictingWireNet(WireId wire) const
{
    NPNR_ASSERT(wire != WireId());
    if (wire_to_net.find(wire) == wire_to_net.end())
        return IdString();
    else
        return wire_to_net.at(wire);
}

IdString ArchReadMethods::getConflictingPipNet(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());
    if (pip_to_net.find(pip) == pip_to_net.end())
        return IdString();
    else
        return pip_to_net.at(pip);
}

IdString ArchReadMethods::getBoundWireNet(WireId wire) const
{
    NPNR_ASSERT(wire != WireId());
    if (wire_to_net.find(wire) == wire_to_net.end())
        return IdString();
    else
        return wire_to_net.at(wire);
}

IdString ArchReadMethods::getBoundPipNet(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());
    if (pip_to_net.find(pip) == pip_to_net.end())
        return IdString();
    else
        return pip_to_net.at(pip);
}

IdString ArchReadMethods::getBoundBelCell(BelId bel) const
{
    NPNR_ASSERT(bel != BelId());
    if (bel_to_cell.find(bel) == bel_to_cell.end())
        return IdString();
    else
        return bel_to_cell.at(bel);
}

void ArchMutateMethods::unbindWire(WireId wire)
{
    NPNR_ASSERT(wire != WireId());
    NPNR_ASSERT(wire_to_net[wire] != IdString());

    auto &net_wires = parent_->nets[wire_to_net[wire]]->wires;
    auto it = net_wires.find(wire);
    NPNR_ASSERT(it != net_wires.end());

    auto pip = it->second.pip;
    if (pip != PipId()) {
        pip_to_net[pip] = IdString();
    }

    net_wires.erase(it);
    wire_to_net[wire] = IdString();
}

void ArchMutateMethods::unbindPip(PipId pip)
{
    NPNR_ASSERT(pip != PipId());
    NPNR_ASSERT(pip_to_net[pip] != IdString());

    WireId dst;
    dst.index = parent_->locInfo(pip)->pip_data[pip.index].dst_idx;
    dst.location = pip.location + parent_->locInfo(pip)->pip_data[pip.index].rel_dst_loc;
    NPNR_ASSERT(wire_to_net[dst] != IdString());
    wire_to_net[dst] = IdString();
    parent_->nets[pip_to_net[pip]]->wires.erase(dst);

    pip_to_net[pip] = IdString();
}

void ArchMutateMethods::unbindBel(BelId bel)
{
    NPNR_ASSERT(bel != BelId());
    NPNR_ASSERT(bel_to_cell[bel] != IdString());
    parent_->cells[bel_to_cell[bel]]->bel = BelId();
    parent_->cells[bel_to_cell[bel]]->belStrength = STRENGTH_NONE;
    bel_to_cell[bel] = IdString();
}

void ArchMutateMethods::bindWire(WireId wire, IdString net, PlaceStrength strength)
{
    NPNR_ASSERT(wire != WireId());
    NPNR_ASSERT(wire_to_net[wire] == IdString());
    wire_to_net[wire] = net;
    parent_->nets[net]->wires[wire].pip = PipId();
    parent_->nets[net]->wires[wire].strength = strength;
}

void ArchMutateMethods::bindPip(PipId pip, IdString net, PlaceStrength strength)
{
    NPNR_ASSERT(pip != PipId());
    NPNR_ASSERT(pip_to_net[pip] == IdString());

    pip_to_net[pip] = net;

    WireId dst;
    dst.index = parent_->locInfo(pip)->pip_data[pip.index].dst_idx;
    dst.location = pip.location + parent_->locInfo(pip)->pip_data[pip.index].rel_dst_loc;
    NPNR_ASSERT(wire_to_net[dst] == IdString());
    wire_to_net[dst] = net;
    parent_->nets[net]->wires[dst].pip = pip;
    parent_->nets[net]->wires[dst].strength = strength;
}

void ArchMutateMethods::bindBel(BelId bel, IdString cell, PlaceStrength strength)
{
    NPNR_ASSERT(bel != BelId());
    NPNR_ASSERT(bel_to_cell[bel] == IdString());
    bel_to_cell[bel] = cell;
    parent_->cells[cell]->bel = bel;
    parent_->cells[cell]->belStrength = strength;
}

CellInfo *ArchMutateMethods::getCell(IdString cell) { return parent_->cells.at(cell).get(); }



NEXTPNR_NAMESPACE_END

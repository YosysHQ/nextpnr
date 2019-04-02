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

#include "nextpnr.h"
#include "placer1.h"
#include "router1.h"

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

void IdString::initialize_arch(const BaseCtx *ctx)
{
    // Nothing here -- IdString is actually initialized in the constructor,
    // because we need to have bba loaded.
}

// Given a device name, figure out what family it belongs to.
static Arch::Family device_to_family(const std::string &device)
{
    size_t pos = 0;

    // Skip the prefix.
    if (device.substr(0, 2) == "xc")
	pos += 2;
    else if (device.substr(0, 2) == "xa")
	pos += 2;
    else if (device.substr(0, 2) == "xq")
	pos += 2;
    else if (device.substr(0, 3) == "xqr")
	pos += 3;
    auto raw = device.substr(pos);

    if (raw.substr(0, 2) == "vu" || raw.substr(0, 2) == "ku") {
	// Ultrascale or Ultrascale+ (needs to be checked before original Virtex).
	if (raw.substr(raw.size() - 1) == "p")
	    return Arch::FAMILY_ULTRASCALE_PLUS;
	return Arch::FAMILY_ULTRASCALE;
    } else if (raw.substr(0, 2) == "zu") {
	// Zynq Ultrascale+.
	return Arch::FAMILY_ULTRASCALE_PLUS;
    } else if (raw.substr(0, 1) == "7") {
	// 7 Series.
	return Arch::FAMILY_SERIES7;
    } else if (raw.substr(0, 2) == "6s") {
	return Arch::FAMILY_SPARTAN6;
    } else if (raw.substr(0, 2) == "6v") {
	return Arch::FAMILY_VIRTEX6;
    } else if (raw.substr(0, 2) == "5v") {
	return Arch::FAMILY_VIRTEX5;
    } else if (raw.substr(0, 2) == "4v") {
	return Arch::FAMILY_VIRTEX4;
    } else if (raw.substr(0, 3) == "3sd") {
	// Needs to be checked before other Spartan 3 variants.
	return Arch::FAMILY_SPARTAN3ADSP;
    } else if (raw.substr(0, 2) == "3s") {
	// One of many Spartan 3 variants.
	if (raw.substr(raw.size() - 1) == "e")
	    return Arch::FAMILY_SPARTAN3E;
	if (raw.substr(raw.size() - 1) == "a")
	    return Arch::FAMILY_SPARTAN3A;
	if (raw.substr(raw.size() - 2) == "an")
	    return Arch::FAMILY_SPARTAN3A;
	return Arch::FAMILY_SPARTAN3;
    } else if (raw.substr(0, 3) == "2vp") {
	// Virtex 2 Pro.
	return Arch::FAMILY_VIRTEX2P;
    } else if (raw.substr(0, 2) == "2v") {
	// Virtex 2.
	return Arch::FAMILY_VIRTEX2;
    } else if (raw.substr(0, 1) == "v" || raw.substr(0, 2) == "2s") {
	// Virtex or Virtex E.
	if (raw.substr(raw.size() - 1) == "e")
	    return Arch::FAMILY_VIRTEXE;
	return Arch::FAMILY_VIRTEX;
    } else if (raw.substr(0, 1) == "s") {
	// Spartan or Spartan XL.
	if (raw.size() >= 2 && raw.substr(raw.size() - 2) == "xl")
	    return Arch::FAMILY_SPARTANXL;
	return Arch::FAMILY_XC4000E;
    } else if (raw.substr(0, 2) == "40") {
	// One of the xc4000 families.
	if (raw.substr(raw.size() - 1) == "e")
	    return Arch::FAMILY_XC4000E;
	if (raw.substr(raw.size() - 2) == "ex")
	    return Arch::FAMILY_XC4000EX;
	if (raw.substr(raw.size() - 2) == "xl")
	    return Arch::FAMILY_XC4000EX;
	if (raw.size() >= 3 && raw.substr(raw.size() - 3) == "xla")
	    return Arch::FAMILY_XC4000XLA;
	if (raw.substr(raw.size() - 2) == "xv")
	    return Arch::FAMILY_XC4000XV;
    }
    log_error("Unknown device family.\n");
}

static std::string family_name(Arch::Family family) {
    switch(family) {
	case Arch::FAMILY_XC4000E:
	    return "xc4000e";
	case Arch::FAMILY_XC4000EX:
	    return "xc4000ex";
	case Arch::FAMILY_XC4000XLA:
	    return "xc4000xla";
	case Arch::FAMILY_XC4000XV:
	    return "xc4000xv";
	case Arch::FAMILY_SPARTANXL:
	    return "spartanxl";
	case Arch::FAMILY_VIRTEX:
	    return "virtex";
	case Arch::FAMILY_VIRTEXE:
	    return "virtexe";
	case Arch::FAMILY_VIRTEX2:
	    return "virtex2";
	case Arch::FAMILY_VIRTEX2P:
	    return "virtex2p";
	case Arch::FAMILY_SPARTAN3:
	    return "spartan3";
	case Arch::FAMILY_SPARTAN3E:
	    return "spartan3e";
	case Arch::FAMILY_SPARTAN3A:
	    return "spartan3a";
	case Arch::FAMILY_SPARTAN3ADSP:
	    return "spartan3adsp";
	case Arch::FAMILY_VIRTEX4:
	    return "virtex4";
	case Arch::FAMILY_VIRTEX5:
	    return "virtex5";
	case Arch::FAMILY_VIRTEX6:
	    return "virtex6";
	case Arch::FAMILY_SPARTAN6:
	    return "spartan6";
	case Arch::FAMILY_SERIES7:
	    return "series7";
	case Arch::FAMILY_ULTRASCALE:
	    return "ultrascale";
	case Arch::FAMILY_ULTRASCALE_PLUS:
	    return "ultrascaleplus";
    }
    NPNR_ASSERT_FALSE("strange family");
    return "";
}

Arch::Arch(ArchArgs args) : args(args)
{
    // Select and load family bba.
    family = device_to_family(args.device);
    std::string fname = family_name(family);
    std::string family_filename = EXTERNAL_CHIPDB_ROOT "/leuctra/" + fname + ".bin";
    try {
        mmap.open(family_filename);
        if (!mmap.is_open())
            log_error("Unable to read chipdb %s\n", family_filename.c_str());
    } catch (...) {
        log_error("Unable to read chipdb %s\n", family_filename.c_str());
    }
    family_info = reinterpret_cast<const FamilyPOD *>(mmap.data());
    if (family_info->format_tag != DB_FORMAT_TAG_CURRENT)
        log_error("Chipdb %s has wrong format tag\n", family_filename.c_str());

    // Slurp IdStrings.
    // Entry 0 must be "".
    NPNR_ASSERT(family_info->idstrings[0].get()[0] == 0);
    for (int i = 1; i < family_info->num_idstrings; i++)
	IdString::initialize_add(this, family_info->idstrings[i].get(), i);

    // Make double sure we got the right family.
    if (IdString(family_info->name_id).str(this) != fname)
        log_error("Chipdb %s is for strange family\n", family_filename.c_str());

    // Search for the device.
    int dev_name_id = id(args.device).index;
    for (int i = 0; i < family_info->num_devices; i++) {
	if (family_info->devices[i].name_id == dev_name_id) {
	    device_info = family_info->devices[i].device.get();
	    break;
	}
    }
    if (device_info == nullptr)
	log_error("Unknown device.\n");

    // Find the right package.
    int pkg_name_id = id(args.package).index;
    if (pkg_name_id == 0) {
	package_info = &device_info->packages[0];
    } else {
        for (int i = 0; i < device_info->num_packages; i++) {
	    if (device_info->packages[i].name_id == pkg_name_id) {
	        package_info = &device_info->packages[i];
	        break;
	    }
        }
    }
    if (package_info == nullptr)
	log_error("Unknown package.\n");
}

// -----------------------------------------------------------------------

BelId Arch::getBelByName(IdString name) const
{
    BelId ret;
    auto it = bel_by_name.find(name);
    if (it != bel_by_name.end())
        return it->second;

    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(this));
    ret.location = loc;
    IdString basename_id = id(basename);
    auto &tt = getTileType(loc.x, loc.y);
    for (int i = 0; i < tt.num_bels; i++) {
        if (tt.bels[i].name_id == basename_id.index) {
            ret.index = i;
            bel_by_name[name] = ret;
            return ret;
        }
    }
    return BelId();
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    WireId ret;

    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = getBelTypeInfo(bel).num_pins;
    const BelTypePinPOD *bel_type_wires = getBelTypeInfo(bel).pins.get();
    for (int i = 0; i < num_bel_wires; i++)
        if (bel_type_wires[i].name_id == pin.index) {
            ret.location = bel.location;
            ret.index = getTileTypeBel(bel).pin_wires[i];
            break;
        }

    return ret;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = getBelTypeInfo(bel).num_pins;
    const BelTypePinPOD *bel_type_wires = getBelTypeInfo(bel).pins.get();
    for (int i = 0; i < num_bel_wires; i++)
        if (bel_type_wires[i].name_id == pin.index) {
            bool is_in = bel_type_wires[i].flags & BelTypePinPOD::FLAG_INPUT;
            bool is_out = bel_type_wires[i].flags & BelTypePinPOD::FLAG_OUTPUT;
            if (is_in && is_out)
                return PORT_INOUT;
            if (is_in)
                return PORT_IN;
            assert(is_out);
            return PORT_OUT;
        }

    return PORT_INOUT;
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdString name) const
{
    WireId ret;
    auto it = wire_by_name.find(name);
    if (it != wire_by_name.end())
        return it->second;

    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(this));
    ret.location = loc;
    IdString basename_id = id(basename);
    auto &tt = getTileType(loc.x, loc.y);
    for (int i = 0; i < tt.num_wires; i++) {
        if (tt.wires[i].name_id == basename_id.index) {
            ret.index = i;
            wire_by_name[name] = ret;
            return ret;
        }
    }
    return WireId();
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    auto it = pip_by_name.find(name);
    if (it != pip_by_name.end())
        return it->second;

    PipId ret;
    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(this));
    ret.location = loc;
    AllPipRange range;
    range.b.cursor_tile = loc.x + device_info->width * loc.y;
    range.b.cursor_kind = PIP_KIND_PIP;
    range.b.cursor_index = 0;
    range.b.cursor_subindex = -1;
    range.b.device = device_info;
    range.b.family = family_info;
    ++range.b;
    range.e.cursor_tile = loc.x + device_info->width * loc.y + 1;
    range.e.cursor_kind = PIP_KIND_PIP;
    range.e.cursor_index = 0;
    range.e.cursor_subindex = -1;
    range.e.device = device_info;
    range.e.family = family_info;
    ++range.e;
    for (const auto& curr: range) {
        pip_by_name[getPipName(curr)] = curr;
    }
    if (pip_by_name.find(name) == pip_by_name.end())
        NPNR_ASSERT_FALSE_STR("no pip named " + name.str(this));
    return pip_by_name[name];
}

IdString Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());

    int x = pip.location.x;
    int y = pip.location.y;

    if (pip.kind == PIP_KIND_PIP) {
      std::string src_name = getWireBasename(getPipSrcWire(pip)).str(this);
      std::string dst_name = getWireBasename(getPipDstWire(pip)).str(this);
      return id("X" + std::to_string(x) + "/Y" + std::to_string(y) + "/" + src_name + ".->." + dst_name);
    } else {
      auto &tt = getTileType(pip.location);
      std::string port_name = IdString(tt.ports[pip.index].name_id).str(this);
      std::string dst_name = getWireBasename(getPipDstWire(pip)).str(this);
      return id("X" + std::to_string(x) + "/Y" + std::to_string(y) + "/" + port_name + "/" + std::to_string(pip.subindex) + ".->." + dst_name);
    }
}

// -----------------------------------------------------------------------

void PipIterator::operator++() {
    cursor_index++;
    auto &ttw = arch->getTileTypeWire(wire);
    if (stage == STAGE_PIPS) {
        int num;
        if (mode == MODE_UPHILL)
            num = ttw.num_pip_dst_xrefs;
        else
            num = ttw.num_pip_src_xrefs;
        if (cursor_index == num) {
            cursor_index = 0;
            stage = STAGE_PORTS;
        }
    }
    if (stage == STAGE_PORTS) {
        while (true) {
            if (cursor_index == ttw.num_port_xrefs) {
                cursor_index = 0;
                stage = STAGE_END;
                break;
            }
            // Make sure the port is connected.
            auto &tile = arch->getTile(wire.location);
            auto &xref = ttw.port_xrefs[cursor_index];
	    auto &conn = tile.conns[xref.port_idx];
            if (conn.port_idx != -1) {
		// Make sure the wire in a port is connected.
		Location other_loc;
		other_loc.x = conn.tile_x;
		other_loc.y = conn.tile_y;
	        auto &other_tt = arch->getTileType(other_loc);
	        if (other_tt.ports[conn.port_idx].wires[xref.wire_idx] != -1)
		    break;
	    }
            cursor_index++;
        }
    }
}

PipId PipIterator::operator*() const {
    PipId ret;
    auto &ttw = arch->getTileTypeWire(wire);
    ret.location = wire.location;
    if (mode == MODE_UPHILL) {
        if (stage == STAGE_PIPS) {
            ret.kind = PIP_KIND_PIP;
            ret.index = ttw.pip_dst_xrefs[cursor_index];
        } else {
            ret.kind = PIP_KIND_PORT;
            auto &xref = ttw.port_xrefs[cursor_index];
            ret.index = xref.port_idx;
            ret.subindex = xref.wire_idx;
        }
    } else {
        if (stage == STAGE_PIPS) {
            ret.kind = PIP_KIND_PIP;
            ret.index = ttw.pip_src_xrefs[cursor_index];
        } else {
            ret.kind = PIP_KIND_PORT;
            auto &tile = arch->getTile(wire.location);
            auto &xref = ttw.port_xrefs[cursor_index];
            auto &conn = tile.conns[xref.port_idx];
            ret.location.x = conn.tile_x;
            ret.location.y = conn.tile_y;
            ret.index = conn.port_idx;
            ret.subindex = xref.wire_idx;
        }
    }
    return ret;
}


BelPin BelPinIterator::operator*() const {
    BelPin ret;
    ret.bel.index = ptr->bel_idx;
    ret.bel.location = bel_loc;
    auto &bt = arch->getBelTypeInfo(ret.bel);
    ret.pin.index = bt.pins[ptr->pin_idx].name_id;
    return ret;
}

// -----------------------------------------------------------------------
//
// XXX package pins

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = getBelTypeInfo(bel).num_pins;
    const BelTypePinPOD *bel_type_wires = getBelTypeInfo(bel).pins.get();

    for (int i = 0; i < num_bel_wires; i++) {
        IdString id;
        id.index = bel_type_wires[i].name_id;
        ret.push_back(id);
    }

    return ret;
}

// -----------------------------------------------------------------------

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    // TODO
    return 13;
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    // TODO
    return 13;
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }


// -----------------------------------------------------------------------

bool Arch::place() { return placer1(getCtx(), Placer1Cfg(getCtx())); }

bool Arch::route() { return router1(getCtx(), Router1Cfg(getCtx())); }

// -----------------------------------------------------------------------

std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const
{
    std::vector<GraphicElement> ret;

    if (decal.type == DecalId::TYPE_BEL) {
        BelId bel;
        bel.index = decal.z;
        bel.location = decal.location;
	int max_z = getTileBelDimZ(bel.location.x, bel.location.y);
        int z = bel.index;
        //auto bel_type = getBelType(bel);

        GraphicElement el;
        el.type = GraphicElement::TYPE_BOX;
        el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
        el.x1 = bel.location.x + 0.05;
        el.x2 = bel.location.x + 0.95;
        el.y1 = bel.location.y + (z + 0.05) / max_z;
        el.y2 = bel.location.y + (z + 0.95) / max_z;
        ret.push_back(el);
    }

    return ret;
}

DecalXY Arch::getBelDecal(BelId bel) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_BEL;
    decalxy.decal.location = bel.location;
    decalxy.decal.z = bel.index;
    decalxy.decal.active = !checkBelAvail(bel);
    return decalxy;
}

DecalXY Arch::getWireDecal(WireId wire) const { return {}; }

DecalXY Arch::getPipDecal(PipId pip) const { return {}; };

DecalXY Arch::getGroupDecal(GroupId pip) const { return {}; };

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    // XXX
    delay.min_delay = 11;
    delay.max_delay = 13;
    return true;
}

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    // XXX
    return TMG_IGNORE;
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo info;
    info.setup = getDelayFromNS(0);
    info.hold = getDelayFromNS(0);
    info.clockToQ = getDelayFromNS(0);
    // XXX
    return info;
}

NEXTPNR_NAMESPACE_END

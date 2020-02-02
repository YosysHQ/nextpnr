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
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
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

BelId Arch::getPackagePinBel(const std::string &pin) const
{
    IdString pin_id = id(pin);
    for (int i = 0; i < package_info->num_pins; i++) {
        if (package_info->pin_data[i].name_id == pin_id.index) {
            BelId bel;
            bel.location.x = package_info->pin_data[i].bel.tile_x;
            bel.location.y = package_info->pin_data[i].bel.tile_y;
            bel.index = package_info->pin_data[i].bel.bel_idx;
            return bel;
        }
    }
    return BelId();
}

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
    // XXX
    int dx = std::abs(src.location.x - dst.location.x);
    int dy = std::abs(src.location.y - dst.location.y);
    return (dx + dy + 10) * 300;
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    // XXX
    if (net_info->driver.cell->type == id("LEUCTRA_LC") &&
		(net_info->driver.port == id("DMO") ||
		 net_info->driver.port == id("DCO")))
	return 150;
    auto &src_loc = net_info->driver.cell->bel.location;
    auto &dst_loc = sink.cell->bel.location;
    int dx = std::abs(src_loc.x - dst_loc.x);
    int dy = std::abs(src_loc.y - dst_loc.y);
    return (dx + dy + 10) * 300;
}

ArcBounds Arch::getRouteBoundingBox(WireId src, WireId dst) const
{

    int x0, x1, y0, y1;
    x0 = x1 = src.location.x;
    y0 = y1 = src.location.y;
    auto expand = [&](int x, int y) {
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    };

    expand(src.location.x-10, src.location.y-10);
    expand(src.location.x+5, src.location.y+5);
    expand(dst.location.x-10, dst.location.y-10);
    expand(dst.location.x+5, dst.location.y+5);
    if (x0 < 0)
	    x0 = 0;
    if (y0 < 0)
	    y0 = 0;
    if (x1 >= device_info->width)
	    x1 = device_info->width - 1;
    if (y1 >= device_info->height)
	    y1 = device_info->height - 1;

    return {x0, y0, x1, y1};
}

delay_t Arch::getBoundingBoxCost(WireId src, WireId dst, int distance) const {
	return 0;
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }


// -----------------------------------------------------------------------

bool Arch::place()
{
    std::string placer = str_or_default(settings, id("placer"), defaultPlacer);

    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.criticalityExponent = 7;
        cfg.ioBufTypes.insert(id("IOB"));
        if (!placer_heap(getCtx(), cfg))
            return false;
        getCtx()->settings[getCtx()->id("place")] = 1;
    } else if (placer == "sa") {
        if (!placer1(getCtx(), Placer1Cfg(getCtx())))
            return false;
        getCtx()->settings[getCtx()->id("place")] = 1;
    } else {
        log_error("Leuctra architecture does not support placer '%s'\n", placer.c_str());
    }
    return true;
}

bool Arch::route() {
    log_info("Running router2 for main routing task\n");
    router2(getCtx());
    log_info("Running router1 to ensure route is legal\n");
    bool retVal = router1(getCtx(), Router1Cfg(getCtx()));
    if (retVal)
        getCtx()->settings[getCtx()->id("route")] = 1;
    return retVal;
}

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
    delay.min_delay = 150;
    delay.max_delay = 150;
    if (cell->type == id("LEUCTRA_LC")) {
	if (toPort == id("O6") || toPort == id("O5") || toPort == id("CO") || toPort == id("DCO") || toPort == id("XO")) {
            if (fromPort == id("I1") || fromPort == id("RA1"))
                return true;
            if (fromPort == id("I2") || fromPort == id("RA2"))
                return true;
            if (fromPort == id("I3") || fromPort == id("RA3"))
                return true;
            if (fromPort == id("I4") || fromPort == id("RA4"))
                return true;
            if (fromPort == id("I5") || fromPort == id("RA5"))
                return true;
	    if (toPort != id("O5") && (fromPort == id("I6") || fromPort == id("RA6")))
                return true;
	}
	if (toPort == id("CO") || toPort == id("DCO") || toPort == id("XO")) {
            if (fromPort == id("XI")) {
		if (cell->params.count(id("CYINIT")) && cell->params.at(id("CYINIT")) == Property("XI"))
                    return true;
		if (cell->params.count(id("CYMUX")) && cell->params.at(id("CYMUX")) == Property("XI"))
                    return true;
	    }
            if (fromPort == id("DCI"))
                return true;
	}
	if ((toPort == id("MO") || toPort == id("DMO")) && (fromPort == id("DMI0") || fromPort == id("DMI1") || fromPort == id("XI")))
            return true;
	return false;
    } else if (cell->type == id("LEUCTRA_FF")) {
	if (cell->params.at(id("MODE")).as_string() == "COMB") {
		return true;
	}
	    return false;
    } else if (cell->type == id("BUFGMUX")) {
	if (toPort == id("O")) {
        if (fromPort == id("I0"))
            return true;
        if (fromPort == id("I1"))
            return true;
	}
	return false;
    } else if (cell->type == id("OLOGIC2")) {
	if (toPort == id("OQ") && fromPort == id("D1"))
            return true;
	return false;
    } else if (cell->type == id("ILOGIC2")) {
	if (toPort == id("FABRICOUT") && fromPort == id("D"))
            return true;
	return false;
    } else if (cell->type == id("RAMB16BWER")) {
	    return false;
    } else if (cell->type == id("RAMB8BWER")) {
	    return false;
    }
    log_warning("cell type '%s' arc '%s' '%s' is unsupported (instantiated as '%s')\n", cell->type.c_str(this), fromPort.c_str(this), toPort.c_str(this), cell->name.c_str(this));
    return false;
}

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    if (cell->type == id("LEUCTRA_LC")) {
	if (cell->attrs.count(id("CONST")))
	    return TMG_IGNORE;
        if (port == id("O6") || port == id("O5") || port == id("MO") || port == id("DMO") || port == id("DCO") || port == id("CO") || port == id("XO"))
            return TMG_COMB_OUTPUT;
        if (port == id("I1") || port == id("RA1"))
            return TMG_COMB_INPUT;
        if (port == id("I2") || port == id("RA2"))
            return TMG_COMB_INPUT;
        if (port == id("I3") || port == id("RA3"))
            return TMG_COMB_INPUT;
        if (port == id("I4") || port == id("RA4"))
            return TMG_COMB_INPUT;
        if (port == id("I5") || port == id("RA5"))
            return TMG_COMB_INPUT;
        if (port == id("I6") || port == id("RA6"))
            return TMG_COMB_INPUT;
        if (port == id("WA1") || port == id("WA2") || port == id("WA3") || port == id("WA4") || port == id("WA5") || port == id("WA6") || port == id("WA7") || port == id("WA8") || port == id("WE") || port == id("DDI5") || port == id("DDI7") || port == id("DDI8")) {
	    clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
	}
        if (port == id("DMI0") || port == id("DMI1") || port == id("DCI"))
            return TMG_COMB_INPUT;
        if (port == id("XI")) {
	    if (cell->params.count(id("DIMUX")) && cell->params.at(id("DIMUX")).as_string() == "XI") {
	    clockInfoCount = 1;
		return TMG_REGISTER_INPUT;
	    } else {
		return TMG_COMB_INPUT;
	    }
	}
	if (port == id("CLK"))
	    return TMG_CLOCK_INPUT;
    }
    if (cell->type == id("LEUCTRA_FF")) {
	if (cell->params.at(id("MODE")).as_string() == "COMB") {
		if (port == id("D") ||
			port == id("SR") ||
			port == id("CLK") ||
			port == id("CE")) {
		    return TMG_COMB_INPUT;
		}
		if (port == id("Q")) {
		    return TMG_COMB_OUTPUT;
		}
	} else {
		if (port == id("D") ||
			port == id("SR") ||
			port == id("CE")) {
		    clockInfoCount = 1;
		    return TMG_REGISTER_INPUT;
		}
		if (port == id("CLK"))
		    return TMG_CLOCK_INPUT;
		if (port == id("Q")) {
		    clockInfoCount = 1;
		    return TMG_REGISTER_OUTPUT;
		}
	}
    }
    if (cell->type == id("BUFGMUX")) {
        if (port == id("O"))
            return TMG_COMB_OUTPUT;
        if (port == id("I0"))
            return TMG_COMB_INPUT;
        if (port == id("I1"))
            return TMG_COMB_INPUT;
        if (port == id("S"))
            return TMG_IGNORE;
    }
    if (cell->type == id("IOB")) {
        if (port == id("I"))
            return TMG_STARTPOINT;
        if (port == id("O"))
            return TMG_ENDPOINT;
        if (port == id("T"))
            return TMG_ENDPOINT;
    }
    if (cell->type == id("ILOGIC2")) {
        if (port == id("D"))
            return TMG_COMB_INPUT;
        if (port == id("FABRICOUT"))
            return TMG_COMB_OUTPUT;
    }
    if (cell->type == id("OLOGIC2")) {
        if (port == id("D1"))
            return TMG_COMB_INPUT;
        if (port == id("OQ"))
            return TMG_COMB_OUTPUT;
    }
    if (cell->type == id("RAMB8BWER")) {
	if (port == id("CLKAWRCLK"))
	    return TMG_CLOCK_INPUT;
	if (port == id("CLKBRDCLK"))
	    return TMG_CLOCK_INPUT;
	clockInfoCount = 1;
	if (cell->ports.at(port).type == PORT_IN)
	    return TMG_REGISTER_INPUT;
	else
	    return TMG_REGISTER_OUTPUT;
    }
    if (cell->type == id("RAMB16BWER")) {
	if (port == id("CLKA"))
	    return TMG_CLOCK_INPUT;
	if (port == id("CLKB"))
	    return TMG_CLOCK_INPUT;
	clockInfoCount = 1;
	if (cell->ports.at(port).type == PORT_IN)
	    return TMG_REGISTER_INPUT;
	else
	    return TMG_REGISTER_OUTPUT;
    }
    // XXX
    log_warning("cell type '%s' port '%s' is unsupported (instantiated as '%s')\n", cell->type.c_str(this), port.c_str(this), cell->name.c_str(this));
    return TMG_IGNORE;
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo info;
    info.setup = getDelayFromNS(0);
    info.hold = getDelayFromNS(0);
    info.clockToQ = getDelayFromNS(0);
    if (cell->type == id("LEUCTRA_LC") || cell->type == id("LEUCTRA_FF")) {
        info.clock_port = id("CLK");
	if (cell->params.count(id("CLKINV")) && cell->params.at(id("CLKINV")) == Property("CLK_B"))
	    info.edge = FALLING_EDGE;
	else
	    info.edge = RISING_EDGE;
    }
    if (cell->type == id("RAMB8BWER")) {
	// XXX wrong wrong wrong
        info.clock_port = id("CLKAWRCLK");
	if (cell->params.count(id("CLKAWRCLKINV")) && cell->params.at(id("CLKAWRCLKINV")) == Property("CLKAWRCLK_B"))
	    info.edge = FALLING_EDGE;
	else
	    info.edge = RISING_EDGE;
    }
    if (cell->type == id("RAMB16BWER")) {
	// XXX wrong wrong wrong
        info.clock_port = id("CLKA");
	if (cell->params.count(id("CLKAINV")) && cell->params.at(id("CLKAINV")) == Property("CLKA_B"))
	    info.edge = FALLING_EDGE;
	else
	    info.edge = RISING_EDGE;
    }
    return info;
}

// TODO: validate bel subtype (SLICEM vs SLICEL, IOBM vs IOBS, ...).
bool Arch::isValidBelForCell(CellInfo *cell, BelId bel) const {
	IdString type = getBelType(bel);
	bool is_slice = false;
	if (type == id("LEUCTRA_FF")) {
	    if (cell && !cell->constr_parent) {
	        if (0x924924ull & 1ull << bel.index)
	            return false;
	    }
	    is_slice = true;
	}
	if (type == id("LEUCTRA_LC")) {
	    if (cell) {
            int mask = cell->attrs[id("LOCMASK")].as_int64();
	    int lci = bel.index / 3 % 4;
	    if (!(mask & 1 << lci))
		return false;
            if (cell->attrs[id("NEEDS_L")].as_bool()) {
		if (!(getBelFlags(bel) & (BelPOD::FLAG_SLICEL | BelPOD::FLAG_SLICEM)))
		    return false;
	    }
            if (cell->attrs[id("NEEDS_M")].as_bool()) {
		if (!(getBelFlags(bel) & BelPOD::FLAG_SLICEM))
		    return false;
	    }
	    }
	    is_slice = true;
	}
	if (type == id("RAMB8BWER") && cell) {
	    BelId obel = bel;
	    obel.index = 2;
	    if (getBoundBelCell(obel))
		return false;
	}
	if (type == id("RAMB16BWER") && cell) {
	    BelId obel = bel;
	    obel.index = 0;
	    if (getBoundBelCell(obel))
		return false;
	    obel.index = 1;
	    if (getBoundBelCell(obel))
		return false;
	}
	if (is_slice) {
	    int slice_z = bel.index / 12;
	    NetInfo *clk = nullptr;
	    NetInfo *we = nullptr;
	    NetInfo *ce = nullptr;
	    NetInfo *sr = nullptr;
	    Property ff_mode;
	    Property clk_inv;
	    bool had_ff = false;
	    CellInfo *lcs[4];
	    CellInfo *ffs[8];
	    bool ff_xi_used[4] = {0};
	    for (int i = 0; i < 4; i++) {
		    BelId obel = bel;
		    obel.index = slice_z * 12 + i * 3;
		    if (obel == bel)
		        lcs[i] = cell;
		    else
		        lcs[i] = getBoundBelCell(obel);
		    obel.index++;
		    if (obel == bel)
		        ffs[2*i] = cell;
		    else
		        ffs[2*i] = getBoundBelCell(obel);
		    ff_xi_used[i] = ffs[2*i] && !ffs[2*i]->constr_parent;
		    obel.index++;
		    if (obel == bel)
		        ffs[2*i+1] = cell;
		    else
		        ffs[2*i+1] = getBoundBelCell(obel);
	    }
	    for (auto ff : ffs) {
		if (ff) {
			if (had_ff) {
				if (clk != ff->ports[id("CLK")].net)
					return false;
				if (ce != ff->ports[id("CE")].net)
					return false;
				if (sr != ff->ports[id("SR")].net)
					return false;
				if (ff_mode != ff->params[id("MODE")])
					return false;
				if (clk_inv != ff->params[id("CLKINV")])
					return false;
			} else {
				clk = ff->ports[id("CLK")].net;
				ce = ff->ports[id("CE")].net;
				sr = ff->ports[id("SR")].net;
				ff_mode = ff->params[id("MODE")];
				clk_inv = ff->params[id("CLKINV")];
			}
			had_ff = true;
		}
	    }
	    for (int i = 0; i < 4; i++) {
		CellInfo *lc = lcs[i];
		if (!lc)
			continue;
		if (lc->ports[id("XI")].net && ff_xi_used[i])
			return false;
		if (lc->ports[id("WA7")].net && ff_xi_used[2])
			return false;
		if (lc->ports[id("WA8")].net && ff_xi_used[1])
			return false;
		if (lc->ports[id("DDI8")].net && ff_xi_used[3])
			return false;
		if (lc->ports[id("DDI7")].net && ff_xi_used[i | 1])
			return false;
		if (lc->ports[id("CLK")].net) {
			if (clk) {
				if (clk != lc->ports[id("CLK")].net)
					return false;
				if (clk_inv != lc->params[id("CLKINV")])
					return false;
			} else {
				clk = lc->ports[id("CLK")].net;
				clk_inv = lc->params[id("CLKINV")];
			}
		}
		if (lc->ports[id("WE")].net) {
			if (we) {
				if (we != lc->ports[id("WE")].net)
					return false;
			} else {
				we = lc->ports[id("WE")].net;
			}
		}
	    }
	}
	return true;
}

// Assign arch arg info
void Arch::assignArchInfo()
{
#if 0
    for (auto &net : getCtx()->nets) {
        NetInfo *ni = net.second.get();
        if (isGlobalNet(ni))
            ni->is_global = true;
        ni->is_enable = false;
        ni->is_reset = false;
        for (auto usr : ni->users) {
            if (is_enable_port(this, usr))
                ni->is_enable = true;
            if (is_reset_port(this, usr))
                ni->is_reset = true;
        }
    }
    for (auto &cell : getCtx()->cells) {
        CellInfo *ci = cell.second.get();
        assignCellInfo(ci);
    }
#endif
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

NEXTPNR_NAMESPACE_END

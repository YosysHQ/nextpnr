#!/usr/bin/env python3
import argparse
import json
import sys
from os import path

location_types = dict()
type_at_location = dict()
tiletype_names = dict()
gfx_wire_ids = dict()
gfx_wire_names = list()

parser = argparse.ArgumentParser(description="import ECP5 routing and bels from Project Trellis")
parser.add_argument("device", type=str, help="target device")
parser.add_argument("-p", "--constids", type=str, help="path to constids.inc")
parser.add_argument("-g", "--gfxh", type=str, help="path to gfx.h")
parser.add_argument("-L", "--libdir", type=str, action="append", help="extra Python library path")
args = parser.parse_args()

sys.path += args.libdir
import pytrellis
import database
import pip_classes
import timing_dbs

with open(args.gfxh) as f:
    state = 0
    for line in f:
        if state == 0 and line.startswith("enum GfxTileWireId"):
            state = 1
        elif state == 1 and line.startswith("};"):
            state = 0
        elif state == 1 and (line.startswith("{") or line.strip() == ""):
            pass
        elif state == 1:
            idx = len(gfx_wire_ids)
            name = line.strip().rstrip(",")
            gfx_wire_ids[name] = idx
            gfx_wire_names.append(name)

def gfx_wire_alias(old, new):
    assert old in gfx_wire_ids
    assert new not in gfx_wire_ids
    gfx_wire_ids[new] = gfx_wire_ids[old]


def wire_type(name):
    longname = name
    name = name.split('/')

    if name[0].startswith("X") and name[1].startswith("Y"):
        name = name[2:]

    if name[0].endswith("_SLICE"):
        return "WIRE_TYPE_SLICE"

    if name[0].endswith("_DQS"):
        return "WIRE_TYPE_DQS"

    if name[0].endswith("_IOLOGIC"):
        return "WIRE_TYPE_IOLOGIC"

    if name[0].endswith("_SIOLOGIC"):
        return "WIRE_TYPE_SIOLOGIC"

    if name[0].endswith("_PIO"):
        return "WIRE_TYPE_PIO"

    if name[0].endswith("_DDRDLL"):
        return "WIRE_TYPE_DDRDLL"

    if name[0].endswith("_CCLK"):
        return "WIRE_TYPE_CCLK"

    if name[0].endswith("_EXTREF"):
        return "WIRE_TYPE_EXTREF"

    if name[0].endswith("_DCU"):
        return "WIRE_TYPE_DCU"

    if name[0].endswith("_EBR"):
        return "WIRE_TYPE_EBR"

    if name[0].endswith("_MULT18"):
        return "WIRE_TYPE_MULT18"

    if name[0].endswith("_ALU54"):
        return "WIRE_TYPE_ALU54"

    if name[0].endswith("_PLL"):
        return "WIRE_TYPE_PLL"

    if name[0].endswith("_SED"):
        return "WIRE_TYPE_SED"

    if name[0].endswith("_OSC"):
        return "WIRE_TYPE_OSC"

    if name[0].endswith("_JTAG"):
        return "WIRE_TYPE_JTAG"

    if name[0].endswith("_GSR"):
        return "WIRE_TYPE_GSR"

    if name[0].endswith("_DTR"):
        return "WIRE_TYPE_DTR"

    if name[0].endswith("_PCSCLKDIV0"):
        return "WIRE_TYPE_PCSCLKDIV"

    if name[0].endswith("_PCSCLKDIV1"):
        return "WIRE_TYPE_PCSCLKDIV"

    if name[0].startswith("H00"):
        return "WIRE_TYPE_H00"

    if name[0].startswith("H01"):
        return "WIRE_TYPE_H01"

    if name[0].startswith("HFI"):
        return "WIRE_TYPE_H01"

    if name[0].startswith("HL7"):
        return "WIRE_TYPE_H01"

    if name[0].startswith("H02"):
        return "WIRE_TYPE_H02"

    if name[0].startswith("H06"):
        return "WIRE_TYPE_H06"

    if name[0].startswith("V00"):
        return "WIRE_TYPE_V00"

    if name[0].startswith("V01"):
        return "WIRE_TYPE_V01"

    if name[0].startswith("V02"):
        return "WIRE_TYPE_V02"

    if name[0].startswith("V06"):
        return "WIRE_TYPE_V06"

    if name[0].startswith("G_HPBX"):
        return "WIRE_TYPE_G_HPBX"

    if name[0].startswith("G_VPTX"):
        return "WIRE_TYPE_G_VPTX"

    if name[0].startswith("L_HPBX"):
        return "WIRE_TYPE_L_HPBX"

    if name[0].startswith("R_HPBX"):
        return "WIRE_TYPE_R_HPBX"

    return "WIRE_TYPE_NONE"

def is_global(loc):
    return loc.x == -2 and loc.y == -2


# Get the index for a tiletype
def get_tiletype_index(name):
    if name in tiletype_names:
        return tiletype_names[name]
    idx = len(tiletype_names)
    tiletype_names[name] = idx
    return idx


constids = dict()


class BinaryBlobAssembler:
    def l(self, name, ltype = None, export = False):
        if ltype is None:
            print("label %s" % (name,))
        else:
            print("label %s %s" % (name, ltype))

    def r(self, name, comment):
        if comment is None:
            print("ref %s" % (name,))
        else:
            print("ref %s %s" % (name, comment))

    def r_slice(self, name, length, comment):
        if comment is None:
            print("ref %s" % (name,))
        else:
            print("ref %s %s" % (name, comment))
        print ("u32 %d" % (length, ))

    def s(self, s, comment):
        assert "|" not in s
        print("str |%s| %s" % (s, comment))

    def u8(self, v, comment):
        assert -128 <= int(v) <= 127
        if comment is None:
            print("u8 %d" % (v,))
        else:
            print("u8 %d %s" % (v, comment))

    def u16(self, v, comment):
        # is actually used as signed 16 bit
        assert -32768 <= int(v) <= 32767
        if comment is None:
            print("u16 %d" % (v,))
        else:
            print("u16 %d %s" % (v, comment))

    def u32(self, v, comment):
        if comment is None:
            print("u32 %d" % (v,))
        else:
            print("u32 %d %s" % (v, comment))

    def pre(self, s):
        print("pre %s" % s)

    def post(self, s):
        print("post %s" % s)

    def push(self, name):
        print("push %s" % name)

    def pop(self):
        print("pop")

def get_bel_index(ddrg, loc, name):
    loctype = ddrg.locationTypes[ddrg.typeAtLocation[loc]]
    idx = 0
    for bel in loctype.bels:
        if ddrg.to_str(bel.name) == name:
            return idx
        idx += 1
    assert loc.y == max_row # Only missing IO should be special pins at bottom of device
    return None


packages = {}
pindata = []

def process_pio_db(ddrg, device, package_filter=None):
    piofile = path.join(database.get_db_root(), "ECP5", device, "iodb.json")
    with open(piofile, 'r') as f:
        piodb = json.load(f)
        for pkgname, pkgdata in sorted(piodb["packages"].items()):
            if package_filter is not None and pkgname not in package_filter:
                # we need to get the TQ144 package only out of the non-SERDES device
                # everything else comes from the SERDES device
                continue
            pins = []
            for name, pinloc in sorted(pkgdata.items()):
                x = pinloc["col"]
                y = pinloc["row"]
                loc = pytrellis.Location(x, y)
                pio = "PIO" + pinloc["pio"]
                bel_idx = get_bel_index(ddrg, loc, pio)
                if bel_idx is not None:
                    pins.append((name, loc, bel_idx))
            packages[pkgname] = pins
        if package_filter is None:
            for metaitem in piodb["pio_metadata"]:
                x = metaitem["col"]
                y = metaitem["row"]
                loc = pytrellis.Location(x, y)
                pio = "PIO" + metaitem["pio"]
                bank = metaitem["bank"]
                if "function" in metaitem:
                    pinfunc = metaitem["function"]
                else:
                    pinfunc = None
                dqs = -1
                if "dqs" in metaitem:
                    tdqs = metaitem["dqs"]
                    if tdqs[0] == "L":
                        dqs = 0
                    elif tdqs[0] == "R":
                        dqs = 2048
                    suffix_size = 0
                    while tdqs[-(suffix_size+1)].isdigit():
                        suffix_size += 1
                    dqs |= int(tdqs[-suffix_size:])
                bel_idx = get_bel_index(ddrg, loc, pio)
                if bel_idx is not None:
                    pindata.append((loc, bel_idx, bank, pinfunc, dqs))

global_data = {}
quadrants = ["UL", "UR", "LL", "LR"]
def process_loc_globals(chip):
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            quad = chip.global_data.get_quadrant(y, x)
            tapdrv = chip.global_data.get_tap_driver(y, x)
            if tapdrv.col == x:
                spinedrv = chip.global_data.get_spine_driver(quad, x)
                spine = (spinedrv.second, spinedrv.first)
            else:
                spine = (-1, -1)
            global_data[x, y] = (quadrants.index(quad), int(tapdrv.dir), tapdrv.col, spine)


speed_grade_names = ["6", "7", "8", "8_5G"]
speed_grade_cells = {}
speed_grade_pips = {}

pip_class_to_idx = {"default": 0, "zero": 1}

timing_port_xform = {
    "RAD0": "D0",
    "RAD1": "B0",
    "RAD2": "C0",
    "RAD3": "A0",
}


delay_db = {}
# Convert from Lattice-style grouped SLICE to new nextpnr split style SLICE
def postprocess_timing_data(cells):
    def delay_diff(x, y):
        return (x[0] - y[0], x[1] - y[1])

    split_cells = {}
    comb_delays = {}
    comb_delays[("A", "F")] = delay_db["SLOGICB"][("A0", "F0")]
    comb_delays[("B", "F")] = delay_db["SLOGICB"][("B0", "F0")]
    comb_delays[("C", "F")] = delay_db["SLOGICB"][("C0", "F0")]
    comb_delays[("D", "F")] = delay_db["SLOGICB"][("D0", "F0")]
    comb_delays[("A", "OFX")] = delay_db["SLOGICB"][("A0", "OFX0")]
    comb_delays[("B", "OFX")] = delay_db["SLOGICB"][("B0", "OFX0")]
    comb_delays[("C", "OFX")] = delay_db["SLOGICB"][("C0", "OFX0")]
    comb_delays[("D", "OFX")] = delay_db["SLOGICB"][("D0", "OFX0")]
    comb_delays[("M", "OFX")] = delay_db["SLOGICB"][("M0", "OFX0")] # worst case
    comb_delays[("F1", "OFX")] = delay_diff(delay_db["SLOGICB"][("A1", "OFX0")],
                                            delay_db["SLOGICB"][("A1", "F1")])
    comb_delays[("FXA", "OFX")] = delay_db["SLOGICB"][("FXA", "OFX1")]
    comb_delays[("FXB", "OFX")] = delay_db["SLOGICB"][("FXB", "OFX1")]
    split_cells["TRELLIS_COMB"] = comb_delays

    carry0_delays = {}
    carry0_delays[("A", "F")] = delay_db["SCCU2C"][("A0", "F0")]
    carry0_delays[("B", "F")] = delay_db["SCCU2C"][("B0", "F0")]
    carry0_delays[("C", "F")] = delay_db["SCCU2C"][("C0", "F0")]
    carry0_delays[("D", "F")] = delay_db["SCCU2C"][("D0", "F0")]
    carry0_delays[("A", "FCO")] = delay_db["SCCU2C"][("A0", "FCO")]
    carry0_delays[("B", "FCO")] = delay_db["SCCU2C"][("B0", "FCO")]
    carry0_delays[("C", "FCO")] = delay_db["SCCU2C"][("C0", "FCO")]
    carry0_delays[("D", "FCO")] = delay_db["SCCU2C"][("D0", "FCO")]
    carry0_delays[("FCI", "F")] = delay_db["SCCU2C"][("FCI", "F0")]
    carry0_delays[("FCI", "FCO")] = delay_db["SCCU2C"][("FCI", "FCO")]

    split_cells["TRELLIS_COMB_CARRY0"] = carry0_delays

    carry1_delays = {}
    carry1_delays[("A", "F")] = delay_db["SCCU2C"][("A1", "F1")]
    carry1_delays[("B", "F")] = delay_db["SCCU2C"][("B1", "F1")]
    carry1_delays[("C", "F")] = delay_db["SCCU2C"][("C1", "F1")]
    carry1_delays[("D", "F")] = delay_db["SCCU2C"][("D1", "F1")]
    carry1_delays[("A", "FCO")] = delay_db["SCCU2C"][("A1", "FCO")]
    carry1_delays[("B", "FCO")] = delay_db["SCCU2C"][("B1", "FCO")]
    carry1_delays[("C", "FCO")] = delay_db["SCCU2C"][("C1", "FCO")]
    carry1_delays[("D", "FCO")] = delay_db["SCCU2C"][("D1", "FCO")]
    carry1_delays[("FCI", "F")] = delay_diff(delay_db["SCCU2C"][("FCI", "F1")], delay_db["SCCU2C"][("FCI", "FCO")])
    carry1_delays[("FCI", "FCO")] = (0, 0)

    split_cells["TRELLIS_COMB_CARRY1"] = carry1_delays

    for celltype, celldelays in sorted(split_cells.items()):
        delays = []
        setupholds = []
        for (from_pin, to_pin), (min_delay, max_delay) in sorted(celldelays.items()):
            delays.append((constids[from_pin], constids[to_pin], min_delay, max_delay))
        cells.append((constids[celltype], delays, setupholds))

def process_timing_data():
    for grade in speed_grade_names:
        with open(timing_dbs.cells_db_path("ECP5", grade)) as f:
            cell_data = json.load(f)
        cells = []
        for cell, cdata in sorted(cell_data.items()):
            celltype = constids[cell.replace(":", "_").replace("=", "_").replace(",", "_")]
            delays = []
            setupholds = []
            delay_db[cell] = {}
            for entry in cdata:
                if entry["type"] == "Width":
                    continue
                elif entry["type"] == "IOPath":
                    from_pin = entry["from_pin"][1] if type(entry["from_pin"]) is list else entry["from_pin"]
                    if from_pin in timing_port_xform:
                        from_pin = timing_port_xform[from_pin]
                    to_pin = entry["to_pin"]
                    if to_pin in timing_port_xform:
                        to_pin = timing_port_xform[to_pin]
                    min_delay = min(entry["rising"][0], entry["falling"][0])
                    max_delay = min(entry["rising"][2], entry["falling"][2])
                    delay_db[cell][(from_pin, to_pin)] = (min_delay, max_delay)
                    delays.append((constids[from_pin], constids[to_pin], min_delay, max_delay))
                elif entry["type"] == "SetupHold":
                    if type(entry["pin"]) is list:
                        continue
                    pin = constids[entry["pin"]]
                    clock = constids[entry["clock"][1]]
                    min_setup = entry["setup"][0]
                    max_setup = entry["setup"][2]
                    min_hold = entry["hold"][0]
                    max_hold = entry["hold"][2]
                    setupholds.append((pin, clock, min_setup, max_setup, min_hold, max_hold))
                else:
                    assert False, entry["type"]
            cells.append((celltype, delays, setupholds))
        postprocess_timing_data(cells)
        pip_class_delays = []
        for i in range(len(pip_class_to_idx)):
            pip_class_delays.append((50, 50, 0, 0))
        pip_class_delays[pip_class_to_idx["zero"]] = (0, 0, 0, 0)
        with open(timing_dbs.interconnect_db_path("ECP5", grade)) as f:
            interconn_data = json.load(f)
        for pipclass, pipdata in sorted(interconn_data.items()):

            min_delay = pipdata["delay"][0] * 1.1
            max_delay = pipdata["delay"][2] * 1.1
            min_fanout = pipdata["fanout"][0]
            max_fanout = pipdata["fanout"][2]
            if grade == "6":
                pip_class_to_idx[pipclass] = len(pip_class_delays)
                pip_class_delays.append((min_delay, max_delay, min_fanout, max_fanout))
            else:
                if pipclass in pip_class_to_idx:
                    pip_class_delays[pip_class_to_idx[pipclass]] = (min_delay, max_delay, min_fanout, max_fanout)
        speed_grade_cells[grade] = cells
        speed_grade_pips[grade] = pip_class_delays


def get_pip_class(wire_from, wire_to):

    if "FCO" in wire_from or "FCI" in wire_to:
        return pip_class_to_idx["zero"]
    if "F5" in wire_from or "FX" in wire_from or "FXA" in wire_to or "FXB" in wire_to:
        return pip_class_to_idx["zero"]

    class_name = pip_classes.get_pip_class(wire_from, wire_to)
    if class_name is None or class_name not in pip_class_to_idx:
        class_name = "default"
    return pip_class_to_idx[class_name]



def write_database(dev_name, chip, ddrg, endianness):
    def write_loc(loc, sym_name):
        bba.u16(loc.x, "%s.x" % sym_name)
        bba.u16(loc.y, "%s.y" % sym_name)

    loctypes = list([_.key() for _ in ddrg.locationTypes])
    loc_with_type = {}
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            loc_with_type[loctypes.index(ddrg.typeAtLocation[pytrellis.Location(x, y)])] = (x, y)

    def get_wire_name(arc_loctype, rel, idx):
        loc = loc_with_type[arc_loctype]
        lt = ddrg.typeAtLocation[pytrellis.Location(loc[0] + rel.x, loc[1] + rel.y)]
        wire = ddrg.locationTypes[lt].wires[idx]
        return "R{}C{}_{}".format(loc[1] + rel.y, loc[0] + rel.x, ddrg.to_str(wire.name))

    bba = BinaryBlobAssembler()
    bba.pre('#include "nextpnr.h"')
    bba.pre('#include "embed.h"')
    bba.pre('NEXTPNR_NAMESPACE_BEGIN')
    bba.post('EmbeddedFile chipdb_file_%s("ecp5/chipdb-%s.bin", chipdb_blob_%s);' % (dev_name, dev_name, dev_name))
    bba.post('NEXTPNR_NAMESPACE_END')
    bba.push("chipdb_blob_%s" % dev_name)
    bba.r("chip_info", "chip_info")


    for idx in range(len(loctypes)):
        loctype = ddrg.locationTypes[loctypes[idx]]
        if len(loctype.arcs) > 0:
            bba.l("loc%d_pips" % idx, "PipInfoPOD")
            for arc in loctype.arcs:
                write_loc(arc.srcWire.rel, "src")
                write_loc(arc.sinkWire.rel, "dst")
                bba.u16(arc.srcWire.id, "src_idx")
                bba.u16(arc.sinkWire.id, "dst_idx")
                src_name = get_wire_name(idx, arc.srcWire.rel, arc.srcWire.id)
                snk_name = get_wire_name(idx, arc.sinkWire.rel, arc.sinkWire.id)
                bba.u16(get_pip_class(src_name, snk_name), "timing_class")
                bba.u8(get_tiletype_index(ddrg.to_str(arc.tiletype)), "tile_type")
                cls = arc.cls
                if cls == 1 and "PCS" in snk_name or "DCU" in snk_name or "DCU" in src_name:
                   cls = 2
                bba.u8(cls, "pip_type")
                bba.u16(arc.lutperm_flags, "lutperm_flags")
                bba.u16(0, "padding")
        if len(loctype.wires) > 0:
            for wire_idx in range(len(loctype.wires)):
                wire = loctype.wires[wire_idx]
                if len(wire.arcsDownhill) > 0:
                    bba.l("loc%d_wire%d_downpips" % (idx, wire_idx), "PipLocatorPOD")
                    for dp in wire.arcsDownhill:
                        write_loc(dp.rel, "rel_loc")
                        bba.u32(dp.id, "index")
                if len(wire.arcsUphill) > 0:
                    bba.l("loc%d_wire%d_uppips" % (idx, wire_idx), "PipLocatorPOD")
                    for up in wire.arcsUphill:
                        write_loc(up.rel, "rel_loc")
                        bba.u32(up.id, "index")
                if len(wire.belPins) > 0:
                    bba.l("loc%d_wire%d_belpins" % (idx, wire_idx), "BelPortPOD")
                    for bp in wire.belPins:
                        write_loc(bp.bel.rel, "rel_bel_loc")
                        bba.u32(bp.bel.id, "bel_index")
                        bba.u32(constids[ddrg.to_str(bp.pin)], "port")
            bba.l("loc%d_wires" % idx, "WireInfoPOD")
            for wire_idx in range(len(loctype.wires)):
                wire = loctype.wires[wire_idx]
                bba.s(ddrg.to_str(wire.name), "name")
                bba.u16(constids[wire_type(ddrg.to_str(wire.name))], "type")
                if ("TILE_WIRE_" + ddrg.to_str(wire.name)) in gfx_wire_ids:
                    bba.u16(gfx_wire_ids["TILE_WIRE_" + ddrg.to_str(wire.name)], "tile_wire")
                else:
                    bba.u16(0, "tile_wire")
                bba.r_slice("loc%d_wire%d_uppips" % (idx, wire_idx) if len(wire.arcsUphill) > 0 else None, len(wire.arcsUphill), "pips_uphill")
                bba.r_slice("loc%d_wire%d_downpips" % (idx, wire_idx) if len(wire.arcsDownhill) > 0 else None, len(wire.arcsDownhill), "pips_downhill")
                bba.r_slice("loc%d_wire%d_belpins" % (idx, wire_idx) if len(wire.belPins) > 0 else None, len(wire.belPins), "bel_pins")

        if len(loctype.bels) > 0:
            for bel_idx in range(len(loctype.bels)):
                bel = loctype.bels[bel_idx]
                bba.l("loc%d_bel%d_wires" % (idx, bel_idx), "BelWirePOD")
                for pin in bel.wires:
                    write_loc(pin.wire.rel, "rel_wire_loc")
                    bba.u32(pin.wire.id, "wire_index")
                    bba.u32(constids[ddrg.to_str(pin.pin)], "port")
                    bba.u32(int(pin.dir), "dir")
            bba.l("loc%d_bels" % idx, "BelInfoPOD")
            for bel_idx in range(len(loctype.bels)):
                bel = loctype.bels[bel_idx]
                bba.s(ddrg.to_str(bel.name), "name")
                bba.u32(constids[ddrg.to_str(bel.type)], "type")
                bba.u32(bel.z, "z")
                bba.r_slice("loc%d_bel%d_wires" % (idx, bel_idx), len(bel.wires), "bel_wires")

    bba.l("locations", "LocationTypePOD")
    for idx in range(len(loctypes)):
        loctype = ddrg.locationTypes[loctypes[idx]]
        bba.r_slice("loc%d_bels" % idx if len(loctype.bels) > 0 else None, len(loctype.bels), "bel_data")
        bba.r_slice("loc%d_wires" % idx if len(loctype.wires) > 0 else None, len(loctype.wires), "wire_data")
        bba.r_slice("loc%d_pips" % idx if len(loctype.arcs) > 0 else None, len(loctype.arcs), "pips_data")

    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            bba.l("tile_info_%d_%d" % (x, y), "TileNamePOD")
            for tile in chip.get_tiles_by_position(y, x):
                bba.s(tile.info.name, "name")
                bba.u16(get_tiletype_index(tile.info.type), "type_idx")
                bba.u16(0, "padding")

    bba.l("tiles_info", "TileInfoPOD")
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            bba.r_slice("tile_info_%d_%d" % (x, y), len(chip.get_tiles_by_position(y, x)), "tile_names")

    bba.l("location_types", "int32_t")
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            bba.u32(loctypes.index(ddrg.typeAtLocation[pytrellis.Location(x, y)]), "loctype")

    bba.l("location_glbinfo", "GlobalInfoPOD")
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            bba.u16(global_data[x, y][2], "tap_col")
            bba.u8(global_data[x, y][1], "tap_dir")
            bba.u8(global_data[x, y][0], "quad")
            bba.u16(global_data[x, y][3][1], "spine_row")
            bba.u16(global_data[x, y][3][0], "spine_col")

    for package, pkgdata in sorted(packages.items()):
        bba.l("package_data_%s" % package, "PackagePinPOD")
        for pin in pkgdata:
            name, loc, bel_idx = pin
            bba.s(name, "name")
            write_loc(loc, "abs_loc")
            bba.u32(bel_idx, "bel_index")

    bba.l("package_data", "PackageInfoPOD")
    for package, pkgdata in sorted(packages.items()):
        bba.s(package, "name")
        bba.r_slice("package_data_%s" % package, len(pkgdata), "pin_data")

    bba.l("pio_info", "PIOInfoPOD")
    for pin in pindata:
        loc, bel_idx, bank, func, dqs = pin
        write_loc(loc, "abs_loc")
        bba.u32(bel_idx, "bel_index")
        if func is not None and func != "WRITEN":
            bba.s(func, "function_name")
        else:
            bba.r(None, "function_name")
        bba.u16(bank, "bank")
        bba.u16(dqs, "dqsgroup")

    bba.l("tiletype_names", "RelPtr<char>")
    for tt, idx in sorted(tiletype_names.items(), key=lambda x: x[1]):
        bba.s(tt, "name")

    for grade in speed_grade_names:
        for cell in speed_grade_cells[grade]:
            celltype, delays, setupholds = cell
            if len(delays) > 0:
                bba.l("cell_%d_delays_%s" % (celltype, grade))
                for delay in delays:
                    from_pin, to_pin, min_delay, max_delay = delay
                    bba.u32(from_pin, "from_pin")
                    bba.u32(to_pin, "to_pin")
                    bba.u32(min_delay, "min_delay")
                    bba.u32(max_delay, "max_delay")
            if len(setupholds) > 0:
                bba.l("cell_%d_setupholds_%s" % (celltype, grade))
                for sh in setupholds:
                    pin, clock, min_setup, max_setup, min_hold, max_hold = sh
                    bba.u32(pin, "sig_port")
                    bba.u32(clock, "clock_port")
                    bba.u32(min_setup, "min_setup")
                    bba.u32(max_setup, "max_setup")
                    bba.u32(min_hold, "min_hold")
                    bba.u32(max_hold, "max_hold")
        bba.l("cell_timing_data_%s" % grade)
        for cell in speed_grade_cells[grade]:
            celltype, delays, setupholds = cell
            bba.u32(celltype, "cell_type")
            bba.r_slice("cell_%d_delays_%s" % (celltype, grade) if len(delays) > 0 else None, len(delays), "delays")
            bba.r_slice("cell_%d_setupholds_%s" % (celltype, grade) if len(delays) > 0 else None, len(setupholds), "setupholds")
        bba.l("pip_timing_data_%s" % grade)
        for pipclass in speed_grade_pips[grade]:
            min_delay, max_delay, min_fanout, max_fanout = pipclass
            bba.u32(min_delay, "min_delay")
            bba.u32(max_delay, "max_delay")
            bba.u32(min_fanout, "min_fanout")
            bba.u32(max_fanout, "max_fanout")
    bba.l("speed_grade_data")
    for grade in speed_grade_names:
        bba.r_slice("cell_timing_data_%s" % grade, len(speed_grade_cells[grade]), "cell_timings")
        bba.r_slice("pip_timing_data_%s" % grade, len(speed_grade_pips[grade]), "pip_classes")

    bba.l("chip_info")
    bba.u32(max_col + 1, "width")
    bba.u32(max_row + 1, "height")
    bba.u32((max_col + 1) * (max_row + 1), "num_tiles")
    bba.u32(const_id_count, "const_id_count")

    bba.r_slice("locations", len(loctypes), "locations")
    bba.r_slice("location_types", (max_col + 1) * (max_row + 1), "location_type")
    bba.r_slice("location_glbinfo", (max_col + 1) * (max_row + 1), "location_glbinfo")
    bba.r_slice("tiletype_names", len(tiletype_names), "tiletype_names")
    bba.r_slice("package_data", len(packages), "package_info")
    bba.r_slice("pio_info", len(pindata), "pio_info")
    bba.r_slice("tiles_info", (max_col + 1) * (max_row + 1), "tile_info")
    bba.r_slice("speed_grade_data", len(speed_grade_names), "speed_grades")

    bba.pop()
    return bba

dev_names = {"25k": "LFE5UM5G-25F", "45k": "LFE5UM5G-45F", "85k": "LFE5UM5G-85F"}

def main():
    global max_row, max_col, const_id_count
    pytrellis.load_database(database.get_db_root())
    args = parser.parse_args()

    # Read port pin file
    const_id_count = 1 # count ID_NONE
    with open(args.constids) as f:
        for line in f:
            line = line.replace("(", " ")
            line = line.replace(")", " ")
            line = line.split()
            if len(line) == 0:
                continue
            assert len(line) == 2
            assert line[0] == "X"
            idx = len(constids) + 1
            constids[line[1]] = idx
            const_id_count += 1

    constids["SLICE"] = constids["TRELLIS_SLICE"]
    constids["PIO"] = constids["TRELLIS_IO"]

    # print("Initialising chip...")
    chip = pytrellis.Chip(dev_names[args.device])
    # print("Building routing graph...")
    ddrg = pytrellis.make_dedup_chipdb(chip, include_lutperm_pips=True, split_slice_mode=True)
    max_row = chip.get_max_row()
    max_col = chip.get_max_col()
    process_timing_data()
    process_pio_db(ddrg, dev_names[args.device])
    # add TQFP144 package from non-SERDES device if appropriate
    if args.device == "25k": process_pio_db(ddrg, "LFE5U-25F", {"TQFP144", })
    if args.device == "45k": process_pio_db(ddrg, "LFE5U-45F", {"TQFP144", })
    process_loc_globals(chip)
    # print("{} unique location types".format(len(ddrg.locationTypes)))
    bba = write_database(args.device, chip, ddrg, "le")



if __name__ == "__main__":
    main()

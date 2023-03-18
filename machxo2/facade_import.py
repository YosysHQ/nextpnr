#!/usr/bin/env python3
import argparse
import json
import sys
from os import path

tiletype_names = dict()
gfx_wire_ids = dict()
gfx_wire_names = list()

parser = argparse.ArgumentParser(description="import MachXO2 routing and bels from Project Trellis")
parser.add_argument("device", type=str, help="target device")
parser.add_argument("-p", "--constids", type=str, help="path to constids.inc")
parser.add_argument("-g", "--gfxh", type=str, help="path to gfx.h (unused)")
parser.add_argument("-L", "--libdir", type=str, action="append", help="extra Python library path")
args = parser.parse_args()

sys.path += args.libdir
import pytrellis
import database

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

def wire_type(name):
    return "WIRE_TYPE_NONE"

# Get the index for a tiletype
def get_tiletype_index(name):
    if name in tiletype_names:
        return tiletype_names[name]
    idx = len(tiletype_names)
    tiletype_names[name] = idx
    return idx

def package_shortname(long_name, family):
    if long_name.startswith("CABGA"):
        if (family == "MachXO"):
            return "B" + long_name[5:]
        else:
            return "BG" + long_name[5:]
    elif long_name.startswith("CSBGA"):
        if (family == "MachXO"):
            return "M" + long_name[5:]
        else:
            return "MG" + long_name[5:]
    elif long_name.startswith("CSFBGA"):
        return "MG" + long_name[6:]
    elif long_name.startswith("UCBGA"):
        return "UMG" + long_name[5:]
    elif long_name.startswith("FPBGA"):
        return "FG" + long_name[5:]
    elif long_name.startswith("FTBGA"):
        if (family == "MachXO"):
            return "FT" + long_name[5:]
        else:
            return "FTG" + long_name[5:]
    elif long_name.startswith("WLCSP"):
        if (family == "MachXO3D"):
            return "UTG" + long_name[5:]
        else:
            return "UWG" + long_name[5:]
    elif long_name.startswith("TQFP"):
        if (family == "MachXO"):
            return "T" + long_name[4:]
        else:
            return "TG" + long_name[4:]
    elif long_name.startswith("QFN"):
        if (family == "MachXO3D"):
            return "SG" + long_name[3:]
        else:
            if long_name[3]=="8":
                return "QN" + long_name[3:]
            else:
                return "SG" + long_name[3:]
    else:
        print("unknown package name " + long_name)
        sys.exit(-1)

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

def get_bel_index(rg, loc, name):
    tile = rg.tiles[loc]
    idx = 0
    for bel in tile.bels:
        if rg.to_str(bel.name) == name:
            return idx
        idx += 1
    # FIXME: I/O pins can be missing in various rows. Is there a nice way to
    # assert on each device size?
    return None


packages = {}
pindata = []
variants = {}

def process_devices_db(family, device):
    devicefile = path.join(database.get_db_root(), "devices.json")
    with open(devicefile, 'r') as f:
        devicedb = json.load(f)
        for varname, vardata in sorted(devicedb["families"][family]["devices"][device]["variants"].items()):
            variants[varname] = vardata

def process_pio_db(rg, device):
    piofile = path.join(database.get_db_root(), dev_family[device], dev_names[device], "iodb.json")
    with open(piofile, 'r') as f:
        piodb = json.load(f)
        for pkgname, pkgdata in sorted(piodb["packages"].items()):
            pins = []
            for name, pinloc in sorted(pkgdata.items()):
                x = pinloc["col"]
                y = pinloc["row"]
                loc = pytrellis.Location(x, y)
                pio = "PIO" + pinloc["pio"]
                bel_idx = get_bel_index(rg, loc, pio)
                if bel_idx is not None:
                    pins.append((name, loc, bel_idx))
            packages[pkgname] = pins
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
                pass
                # tdqs = metaitem["dqs"]
                # if tdqs[0] == "L":
                #     dqs = 0
                # elif tdqs[0] == "R":
                #     dqs = 2048
                # suffix_size = 0
                # while tdqs[-(suffix_size+1)].isdigit():
                #     suffix_size += 1
                # dqs |= int(tdqs[-suffix_size:])
            bel_idx = get_bel_index(rg, loc, pio)
            if bel_idx is not None:
                pindata.append((loc, bel_idx, bank, pinfunc, dqs))

def write_database(dev_name, chip, rg, endianness):
    def write_loc(loc, sym_name):
        bba.u16(loc.x, "%s.x" % sym_name)
        bba.u16(loc.y, "%s.y" % sym_name)

    # Use Lattice naming conventions, so convert to 1-based col indexing.
    def get_wire_name(loc, idx):
        tile = rg.tiles[loc]
        return "R{}C{}_{}".format(loc.y, loc.x + 1, rg.to_str(tile.wires[idx].name))

    # Before doing anything, ensure sorted routing graph iteration matches
    # y, x
    loc_iter = list(sorted(rg.tiles, key=lambda l : (l.y, l.x)))

    i = 1 # Drop (-2, -2) location.
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            l = loc_iter[i]
            assert((y, x) == (l.y, l.x))
            i = i + 1

    bba = BinaryBlobAssembler()
    bba.pre('#include "nextpnr.h"')
    bba.pre('#include "embed.h"')
    bba.pre('NEXTPNR_NAMESPACE_BEGIN')
    bba.post('EmbeddedFile chipdb_file_%s("machxo2/chipdb-%s.bin", chipdb_blob_%s);' % (dev_name, dev_name, dev_name))
    bba.post('NEXTPNR_NAMESPACE_END')
    bba.push("chipdb_blob_%s" % args.device)
    bba.r("chip_info", "chip_info")

    # Nominally should be in order, but support situations where python
    # decides to iterate over rg.tiles out-of-order.
    for l in loc_iter:
        t = rg.tiles[l]

        # Do not include special globals location for now.
        if (l.x, l.y) == (-2, -2):
            continue

        if len(t.arcs) > 0:
            bba.l("loc%d_%d_pips" % (l.y, l.x), "PipInfoPOD")
            for arc in t.arcs:
                write_loc(arc.srcWire.rel, "src")
                write_loc(arc.sinkWire.rel, "dst")
                bba.u32(arc.srcWire.id, "src_idx {}".format(get_wire_name(arc.srcWire.rel, arc.srcWire.id)))
                bba.u32(arc.sinkWire.id, "dst_idx {}".format(get_wire_name(arc.sinkWire.rel, arc.sinkWire.id)))
                src_name = get_wire_name(arc.srcWire.rel, arc.srcWire.id)
                snk_name = get_wire_name(arc.sinkWire.rel, arc.sinkWire.id)
                # TODO: ECP5 timing-model-specific. Reuse for MachXO2?
                # bba.u32(get_pip_class(src_name, snk_name), "timing_class")
                bba.u32(0, "timing_class")
                bba.u16(get_tiletype_index(rg.to_str(arc.tiletype)), "tile_type")
                cls = arc.cls
                bba.u8(arc.cls, "pip_type")
                bba.u8(0, "padding")

        if len(t.wires) > 0:
            for wire_idx in range(len(t.wires)):
                wire = t.wires[wire_idx]
                if len(wire.arcsDownhill) > 0:
                    bba.l("loc%d_%d_wire%d_downpips" % (l.y, l.x, wire_idx), "PipLocatorPOD")
                    for dp in wire.arcsDownhill:
                        write_loc(dp.rel, "rel_loc")
                        bba.u32(dp.id, "index")
                if len(wire.arcsUphill) > 0:
                    bba.l("loc%d_%d_wire%d_uppips" % (l.y, l.x, wire_idx), "PipLocatorPOD")
                    for up in wire.arcsUphill:
                        write_loc(up.rel, "rel_loc")
                        bba.u32(up.id, "index")
                if len(wire.belPins) > 0:
                    bba.l("loc%d_%d_wire%d_belpins" % (l.y, l.x, wire_idx), "BelPortPOD")
                    for bp in wire.belPins:
                        write_loc(bp.bel.rel, "rel_bel_loc")
                        bba.u32(bp.bel.id, "bel_index")
                        bba.u32(constids[rg.to_str(bp.pin)], "port")

            bba.l("loc%d_%d_wires" % (l.y, l.x), "WireInfoPOD")
            for wire_idx in range(len(t.wires)):
                wire = t.wires[wire_idx]
                bba.s(rg.to_str(wire.name), "name")
                bba.u16(constids[wire_type(rg.to_str(wire.name))], "type")
                if ("TILE_WIRE_" + rg.to_str(wire.name)) in gfx_wire_ids:
                    bba.u16(gfx_wire_ids["TILE_WIRE_" + rg.to_str(wire.name)], "tile_wire")
                else:
                    bba.u16(0, "tile_wire")
                bba.r_slice("loc%d_%d_wire%d_uppips" % (l.y, l.x, wire_idx) if len(wire.arcsUphill) > 0 else None, len(wire.arcsUphill), "pips_uphill")
                bba.r_slice("loc%d_%d_wire%d_downpips" % (l.y, l.x, wire_idx) if len(wire.arcsDownhill) > 0 else None, len(wire.arcsDownhill), "pips_downhill")
                bba.r_slice("loc%d_%d_wire%d_belpins" % (l.y, l.x, wire_idx) if len(wire.belPins) > 0 else None, len(wire.belPins), "bel_pins")

        if len(t.bels) > 0:
            for bel_idx in range(len(t.bels)):
                bel = t.bels[bel_idx]
                bba.l("loc%d_%d_bel%d_wires" % (l.y, l.x, bel_idx), "BelWirePOD")
                for pin in bel.wires:
                    write_loc(pin.wire.rel, "rel_wire_loc")
                    bba.u32(pin.wire.id, "wire_index")
                    bba.u32(constids[rg.to_str(pin.pin)], "port")
                    bba.u32(int(pin.dir), "type")
            bba.l("loc%d_%d_bels" % (l.y, l.x), "BelInfoPOD")
            for bel_idx in range(len(t.bels)):
                bel = t.bels[bel_idx]
                bba.s(rg.to_str(bel.name), "name")
                bba.u32(constids[rg.to_str(bel.type)], "type")
                bba.u32(bel.z, "z")
                bba.r_slice("loc%d_%d_bel%d_wires" % (l.y, l.x, bel_idx), len(bel.wires), "bel_wires")

    bba.l("tiles", "TileTypePOD")
    for l in loc_iter:
        t = rg.tiles[l]

        if (l.y, l.x) == (-2, -2):
            continue

        bba.r_slice("loc%d_%d_bels" % (l.y, l.x) if len(t.bels) > 0 else None, len(t.bels), "bel_data")
        bba.r_slice("loc%d_%d_wires" % (l.y, l.x) if len(t.wires) > 0 else None, len(t.wires), "wire_data")
        bba.r_slice("loc%d_%d_pips" % (l.y, l.x) if len(t.arcs) > 0 else None, len(t.arcs), "pips_data")

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
        # TODO: io_grouping? And DQS.
        bba.u16(bank, "bank")
        bba.u16(dqs, "dqsgroup")

    bba.l("tiletype_names", "RelPtr<char>")
    for tt, idx in sorted(tiletype_names.items(), key=lambda x: x[1]):
        bba.s(tt, "name")

    for name, var_data in sorted(variants.items()):
        bba.l("supported_packages_%s" % name, "PackageSupportedPOD")
        for package in var_data["packages"]:
            bba.s(package, "name")
            bba.s(package_shortname(package, chip.info.family), "short_name")
        bba.l("supported_speed_grades_%s" % name, "SpeedSupportedPOD")
        for speed in var_data["speeds"]:
            bba.u32(speed, "speed")
        bba.l("supported_suffixes_%s" % name, "SuffixeSupportedPOD")
        for suffix in var_data["suffixes"]:
            bba.s(suffix, "suffix")

    bba.l("variant_data", "VariantInfoPOD")
    for name, var_data in sorted(variants.items()):
        bba.s(name, "variant_name")
        bba.r_slice("supported_packages_%s" % name, len(var_data["packages"]), "supported_packages")
        bba.r_slice("supported_speed_grades_%s" % name, len(var_data["speeds"]), "supported_speed_grades")
        bba.r_slice("supported_suffixes_%s" % name, len(var_data["suffixes"]), "supported_suffixes")

    bba.l("chip_info")
    bba.s(chip.info.family, "family")
    bba.s(chip.info.name, "device_name")
    bba.u32(max_col + 1, "width")
    bba.u32(max_row + 1, "height")
    bba.u32((max_col + 1) * (max_row + 1), "num_tiles")
    bba.u32(const_id_count, "const_id_count")

    bba.r_slice("tiles", (max_col + 1) * (max_row + 1), "tiles")
    bba.r_slice("tiletype_names", len(tiletype_names), "tiletype_names")
    bba.r_slice("package_data", len(packages), "package_info")
    bba.r_slice("pio_info", len(pindata), "pio_info")
    bba.r_slice("tiles_info", (max_col + 1) * (max_row + 1), "tile_info")
    bba.r_slice("variant_data", len(variants), "variant_info")

    bba.pop()
    return bba


dev_family = {
    "256X": "MachXO",
    "640X": "MachXO",
    "1200X":"MachXO",
    "2280X":"MachXO",

    "256":  "MachXO2",
    "640":  "MachXO2",
    "1200": "MachXO2",
    "2000": "MachXO2",
    "4000": "MachXO2",
    "7000": "MachXO2"
}

dev_names = {
    "256X": "LCMXO256",
    "640X": "LCMXO640",
    "1200X":"LCMXO1200",
    "2280X":"LCMXO2280",

    "256":  "LCMXO2-256",
    "640":  "LCMXO2-640",
    "1200": "LCMXO2-1200",
    "2000": "LCMXO2-2000",
    "4000": "LCMXO2-4000",
    "7000": "LCMXO2-7000"
}

def main():
    global max_row, max_col, const_id_count

    pytrellis.load_database(database.get_db_root())
    args = parser.parse_args()

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

    chip = pytrellis.Chip(dev_names[args.device])
    rg = pytrellis.make_optimized_chipdb(chip, split_slice_mode=False)
    max_row = chip.get_max_row()
    max_col = chip.get_max_col()
    process_pio_db(rg, args.device)
    process_devices_db(chip.info.family, chip.info.name)
    bba = write_database(args.device, chip, rg, "le")



if __name__ == "__main__":
    main()

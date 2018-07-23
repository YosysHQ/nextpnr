#!/usr/bin/env python3
import pytrellis
import database
import argparse
import json
from os import path

location_types = dict()
type_at_location = dict()
tiletype_names = dict()

parser = argparse.ArgumentParser(description="import ECP5 routing and bels from Project Trellis")
group = parser.add_mutually_exclusive_group()
group.add_argument("-b", "--binary", action="store_true")
group.add_argument("-c", "--c_file", action="store_true")
parser.add_argument("device", type=str, help="target device")
parser.add_argument("outfile", type=argparse.FileType('w'), help="output filename")
parser.add_argument("-p", "--portspins", type=str, help="path to portpins.inc")
args = parser.parse_args()


def is_global(loc):
    return loc.x == -2 and loc.y == -2


# Get the index for a tiletype
def get_tiletype_index(name):
    if name in tiletype_names:
        return tiletype_names[name]
    idx = len(tiletype_names)
    tiletype_names[name] = idx
    return idx


portpins = dict()


class BinaryBlobAssembler:
    def __init__(self, cname, endianness, nodebug=False):
        assert endianness in ["le", "be"]
        self.cname = cname
        self.endianness = endianness
        self.finalized = False
        self.data = bytearray()
        self.comments = dict()
        self.labels = dict()
        self.exports = set()
        self.labels_byaddr = dict()
        self.ltypes_byaddr = dict()
        self.strings = dict()
        self.refs = dict()
        self.nodebug = nodebug

    def l(self, name, ltype=None, export=False):
        assert not self.finalized
        assert name not in self.labels
        assert len(self.data) not in self.labels_byaddr
        self.labels[name] = len(self.data)
        if ltype is not None:
            self.ltypes_byaddr[len(self.data)] = ltype
        self.labels_byaddr[len(self.data)] = name
        if export:
            assert ltype is not None
            self.exports.add(len(self.data))

    def r(self, name, comment):
        assert not self.finalized
        assert len(self.data) % 4 == 0
        assert len(self.data) not in self.refs
        if self.nodebug:
            comment = None
        if name is not None:
            self.refs[len(self.data)] = (name, comment)
        self.data.append(0)
        self.data.append(0)
        self.data.append(0)
        self.data.append(0)
        if (name is None) and (comment is not None):
            self.comments[len(self.data)] = comment + " (null reference)"

    def s(self, s, comment):
        assert not self.finalized
        if self.nodebug:
            comment = None
        if s not in self.strings:
            index = len(self.strings)
            self.strings[s] = index
        else:
            index = self.strings[s]
        if comment is not None:
            self.r("str%d" % index, '%s: "%s"' % (comment, s))
        else:
            self.r("str%d" % index, None)

    def u8(self, v, comment):
        assert not self.finalized
        if self.nodebug:
            comment = None
        self.data.append(v)
        if comment is not None:
            self.comments[len(self.data)] = comment

    def u16(self, v, comment):
        assert not self.finalized
        assert len(self.data) % 2 == 0
        if self.nodebug:
            comment = None
        if self.endianness == "le":
            self.data.append(v & 255)
            self.data.append((v >> 8) & 255)
        elif self.endianness == "be":
            self.data.append((v >> 8) & 255)
            self.data.append(v & 255)
        else:
            assert 0
        if comment is not None:
            self.comments[len(self.data)] = comment

    def s16(self, v, comment):
        assert not self.finalized
        assert len(self.data) % 2 == 0
        if self.nodebug:
            comment = None
        c2val = (((-v) ^ 0xffff) + 1) if v < 0 else v
        if self.endianness == "le":
            self.data.append(c2val & 255)
            self.data.append((c2val >> 8) & 255)
        elif self.endianness == "be":
            self.data.append((c2val >> 8) & 255)
            self.data.append(c2val & 255)
        else:
            assert 0
        if comment is not None:
            self.comments[len(self.data)] = comment

    def u32(self, v, comment):
        assert not self.finalized
        assert len(self.data) % 4 == 0
        if self.nodebug:
            comment = None
        if self.endianness == "le":
            self.data.append(v & 255)
            self.data.append((v >> 8) & 255)
            self.data.append((v >> 16) & 255)
            self.data.append((v >> 24) & 255)
        elif self.endianness == "be":
            self.data.append((v >> 24) & 255)
            self.data.append((v >> 16) & 255)
            self.data.append((v >> 8) & 255)
            self.data.append(v & 255)
        else:
            assert 0
        if comment is not None:
            self.comments[len(self.data)] = comment

    def finalize(self):
        assert not self.finalized
        for s, index in sorted(self.strings.items()):
            self.l("str%d" % index, "char")
            for c in s:
                self.data.append(ord(c))
            self.data.append(0)
        self.finalized = True
        cursor = 0
        while cursor < len(self.data):
            if cursor in self.refs:
                v = self.labels[self.refs[cursor][0]] - cursor
                if self.endianness == "le":
                    self.data[cursor + 0] = (v & 255)
                    self.data[cursor + 1] = ((v >> 8) & 255)
                    self.data[cursor + 2] = ((v >> 16) & 255)
                    self.data[cursor + 3] = ((v >> 24) & 255)
                elif self.endianness == "be":
                    self.data[cursor + 0] = ((v >> 24) & 255)
                    self.data[cursor + 1] = ((v >> 16) & 255)
                    self.data[cursor + 2] = ((v >> 8) & 255)
                    self.data[cursor + 3] = (v & 255)
                else:
                    assert 0
                cursor += 4
            else:
                cursor += 1

    def write_verbose_c(self, f, ctype="const unsigned char"):
        assert self.finalized
        print("%s %s[%d] = {" % (ctype, self.cname, len(self.data)), file=f)
        cursor = 0
        bytecnt = 0
        while cursor < len(self.data):
            if cursor in self.comments:
                if bytecnt == 0:
                    print(" ", end="", file=f)
                print(" // %s" % self.comments[cursor], file=f)
                bytecnt = 0
            if cursor in self.labels_byaddr:
                if bytecnt != 0:
                    print(file=f)
                if cursor in self.exports:
                    print("#define %s ((%s*)(%s+%d))" % (
                        self.labels_byaddr[cursor], self.ltypes_byaddr[cursor], self.cname, cursor), file=f)
                else:
                    print("  // [%d] %s" % (cursor, self.labels_byaddr[cursor]), file=f)
                bytecnt = 0
            if cursor in self.refs:
                if bytecnt != 0:
                    print(file=f)
                print(" ", end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor + 0]), end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor + 1]), end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor + 2]), end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor + 3]), end="", file=f)
                print(" // [%d] %s (reference to %s)" % (cursor, self.refs[cursor][1], self.refs[cursor][0]), file=f)
                bytecnt = 0
                cursor += 4
            else:
                if bytecnt == 0:
                    print(" ", end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor]), end=("" if bytecnt < 15 else "\n"), file=f)
                bytecnt = (bytecnt + 1) & 15
                cursor += 1
        if bytecnt != 0:
            print(file=f)
        print("};", file=f)

    def write_compact_c(self, f, ctype="const unsigned char"):
        assert self.finalized
        print("%s %s[%d] = {" % (ctype, self.cname, len(self.data)), file=f)
        column = 0
        for v in self.data:
            if column == 0:
                print("  ", end="", file=f)
                column += 2
            s = "%d," % v
            print(s, end="", file=f)
            column += len(s)
            if column > 75:
                print(file=f)
                column = 0
        if column != 0:
            print(file=f)
        for cursor in self.exports:
            print("#define %s ((%s*)(%s+%d))" % (
                self.labels_byaddr[cursor], self.ltypes_byaddr[cursor], self.cname, cursor), file=f)
        print("};", file=f)

    def write_uint64_c(self, f, ctype="const uint64_t"):
        assert self.finalized
        print("%s %s[%d] = {" % (ctype, self.cname, (len(self.data) + 7) // 8), file=f)
        column = 0
        for i in range((len(self.data) + 7) // 8):
            v0 = self.data[8 * i + 0] if 8 * i + 0 < len(self.data) else 0
            v1 = self.data[8 * i + 1] if 8 * i + 1 < len(self.data) else 0
            v2 = self.data[8 * i + 2] if 8 * i + 2 < len(self.data) else 0
            v3 = self.data[8 * i + 3] if 8 * i + 3 < len(self.data) else 0
            v4 = self.data[8 * i + 4] if 8 * i + 4 < len(self.data) else 0
            v5 = self.data[8 * i + 5] if 8 * i + 5 < len(self.data) else 0
            v6 = self.data[8 * i + 6] if 8 * i + 6 < len(self.data) else 0
            v7 = self.data[8 * i + 7] if 8 * i + 7 < len(self.data) else 0
            if self.endianness == "le":
                v = v0 << 0
                v |= v1 << 8
                v |= v2 << 16
                v |= v3 << 24
                v |= v4 << 32
                v |= v5 << 40
                v |= v6 << 48
                v |= v7 << 56
            elif self.endianness == "be":
                v = v7 << 0
                v |= v6 << 8
                v |= v5 << 16
                v |= v4 << 24
                v |= v3 << 32
                v |= v2 << 40
                v |= v1 << 48
                v |= v0 << 56
            else:
                assert 0
            if column == 3:
                print(" 0x%016x," % v, file=f)
                column = 0
            else:
                if column == 0:
                    print(" ", end="", file=f)
                print(" 0x%016x," % v, end="", file=f)
                column += 1
        if column != 0:
            print("", file=f)
        print("};", file=f)

    def write_string_c(self, f, ctype="const char"):
        assert self.finalized
        assert self.data[len(self.data) - 1] == 0
        print("%s %s[%d] =" % (ctype, self.cname, len(self.data)), file=f)
        print("  \"", end="", file=f)
        column = 0
        for i in range(len(self.data) - 1):
            if (self.data[i] < 32) or (self.data[i] > 126):
                print("\\%03o" % self.data[i], end="", file=f)
                column += 4
            elif self.data[i] == ord('"') or self.data[i] == ord('\\'):
                print("\\" + chr(self.data[i]), end="", file=f)
                column += 2
            else:
                print(chr(self.data[i]), end="", file=f)
                column += 1
            if column > 70 and (i != len(self.data) - 2):
                print("\"\n  \"", end="", file=f)
                column = 0
        print("\";", file=f)

    def write_binary(self, f):
        assert self.finalized
        assert self.data[len(self.data) - 1] == 0
        f.buffer.write(self.data)


bel_types = {
    "NONE": 0,
    "SLICE": 1,
    "PIO": 2
}

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

def process_pio_db(ddrg, device):
    piofile = path.join(database.get_db_root(), "ECP5", dev_names[device], "iodb.json")
    with open(piofile, 'r') as f:
        piodb = json.load(f)
        for pkgname, pkgdata in sorted(piodb["packages"].items()):
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
            bel_idx = get_bel_index(ddrg, loc, pio)
            if bel_idx is not None:
                pindata.append((loc, bel_idx, bank, pinfunc))


def write_database(dev_name, ddrg, endianness):
    def write_loc(loc, sym_name):
        bba.s16(loc.x, "%s.x" % sym_name)
        bba.s16(loc.y, "%s.y" % sym_name)

    bba = BinaryBlobAssembler("chipdb_blob_%s" % dev_name, endianness)
    bba.r("chip_info", "chip_info")

    loctypes = list([_.key() for _ in ddrg.locationTypes])

    for idx in range(len(loctypes)):
        loctype = ddrg.locationTypes[loctypes[idx]]
        if len(loctype.arcs) > 0:
            bba.l("loc%d_pips" % idx, "PipInfoPOD")
            for arc in loctype.arcs:
                write_loc(arc.srcWire.rel, "src")
                write_loc(arc.sinkWire.rel, "dst")
                bba.u32(arc.srcWire.id, "src_idx")
                bba.u32(arc.sinkWire.id, "dst_idx")
                bba.u32(arc.delay, "delay")  # TODO:delay
                bba.u16(get_tiletype_index(ddrg.to_str(arc.tiletype)), "tile_type")
                bba.u8(int(arc.cls), "pip_type")
                bba.u8(0, "padding")
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
                        bba.u32(portpins[ddrg.to_str(bp.pin)], "port")
            bba.l("loc%d_wires" % idx, "WireInfoPOD")
            for wire_idx in range(len(loctype.wires)):
                wire = loctype.wires[wire_idx]
                bba.s(ddrg.to_str(wire.name), "name")
                bba.u32(len(wire.arcsUphill), "num_uphill")
                bba.u32(len(wire.arcsDownhill), "num_downhill")
                bba.r("loc%d_wire%d_uppips" % (idx, wire_idx) if len(wire.arcsUphill) > 0 else None, "pips_uphill")
                bba.r("loc%d_wire%d_downpips" % (idx, wire_idx) if len(wire.arcsDownhill) > 0 else None, "pips_downhill")
                bba.u32(len(wire.belPins), "num_bel_pins")
                bba.r("loc%d_wire%d_belpins" % (idx, wire_idx) if len(wire.belPins) > 0 else None, "bel_pins")

        if len(loctype.bels) > 0:
            for bel_idx in range(len(loctype.bels)):
                bel = loctype.bels[bel_idx]
                bba.l("loc%d_bel%d_wires" % (idx, bel_idx), "BelWirePOD")
                for pin in bel.wires:
                    write_loc(pin.wire.rel, "rel_wire_loc")
                    bba.u32(pin.wire.id, "wire_index")
                    bba.u32(portpins[ddrg.to_str(pin.pin)], "port")
                    bba.u32(int(pin.dir), "dir")
            bba.l("loc%d_bels" % idx, "BelInfoPOD")
            for bel_idx in range(len(loctype.bels)):
                bel = loctype.bels[bel_idx]
                bba.s(ddrg.to_str(bel.name), "name")
                bba.u32(bel_types[ddrg.to_str(bel.type)], "type")
                bba.u32(bel.z, "z")
                bba.u32(len(bel.wires), "num_bel_wires")
                bba.r("loc%d_bel%d_wires" % (idx, bel_idx), "bel_wires")

    bba.l("locations", "LocationTypePOD")
    for idx in range(len(loctypes)):
        loctype = ddrg.locationTypes[loctypes[idx]]
        bba.u32(len(loctype.bels), "num_bels")
        bba.u32(len(loctype.wires), "num_wires")
        bba.u32(len(loctype.arcs), "num_pips")
        bba.r("loc%d_bels" % idx if len(loctype.bels) > 0 else None, "bel_data")
        bba.r("loc%d_wires" % idx if len(loctype.wires) > 0 else None, "wire_data")
        bba.r("loc%d_pips" % idx if len(loctype.arcs) > 0 else None, "pips_data")

    bba.l("location_types", "int32_t")
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            bba.u32(loctypes.index(ddrg.typeAtLocation[pytrellis.Location(x, y)]), "loctype")
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
        bba.u32(len(pkgdata), "num_pins")
        bba.r("package_data_%s" % package, "pin_data")

    bba.l("pio_info", "PIOInfoPOD")
    for pin in pindata:
        loc, bel_idx, bank, func = pin
        write_loc(loc, "abs_loc")
        bba.u32(bel_idx, "bel_index")
        if func is not None:
            bba.s(func, "function_name")
        else:
            bba.r(None, "function_name")
        bba.u16(bank, "bank")
        bba.u16(0, "padding")


    bba.l("tiletype_names", "RelPtr<char>")
    for tt in tiletype_names:
        bba.s(tt, "name")

    bba.l("chip_info")
    bba.u32(max_col + 1, "width")
    bba.u32(max_row + 1, "height")
    bba.u32((max_col + 1) * (max_row + 1), "num_tiles")
    bba.u32(len(location_types), "num_location_types")
    bba.u32(len(packages), "num_packages")
    bba.u32(len(pindata), "num_pios")

    bba.r("locations", "locations")
    bba.r("location_types", "location_type")
    bba.r("tiletype_names", "tiletype_names")
    bba.r("package_data", "package_info")
    bba.r("pio_info", "pio_info")

    bba.finalize()
    return bba

dev_names = {"25k": "LFE5U-25F", "45k": "LFE5U-45F", "85k": "LFE5U-85F"}

def main():
    global max_row, max_col
    pytrellis.load_database(database.get_db_root())
    args = parser.parse_args()

    # Read port pin file
    with open(args.portspins) as f:
        for line in f:
            line = line.replace("(", " ")
            line = line.replace(")", " ")
            line = line.split()
            if len(line) == 0:
                continue
            assert len(line) == 2
            assert line[0] == "X"
            idx = len(portpins) + 1
            portpins[line[1]] = idx

    print("Initialising chip...")
    chip = pytrellis.Chip(dev_names[args.device])
    print("Building routing graph...")
    ddrg = pytrellis.make_dedup_chipdb(chip)
    max_row = chip.get_max_row()
    max_col = chip.get_max_col()
    process_pio_db(ddrg, args.device)
    print("{} unique location types".format(len(ddrg.locationTypes)))
    bba = write_database(args.device, ddrg, "le")


    if args.c_file:
        print('#include "nextpnr.h"', file=args.outfile)
        print('NEXTPNR_NAMESPACE_BEGIN', file=args.outfile)


    if args.binary:
        bba.write_binary(args.outfile)

    if args.c_file:
        bba.write_string_c(args.outfile)

    if args.c_file:
        print('NEXTPNR_NAMESPACE_END', file=args.outfile)

if __name__ == "__main__":
    main()

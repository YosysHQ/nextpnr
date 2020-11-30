#!/usr/bin/env python3
import argparse
import json
import sys

tiletype_names = dict()

parser = argparse.ArgumentParser(description="import MachXO2 routing and bels from Project Trellis")
parser.add_argument("device", type=str, help="target device")
parser.add_argument("-p", "--constids", type=str, help="path to constids.inc")
parser.add_argument("-g", "--gfxh", type=str, help="path to gfx.h (unused)")
parser.add_argument("-L", "--libdir", type=str, action="append", help="extra Python library path")
args = parser.parse_args()

sys.path += args.libdir
import pytrellis
import database

# Get the index for a tiletype
def get_tiletype_index(name):
    if name in tiletype_names:
        return tiletype_names[name]
    idx = len(tiletype_names)
    tiletype_names[name] = idx
    return idx


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

    def s(self, s, comment):
        assert "|" not in s
        print("str |%s| %s" % (s, comment))

    def u8(self, v, comment):
        if comment is None:
            print("u8 %d" % (v,))
        else:
            print("u8 %d %s" % (v, comment))

    def u16(self, v, comment):
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

def write_database(dev_name, chip, rg, endianness):
    def write_loc(loc, sym_name):
        bba.u16(loc.x, "%s.x" % sym_name)
        bba.u16(loc.y, "%s.y" % sym_name)

    # Before doing anything, ensure sorted routing graph iteration matches
    # y, x
    tile_iter = list(sorted(rg.tiles, key=lambda l : (l.key().y, l.key().x)))

    i = 1 # Drop (-2, -2) location.
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            l = tile_iter[i]
            assert((y, x) == (l.key().y, l.key().x))
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
    for lt in sorted(rg.tiles, key=lambda l : (l.key().y, l.key().x)):
        l = lt.key()
        t = lt.data()

        # Do not include special globals location for now.
        if (l.x, l.y) == (-2, -2):
            continue

        if len(t.arcs) > 0:
            bba.l("loc%d_%d_pips" % (l.y, l.x), "PipInfoPOD")

        if len(t.wires) > 0:
            bba.l("loc%d_%d_wires" % (l.y, l.x), "WireInfoPOD")

        if len(t.bels) > 0:
            bba.l("loc%d_%d_bels" % (l.y, l.x), "BelInfoPOD")

    bba.l("tiles", "TileTypePOD")
    for lt in sorted(rg.tiles, key=lambda l : (l.key().y, l.key().x)):
        l = lt.key()
        t = lt.data()

        if (l.y, l.x) == (-2, -2):
            continue

        bba.u32(len(t.bels), "num_bels")
        bba.u32(len(t.wires), "num_wires")
        bba.u32(len(t.arcs), "num_pips")
        bba.r("loc%d_%d_bels" % (l.y, l.x) if len(t.bels) > 0 else None, "bel_data")
        bba.r("loc%d_%d_wires" % (l.y, l.x) if len(t.wires) > 0 else None, "wire_data")
        bba.r("loc%d_%d_pips" % (l.y, l.x) if len(t.arcs) > 0 else None, "pips_data")

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
            bba.u32(len(chip.get_tiles_by_position(y, x)), "num_tiles")
            bba.r("tile_info_%d_%d" % (x, y), "tile_names")

    bba.l("tiletype_names", "RelPtr<char>")
    for tt, idx in sorted(tiletype_names.items(), key=lambda x: x[1]):
        bba.s(tt, "name")


    bba.l("chip_info")
    bba.u32(max_col + 1, "width")
    bba.u32(max_row + 1, "height")
    bba.u32((max_col + 1) * (max_row + 1), "num_tiles")
    bba.u32(0, "num_packages") # len(packages)
    bba.u32(0, "num_pios") # len(pindata)
    bba.u32(const_id_count, "const_id_count")

    bba.r("tiles", "tiles")
    bba.r("tiletype_names", "tiletype_names")
    bba.r("tiles_info", "tile_info")

    bba.pop()


dev_names = {"1200": "LCMXO2-1200HC"}

def main():
    global max_row, max_col, const_id_count

    pytrellis.load_database(database.get_db_root())
    args = parser.parse_args()

    const_id_count = 1 # count ID_NONE

    chip = pytrellis.Chip(dev_names[args.device])
    rg = pytrellis.make_optimized_chipdb(chip)
    max_row = chip.get_max_row()
    max_col = chip.get_max_col()
    bba = write_database(args.device, chip, rg, "le")



if __name__ == "__main__":
    main()

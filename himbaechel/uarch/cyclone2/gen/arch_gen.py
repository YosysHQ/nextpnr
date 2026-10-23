#!/usr/bin/env python3
"""Turn a cyclone2-re device database into a himbaechel .bba with nextpnr's
own generator (himbaechel_dbgen). The database's chipdb.txt.xz, written by
`cram chipdb` (https://github.com/cramt/cyclone2-re), is already flat (one
line per wire, pip, bel, tile instance and inter-tile node), so this is a
transcription.

    arch_gen.py --db <cyclone2-re>/db --device EP2C35F672C6 --bba <out.bba>
"""
import argparse
import lzma
import sys
from os import path

sys.path.append(path.join(path.dirname(__file__), "../../.."))
from himbaechel_dbgen.chip import Chip, NodeWire, PinType  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--db", required=True, help="the db/ directory of a cyclone2-re checkout")
parser.add_argument("--device", required=True)
parser.add_argument("--bba", required=True)
args = parser.parse_args()

constids = path.join(path.dirname(__file__), "../constids.inc")
src = path.join(args.db, args.device, "chipdb.txt.xz")
out = args.bba
ch = tt = bel = pkg = None
with lzma.open(src, "rt") as f:
    for line in f:
        a = line.split()
        k = a[0]
        if k == "p":
            tt.create_pip(a[1], a[2])
        elif k == "w":
            tt.create_wire(a[1], a[2])
        elif k == "node":
            ch.add_node([NodeWire(int(a[i]), int(a[i + 1]), a[i + 2]) for i in range(1, len(a), 3)])
        elif k == "t":
            ch.set_tile_type(int(a[1]), int(a[2]), a[3])
        elif k == "tt":
            tt = ch.create_tile_type(a[1])
        elif k == "b":
            bel = tt.create_bel(a[1], a[2], int(a[3]))
        elif k == "bp":
            tt.add_bel_pin(bel, a[1], a[2], PinType.OUTPUT if a[3] == "O" else PinType.INPUT)
        elif k == "end":
            tt = None
        elif k == "chip":
            ch = Chip("cyclone2", "EP2C35F672C6", int(a[1]), int(a[2]))
            ch.strs.read_constids(constids)
            ch.set_speed_grades(["C6"])
        elif k == "pkg":
            pkg = ch.create_package(a[1])
        elif k == "pad":
            pkg.create_pad(a[1], a[2], a[3], "", int(a[4]))
        else:
            raise ValueError(f"unknown line {line!r}")
ch.write_bba(out)

#!/usr/bin/env python3

import sys

dev_name = None
dev_width = None
dev_height = None
num_wires = None

tiles = dict()

wire_uphill = dict()
wire_downhill = dict()
wire_bidir = dict()

bel_name = list()
bel_type = list()

wire_uphill_belport = dict()
wire_downhill_belports = dict()

wire_names = dict()
wire_names_r = dict()

def cmp_wire_names(newname, oldname):
    return newname < oldname

with open(sys.argv[1], "r") as f:
    mode = None

    for line in f:
        line = line.split()

        if len(line) == 0 or line[0] == "#":
            continue

        if line[0] == ".device":
            dev_name = line[1]
            dev_width = int(line[2])
            dev_height = int(line[3])
            num_wires = int(line[4])
            continue

        if line[0] == ".net":
            mode = ("net", int(line[1]))
            continue

        if line[0] == ".buffer":
            mode = ("buffer", int(line[3]))
            continue

        if line[0] == ".routing":
            mode = ("routing", int(line[3]))
            continue

        if line[0] == ".io_tile":
            tiles[(int(line[1]), int(line[2]))] = "io"
            mode = None
            continue

        if line[0] == ".logic_tile":
            tiles[(int(line[1]), int(line[2]))] = "logic"
            mode = None
            continue

        if line[0] == ".ramb_tile":
            tiles[(int(line[1]), int(line[2]))] = "ramb"
            mode = None
            continue

        if line[0] == ".ramt_tile":
            tiles[(int(line[1]), int(line[2]))] = "ramt"
            mode = None
            continue

        if (line[0][0] == ".") or (mode is None):
            mode = None
            continue

        if mode[0] == "net":
            wname = (int(line[0]), int(line[1]), line[2])
            wire_names[wname] = mode[1]
            if (mode[1] not in wire_names_r) or cmp_wire_names(wname, wire_names_r[mode[1]]):
                wire_names_r[mode[1]] = wname
            continue

        if mode[0] == "buffer":
            wire_a = int(line[1])
            wire_b = mode[1]
            if wire_a not in wire_downhill:
                wire_downhill[wire_a] = set()
            if wire_b not in wire_uphill:
                wire_uphill[wire_b] = set()
            wire_downhill[wire_a].add(wire_b)
            wire_uphill[wire_b].add(wire_a)
            continue

        if mode[0] == "routing":
            wire_a = int(line[1])
            wire_b = mode[1]
            if wire_a not in wire_bidir:
                wire_bidir[wire_a] = set()
            if wire_b not in wire_bidir:
                wire_bidir[wire_b] = set()
            wire_bidir[wire_a].add(wire_b)
            wire_bidir[wire_b].add(wire_b)
            continue

def add_bel_input(bel, wire, port):
    if wire not in wire_downhill_belports:
        wire_downhill_belports[wire] = set()
    wire_downhill_belports[wire].add((bel, port))

def add_bel_output(bel, wire, port):
    assert wire not in wire_uphill_belport
    wire_uphill_belport[wire] = (bel, port)

def add_bel_lc(x, y, z):
    bel = len(bel_name)
    bel_name.append("%d_%d_lc%d" % (x, y, z))
    bel_type.append("ICESTORM_LC")

    wire_cen = wire_names[(x, y, "lutff_global/cen")]
    wire_clk = wire_names[(x, y, "lutff_global/clk")]
    wire_s_r = wire_names[(x, y, "lutff_global/s_r")]

    if z == 0:
        wire_cin = wire_names[(x, y, "carry_in_mux")]
    else:
        wire_cin = wire_names[(x, y, "lutff_%d/cout" % (z-1))]

    wire_in_0 = wire_names[(x, y, "lutff_%d/in_0" % z)]
    wire_in_1 = wire_names[(x, y, "lutff_%d/in_1" % z)]
    wire_in_2 = wire_names[(x, y, "lutff_%d/in_2" % z)]
    wire_in_3 = wire_names[(x, y, "lutff_%d/in_3" % z)]
    wire_out  = wire_names[(x, y, "lutff_%d/out"  % z)]
    wire_cout = wire_names[(x, y, "lutff_%d/cout" % z)]
    wire_lout = wire_names[(x, y, "lutff_%d/lout" % z)] if z < 7 else None

    add_bel_input(bel, wire_cen, "CEN")
    add_bel_input(bel, wire_clk, "CLK")
    add_bel_input(bel, wire_s_r, "SR")
    add_bel_input(bel, wire_cin, "CIN")

    add_bel_input(bel, wire_in_0, "IN_0")
    add_bel_input(bel, wire_in_1, "IN_1")
    add_bel_input(bel, wire_in_2, "IN_2")
    add_bel_input(bel, wire_in_3, "IN_3")

    add_bel_output(bel, wire_out,  "O")
    add_bel_output(bel, wire_cout, "COUT")

    if wire_lout is not None:
        add_bel_output(bel, wire_lout, "LO")

for tile_xy, tile_type in sorted(tiles.items()):
    if tile_type == "logic":
        for i in range(8):
            add_bel_lc(tile_xy[0], tile_xy[1], i)

print('#include "chip.h"')

print("int num_bels_%s = %d;" % (dev_name, num_wires))
print("BelInfoPOD bel_data_%s[%d] = {" % (dev_name, num_wires))
for bel in range(len(bel_name)):
    print("  {\"%s\", TYPE_%s}%s" % (bel_name[bel], bel_type[bel], "," if bel+1 < len(bel_name) else ""))
print("};")

wireinfo = list()

for wire in range(num_wires):
    num_uphill = 0
    num_downhill = 0
    num_bidir = 0
    num_bels_downhill = 0

    if wire in wire_uphill:
        num_uphill = len(wire_uphill[wire])
        print("static WireDelayPOD wire%d_uphill[] = {" % wire)
        print(",\n".join(["  {%d, 1.0}" % other_wire for other_wire in wire_uphill[wire]]))
        print("};")

    if wire in wire_downhill:
        num_downhill = len(wire_downhill[wire])
        print("static WireDelayPOD wire%d_downhill[] = {" % wire)
        print(",\n".join(["  {%d, 1.0}" % other_wire for other_wire in wire_downhill[wire]]))
        print("};")

    if wire in wire_bidir:
        num_bidir = len(wire_bidir[wire])
        print("static WireDelayPOD wire%d_bidir[] = {" % wire)
        print(",\n".join(["  {%d, 1.0}" % other_wire for other_wire in wire_bidir[wire]]))
        print("};")

    if wire in wire_downhill_belports:
        num_bels_downhill = len(wire_downhill_belports[wire])
        print("static BelPortPOD wire%d_downbels[] = {" % wire)
        print(",\n".join(["  {%d, PIN_%s}" % it for it in wire_downhill_belports[wire]]))
        print("};")

    info = "  {"
    info += "\"%d_%d_%s\", " % wire_names_r[wire]
    info += "%d, %d, %d, " % (num_uphill, num_downhill, num_bidir)
    info += ("wire%d_uphill, " % wire) if num_uphill > 0 else "nullptr, "
    info += ("wire%d_downhill, " % wire) if num_downhill > 0 else "nullptr, "
    info += ("wire%d_bidir, " % wire) if num_bidir > 0 else "nullptr, "
    info += "%d, " % (num_bels_downhill)

    if wire in wire_uphill_belport:
        info += "{%d, PIN_%s}, " % wire_uphill_belport[wire]
    else:
        info += "{-1, PIN_NIL}, "

    info += ("wire%d_downbels" % wire) if num_bels_downhill > 0 else "nullptr"
    info += "}"

    wireinfo.append(info)

print("int num_wires_%s = %d;" % (dev_name, num_wires))
print("WireInfoPOD wire_data_%s[%d] = {" % (dev_name, num_wires))
print(",\n".join(wireinfo))
print("};")

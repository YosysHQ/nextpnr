#!/usr/bin/env python3

import sys

dev_name = None
dev_width = None
dev_height = None
num_wires = None

wire_uphill = dict()
wire_downhill = dict()
wire_bidir = dict()

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

        if (line[0][0] == ".") or (mode is None):
            mode = None
            continue

        if mode[0] == "net":
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

print('#include "chip.h"')

wireinfo = list()

for wire in range(num_wires):
    num_uphill = 0
    num_downhill = 0
    num_bidir = 0

    has_bel_uphill = False
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

    info = "  {"
    info += "\"wire%d\", " % wire
    info += "%d, %d, %d, " % (num_uphill, num_downhill, num_bidir)
    info += ("wire%d_uphill, " % wire) if num_uphill > 0 else "nullptr, "
    info += ("wire%d_downhill, " % wire) if num_downhill > 0 else "nullptr, "
    info += ("wire%d_bidir, " % wire) if num_bidir > 0 else "nullptr, "
    info += "}"

    wireinfo.append(info)

print("int num_wires_%s = %d;" % (dev_name, num_wires))
print("WireInfoPOD wire_data_%s[%d] = {" % (dev_name, num_wires))
print(",\n".join(wireinfo))
print("};")

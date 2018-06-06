#!/usr/bin/env python3

import sys

dev_name = None
dev_width = None
dev_height = None
num_wires = None

tiles = dict()

wire_uphill = dict()
wire_downhill = dict()
pip_xy = dict()

bel_name = list()
bel_type = list()
bel_pos = list()

wire_uphill_belport = dict()
wire_downhill_belports = dict()

wire_names = dict()
wire_names_r = dict()
wire_xy = dict()

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
            mode = ("buffer", int(line[3]), int(line[1]), int(line[2]))
            continue

        if line[0] == ".routing":
            mode = ("routing", int(line[3]), int(line[1]), int(line[2]))
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
            if mode[1] not in wire_xy:
                wire_xy[mode[1]] = list()
            wire_xy[mode[1]].append((int(line[0]), int(line[1])))
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
            pip_xy[(wire_a, wire_b)] = (mode[2], mode[3])
            continue

        if mode[0] == "routing":
            wire_a = int(line[1])
            wire_b = mode[1]

            if wire_a not in wire_downhill:
                wire_downhill[wire_a] = set()
            if wire_b not in wire_uphill:
                wire_uphill[wire_b] = set()
            wire_downhill[wire_a].add(wire_b)
            wire_uphill[wire_b].add(wire_a)
            pip_xy[(wire_a, wire_b)] = (mode[2], mode[3])

            if wire_b not in wire_downhill:
                wire_downhill[wire_b] = set()
            if wire_a not in wire_uphill:
                wire_uphill[wire_a] = set()
            wire_downhill[wire_b].add(wire_a)
            wire_uphill[wire_a].add(wire_b)
            pip_xy[(wire_b, wire_a)] = (mode[2], mode[3])
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
    bel_pos.append((x, y, z))

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

def add_bel_io(x, y, z):
    bel = len(bel_name)
    bel_name.append("%d_%d_lc%d" % (x, y, z))
    bel_type.append("SB_IO")
    bel_pos.append((x, y, z))

    wire_cen   = wire_names[(x, y, "io_global/cen")]
    wire_iclk  = wire_names[(x, y, "io_global/inclk")]
    wire_oclk  = wire_names[(x, y, "io_global/latch")]
    wire_latch = wire_names[(x, y, "io_global/outclk")]

    wire_din_0  = wire_names[(x, y, "io_%d/D_IN_0"  % z)]
    wire_din_1  = wire_names[(x, y, "io_%d/D_IN_1"  % z)]
    wire_dout_0 = wire_names[(x, y, "io_%d/D_OUT_0" % z)]
    wire_dout_1 = wire_names[(x, y, "io_%d/D_OUT_1" % z)]
    wire_out_en = wire_names[(x, y, "io_%d/OUT_ENB" % z)]

    add_bel_input(bel, wire_cen,    "CLOCK_ENABLE")
    add_bel_input(bel, wire_iclk,   "INPUT_CLK")
    add_bel_input(bel, wire_oclk,   "OUTPUT_CLK")
    add_bel_input(bel, wire_latch,  "LATCH_INPUT_VALUE")

    add_bel_output(bel, wire_din_0, "D_IN_0")
    add_bel_output(bel, wire_din_1, "D_IN_1")

    add_bel_input(bel, wire_dout_0, "D_OUT_0")
    add_bel_input(bel, wire_dout_1, "D_OUT_1")
    add_bel_input(bel, wire_out_en, "OUTPUT_ENABLE")

def add_bel_ram(x, y):
    bel = len(bel_name)
    bel_name.append("%d_%d_ram" % (x, y))
    bel_type.append("ICESTORM_RAM")
    bel_pos.append((x, y, 0))

    if (x, y, "ram/WE") in wire_names:
        # iCE40 1K-style memories
        y0, y1 = y, y+1
    else:
        # iCE40 8K-style memories
        y1, y0 = y, y+1

    for i in range(16):
        add_bel_input (bel, wire_names[(x, y0 if i < 8 else y1, "ram/MASK_%d"  % i)], "MASK_%d"  % i)
        add_bel_input (bel, wire_names[(x, y0 if i < 8 else y1, "ram/WDATA_%d" % i)], "WDATA_%d" % i)
        add_bel_output(bel, wire_names[(x, y0 if i < 8 else y1, "ram/RDATA_%d" % i)], "RDATA_%d" % i)

    for i in range(11):
        add_bel_input(bel, wire_names[(x, y0, "ram/WADDR_%d" % i)], "WADDR_%d" % i)
        add_bel_input(bel, wire_names[(x, y1, "ram/RADDR_%d" % i)], "RADDR_%d" % i)

    add_bel_input(bel, wire_names[(x, y0, "ram/WCLK")], "WCLK")
    add_bel_input(bel, wire_names[(x, y0, "ram/WCLKE")], "WCLKE")
    add_bel_input(bel, wire_names[(x, y0, "ram/WE")], "WE")

    add_bel_input(bel, wire_names[(x, y1, "ram/RCLK")], "RCLK")
    add_bel_input(bel, wire_names[(x, y1, "ram/RCLKE")], "RCLKE")
    add_bel_input(bel, wire_names[(x, y1, "ram/RE")], "RE")

for tile_xy, tile_type in sorted(tiles.items()):
    if tile_type == "logic":
        for i in range(8):
            add_bel_lc(tile_xy[0], tile_xy[1], i)
    if tile_type == "io":
        for i in range(2):
            add_bel_io(tile_xy[0], tile_xy[1], i)
    if tile_type == "ramb":
        add_bel_ram(tile_xy[0], tile_xy[1])

print('#include "chip.h"')

print("BelInfoPOD bel_data_%s[%d] = {" % (dev_name, len(bel_name)))
for bel in range(len(bel_name)):
    print("  {\"%s\", TYPE_%s, %d, %d, %d}%s" % (bel_name[bel], bel_type[bel],
            bel_pos[bel][0], bel_pos[bel][1], bel_pos[bel][2],
            "," if bel+1 < len(bel_name) else ""))
print("};")

wireinfo = list()
pipinfo = list()
pipcache = dict()

for wire in range(num_wires):
    if wire in wire_uphill:
        pips = list()
        for src in wire_uphill[wire]:
            if (src, wire) not in pipcache:
                pipcache[(src, wire)] = len(pipinfo)
                pipinfo.append("  {%d, %d, 1.0, %d, %d}" % (src, wire, pip_xy[(src, wire)][0], pip_xy[(src, wire)][1]))
            pips.append("%d" % pipcache[(src, wire)])
        num_uphill = len(pips)
        list_uphill = "wire%d_uppips" % wire
        print("static int wire%d_uppips[] = {%s};" % (wire, ", ".join(pips)))
    else:
        num_uphill = 0
        list_uphill = "nullptr"

    if wire in wire_downhill:
        pips = list()
        for dst in wire_downhill[wire]:
            if (wire, dst) not in pipcache:
                pipcache[(wire, dst)] = len(pipinfo)
                pipinfo.append("  {%d, %d, 1.0, %d, %d}" % (wire, dst, pip_xy[(wire, dst)][0], pip_xy[(wire, dst)][1]))
            pips.append("%d" % pipcache[(wire, dst)])
        num_downhill = len(pips)
        list_downhill = "wire%d_downpips" % wire
        print("static int wire%d_downpips[] = {%s};" % (wire, ", ".join(pips)))
    else:
        num_downhill = 0
        list_downhill = "nullptr"

    if wire in wire_downhill_belports:
        num_bels_downhill = len(wire_downhill_belports[wire])
        print("static BelPortPOD wire%d_downbels[] = {" % wire)
        print(",\n".join(["  {%d, PIN_%s}" % it for it in wire_downhill_belports[wire]]))
        print("};")
    else:
        num_bels_downhill = 0

    info = "  {"
    info += "\"%d_%d_%s\", " % wire_names_r[wire]
    info += "%d, %d, %s, %s, %d, " % (num_uphill, num_downhill, list_uphill, list_downhill, num_bels_downhill)

    if wire in wire_uphill_belport:
        info += "{%d, PIN_%s}, " % wire_uphill_belport[wire]
    else:
        info += "{-1, PIN_NIL}, "

    info += ("wire%d_downbels, " % wire) if num_bels_downhill > 0 else "nullptr, "

    avg_x, avg_y = 0, 0
    if wire in wire_xy:
        for x, y in wire_xy[wire]:
            avg_x += x
            avg_y += y
        avg_x /= len(wire_xy[wire])
        avg_y /= len(wire_xy[wire])

    info += "%f, %f}" % (avg_x, avg_y)

    wireinfo.append(info)

print("static WireInfoPOD wire_data_%s[%d] = {" % (dev_name, num_wires))
print(",\n".join(wireinfo))
print("};")

print("static PipInfoPOD pip_data_%s[%d] = {" % (dev_name, len(pipinfo)))
print(",\n".join(pipinfo))
print("};")

print("ChipInfoPOD chip_info_%s = {" % dev_name)
print("  %d, %d, %d, %d, %d," % (dev_width, dev_height, len(bel_name), num_wires, len(pipinfo)))
print("  bel_data_%s, wire_data_%s, pip_data_%s" % (dev_name, dev_name, dev_name))
print("};")

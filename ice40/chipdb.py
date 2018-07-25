#!/usr/bin/env python3

import sys
import re
import textwrap
import argparse

parser = argparse.ArgumentParser(description="convert ICE40 chip database")
parser.add_argument("filename", type=str, help="chipdb input filename")
parser.add_argument("-p", "--portspins", type=str, help="path to portpins.inc")
parser.add_argument("-g", "--gfxh", type=str, help="path to gfx.h")
args = parser.parse_args()

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
bel_wires = list()

switches = list()

ierens = list()

extra_cells = dict()
extra_cell_config = dict()
packages = list()

wire_belports = dict()

wire_names = dict()
wire_names_r = dict()
wire_xy = dict()

cbit_re = re.compile(r'B(\d+)\[(\d+)\]')

portpins = dict()
beltypes = dict()
tiletypes = dict()
wiretypes = dict()

gfx_wire_ids = dict()
wire_segments = dict()

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

beltypes["ICESTORM_LC"] = 1
beltypes["ICESTORM_RAM"] = 2
beltypes["SB_IO"] = 3
beltypes["SB_GB"] = 4
beltypes["PLL"] = 5
beltypes["WARMBOOT"] = 6
beltypes["MAC16"] = 7
beltypes["HFOSC"] = 8
beltypes["LFOSC"] = 9
beltypes["I2C"] = 10
beltypes["SPI"] = 11
beltypes["IO_I3C"] = 12
beltypes["LEDDA_IP"] = 13
beltypes["RGBA_DRV"] = 14
beltypes["SPRAM"] = 15

tiletypes["NONE"] = 0
tiletypes["LOGIC"] = 1
tiletypes["IO"] = 2
tiletypes["RAMB"] = 3
tiletypes["RAMT"] = 4
tiletypes["DSP0"] = 5
tiletypes["DSP1"] = 6
tiletypes["DSP2"] = 7
tiletypes["DSP3"] = 8
tiletypes["IPCON"] = 9

wiretypes["LOCAL"] = 1
wiretypes["GLOBAL"] = 2
wiretypes["SP4_VERT"] = 3
wiretypes["SP4_HORZ"] = 4
wiretypes["SP12_HORZ"] = 5
wiretypes["SP12_VERT"] = 6

def maj_wire_name(name):
    if name[2].startswith("lutff_"):
        return True
    if name[2].startswith("io_"):
        return True
    if name[2].startswith("ram/"):
        return True
    if name[2].startswith("sp4_h_r_"):
        return name[2] in ("sp4_h_r_0", "sp4_h_r_1", "sp4_h_r_2", "sp4_h_r_3", "sp4_h_r_4", "sp4_h_r_5",
                           "sp4_h_r_6", "sp4_h_r_7", "sp4_h_r_8", "sp4_h_r_9", "sp4_h_r_10", "sp4_h_r_11")
    if name[2].startswith("sp4_v_b_"):
        return name[2] in ("sp4_v_b_0", "sp4_v_b_1", "sp4_v_b_2", "sp4_v_b_3", "sp4_v_b_4", "sp4_v_b_5",
                           "sp4_v_b_6", "sp4_v_b_7", "sp4_v_b_8", "sp4_v_b_9", "sp4_v_b_10", "sp4_v_b_11")
    if name[2].startswith("sp12_h_r_"):
        return name[2] in ("sp12_h_r_0", "sp12_h_r_1")
    if name[2].startswith("sp12_v_b_"):
        return name[2] in ("sp12_v_b_0", "sp12_v_b_1")
    return False

def cmp_wire_names(newname, oldname):
    if maj_wire_name(newname):
        return True
    if maj_wire_name(oldname):
        return False

    if newname[2].startswith("sp") and oldname[2].startswith("sp"):
        m1 = re.match(r".*_(\d+)$", newname[2])
        m2 = re.match(r".*_(\d+)$", oldname[2])
        if m1 and m2:
            idx1 = int(m1.group(1))
            idx2 = int(m2.group(1))
            if idx1 != idx2:
                return idx1 < idx2

    return newname < oldname

def wire_type(name):
    longname = name
    name = name.split('/')[-1]
    wt = None

    if name.startswith("glb_netwk_") or name.startswith("padin_"):
        wt = "GLOBAL"
    elif name.startswith("D_IN_") or name.startswith("D_OUT_"):
        wt = "LOCAL"
    elif name in ("OUT_ENB", "cen", "inclk", "latch", "outclk", "clk", "s_r", "carry_in", "carry_in_mux"):
        wt = "LOCAL"
    elif name in ("in_0", "in_1", "in_2", "in_3", "cout", "lout", "out", "fabout") or name.startswith("slf_op") or name.startswith("O_"):
        wt = "LOCAL"
    elif name.startswith("local_g") or name.startswith("glb2local_"):
        wt = "LOCAL"
    elif name.startswith("span4_horz_") or name.startswith("sp4_h_"):
        wt = "SP4_HORZ"
    elif name.startswith("span4_vert_") or name.startswith("sp4_v_") or name.startswith("sp4_r_v_"):
        wt = "SP4_VERT"
    elif name.startswith("span12_horz_") or name.startswith("sp12_h_"):
        wt = "SP12_HORZ"
    elif name.startswith("span12_vert_") or name.startswith("sp12_v_"):
        wt = "SP12_VERT"
    elif name.startswith("MASK_") or name.startswith("RADDR_") or name.startswith("WADDR_"):
        wt = "LOCAL"
    elif name.startswith("RDATA_")  or name.startswith("WDATA_") or name.startswith("neigh_op_"):
        wt = "LOCAL"
    elif name in ("WCLK", "WCLKE", "WE", "RCLK", "RCLKE", "RE"):
        wt = "LOCAL"
    elif name in ("PLLOUT_A", "PLLOUT_B"):
        wt = "LOCAL"

    if wt is None:
        print("No type for wire: %s (%s)" % (longname, name), file=sys.stderr)
        assert 0
    return wt

def pipdelay(src, dst):
    src = wire_names_r[src]
    dst = wire_names_r[dst]
    src_type = wire_type(src[2])
    dst_type = wire_type(dst[2])

    if src_type == "LOCAL" and dst_type == "LOCAL":
       return 250

    if src_type == "GLOBAL" and dst_type == "LOCAL":
       return 400

    # Local -> Span

    if src_type == "LOCAL" and dst_type in ("SP4_HORZ", "SP4_VERT"):
       return 350

    if src_type == "LOCAL" and dst_type in ("SP12_HORZ", "SP12_VERT"):
       return 500

    # Span -> Local

    if src_type in ("SP4_HORZ", "SP4_VERT", "SP12_HORZ", "SP12_VERT") and dst_type == "LOCAL":
       return 300

    # Span -> Span

    if src_type in ("SP12_HORZ", "SP12_VERT") and dst_type in ("SP12_HORZ", "SP12_VERT"):
       return 450

    if src_type in ("SP4_HORZ", "SP4_VERT") and dst_type in ("SP4_HORZ", "SP4_VERT"):
       return 300

    if src_type in ("SP12_HORZ", "SP12_VERT") and dst_type in ("SP4_HORZ", "SP4_VERT"):
       return 380

    # print(src, dst, src_type, dst_type, file=sys.stderr)
    assert 0



def init_tiletypes(device):
    global num_tile_types, tile_sizes, tile_bits
    if device == "5k":
        num_tile_types = 10
    else:
        num_tile_types = 5
    tile_sizes = {i: (0, 0) for i in range(num_tile_types)}
    tile_bits = [[] for _ in range(num_tile_types)]

with open(args.filename, "r") as f:
    mode = None

    for line in f:
        line = line.split()

        if len(line) == 0 or line[0] == "#":
            continue

        if line[0] == ".device":
            dev_name = line[1]
            init_tiletypes(dev_name)
            dev_width = int(line[2])
            dev_height = int(line[3])
            num_wires = int(line[4])
            continue

        if line[0] == ".net":
            mode = ("net", int(line[1]))
            continue

        if line[0] == ".buffer":
            mode = ("buffer", int(line[3]), int(line[1]), int(line[2]))
            switches.append((line[3], int(line[1]), int(line[2]), line[4:]))
            continue

        if line[0] == ".routing":
            mode = ("routing", int(line[3]), int(line[1]), int(line[2]))
            switches.append((line[3], int(line[1]), int(line[2]), line[4:]))
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

        if line[0] == ".dsp0_tile":
            tiles[(int(line[1]), int(line[2]))] = "dsp0"
            mode = None
            continue

        if line[0] == ".dsp1_tile":
            tiles[(int(line[1]), int(line[2]))] = "dsp1"
            mode = None
            continue

        if line[0] == ".dsp2_tile":
            tiles[(int(line[1]), int(line[2]))] = "dsp2"
            mode = None
            continue

        if line[0] == ".dsp3_tile":
            tiles[(int(line[1]), int(line[2]))] = "dsp3"
            mode = None
            continue

        if line[0] == ".ipcon_tile":
            tiles[(int(line[1]), int(line[2]))] = "ipcon"
            mode = None
            continue

        if line[0] == ".logic_tile_bits":
            mode = ("bits", 1)
            tile_sizes[1] = (int(line[1]), int(line[2]))
            continue

        if line[0] == ".io_tile_bits":
            mode = ("bits", 2)
            tile_sizes[2] = (int(line[1]), int(line[2]))
            continue

        if line[0] == ".ramb_tile_bits":
            mode = ("bits", 3)
            tile_sizes[3] = (int(line[1]), int(line[2]))
            continue

        if line[0] == ".ramt_tile_bits":
            mode = ("bits", 4)
            tile_sizes[4] = (int(line[1]), int(line[2]))
            continue

        if line[0] == ".dsp0_tile_bits":
            mode = ("bits", 5)
            tile_sizes[5] = (int(line[1]), int(line[2]))
            continue

        if line[0] == ".dsp1_tile_bits":
            mode = ("bits", 6)
            tile_sizes[6] = (int(line[1]), int(line[2]))
            continue

        if line[0] == ".dsp2_tile_bits":
            mode = ("bits", 7)
            tile_sizes[7] = (int(line[1]), int(line[2]))
            continue

        if line[0] == ".dsp3_tile_bits":
            mode = ("bits", 8)
            tile_sizes[8] = (int(line[1]), int(line[2]))
            continue

        if line[0] == ".ipcon_tile_bits":
            mode = ("bits", 9)
            tile_sizes[9] = (int(line[1]), int(line[2]))
            continue

        if line[0] == ".ieren":
            mode = ("ieren",)
            continue

        if line[0] == ".pins":
            mode = ("pins", line[1])
            packages.append((line[1], []))
            continue

        if line[0] == ".extra_cell":
            if len(line) >= 5:
                mode = ("extra_cell", (line[4], int(line[1]), int(line[2]), int(line[3])))
            elif line[3] == "WARMBOOT":
                mode = ("extra_cell", (line[3], int(line[1]), int(line[2]), 0))
            elif line[3] == "PLL":
                mode = ("extra_cell", (line[3], int(line[1]), int(line[2]), 3))
            else:
                assert 0
            extra_cells[mode[1]] = []
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
            if mode[1] not in wire_segments:
                wire_segments[mode[1]] = dict()
            if ("TILE_WIRE_" + wname[2].upper().replace("/", "_")) in gfx_wire_ids:
                wire_segments[mode[1]][(wname[0], wname[1])] = wname[2]
            continue

        if mode[0] in ("buffer", "routing"):
            wire_a = int(line[1])
            wire_b = mode[1]
            if wire_a not in wire_downhill:
                wire_downhill[wire_a] = set()
            if wire_b not in wire_uphill:
                wire_uphill[wire_b] = set()
            wire_downhill[wire_a].add(wire_b)
            wire_uphill[wire_b].add(wire_a)
            pip_xy[(wire_a, wire_b)] = (mode[2], mode[3], int(line[0], 2), len(switches) - 1)
            continue

        if mode[0] == "bits":
            name = line[0]
            bits = []
            for b in line[1:]:
                m = cbit_re.match(b)
                assert m
                bits.append((int(m.group(1)), int(m.group(2))))
            tile_bits[mode[1]].append((name, bits))
            continue

        if mode[0] == "ieren":
            ierens.append(tuple([int(_) for _ in line]))
            continue

        if mode[0] == "pins":
            packages[-1][1].append((line[0], int(line[1]), int(line[2]), int(line[3])))
            continue

        if mode[0] == "extra_cell":
            if line[0] == "LOCKED":
                extra_cells[mode[1]].append((("LOCKED_" + line[1]), (0, 0, "LOCKED")))
            else:
                extra_cells[mode[1]].append((line[0], (int(line[1]), int(line[2]), line[3])))
            continue

def add_wire(x, y, name):
    global num_wires
    wire_idx = num_wires
    num_wires = num_wires + 1
    wname = (x, y, name)
    wire_names[wname] = wire_idx
    wire_names_r[wire_idx] = wname
    wire_segments[wire_idx] = dict()

# Add virtual padin wires
for i in range(8):
    add_wire(0, 0, "padin_%d" % i)

def add_bel_input(bel, wire, port):
    if wire not in wire_belports:
        wire_belports[wire] = set()
    wire_belports[wire].add((bel, port))
    bel_wires[bel].append((wire, port, 0))

def add_bel_output(bel, wire, port):
    if wire not in wire_belports:
        wire_belports[wire] = set()
    wire_belports[wire].add((bel, port))
    bel_wires[bel].append((wire, port, 1))

def add_bel_lc(x, y, z):
    bel = len(bel_name)
    bel_name.append("X%d/Y%d/lc%d" % (x, y, z))
    bel_type.append("ICESTORM_LC")
    bel_pos.append((x, y, z))
    bel_wires.append(list())

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

    add_bel_input(bel, wire_in_0, "I0")
    add_bel_input(bel, wire_in_1, "I1")
    add_bel_input(bel, wire_in_2, "I2")
    add_bel_input(bel, wire_in_3, "I3")

    add_bel_output(bel, wire_out,  "O")
    add_bel_output(bel, wire_cout, "COUT")

    if wire_lout is not None:
        add_bel_output(bel, wire_lout, "LO")

def add_bel_io(x, y, z):
    bel = len(bel_name)
    bel_name.append("X%d/Y%d/io%d" % (x, y, z))
    bel_type.append("SB_IO")
    bel_pos.append((x, y, z))
    bel_wires.append(list())

    wire_cen   = wire_names[(x, y, "io_global/cen")]
    wire_iclk  = wire_names[(x, y, "io_global/inclk")]
    wire_latch = wire_names[(x, y, "io_global/latch")]
    wire_oclk  = wire_names[(x, y, "io_global/outclk")]

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
    bel_name.append("X%d/Y%d/ram" % (x, y))
    bel_type.append("ICESTORM_RAM")
    bel_pos.append((x, y, 0))
    bel_wires.append(list())

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

def add_bel_gb(xy, x, y, g):
    if xy[0] != x or xy[1] != y:
        return

    bel = len(bel_name)
    bel_name.append("X%d/Y%d/gb" % (x, y))
    bel_type.append("SB_GB")
    bel_pos.append((x, y, 2))
    bel_wires.append(list())

    add_bel_input(bel, wire_names[(x, y, "fabout")], "USER_SIGNAL_TO_GLOBAL_BUFFER")
    add_bel_output(bel, wire_names[(x, y, "glb_netwk_%d" % g)], "GLOBAL_BUFFER_OUTPUT")

def is_ec_wire(ec_entry):
    return ec_entry[1] in wire_names

def is_ec_output(ec_entry):
    wirename = ec_entry[1][2]
    if "O_" in wirename or "slf_op_" in wirename: return True
    if "neigh_op_" in wirename: return True
    if "glb_netwk_" in wirename: return True
    return False

def is_ec_pll_clock_output(ec, ec_entry):
    return ec[0] == 'PLL' and ec_entry[0] in ('PLLOUT_A', 'PLLOUT_B')

def add_bel_ec(ec):
    ectype, x, y, z = ec
    bel = len(bel_name)
    extra_cell_config[bel] = []
    bel_name.append("X%d/Y%d/%s_%d" % (x, y, ectype.lower(), z))
    bel_type.append(ectype)
    bel_pos.append((x, y, z))
    bel_wires.append(list())
    for entry in extra_cells[ec]:
        if is_ec_wire(entry) and "glb_netwk_" not in entry[1][2]: # TODO: osc glb output conflicts with GB
            if is_ec_output(entry):
                add_bel_output(bel, wire_names[entry[1]], entry[0])
            else:
                add_bel_input(bel, wire_names[entry[1]], entry[0])
        elif is_ec_pll_clock_output(ec, entry):
            x, y, z = entry[1]
            z = 'io_{}/D_IN_0'.format(z)
            add_bel_output(bel, wire_names[(x, y, z)], entry[0])
        else:
            extra_cell_config[bel].append(entry)

for tile_xy, tile_type in sorted(tiles.items()):
    if tile_type == "logic":
        for i in range(8):
            add_bel_lc(tile_xy[0], tile_xy[1], i)

    if tile_type == "io":
        for i in range(2):
            add_bel_io(tile_xy[0], tile_xy[1], i)

        if dev_name == "1k":
            add_bel_gb(tile_xy,  7,  0, 0)
            add_bel_gb(tile_xy,  7, 17, 1)
            add_bel_gb(tile_xy, 13,  9, 2)
            add_bel_gb(tile_xy,  0,  9, 3)
            add_bel_gb(tile_xy,  6, 17, 4)
            add_bel_gb(tile_xy,  6,  0, 5)
            add_bel_gb(tile_xy,  0,  8, 6)
            add_bel_gb(tile_xy, 13,  8, 7)
        elif dev_name == "5k":
            add_bel_gb(tile_xy, 13,  0, 0)
            add_bel_gb(tile_xy, 13, 31, 1)
            add_bel_gb(tile_xy, 19, 31, 2)
            add_bel_gb(tile_xy,  6, 31, 3)
            add_bel_gb(tile_xy, 12, 31, 4)
            add_bel_gb(tile_xy, 12,  0, 5)
            add_bel_gb(tile_xy,  6,  0, 6)
            add_bel_gb(tile_xy, 19,  0, 7)
        elif dev_name == "8k":
            add_bel_gb(tile_xy, 33, 16,  7)
            add_bel_gb(tile_xy,  0, 16,  6)
            add_bel_gb(tile_xy, 17, 33,  1)
            add_bel_gb(tile_xy, 17,  0,  0)
            add_bel_gb(tile_xy,  0, 17,  3)
            add_bel_gb(tile_xy, 33, 17,  2)
            add_bel_gb(tile_xy, 16,  0,  5)
            add_bel_gb(tile_xy, 16, 33,  4)
        elif dev_name == "384":
            add_bel_gb(tile_xy,  7,  4,  7)
            add_bel_gb(tile_xy,  0,  4,  6)
            add_bel_gb(tile_xy,  4,  9,  1)
            add_bel_gb(tile_xy,  4,  0,  0)
            add_bel_gb(tile_xy,  0,  5,  3)
            add_bel_gb(tile_xy,  7,  5,  2)
            add_bel_gb(tile_xy,  3,  0,  5)
            add_bel_gb(tile_xy,  3,  9,  4)

    if tile_type == "ramb":
        add_bel_ram(tile_xy[0], tile_xy[1])

    for ec in sorted(extra_cells.keys()):
        if ec[1] == tile_xy[0] and ec[2] == tile_xy[1]:
            add_bel_ec(ec)

for ec in sorted(extra_cells.keys()):
    if ec[1] in (0, dev_width - 1) and ec[2] in (0, dev_height - 1):
        add_bel_ec(ec)

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
        print("str %s" % s)

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

bba = BinaryBlobAssembler()
bba.pre('#include "nextpnr.h"')
bba.pre('NEXTPNR_NAMESPACE_BEGIN')
bba.post('NEXTPNR_NAMESPACE_END')
bba.push("chipdb_blob_%s" % dev_name)
bba.r("chip_info_%s" % dev_name, "chip_info")

index = 0
for bel in range(len(bel_name)):
    bba.l("bel_wires_%d" % bel, "BelWirePOD")
    for i in range(len(bel_wires[bel])):
        bba.u32(bel_wires[bel][i][0], "wire_index")
        bba.u32(portpins[bel_wires[bel][i][1]], "port")
        bba.u32(bel_wires[bel][i][2], "type")
        index += 1

bba.l("bel_data_%s" % dev_name, "BelInfoPOD")
for bel in range(len(bel_name)):
    bba.s(bel_name[bel], "name")
    bba.u32(beltypes[bel_type[bel]], "type")
    bba.u32(len(bel_wires[bel]), "num_bel_wires")
    bba.r("bel_wires_%d" % bel, "bel_wires")
    bba.u8(bel_pos[bel][0], "x")
    bba.u8(bel_pos[bel][1], "y")
    bba.u8(bel_pos[bel][2], "z")
    bba.u8(0, "padding")

wireinfo = list()
pipinfo = list()
pipcache = dict()

for wire in range(num_wires):
    if wire in wire_uphill:
        pips = list()
        for src in wire_uphill[wire]:
            if (src, wire) not in pipcache:
                pipcache[(src, wire)] = len(pipinfo)
                pi = dict()
                pi["src"] = src
                pi["dst"] = wire
                pi["delay"] = pipdelay(src, wire)
                pi["x"] = pip_xy[(src, wire)][0]
                pi["y"] = pip_xy[(src, wire)][1]
                pi["switch_mask"] = pip_xy[(src, wire)][2]
                pi["switch_index"] = pip_xy[(src, wire)][3]
                pipinfo.append(pi)
            pips.append(pipcache[(src, wire)])
        num_uphill = len(pips)
        list_uphill = "wire%d_uppips" % wire
        bba.l(list_uphill, "int32_t")
        for p in pips:
            bba.u32(p, None)
    else:
        num_uphill = 0
        list_uphill = None

    if wire in wire_downhill:
        pips = list()
        for dst in wire_downhill[wire]:
            if (wire, dst) not in pipcache:
                pipcache[(wire, dst)] = len(pipinfo)
                pi = dict()
                pi["src"] = wire
                pi["dst"] = dst
                pi["delay"] = pipdelay(wire, dst)
                pi["x"] = pip_xy[(wire, dst)][0]
                pi["y"] = pip_xy[(wire, dst)][1]
                pi["switch_mask"] = pip_xy[(wire, dst)][2]
                pi["switch_index"] = pip_xy[(wire, dst)][3]
                pipinfo.append(pi)
            pips.append(pipcache[(wire, dst)])
        num_downhill = len(pips)
        list_downhill = "wire%d_downpips" % wire
        bba.l(list_downhill, "int32_t")
        for p in pips:
            bba.u32(p, None)
    else:
        num_downhill = 0
        list_downhill = None

    if wire in wire_belports:
        num_bel_pins = len(wire_belports[wire])
        bba.l("wire%d_bels" % wire, "BelPortPOD")
        for belport in sorted(wire_belports[wire]):
            bba.u32(belport[0], "bel_index")
            bba.u32(portpins[belport[1]], "port")
    else:
        num_bel_pins = 0

    info = dict()
    info["name"] = "X%d/Y%d/%s" % wire_names_r[wire]

    info["num_uphill"] = num_uphill
    info["list_uphill"] = list_uphill

    info["num_downhill"] = num_downhill
    info["list_downhill"] = list_downhill

    info["num_bel_pins"] = num_bel_pins
    info["list_bel_pins"] = ("wire%d_bels" % wire) if num_bel_pins > 0 else None

    avg_x, avg_y = 0, 0
    if wire in wire_xy:
        for x, y in wire_xy[wire]:
            avg_x += x
            avg_y += y
        avg_x /= len(wire_xy[wire])
        avg_y /= len(wire_xy[wire])

    info["x"] = int(round(avg_x))
    info["y"] = int(round(avg_y))

    wireinfo.append(info)

packageinfo = []

for package in packages:
    name, pins = package
    safename = re.sub("[^A-Za-z0-9]", "_", name)
    pins_info = []
    for pin in pins:
        pinname, x, y, z = pin
        pin_bel = "X%d/Y%d/io%d" % (x, y, z)
        bel_idx = bel_name.index(pin_bel)
        pins_info.append((pinname, bel_idx))
    bba.l("package_%s_pins" % safename, "PackagePinPOD")
    for pi in pins_info:
        bba.s(pi[0], "name")
        bba.u32(pi[1], "bel_index")
    packageinfo.append((name, len(pins_info), "package_%s_pins" % safename))

tilegrid = []
for y in range(dev_height):
    for x in range(dev_width):
        if (x, y) in tiles:
            tilegrid.append(tiles[x, y].upper())
        else:
            tilegrid.append("NONE")

tileinfo = []
for t in range(num_tile_types):
    centries_info = []
    for cb in tile_bits[t]:
        name, bits = cb
        safename = re.sub("[^A-Za-z0-9]", "_", name)
        bba.l("tile%d_%s_bits" % (t, safename), "ConfigBitPOD")
        for row, col in bits:
            bba.u8(row, "row")
            bba.u8(col, "col")
        if len(bits) == 0:
            bba.u32(0, "padding")
        elif len(bits) % 2 == 1:
            bba.u16(0, "padding")
        centries_info.append((name, len(bits), t, safename))
    bba.l("tile%d_config" % t, "ConfigEntryPOD")
    for name, num_bits, t, safename in centries_info:
        bba.s(name, "name")
        bba.u32(num_bits, "num_bits")
        bba.r("tile%d_%s_bits" % (t, safename), "num_bits")
    if len(centries_info) == 0:
        bba.u32(0, "padding")
    ti = dict()
    ti["cols"] = tile_sizes[t][0]
    ti["rows"] = tile_sizes[t][1]
    ti["num_entries"] = len(centries_info)
    ti["entries"] = "tile%d_config" % t
    tileinfo.append(ti)

bba.l("wire_data_%s" % dev_name, "WireInfoPOD")
for wire, info in enumerate(wireinfo):
    bba.s(info["name"], "name")
    bba.u32(info["num_uphill"], "num_uphill")
    bba.u32(info["num_downhill"], "num_downhill")
    bba.r(info["list_uphill"], "pips_uphill")
    bba.r(info["list_downhill"], "pips_downhill")
    bba.u32(info["num_bel_pins"], "num_bel_pins")
    bba.r(info["list_bel_pins"], "bel_pins")
    bba.u32(len(wire_segments[wire]), "num_segments")
    if len(wire_segments[wire]):
        bba.r("wire_segments_%d" % wire, "segments")
    else:
        bba.u32(0, "segments")

    bba.u8(info["x"], "x")
    bba.u8(info["y"], "y")
    bba.u8(wiretypes[wire_type(info["name"])], "type")
    bba.u8(0, "padding")

for wire in range(num_wires):
    if len(wire_segments[wire]):
        bba.l("wire_segments_%d" % wire, "WireSegmentPOD")
        for xy, seg in sorted(wire_segments[wire].items()):
            bba.u8(xy[0], "x")
            bba.u8(xy[1], "y")
            bba.u16(gfx_wire_ids["TILE_WIRE_" + seg.upper().replace("/", "_")], "index")

bba.l("pip_data_%s" % dev_name, "PipInfoPOD")
for info in pipinfo:
    src_seg = -1
    src_segname = wire_names_r[info["src"]]
    if (info["x"], info["y"]) in wire_segments[info["src"]]:
        src_segname = wire_segments[info["src"]][(info["x"], info["y"])]
        src_seg = gfx_wire_ids["TILE_WIRE_" + src_segname.upper().replace("/", "_")]
        src_segname = src_segname.replace("/", ".")

    dst_seg = -1
    dst_segname = wire_names_r[info["dst"]]
    if (info["x"], info["y"]) in wire_segments[info["dst"]]:
        dst_segname = wire_segments[info["dst"]][(info["x"], info["y"])]
        dst_seg = gfx_wire_ids["TILE_WIRE_" + dst_segname.upper().replace("/", "_")]
        dst_segname = dst_segname.replace("/", ".")

    # bba.s("X%d/Y%d/%s->%s" % (info["x"], info["y"], src_segname, dst_segname), "name")
    bba.u32(info["src"], "src")
    bba.u32(info["dst"], "dst")
    bba.u32(info["delay"], "delay")
    bba.u8(info["x"], "x")
    bba.u8(info["y"], "y")
    bba.u16(src_seg, "src_seg")
    bba.u16(dst_seg, "dst_seg")
    bba.u16(info["switch_mask"], "switch_mask")
    bba.u32(info["switch_index"], "switch_index")

switchinfo = []
for switch in switches:
    dst, x, y, bits = switch
    bitlist = []
    for b in bits:
        m = cbit_re.match(b)
        assert m
        bitlist.append((int(m.group(1)), int(m.group(2))))
    si = dict()
    si["x"] = x
    si["y"] = y
    si["bits"] = bitlist
    switchinfo.append(si)

bba.l("switch_data_%s" % dev_name, "SwitchInfoPOD")
for info in switchinfo:
    bba.u32(len(info["bits"]), "num_bits")
    bba.u8(info["x"], "x")
    bba.u8(info["y"], "y")
    for i in range(5):
        if i < len(info["bits"]):
            bba.u8(info["bits"][i][0], "row<%d>" % i)
            bba.u8(info["bits"][i][1], "col<%d>" % i)
        else:
            bba.u8(0, "row<%d> (unused)" % i)
            bba.u8(0, "col<%d> (unused)" % i)

bba.l("tile_data_%s" % dev_name, "TileInfoPOD")
for info in tileinfo:
    bba.u8(info["cols"], "cols")
    bba.u8(info["rows"], "rows")
    bba.u16(info["num_entries"], "num_entries")
    bba.r(info["entries"], "entries")

bba.l("ieren_data_%s" % dev_name, "IerenInfoPOD")
for ieren in ierens:
    bba.u8(ieren[0], "iox")
    bba.u8(ieren[1], "ioy")
    bba.u8(ieren[2], "ioz")
    bba.u8(ieren[3], "ierx")
    bba.u8(ieren[4], "iery")
    bba.u8(ieren[5], "ierz")

if len(ierens) % 2 == 1:
    bba.u16(0, "padding")

bba.l("bits_info_%s" % dev_name, "BitstreamInfoPOD")
bba.u32(len(switchinfo), "num_switches")
bba.u32(len(ierens), "num_ierens")
bba.r("tile_data_%s" % dev_name, "tiles_nonrouting")
bba.r("switch_data_%s" % dev_name, "switches")
bba.r("ieren_data_%s" % dev_name, "ierens")

bba.l("tile_grid_%s" % dev_name, "TileType")
for t in tilegrid:
    bba.u32(tiletypes[t], "tiletype")

for bel_idx, entries in sorted(extra_cell_config.items()):
    if len(entries) > 0:
        bba.l("bel%d_config_entries" % bel_idx, "BelConfigEntryPOD")
        for entry in entries:
            bba.s(entry[0], "entry_name")
            bba.s(entry[1][2], "cbit_name")
            bba.u8(entry[1][0], "x")
            bba.u8(entry[1][1], "y")
            bba.u16(0, "padding")

if len(extra_cell_config) > 0:
    bba.l("bel_config_%s" % dev_name, "BelConfigPOD")
    for bel_idx, entries in sorted(extra_cell_config.items()):
        bba.u32(bel_idx, "bel_index")
        bba.u32(len(entries), "num_entries")
        bba.r("bel%d_config_entries" % bel_idx if len(entries) > 0 else None, "entries")

bba.l("package_info_%s" % dev_name, "PackageInfoPOD")
for info in packageinfo:
    bba.s(info[0], "name")
    bba.u32(info[1], "num_pins")
    bba.r(info[2], "pins")

bba.l("chip_info_%s" % dev_name)
bba.u32(dev_width, "dev_width")
bba.u32(dev_height, "dev_height")
bba.u32(len(bel_name), "num_bels")
bba.u32(num_wires, "num_wires")
bba.u32(len(pipinfo), "num_pips")
bba.u32(len(switchinfo), "num_switches")
bba.u32(len(extra_cell_config), "num_belcfgs")
bba.u32(len(packageinfo), "num_packages")
bba.r("bel_data_%s" % dev_name, "bel_data")
bba.r("wire_data_%s" % dev_name, "wire_data")
bba.r("pip_data_%s" % dev_name, "pip_data")
bba.r("tile_grid_%s" % dev_name, "tile_grid")
bba.r("bits_info_%s" % dev_name, "bits_info")
bba.r("bel_config_%s" % dev_name if len(extra_cell_config) > 0 else None, "bel_config")
bba.r("package_info_%s" % dev_name, "packages_data")

bba.pop()

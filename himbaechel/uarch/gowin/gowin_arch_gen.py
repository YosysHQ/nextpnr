from os import path
import sys

import importlib.resources
import pickle
import gzip
import re
import argparse

sys.path.append(path.join(path.dirname(__file__), "../.."))
from himbaechel_dbgen.chip import *
from apycula import chipdb

# Bel flags
BEL_FLAG_SIMPLE_IO = 0x100

# Chip flags
CHIP_HAS_SP32              = 0x1
CHIP_NEED_SP_FIX           = 0x2
CHIP_NEED_BSRAM_OUTREG_FIX = 0x4
CHIP_NEED_BLKSEL_FIX       = 0x8
CHIP_HAS_BANDGAP           = 0x10
CHIP_HAS_PLL_HCLK          = 0x20
CHIP_HAS_CLKDIV_HCLK       = 0x40
CHIP_HAS_PINCFG            = 0x80
CHIP_HAS_DFF67             = 0x100

# Tile flags
TILE_I3C_CAPABLE_IO        = 0x1

# Z of the bels
# sync with C++ part!
LUT0_Z  = 0       # z(DFFx) = z(LUTx) + 1
LUT7_Z  = 14
MUX20_Z = 16
MUX21_Z = 18
MUX23_Z = 22
MUX27_Z = 29
ALU0_Z  = 30 # : 35, 6 ALUs
RAMW_Z  = 36 # RAM16SDP4

IOBA_Z  = 50
IOBB_Z  = 51

IOLOGICA_Z = 70
IDES16_Z   = 74
OSER16_Z   = 75

BUFG_Z  = 76 # : 81 reserve just in case
BSRAM_Z = 100

OSC_Z   = 274
PLL_Z   = 275
GSR_Z   = 276
VCC_Z   = 277
GND_Z   = 278
BANDGAP_Z = 279

DQCE_Z = 280 # : 286 reserve for 6 DQCEs
DCS_Z  = 286 # : 288 reserve for 2 DCSs
DHCEN_Z = 288 # : 298

USERFLASH_Z = 298

EMCU_Z      = 300

MIPIOBUF_Z  = 301
MIPIIBUF_Z  = 302

DLLDLY_Z    = 303 # : 305 reserve for 2 DLLDLYs

PINCFG_Z    = 400 #

DSP_Z          = 509

DSP_0_Z        = 511 # DSP macro 0
PADD18_0_0_Z   = 512
PADD9_0_0_Z    = 512 + 1
PADD9_0_1_Z    = 512 + 2
PADD18_0_1_Z   = 516
PADD9_0_2_Z    = 516 + 1
PADD9_0_3_Z    = 516 + 2

MULT18X18_0_0_Z  = 520
MULT9X9_0_0_Z    = 520 + 1
MULT9X9_0_1_Z    = 520 + 2
MULT18X18_0_1_Z  = 524
MULT9X9_0_2_Z    = 524 + 1
MULT9X9_0_3_Z    = 524 + 2

ALU54D_0_Z       = 524 + 3
MULTALU18X18_0_Z = 528
MULTALU36X18_0_Z = 528 + 1
MULTADDALU18X18_0_Z = 528 + 2

MULT36X36_Z    = 528 + 3

DSP_1_Z        = 543 # DSP macro 1
PADD18_1_0_Z   = 544
PADD9_1_0_Z    = 544 + 1
PADD9_1_1_Z    = 544 + 2
PADD18_1_1_Z   = 548
PADD9_1_2_Z    = 548 + 1
PADD9_1_3_Z    = 548 + 2

MULT18X18_1_0_Z  = 552
MULT9X9_1_0_Z    = 552 + 1
MULT9X9_1_1_Z    = 552 + 2
MULT18X18_1_1_Z  = 556
MULT9X9_1_2_Z    = 556 + 1
MULT9X9_1_3_Z    = 556 + 2

ALU54D_1_Z       = 556 + 3
MULTALU18X18_1_Z = 560
MULTALU36X18_1_Z = 560 + 1
MULTADDALU18X18_1_Z = 560 + 2

CLKDIV2_0_Z = 610
CLKDIV2_1_Z = 611
CLKDIV2_2_Z = 612
CLKDIV2_3_Z = 613


CLKDIV_0_Z = 620
CLKDIV_1_Z = 621
CLKDIV_2_Z = 622
CLKDIV_3_Z = 623

# =======================================
# Chipdb additional info
# =======================================
@dataclass
class TileExtraData(BBAStruct):
    tile_class: IdString # The general functionality of the slightly different tiles,
                         # let's say the behavior of LUT+DFF in the tiles are completely identical,
                         # but one of them also contains clock-wire switches,
                         # then we assign them to the same LOGIC class.
    io16_x_off: int = 0  # OSER16/IDES16 offsets to the aux cell
    io16_y_off: int = 0
    tile_flags: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.tile_class.index)
        bba.u16(self.io16_x_off)
        bba.u16(self.io16_y_off)
        bba.u32(self.tile_flags)

@dataclass
class BottomIOCnd(BBAStruct):
    wire_a_net: IdString
    wire_b_net: IdString

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.wire_a_net.index)
        bba.u32(self.wire_b_net.index)

@dataclass
class BottomIO(BBAStruct):
    conditions: list[BottomIOCnd] = field(default_factory = list)

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_conditions")
        for i, cnd in enumerate(self.conditions):
            cnd.serialise(f"{context}_cnd{i}", bba)

    def serialise(self, context: str, bba: BBAWriter):
        bba.slice(f"{context}_conditions", len(self.conditions))

# spine -> bel for different bels
@dataclass
class SpineBel(BBAStruct):
    spine: IdString
    bel_x: int
    bel_y: int
    bel_z: int

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.spine.index)
        bba.u32(self.bel_x)
        bba.u32(self.bel_y)
        bba.u32(self.bel_z)

# io -> dlldly bels
@dataclass
class IoBel(BBAStruct):
    io: IdString
    dlldly: IdString

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.io.index)
        bba.u32(self.dlldly.index)

# wire -> bel for DHCEN bels
@dataclass
class WireBel(BBAStruct):
    pip_xy: IdString
    pip_dst: IdString
    pip_src: IdString
    bel_x: int
    bel_y: int
    bel_z: int
    hclk_side: IdString

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.pip_xy.index)
        bba.u32(self.pip_dst.index)
        bba.u32(self.pip_src.index)
        bba.u32(self.bel_x)
        bba.u32(self.bel_y)
        bba.u32(self.bel_z)
        bba.u32(self.hclk_side.index)

# segment column description
@dataclass
class Segment(BBAStruct):
    x: int
    seg_idx: int
    min_x: int
    min_y: int
    max_x: int
    max_y: int
    top_row: int
    bottom_row: int
    top_wire: IdString
    bottom_wire: IdString
    top_gate_wire: list[IdString] = field(default_factory = list)
    bottom_gate_wire: list[IdString] = field(default_factory = list)

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_top_gate_wire")
        for i, wire in enumerate(self.top_gate_wire):
            bba.u32(wire.index)
        bba.label(f"{context}_bottom_gate_wire")
        for i, wire in enumerate(self.bottom_gate_wire):
            bba.u32(wire.index)

    def serialise(self, context: str, bba: BBAWriter):
        bba.u16(self.x)
        bba.u16(self.seg_idx)
        bba.u16(self.min_x)
        bba.u16(self.min_y)
        bba.u16(self.max_x)
        bba.u16(self.max_y)
        bba.u16(self.top_row)
        bba.u16(self.bottom_row)
        bba.u32(self.top_wire.index)
        bba.u32(self.bottom_wire.index)
        bba.slice(f"{context}_top_gate_wire", len(self.top_gate_wire))
        bba.slice(f"{context}_bottom_gate_wire", len(self.bottom_gate_wire))

@dataclass
class ChipExtraData(BBAStruct):
    strs: StringPool
    flags: int
    bottom_io: BottomIO
    diff_io_types: list[IdString] = field(default_factory = list)
    dqce_bels: list[SpineBel] = field(default_factory = list)
    dcs_bels: list[SpineBel] = field(default_factory = list)
    dhcen_bels: list[WireBel] = field(default_factory = list)
    io_dlldly_bels: list[IoBel] = field(default_factory = list)
    segments: list[Segment] = field(default_factory = list)

    def create_bottom_io(self):
        self.bottom_io = BottomIO()

    def add_bottom_io_cnd(self, net_a: str, net_b: str):
        self.bottom_io.conditions.append(BottomIOCnd(self.strs.id(net_a), self.strs.id(net_b)))

    def add_diff_io_type(self, diff_type: str):
        self.diff_io_types.append(self.strs.id(diff_type))

    def add_dhcen_bel(self, pip_xy: str, pip_dst: str, pip_src, x: int, y: int, z: int, side: str):
        self.dhcen_bels.append(WireBel(self.strs.id(pip_xy), self.strs.id(pip_dst), self.strs.id(pip_src), x, y, z, self.strs.id(side)))

    def add_dqce_bel(self, spine: str, x: int, y: int, z: int):
        self.dqce_bels.append(SpineBel(self.strs.id(spine), x, y, z))

    def add_dcs_bel(self, spine: str, x: int, y: int, z: int):
        self.dcs_bels.append(SpineBel(self.strs.id(spine), x, y, z))

    def add_io_dlldly_bel(self, io: str, dlldly: str):
        self.io_dlldly_bels.append(IoBel(self.strs.id(io), self.strs.id(dlldly)))
    def add_segment(self, x: int, seg_idx: int, min_x: int, min_y: int, max_x: int, max_y: int,
            top_row: int, bottom_row: int, top_wire: str, bottom_wire: str, top_gate_wire: list, bottom_gate_wire: list):
        new_seg = Segment(x, seg_idx, min_x, min_y, max_x, max_y, top_row, bottom_row,
                self.strs.id(top_wire), self.strs.id(bottom_wire),
                [self.strs.id(top_gate_wire[0])], [self.strs.id(bottom_gate_wire[0])])
        if top_gate_wire[1]:
            new_seg.top_gate_wire.append(self.strs.id(top_gate_wire[1]))
        else:
            new_seg.top_gate_wire.append(self.strs.id(''))
        if bottom_gate_wire[1]:
            new_seg.bottom_gate_wire.append(self.strs.id(bottom_gate_wire[1]))
        else:
            new_seg.bottom_gate_wire.append(self.strs.id(''))
        self.segments.append(new_seg)

    def serialise_lists(self, context: str, bba: BBAWriter):
        self.bottom_io.serialise_lists(f"{context}_bottom_io", bba)
        for i, t in enumerate(self.segments):
            t.serialise_lists(f"{context}_segment{i}", bba)
        bba.label(f"{context}_diff_io_types")
        for i, diff_io_type in enumerate(self.diff_io_types):
            bba.u32(diff_io_type.index)
        bba.label(f"{context}_dqce_bels")
        for i, t in enumerate(self.dqce_bels):
            t.serialise(f"{context}_dqce_bel{i}", bba)
        bba.label(f"{context}_dcs_bels")
        for i, t in enumerate(self.dcs_bels):
            t.serialise(f"{context}_dcs_bel{i}", bba)
        bba.label(f"{context}_dhcen_bels")
        for i, t in enumerate(self.dhcen_bels):
            t.serialise(f"{context}_dhcen_bel{i}", bba)
        bba.label(f"{context}_io_dlldly_bels")
        for i, t in enumerate(self.io_dlldly_bels):
            t.serialise(f"{context}_io_dlldly_bel{i}", bba)
        bba.label(f"{context}_segments")
        for i, t in enumerate(self.segments):
            t.serialise(f"{context}_segment{i}", bba)

    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.flags)
        self.bottom_io.serialise(f"{context}_bottom_io", bba)
        bba.slice(f"{context}_diff_io_types", len(self.diff_io_types))
        bba.slice(f"{context}_dqce_bels", len(self.dqce_bels))
        bba.slice(f"{context}_dcs_bels", len(self.dcs_bels))
        bba.slice(f"{context}_dhcen_bels", len(self.dhcen_bels))
        bba.slice(f"{context}_io_dlldly_bels", len(self.io_dlldly_bels))
        bba.slice(f"{context}_segments", len(self.segments))

@dataclass
class PackageExtraData(BBAStruct):
    strs: StringPool
    cst: list

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_constraints")
        for (net, row, col, bel, iostd) in self.cst:
            bba.u32(self.strs.id(net).index)
            bba.u32(row)
            bba.u32(col)
            bba.u32(ord(bel[0])-ord('A')+IOBA_Z)
            bba.u32(self.strs.id(iostd).index if iostd else 0)

    def serialise(self, context: str, bba: BBAWriter):
        bba.slice(f"{context}_constraints", len(self.cst))

@dataclass
class PadExtraData(BBAStruct):
    # Which PLL does this pad belong to.
    pll_tile: IdString
    pll_bel:  IdString
    pll_type: IdString

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.pll_tile.index)
        bba.u32(self.pll_bel.index)
        bba.u32(self.pll_type.index)

# Unique features of the tiletype
class TypeDesc:
    def __init__(self, dups, tiletype = '', extra_func = None, sfx = 0):
        self.tiletype = tiletype
        self.extra_func = extra_func
        self.dups = dups
        self.sfx = sfx
created_tiletypes = {}

# get timing class by wire name
def get_tm_class(db: chipdb, wire: str):
    assert wire in db.wire_delay, f"Unknown timing class for {wire}"
    return db.wire_delay[wire]

# u-turn at the rim
uturnlut = {'N': 'S', 'S': 'N', 'E': 'W', 'W': 'E'}
def uturn(db: chipdb, x: int, y: int, wire: str):
    m = re.match(r"([NESW])([128]\d)(\d)", wire)
    if m:
        direction, num, segment = m.groups()
        # wires wrap around the edges
        # assumes 0-based indexes
        if y < 0:
            y = -1 - y
            direction = uturnlut[direction]
        if x < 0:
            x = -1 - x
            direction = uturnlut[direction]
        if y > db.rows - 1:
            y = 2 * db.rows - 1 - y
            direction = uturnlut[direction]
        if x > db.cols - 1:
            x = 2 * db.cols - 1 - x
            direction = uturnlut[direction]
        wire = f'{direction}{num}{segment}'
    return (x, y, wire)

def create_nodes(chip: Chip, db: chipdb):
    # : (x, y)
    dirs = { 'N': (0, -1), 'S': (0, 1), 'W': (-1, 0), 'E': (1, 0) }
    X = db.cols
    Y = db.rows
    global_nodes = {}
    for y in range(Y):
        for x in range(X):
            nodes = []
            tt = chip.tile_type_at(x, y)
            extra_tile_data = tt.extra_data
            # SN and EW
            for i in [1, 2]:
                nodes.append([NodeWire(x, y, f'SN{i}0'),
                        NodeWire(*uturn(db, x, y - 1, f'N1{i}1')),
                        NodeWire(*uturn(db, x, y + 1, f'S1{i}1'))])
                nodes.append([NodeWire(x, y, f'EW{i}0'),
                        NodeWire(*uturn(db, x - 1, y, f'W1{i}1')),
                        NodeWire(*uturn(db, x + 1, y, f'E1{i}1'))])
            for d, offs in dirs.items():
                # 1-hop
                for i in [0, 3]:
                    nodes.append([NodeWire(x, y, f'{d}1{i}0'),
                        NodeWire(*uturn(db, x + offs[0], y + offs[1], f'{d}1{i}1'))])
                # 2-hop
                for i in range(8):
                    nodes.append([NodeWire(x, y, f'{d}2{i}0'),
                        NodeWire(*uturn(db, x + offs[0], y + offs[1], f'{d}2{i}1')),
                        NodeWire(*uturn(db, x + offs[0] * 2, y + offs[1] * 2, f'{d}2{i}2'))])
                # 4-hop
                for i in range(4):
                    nodes.append([NodeWire(x, y, f'{d}8{i}0'),
                        NodeWire(*uturn(db, x + offs[0] * 4, y + offs[1] * 4, f'{d}8{i}4')),
                        NodeWire(*uturn(db, x + offs[0] * 8, y + offs[1] * 8, f'{d}8{i}8'))])
            # I0 for MUX2_LUT8
            if (x < X - 1 and extra_tile_data.tile_class == chip.strs.id('LOGIC')
                    and  chip.tile_type_at(x + 1, y).extra_data.tile_class == chip.strs.id('LOGIC')):
                nodes.append([NodeWire(x, y, 'OF30'),
                             NodeWire(x + 1, y, 'OF3')])

            # ALU
            if extra_tile_data.tile_class == chip.strs.id('LOGIC'):
                # local carry chain
                for i in range(5):
                    nodes.append([NodeWire(x, y, f'COUT{i}'),
                                  NodeWire(x, y, f'CIN{i + 1}')]);
                # gobal carry chain
                if x > 1 and chip.tile_type_at(x - 1, y).extra_data.tile_class == chip.strs.id('LOGIC'):
                    nodes.append([NodeWire(x, y, f'CIN0'),
                                  NodeWire(x - 1, y, f'COUT5')])

            for node in nodes:
                chip.add_node(node)

            # VCC and VSS sources in the all tiles
            global_nodes.setdefault('GND', []).append(NodeWire(x, y, 'VSS'))
            global_nodes.setdefault('VCC', []).append(NodeWire(x, y, 'VCC'))

    # add nodes from the apicula db
    for node_name, node_hdr in db.nodes.items():
        wire_type, node = node_hdr
        if len(node) < 2:
            continue
        min_wire_name_len = 0
        if node:
            min_wire_name_len = len(next(iter(node))[2])
        for y, x, wire in node:
            if wire_type:
                if not chip.tile_type_at(x, y).has_wire(wire):
                    chip.tile_type_at(x, y).create_wire(wire, wire_type)
                else:
                    chip.tile_type_at(x, y).set_wire_type(wire, wire_type)
            new_node = NodeWire(x, y, wire)
            gl_nodes = global_nodes.setdefault(node_name, [])
            if new_node not in gl_nodes:
                if len(wire) < min_wire_name_len:
                    min_wire_name_len = len(wire)
                    gl_nodes.insert(0, new_node)
                else:
                    gl_nodes.append(new_node)

    for name, node in global_nodes.items():
        chip.add_node(node)

def create_switch_matrix(tt: TileType, db: chipdb, x: int, y: int):
    def get_wire_type(name):
        if name in {'XD0', 'XD1', 'XD2', 'XD3', 'XD4', 'XD5',}:
            return "X0"
        if name in {"PCLK_DUMMY"}:
            return "GLOBAL_CLK"
        if name in {"DLLDLY_OUT"}:
            return "DLLDLY_O"
        if name in {'LT00', 'LT10', 'LT20', 'LT30', 'LT02', 'LT13'}:
            return "LW_TAP"
        return ""

    for dst, srcs in db.grid[y][x].pips.items():
        if not tt.has_wire(dst):
            tt.create_wire(dst, get_wire_type(dst))
        for src in srcs.keys():
            if src not in db.wire_delay:
                continue
            if not tt.has_wire(src):
                if src in {"VSS", "VCC"}:
                    tt.create_wire(src, get_wire_type(src), const_value = src)
                else:
                    tt.create_wire(src, get_wire_type(src))
            tt.create_pip(src, dst, get_tm_class(db, src))

    # clock wires
    for dst, srcs in db.grid[y][x].clock_pips.items():
        if not tt.has_wire(dst):
            tt.create_wire(dst, "GLOBAL_CLK")
        for src in srcs.keys():
            if not tt.has_wire(src):
                tt.create_wire(src, "GLOBAL_CLK")
            src_tm_class = get_tm_class(db, src)
            tt.create_pip(src, dst, src_tm_class)

def create_hclk_switch_matrix(tt: TileType, db: chipdb, x: int, y: int):
    if (y, x) not in db.hclk_pips:
        return
    # hclk wires
    for dst, srcs in db.hclk_pips[y, x].items():
        if not tt.has_wire(dst):
            tt.create_wire(dst, "HCLK")
        for src in srcs.keys():
            if not tt.has_wire(src):
                tt.create_wire(src, "HCLK")
            tt.create_pip(src, dst, get_tm_class(db, "X01")) # XXX

    hclk_bel_zs = {
        "CLKDIV2_HCLK0_SECT0": CLKDIV2_0_Z,
        "CLKDIV2_HCLK0_SECT1": CLKDIV2_1_Z,
        "CLKDIV2_HCLK1_SECT0": CLKDIV2_2_Z,
        "CLKDIV2_HCLK1_SECT1": CLKDIV2_3_Z,
        "CLKDIV_HCLK0_SECT0": CLKDIV_0_Z,
        "CLKDIV_HCLK0_SECT1": CLKDIV_1_Z,
        "CLKDIV_HCLK1_SECT0": CLKDIV_2_Z,
        "CLKDIV_HCLK1_SECT1": CLKDIV_3_Z
    }

    for bel_name, bel_props in db.grid[y][x].bels.items():
        if (bel_name not in hclk_bel_zs):
            continue
        this_portmap = bel_props.portmap

        if bel_name.startswith("CLKDIV2_"):
            bel_type = "CLKDIV2"
        elif bel_name.startswith("CLKDIV_"):
            bel_type = "CLKDIV"
        this_bel = tt.create_bel(bel_name, bel_type, hclk_bel_zs[bel_name])

        if (bel_name in ["CLKDIV_HCLK0_SECT1", "CLKDIV_HCLK1_SECT1"]):
            this_bel.flags |= BEL_FLAG_HIDDEN
        if bel_type=="CLKDIV":
            this_bel.flags |= BEL_FLAG_GLOBAL

        known_pins = ["HCLKIN", "RESETN", "CLKOUT"]
        if bel_type == "CLKDIV":
            known_pins.append("CALIB")

        for pin in this_portmap.keys():
            assert pin in known_pins, f"Unknown pin {pin} for bel {this_bel}"
            if pin in ["CALIB", "RESETN", "HCLKIN"]:
                pin_direction = PinType.INPUT
            elif pin in ["CLKOUT"]:
                pin_direction = PinType.OUTPUT
            wire_type = "HCLK_CTRL" if pin in ("CALIB", "RESETN") else "HCLK"
            add_port_wire(tt, this_bel, this_portmap, pin, wire_type, pin_direction)


# map spine -> dqce bel
dqce_bels = {}
# map spine -> dcs bel
dcs_bels = {}

# map HCLKIN wire -> dhcen bel
dhcen_bels = {}

# map io bel -> dlldly bel
io_dlldly_bels = {}

def create_extra_funcs(tt: TileType, db: chipdb, x: int, y: int):
    if (y, x) not in db.extra_func:
        return
    for func, desc in db.extra_func[(y, x)].items():
        if func == 'osc':
            osc_type = desc['type']
            portmap = db.grid[y][x].bels[osc_type].portmap
            for port, wire in portmap.items():
                if not tt.has_wire(wire):
                    tt.create_wire(wire, port)
            bel = tt.create_bel(osc_type, osc_type, z = OSC_Z)
            for port, wire in portmap.items():
                if 'OUT' in port:
                    tt.add_bel_pin(bel, port, wire, PinType.OUTPUT)
                else:
                    tt.add_bel_pin(bel, port, wire, PinType.INPUT)
        elif func == 'gsr':
            wire = desc['wire']
            if not tt.has_wire(wire):
                tt.create_wire(wire)
            bel = tt.create_bel("GSR", "GSR", z = GSR_Z)
            tt.add_bel_pin(bel, "GSRI", wire, PinType.INPUT)
        elif func == 'bandgap':
            wire = desc['wire']
            if not tt.has_wire(wire):
                tt.create_wire(wire)
            bel = tt.create_bel("BANDGAP", "BANDGAP", z = BANDGAP_Z)
            tt.add_bel_pin(bel, "BGEN", wire, PinType.INPUT)
        elif func == 'dhcen':
            for idx, dhcen in enumerate(desc):
                wire = dhcen['ce']
                if not tt.has_wire(wire):
                    tt.create_wire(wire)
                bel_z = DHCEN_Z + idx
                bel = tt.create_bel(f"DHCEN{idx}", "DHCEN", z = bel_z)
                tt.add_bel_pin(bel, "CE", wire, PinType.INPUT)
                pip_xy, pip_dst, pip_src, side = dhcen['pip']
                dhcen_bels[pip_xy, pip_dst, pip_src] = (x, y, bel_z, side)
        elif func == 'dlldly':
            for idx, dlldly in desc.items():
                bel_z = DLLDLY_Z + idx
                bel = tt.create_bel(f"DLLDLY{idx}", "DLLDLY", z = bel_z)
                for pin, wire in dlldly['in_wires'].items():
                    if not tt.has_wire(wire):
                        tt.create_wire(wire)
                    tt.add_bel_pin(bel, pin, wire, PinType.INPUT)
                for pin, wire in dlldly['out_wires'].items():
                    if not tt.has_wire(wire):
                        tt.create_wire(wire)
                    tt.add_bel_pin(bel, pin, wire, PinType.OUTPUT)
                io_dlldly_bels[f"{dlldly['io_loc']}/{dlldly['io_bel']}"] = f"X{x}Y{y}/DLLDLY{idx}"

        elif func == 'dqce':
            for idx in range(6):
                bel_z = DQCE_Z + idx
                bel = tt.create_bel(f"DQCE{idx}", "DQCE", bel_z)
                wire = desc[idx]['clkin']
                dqce_bels[wire] = (x, y, bel_z)
                if not tt.has_wire(wire):
                    tt.create_wire(wire, "GLOBAL_CLK")
                tt.add_bel_pin(bel, "CLKIN", wire, PinType.INPUT)
                tt.add_bel_pin(bel, "CLKOUT", wire, PinType.OUTPUT)
                wire = desc[idx]['ce']
                if not tt.has_wire(wire):
                    tt.create_wire(wire)
                tt.add_bel_pin(bel, "CE", wire, PinType.INPUT)
        elif func == 'dcs':
            for idx in range(2):
                if idx not in desc:
                    continue
                bel_z = DCS_Z + idx
                bel = tt.create_bel(f"DCS{idx}", "DCS", bel_z)
                wire = desc[idx]['clkout']
                if not tt.has_wire(wire):
                    tt.create_wire(wire)
                tt.add_bel_pin(bel, "CLKOUT", wire, PinType.OUTPUT)
                clkout_wire = wire
                for clk_idx, wire in enumerate(desc[idx]['clk']):
                    if not tt.has_wire(wire):
                        tt.create_wire(wire, "GLOBAL_CLK")
                    tt.add_bel_pin(bel, f"CLK{clk_idx}", wire, PinType.INPUT)
                    # This is a fake PIP that allows routing “through” this
                    # primitive from the CLK input to the CLKOUT output.
                    tt.create_pip(wire, clkout_wire)
                    dcs_bels[wire] = (x, y, bel_z)
                for i, wire in enumerate(desc[idx]['clksel']):
                    if not tt.has_wire(wire):
                        tt.create_wire(wire)
                    tt.add_bel_pin(bel, f"CLKSEL{i}", wire, PinType.INPUT)
                wire = desc[idx]['selforce']
                if not tt.has_wire(wire):
                    tt.create_wire(wire)
                tt.add_bel_pin(bel, "SELFORCE", wire, PinType.INPUT)
        elif func == 'io16':
            role = desc['role']
            if role == 'MAIN':
                y_off, x_off = desc['pair']
                tt.extra_data.io16_x_off = x_off
                tt.extra_data.io16_y_off = y_off

            for io_type, z in {('IDES16', IDES16_Z), ('OSER16', OSER16_Z)}:
                bel = tt.create_bel(io_type, io_type, z = z)
                portmap = db.grid[y][x].bels[io_type].portmap
                for port, wire in portmap.items():
                    if port == 'FCLK': # XXX compatibility
                        wire = 'FCLKA'
                    if not tt.has_wire(wire):
                        if port in {'CLK', 'PCLK'}:
                            tt.create_wire(wire, "TILE_CLK")
                        else:
                            tt.create_wire(wire, "IOL_PORT")
                    if 'OUT' in port:
                        tt.add_bel_pin(bel, port, wire, PinType.OUTPUT)
                    else:
                        tt.add_bel_pin(bel, port, wire, PinType.INPUT)
        elif func == 'i3c_capable':
            tt.extra_data.tile_flags |= TILE_I3C_CAPABLE_IO
        elif func == 'mipi_obuf':
            bel = tt.create_bel('MIPI_OBUF', 'MIPI_OBUF', MIPIOBUF_Z)
        elif func == 'mipi_ibuf':
            bel = tt.create_bel('MIPI_IBUF', 'MIPI_IBUF', MIPIIBUF_Z)
            wire = desc['HSREN']
            if not tt.has_wire(wire):
                tt.create_wire(wire)
            tt.add_bel_pin(bel, 'HSREN', wire, PinType.INPUT)
            wire = 'MIPIOL'
            if not tt.has_wire(wire):
                tt.create_wire(wire)
            tt.add_bel_pin(bel, 'OL', wire, PinType.OUTPUT)
            for i in range(2):
                wire = f'MIPIEN{i}'
                if not tt.has_wire(wire):
                    tt.create_wire(wire)
                tt.add_bel_pin(bel, f'MIPIEN{i}', wire, PinType.INPUT)
        elif func == 'buf':
            for buf_type, wires in desc.items():
                for i, wire in enumerate(wires):
                    if not tt.has_wire(wire):
                        tt.create_wire(wire, "TILE_CLK")
                    wire_out = f'{buf_type}{i}_O'
                    tt.create_wire(wire_out, "BUFG_O")
                    # XXX make Z from buf_type
                    bel = tt.create_bel(f'{buf_type}{i}', buf_type, z = BUFG_Z + i)
                    bel.flags = BEL_FLAG_GLOBAL
                    tt.add_bel_pin(bel, "I", wire, PinType.INPUT)
                    tt.add_bel_pin(bel, "O", wire_out, PinType.OUTPUT)
        elif func == 'userflash':
                bel = tt.create_bel("USERFLASH", desc['type'], USERFLASH_Z)
                portmap = desc['ins']
                for port, wire in portmap.items():
                    if not tt.has_wire(wire):
                        tt.create_wire(wire, "FLASH_IN")
                    tt.add_bel_pin(bel, port, wire, PinType.INPUT)
                portmap = desc['outs']
                for port, wire in portmap.items():
                    if not tt.has_wire(wire):
                        tt.create_wire(wire, "FLASH_OUT")
                    tt.add_bel_pin(bel, port, wire, PinType.OUTPUT)
        elif func == 'emcu':
                bel = tt.create_bel("EMCU", "EMCU", EMCU_Z)
                portmap = desc['ins']
                for port, wire in portmap.items():
                    if not tt.has_wire(wire):
                        tt.create_wire(wire, "EMCU_IN")
                    tt.add_bel_pin(bel, port, wire, PinType.INPUT)
                portmap = desc['outs']
                for port, wire in portmap.items():
                    if not tt.has_wire(wire):
                        tt.create_wire(wire, "EMCU_OUT")
                    tt.add_bel_pin(bel, port, wire, PinType.OUTPUT)
        elif func == 'pincfg':
                bel = tt.create_bel("PINCFG", "PINCFG", PINCFG_Z)
                portmap = desc['ins']
                for port, wire in portmap.items():
                    if not tt.has_wire(wire):
                        tt.create_wire(wire, "PINCFG_IN")
                    tt.add_bel_pin(bel, port, wire, PinType.INPUT)

def create_tiletype(create_func, chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    has_extra_func = (y, x) in db.extra_func

    # (found, TypeDesc)
    def find_or_make_dup():
        for d in created_tiletypes[ttyp].dups:
            if has_extra_func and d.extra_func == db.extra_func[(y, x)]:
                return (True, d)
            elif not has_extra_func and not d.extra_func:
                return (True, d)
        sfx = len(created_tiletypes[ttyp].dups) + 1
        if has_extra_func:
            tdesc = TypeDesc(extra_func = db.extra_func[(y, x)], sfx = sfx, dups = [])
        else:
            tdesc = TypeDesc(sfx = sfx, dups = [])
        created_tiletypes[ttyp].dups.append(tdesc)
        return (False, tdesc)

    old_type = False
    if ttyp not in created_tiletypes:
        # new type
        if has_extra_func:
            tdesc = TypeDesc(extra_func = db.extra_func[(y, x)], dups = [])
        else:
            tdesc = TypeDesc(dups = [])
        created_tiletypes.update({ttyp: tdesc})
    else:
        # find similar
        if has_extra_func:
            if created_tiletypes[ttyp].extra_func == db.extra_func[(y, x)]:
                tdesc = created_tiletypes[ttyp]
                old_type = True
            else:
                old_type, tdesc = find_or_make_dup()
        elif not created_tiletypes[ttyp].extra_func:
                tdesc = created_tiletypes[ttyp]
                old_type = True
        else:
                old_type, tdesc = find_or_make_dup()

    if old_type:
        chip.set_tile_type(x, y, tdesc.tiletype)
        return

    tt = create_func(chip, db, x, y, ttyp, tdesc)

    create_extra_funcs(tt, db, x, y)
    create_hclk_switch_matrix(tt, db, x, y)
    create_switch_matrix(tt, db, x, y)
    chip.set_tile_type(x, y, tdesc.tiletype)

def add_port_wire(tt, bel, portmap, name, wire_type, port_type, pin_name = None):
    wire = portmap[name]
    if not tt.has_wire(wire):
        if name.startswith('CLK'):
            tt.create_wire(wire, "TILE_CLK")
        else:
            tt.create_wire(wire, wire_type)
    if pin_name:
        tt.add_bel_pin(bel, pin_name, wire, port_type)
    else:
        tt.add_bel_pin(bel, name, wire, port_type)

def create_null_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int, tdesc: TypeDesc):
    typename = "NULL"
    tiletype = f"{typename}_{ttyp}"
    if tdesc.sfx != 0:
        tiletype += f"_{tdesc.sfx}"
    tt = chip.create_tile_type(tiletype)
    tt.extra_data = TileExtraData(chip.strs.id(typename))
    tdesc.tiletype = tiletype
    return tt

# responsible nodes, there will be IO banks, configuration, etc.
def create_corner_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int, tdesc: TypeDesc):
    typename = "CORNER"
    tiletype = f"{typename}_{ttyp}"
    if tdesc.sfx != 0:
        tiletype += f"_{tdesc.sfx}"
    tt = chip.create_tile_type(tiletype)
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    if x == 0 and y == 0:
        # GND is the logic low level generator
        tt.create_wire('VSS', 'GND', const_value = 'VSS')
        gnd = tt.create_bel('GND', 'GND', z = GND_Z)
        tt.add_bel_pin(gnd, "G", "VSS", PinType.OUTPUT)
        # VCC is the logic high level generator
        tt.create_wire('VCC', 'VCC', const_value = 'VCC')
        gnd = tt.create_bel('VCC', 'VCC', z = VCC_Z)
        tt.add_bel_pin(gnd, "V", "VCC", PinType.OUTPUT)

    tdesc.tiletype = tiletype
    return tt


# IO
def create_io_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int, tdesc: TypeDesc):
    typename = "IO"
    tiletype = f"{typename}_{ttyp}"
    if tdesc.sfx != 0:
        tiletype += f"_{tdesc.sfx}"
    tt = chip.create_tile_type(tiletype)
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    simple_io = y in db.simplio_rows and chip.name in {'GW1N-1', 'GW1NZ-1', 'GW1N-4'}
    if simple_io:
        rng = 10
    else:
        rng = 2
    for i in range(rng):
        name = 'IOB' + 'ABCDEFGHIJ'[i]
        # XXX some IOBs excluded from generic chipdb for some reason
        if name not in db.grid[y][x].bels:
            continue
        # wires
        portmap = db.grid[y][x].bels[name].portmap
        tt.create_wire(portmap['I'], "IO_I")
        tt.create_wire(portmap['O'], "IO_O")
        tt.create_wire(portmap['OE'], "IO_OE")
        # bels
        io = tt.create_bel(name, "IOB", z = IOBA_Z + i)
        if simple_io and chip.name in {'GW1N-1'}:
            io.flags |= BEL_FLAG_SIMPLE_IO
        tt.add_bel_pin(io, "I", portmap['I'], PinType.INPUT)
        tt.add_bel_pin(io, "OEN", portmap['OE'], PinType.INPUT)
        tt.add_bel_pin(io, "O", portmap['O'], PinType.OUTPUT)
        # bottom io
        if 'BOTTOM_IO_PORT_A' in portmap and portmap['BOTTOM_IO_PORT_A']:
            if not tt.has_wire(portmap['BOTTOM_IO_PORT_A']):
                tt.create_wire(portmap['BOTTOM_IO_PORT_A'], "IO_I")
                tt.create_wire(portmap['BOTTOM_IO_PORT_B'], "IO_I")
            tt.add_bel_pin(io, "BOTTOM_IO_PORT_A", portmap['BOTTOM_IO_PORT_A'], PinType.INPUT)
            tt.add_bel_pin(io, "BOTTOM_IO_PORT_B", portmap['BOTTOM_IO_PORT_B'], PinType.INPUT)
    # create IOLOGIC bels if any
    for idx, name in {(IOLOGICA_Z, 'IOLOGICA'), (IOLOGICA_Z + 1, 'IOLOGICB')}:
        if name not in db.grid[y][x].bels:
            continue
        for off, io_type in {(0, 'O'), (2, 'I')}:
            iol = tt.create_bel(f"{name}{io_type}", f"IOLOGIC{io_type}", z = idx + off)
            for port, wire in db.grid[y][x].bels[name].portmap.items():
                if port == 'FCLK': # XXX compatibility
                    wire = f'FCLK{name[-1]}'
                if not tt.has_wire(wire):
                    if port in {'CLK', 'PCLK', 'MCLK'}:
                        tt.create_wire(wire, "TILE_CLK")
                    else:
                        tt.create_wire(wire, "IOL_PORT")
                if port in {'Q', 'Q0', 'Q1', 'Q2', 'Q3', 'Q4', 'Q5', 'Q6', 'Q7', 'Q8', 'Q9', 'DF', 'LAG', 'LEAD'}:
                    tt.add_bel_pin(iol, port, wire, PinType.OUTPUT)
                else:
                    tt.add_bel_pin(iol, port, wire, PinType.INPUT)
    tdesc.tiletype = tiletype
    return tt

# logic: luts, dffs, alu etc
def create_logic_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int, tdesc: TypeDesc):
    typename = "LOGIC"
    tiletype = f"{typename}_{ttyp}"
    if tdesc.sfx != 0:
        tiletype += f"_{tdesc.sfx}"
    tt = chip.create_tile_type(tiletype)
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    lut_inputs = ['A', 'B', 'C', 'D']
    # setup LUT wires
    for i in range(8):
        for inp_name in lut_inputs:
            tt.create_wire(f"{inp_name}{i}", "LUT_IN")
        tt.create_wire(f"F{i}", "LUT_OUT")
        # experimental. the wire is false - it is assumed that DFF is always
        # connected to the LUT's output F{i}, but we can place primitives
        # arbitrarily and create a pass-through LUT afterwards.
        # just out of curiosity
        tt.create_wire(f"XD{i}", "FF_INPUT")
        tt.create_wire(f"Q{i}", "FF_OUT")
    # setup DFF wires
    for j in range(3):
        tt.create_wire(f"CLK{j}", "TILE_CLK")
        tt.create_wire(f"LSR{j}", "TILE_LSR")
        tt.create_wire(f"CE{j}",  "TILE_CE")
    # setup MUX2 wires
    for j in range(8):
        tt.create_wire(f"OF{j}", "MUX_OUT")
        tt.create_wire(f"SEL{j}", "MUX_SEL")
    tt.create_wire("OF30", "MUX_OUT")
    # setup ALU wires
    for j in range(6):
        tt.create_wire(f"CIN{j}", "ALU_CIN")
        tt.create_wire(f"COUT{j}", "ALU_COUT")

    # create logic cells
    for i in range(8):
        # LUT
        lut = tt.create_bel(f"LUT{i}", "LUT4", z = (i * 2 + 0))
        for j, inp_name in enumerate(lut_inputs):
            tt.add_bel_pin(lut, f"I{j}", f"{inp_name}{i}", PinType.INPUT)
        tt.add_bel_pin(lut, "F", f"F{i}", PinType.OUTPUT)
        if i < 6 or "HAS_DFF67" in db.chip_flags:
            tt.create_pip(f"F{i}", f"XD{i}", get_tm_class(db, f"F{i}"))
            # also experimental input for FF using SEL wire - this theory will
            # allow to place unrelated LUT and FF next to each other
            # don't create for now
            #tt.create_pip(f"SEL{i}", f"XD{i}", get_tm_class(db, f"SEL{i}"))

            # FF
            ff = tt.create_bel(f"DFF{i}", "DFF", z =(i * 2 + 1))
            tt.add_bel_pin(ff, "D", f"XD{i}", PinType.INPUT)
            tt.add_bel_pin(ff, "Q", f"Q{i}", PinType.OUTPUT)
            if i < 6:
                tt.add_bel_pin(ff, "CLK", f"CLK{i // 2}", PinType.INPUT)
                tt.add_bel_pin(ff, "SET", f"LSR{i // 2}", PinType.INPUT)
                tt.add_bel_pin(ff, "RESET", f"LSR{i // 2}", PinType.INPUT)
                tt.add_bel_pin(ff, "PRESET", f"LSR{i // 2}", PinType.INPUT)
                tt.add_bel_pin(ff, "CLEAR", f"LSR{i // 2}", PinType.INPUT)
                tt.add_bel_pin(ff, "CE", f"CE{i // 2}", PinType.INPUT)
            else:
                tt.add_bel_pin(ff, "CLK", "CLK2", PinType.INPUT)
                tt.add_bel_pin(ff, "SET", "LSR2", PinType.INPUT)
                tt.add_bel_pin(ff, "RESET", "LSR2", PinType.INPUT)
                tt.add_bel_pin(ff, "PRESET", "LSR2", PinType.INPUT)
                tt.add_bel_pin(ff, "CLEAR", "LSR2", PinType.INPUT)
                tt.add_bel_pin(ff, "CE", "CE2", PinType.INPUT)

        if i < 6:
            # ALU
            ff = tt.create_bel(f"ALU{i}", "ALU", z = i + ALU0_Z)
            tt.add_bel_pin(ff, "SUM", f"F{i}", PinType.OUTPUT)
            tt.add_bel_pin(ff, "COUT", f"COUT{i}", PinType.OUTPUT)
            tt.add_bel_pin(ff, "CIN", f"CIN{i}", PinType.INPUT)
            # pinout for the ADDSUB ALU mode
            tt.add_bel_pin(ff, "I0", f"A{i}", PinType.INPUT)
            tt.add_bel_pin(ff, "I1", f"B{i}", PinType.INPUT)
            tt.add_bel_pin(ff, "I2", f"C{i}", PinType.INPUT)
            tt.add_bel_pin(ff, "I3", f"D{i}", PinType.INPUT)

    # wide luts
    for i in range(4):
        ff = tt.create_bel(f"MUX{i * 2}", "MUX2_LUT5", z = MUX20_Z + i * 4)
        tt.add_bel_pin(ff, "I0", f"F{i * 2}", PinType.INPUT)
        tt.add_bel_pin(ff, "I1", f"F{i * 2 + 1}", PinType.INPUT)
        tt.add_bel_pin(ff, "O",  f"OF{i * 2}", PinType.OUTPUT)
        tt.add_bel_pin(ff, "S0", f"SEL{i * 2}", PinType.INPUT)
    for i in range(2):
        ff = tt.create_bel(f"MUX{i * 4 + 1}", "MUX2_LUT6", z = MUX21_Z + i * 8)
        tt.add_bel_pin(ff, "I0", f"OF{i * 4 + 2}", PinType.INPUT)
        tt.add_bel_pin(ff, "I1", f"OF{i * 4}", PinType.INPUT)
        tt.add_bel_pin(ff, "O",  f"OF{i * 4 + 1}", PinType.OUTPUT)
        tt.add_bel_pin(ff, "S0", f"SEL{i * 4 + 1}", PinType.INPUT)
    ff = tt.create_bel(f"MUX3", "MUX2_LUT7", z = MUX23_Z)
    tt.add_bel_pin(ff, "I0", f"OF5", PinType.INPUT)
    tt.add_bel_pin(ff, "I1", f"OF1", PinType.INPUT)
    tt.add_bel_pin(ff, "O",  f"OF3", PinType.OUTPUT)
    tt.add_bel_pin(ff, "S0", f"SEL3", PinType.INPUT)
    ff = tt.create_bel(f"MUX7", "MUX2_LUT8", z = MUX27_Z)
    tt.add_bel_pin(ff, "I0", f"OF30", PinType.INPUT)
    tt.add_bel_pin(ff, "I1", f"OF3", PinType.INPUT)
    tt.add_bel_pin(ff, "O",  f"OF7", PinType.OUTPUT)
    tt.add_bel_pin(ff, "S0", f"SEL7", PinType.INPUT)

    tdesc.tiletype = tiletype
    return tt

def create_ssram_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int, tdesc: TypeDesc):
    # SSRAM is LUT based, so it's logic-like
    tt = create_logic_tiletype(chip, db, x, y, ttyp, tdesc)

    lut_inputs = ['A', 'B', 'C', 'D']
    ff = tt.create_bel(f"RAM16SDP4", "RAM16SDP4", z = RAMW_Z)
    for i in range(4):
        tt.add_bel_pin(ff, f"DI[{i}]", f"{lut_inputs[i]}5", PinType.INPUT)
        tt.add_bel_pin(ff, f"WAD[{i}]", f"{lut_inputs[i]}4", PinType.INPUT)
        # RAD[0] is assumed to be connected to A3, A2, A1 and A0. But
        # for now we connect it only to A0, the others will be connected
        # directly during packing. RAD[1...3] - similarly.
        tt.add_bel_pin(ff, f"RAD[{i}]", f"{lut_inputs[i]}0", PinType.INPUT)
        tt.add_bel_pin(ff, f"DO[{i}]", f"F{i}", PinType.OUTPUT)

    tt.add_bel_pin(ff, "CLK", "CLK2", PinType.INPUT)
    tt.add_bel_pin(ff, "CE",  "CE2", PinType.INPUT)
    tt.add_bel_pin(ff, "WRE", "LSR2", PinType.INPUT)
    return tt

# BSRAM
_bsram_inputs = {'CLK', 'OCE', 'CE', 'RESET', 'WRE'}
def create_bsram_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int, tdesc: TypeDesc):
    typename = "BSRAM"
    tiletype = f"{typename}_{ttyp}"
    if tdesc.sfx != 0:
        tiletype += f"_{tdesc.sfx}"
    tt = chip.create_tile_type(tiletype)
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    portmap = db.grid[y][x].bels['BSRAM'].portmap
    bsram = tt.create_bel("BSRAM", "BSRAM", z = BSRAM_Z)


    for sfx in {'', 'A', 'B'}:
        for inp in _bsram_inputs:
            add_port_wire(tt, bsram, portmap, f"{inp}{sfx}", "BSRAM_I", PinType.INPUT)
        for idx in range(3):
            add_port_wire(tt, bsram, portmap, f"BLKSEL{sfx}{idx}", "BSRAM_I", PinType.INPUT)
        for idx in range(14):
            add_port_wire(tt, bsram, portmap, f"AD{sfx}{idx}", "BSRAM_I", PinType.INPUT)
        for idx in range(18):
            add_port_wire(tt, bsram, portmap, f"DI{sfx}{idx}", "BSRAM_I", PinType.INPUT)
            add_port_wire(tt, bsram, portmap, f"DO{sfx}{idx}", "BSRAM_O", PinType.OUTPUT)
        if not sfx:
            for idx in range(18, 36):
                add_port_wire(tt, bsram, portmap, f"DI{idx}", "BSRAM_I", PinType.INPUT)
                add_port_wire(tt, bsram, portmap, f"DO{idx}", "BSRAM_O", PinType.OUTPUT)

    tdesc.tiletype = tiletype
    return tt

# DSP
_mult_inputs = {'ASEL', 'BSEL', 'ASIGN', 'BSIGN'}
def create_dsp_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int, tdesc: TypeDesc):
    typename = "DSP"
    tiletype = f"{typename}_{ttyp}"
    if tdesc.sfx != 0:
        tiletype += f"_{tdesc.sfx}"
    tt = chip.create_tile_type(tiletype)
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    # create big DSP
    belname = f'DSP'
    portmap = db.grid[y][x].bels[belname].portmap
    dsp = tt.create_bel(belname, "DSP", DSP_Z)
    dsp.flags = BEL_FLAG_HIDDEN

    # create DSP macros
    for idx in range(2):
        belname = f'DSP{idx}'
        portmap = db.grid[y][x].bels[belname].portmap
        dsp = tt.create_bel(belname, "DSP", eval(f'DSP_{idx}_Z'))
        dsp.flags = BEL_FLAG_HIDDEN

    # create pre-adders
    for mac, idx in [(mac, idx) for mac in range(2) for idx in range(4)]:
        belname = f'PADD9{mac}{idx}'
        portmap = db.grid[y][x].bels[belname].portmap
        dsp = tt.create_bel(belname, "PADD9", eval(f'PADD9_{mac}_{idx}_Z'))

        add_port_wire(tt, dsp, portmap, "ADDSUB", "DSP_I", PinType.INPUT)
        for sfx in {'A', 'B'}:
            for inp in range(9):
                add_port_wire(tt, dsp, portmap, f"{sfx}{inp}", "DSP_I", PinType.INPUT)
        for inp in range(9):
            add_port_wire(tt, dsp, portmap, f"C{inp}", "DSP_I", PinType.INPUT)
        for inp in range(4):
            add_port_wire(tt, dsp, portmap, f"CE{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"CLK{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"RESET{inp}", "DSP_I", PinType.INPUT)
        add_port_wire(tt, dsp, portmap, "ASEL", "DSP_I", PinType.INPUT)
        for outp in range(9):
            add_port_wire(tt, dsp, portmap, f"DOUT{outp}", "DSP_O", PinType.OUTPUT)

    for mac, idx in [(mac, idx) for mac in range(2) for idx in range(2)]:
        belname = f'PADD18{mac}{idx}'
        portmap = db.grid[y][x].bels[belname].portmap
        dsp = tt.create_bel(belname, "PADD18", eval(f'PADD18_{mac}_{idx}_Z'))

        add_port_wire(tt, dsp, portmap, "ADDSUB", "DSP_I", PinType.INPUT)
        for sfx in {'A', 'B'}:
            for inp in range(18):
                add_port_wire(tt, dsp, portmap, f"{sfx}{inp}", "DSP_I", PinType.INPUT)
        for inp in range(18):
            add_port_wire(tt, dsp, portmap, f"C{inp}", "DSP_I", PinType.INPUT)
        for inp in range(4):
            add_port_wire(tt, dsp, portmap, f"CE{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"CLK{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"RESET{inp}", "DSP_I", PinType.INPUT)
        add_port_wire(tt, dsp, portmap, "ASEL", "DSP_I", PinType.INPUT)
        for outp in range(18):
            add_port_wire(tt, dsp, portmap, f"DOUT{outp}", "DSP_O", PinType.OUTPUT)

    # create multipliers
    # mult 9x9
    for mac, idx in [(mac, idx) for mac in range(2) for idx in range(4)]:
        belname = f'MULT9X9{mac}{idx}'
        portmap = db.grid[y][x].bels[belname].portmap
        dsp = tt.create_bel(belname, "MULT9X9", eval(f'MULT9X9_{mac}_{idx}_Z'))

        for sfx in {'A', 'B'}:
            for inp in range(9):
                add_port_wire(tt, dsp, portmap, f"{sfx}{inp}", "DSP_I", PinType.INPUT)
        for inp in _mult_inputs:
            add_port_wire(tt, dsp, portmap, inp, "DSP_I", PinType.INPUT)
        for inp in range(4):
            add_port_wire(tt, dsp, portmap, f"CE{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"CLK{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"RESET{inp}", "DSP_I", PinType.INPUT)
        for outp in range(18):
            add_port_wire(tt, dsp, portmap, f"DOUT{outp}", "DSP_O", PinType.OUTPUT)

    # mult 18x18
    for mac, idx in [(mac, idx) for mac in range(2) for idx in range(2)]:
        belname = f'MULT18X18{mac}{idx}'
        portmap = db.grid[y][x].bels[belname].portmap
        dsp = tt.create_bel(belname, "MULT18X18", eval(f'MULT18X18_{mac}_{idx}_Z'))

        for sfx in {'A', 'B'}:
            for inp in range(18):
                add_port_wire(tt, dsp, portmap, f"{sfx}{inp}", "DSP_I", PinType.INPUT)
        for inp in _mult_inputs:
            add_port_wire(tt, dsp, portmap, inp, "DSP_I", PinType.INPUT)
        for inp in range(4):
            add_port_wire(tt, dsp, portmap, f"CE{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"CLK{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"RESET{inp}", "DSP_I", PinType.INPUT)
        for outp in range(36):
            add_port_wire(tt, dsp, portmap, f"DOUT{outp}", "DSP_O", PinType.OUTPUT)

    # mult 36x36
    belname = 'MULT36X36'
    portmap = db.grid[y][x].bels[belname].portmap
    dsp = tt.create_bel(belname, "MULT36X36", MULT36X36_Z)

    # LSB 18x18 multipliers sign ports must be zero
    add_port_wire(tt, dsp, db.grid[y][x].bels['MULT18X1800'].portmap, 'ASIGN', "DSP_I", PinType.INPUT, 'ZERO_ASIGN0')
    add_port_wire(tt, dsp, db.grid[y][x].bels['MULT18X1800'].portmap, 'BSIGN', "DSP_I", PinType.INPUT, 'ZERO_BSIGN0')
    add_port_wire(tt, dsp, db.grid[y][x].bels['MULT18X1801'].portmap, 'BSIGN', "DSP_I", PinType.INPUT, 'ZERO_BSIGN1')
    add_port_wire(tt, dsp, db.grid[y][x].bels['MULT18X1810'].portmap, 'ASIGN', "DSP_I", PinType.INPUT, 'ZERO_ASIGN1')
    for i in range(2):
        for sfx in {'A', 'B'}:
            for inp in range(36):
                add_port_wire(tt, dsp, portmap, f"{sfx}{inp}{i}", "DSP_I", PinType.INPUT)
        for inp in {'ASIGN', 'BSIGN'}:
            add_port_wire(tt, dsp, portmap, f"{inp}{i}", "DSP_I", PinType.INPUT)
        for inp in range(4):
            add_port_wire(tt, dsp, portmap, f"CE{inp}{i}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"CLK{inp}{i}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"RESET{inp}{i}", "DSP_I", PinType.INPUT)
    for outp in range(72):
        add_port_wire(tt, dsp, portmap, f"DOUT{outp}", "DSP_O", PinType.OUTPUT)

    # create alus
    for mac in range(2):
        belname = f'ALU54D{mac}'
        portmap = db.grid[y][x].bels[belname].portmap
        dsp = tt.create_bel(belname, "ALU54D", eval(f'ALU54D_{mac}_Z'))

        for sfx in {'A', 'B'}:
            for inp in range(54):
                add_port_wire(tt, dsp, portmap, f"{sfx}{inp}", "DSP_I", PinType.INPUT)
        for inp in {'ASIGN', 'BSIGN'}:
            add_port_wire(tt, dsp, portmap, inp, "DSP_I", PinType.INPUT)
        for inp in range(4):
            add_port_wire(tt, dsp, portmap, f"CE{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"CLK{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"RESET{inp}", "DSP_I", PinType.INPUT)
            if inp < 2:
                add_port_wire(tt, dsp, portmap, f"ACCLOAD{inp}", "DSP_I", PinType.INPUT)
        for outp in range(54):
            add_port_wire(tt, dsp, portmap, f"DOUT{outp}", "DSP_O", PinType.OUTPUT)

    # create multalus
    # MULTALU18X18
    for mac in range(2):
        belname = f'MULTALU18X18{mac}'
        portmap = db.grid[y][x].bels[belname].portmap
        dsp = tt.create_bel(belname, "MULTALU18X18", eval(f'MULTALU18X18_{mac}_Z'))

        for i in range(2):
            for sfx in {'ASIGN', 'BSIGN'}:
                add_port_wire(tt, dsp, portmap, f"{sfx}{i}", "DSP_I", PinType.INPUT)
            for sfx in {'A', 'B'}:
                for inp in range(18):
                    add_port_wire(tt, dsp, portmap, f"{sfx}{inp}{i}", "DSP_I", PinType.INPUT)
        for sfx in {'C', 'D'}:
            for inp in range(54):
                add_port_wire(tt, dsp, portmap, f"{sfx}{inp}", "DSP_I", PinType.INPUT)
        add_port_wire(tt, dsp, portmap, "DSIGN", "DSP_I", PinType.INPUT)
        for inp in range(4):
            add_port_wire(tt, dsp, portmap, f"CE{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"CLK{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"RESET{inp}", "DSP_I", PinType.INPUT)
            if inp < 2:
                add_port_wire(tt, dsp, portmap, f"ACCLOAD{inp}", "DSP_I", PinType.INPUT)
        for outp in range(54):
            add_port_wire(tt, dsp, portmap, f"DOUT{outp}", "DSP_O", PinType.OUTPUT)

    # MULTALU36X18
    for mac in range(2):
        belname = f'MULTALU36X18{mac}'
        portmap = db.grid[y][x].bels[belname].portmap
        dsp = tt.create_bel(belname, "MULTALU36X18", eval(f'MULTALU36X18_{mac}_Z'))

        for i in range(2):
            for sfx in {'ASIGN', 'BSIGN'}:
                add_port_wire(tt, dsp, portmap, f"{sfx}{i}", "DSP_I", PinType.INPUT)
            for inp in range(18):
                add_port_wire(tt, dsp, portmap, f"A{inp}{i}", "DSP_I", PinType.INPUT)
        for inp in range(7):
            add_port_wire(tt, dsp, portmap, f"ALUSEL{inp}", "DSP_I", PinType.INPUT)
        for inp in range(36):
            add_port_wire(tt, dsp, portmap, f"B{inp}", "DSP_I", PinType.INPUT)
        for inp in range(54):
            add_port_wire(tt, dsp, portmap, f"C{inp}", "DSP_I", PinType.INPUT)
        for inp in range(4):
            add_port_wire(tt, dsp, portmap, f"CE{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"CLK{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"RESET{inp}", "DSP_I", PinType.INPUT)
        for outp in range(54):
            add_port_wire(tt, dsp, portmap, f"DOUT{outp}", "DSP_O", PinType.OUTPUT)

    # MULTADDALU18X18
    for mac in range(2):
        belname = f'MULTADDALU18X18{mac}'
        portmap = db.grid[y][x].bels[belname].portmap
        dsp = tt.create_bel(belname, "MULTADDALU18X18", eval(f'MULTADDALU18X18_{mac}_Z'))

        for i in range(2):
            for sfx in {'ASIGN', 'BSIGN', 'ASEL', 'BSEL'}:
                add_port_wire(tt, dsp, portmap, f"{sfx}{i}", "DSP_I", PinType.INPUT)
            for inp in range(18):
                add_port_wire(tt, dsp, portmap, f"A{inp}{i}", "DSP_I", PinType.INPUT)
                add_port_wire(tt, dsp, portmap, f"B{inp}{i}", "DSP_I", PinType.INPUT)
        for inp in range(7):
            add_port_wire(tt, dsp, portmap, f"ALUSEL{inp}", "DSP_I", PinType.INPUT)
        for inp in range(54):
            add_port_wire(tt, dsp, portmap, f"C{inp}", "DSP_I", PinType.INPUT)
        for inp in range(4):
            add_port_wire(tt, dsp, portmap, f"CE{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"CLK{inp}", "DSP_I", PinType.INPUT)
            add_port_wire(tt, dsp, portmap, f"RESET{inp}", "DSP_I", PinType.INPUT)
        for outp in range(54):
            add_port_wire(tt, dsp, portmap, f"DOUT{outp}", "DSP_O", PinType.OUTPUT)

    tdesc.tiletype = tiletype
    return tt

# PLL main tile
_pll_inputs = {'CLKFB', 'FBDSEL0', 'FBDSEL1', 'FBDSEL2', 'FBDSEL3',
        'FBDSEL4', 'FBDSEL5', 'IDSEL0', 'IDSEL1', 'IDSEL2', 'IDSEL3',
        'IDSEL4', 'IDSEL5', 'ODSEL0', 'ODSEL1', 'ODSEL2', 'ODSEL3',
        'ODSEL4', 'ODSEL5', 'RESET', 'RESET_P', 'PSDA0', 'PSDA1',
        'PSDA2', 'PSDA3', 'DUTYDA0', 'DUTYDA1', 'DUTYDA2', 'DUTYDA3',
        'FDLY0', 'FDLY1', 'FDLY2', 'FDLY3', 'CLKIN', 'VREN'}
_pll_outputs = {'CLKOUT', 'LOCK', 'CLKOUTP', 'CLKOUTD', 'CLKOUTD3'}
def create_pll_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int, tdesc: TypeDesc):
    typename = "PLL"
    tiletype = f"{typename}_{ttyp}"
    if tdesc.sfx != 0:
        tiletype += f"_{tdesc.sfx}"

    # disabled PLLs
    if tdesc.extra_func and 'disabled' in tdesc.extra_func and 'PLL' in tdesc.extra_func['disabled']:
        tiletype += '_disabled'
        tt = chip.create_tile_type(tiletype)
        tt.extra_data = TileExtraData(chip.strs.id(typename))
        tdesc.tiletype = tiletype
        return tt
    tt = chip.create_tile_type(tiletype)
    tt.extra_data = TileExtraData(chip.strs.id(typename))


    # wires
    if chip.name == 'GW1NS-4':
        pll_name = 'PLLVR'
        bel_type = 'PLLVR'
    else:
        pll_name = 'RPLLA'
        bel_type = 'rPLL'
    portmap = db.grid[y][x].bels[pll_name].portmap
    pll = tt.create_bel("PLL", bel_type, z = PLL_Z)
    pll.flags = BEL_FLAG_GLOBAL
    for pin, wire in portmap.items():
        if pin in _pll_inputs:
            tt.create_wire(wire, "PLL_I")
            tt.add_bel_pin(pll, pin, wire, PinType.INPUT)
        else:
            assert pin in _pll_outputs, f"Unknown PLL pin {pin}"
            tt.create_wire(wire, "PLL_O")
            tt.add_bel_pin(pll, pin, wire, PinType.OUTPUT)
    tdesc.tiletype = tiletype
    return tt

# add Pll's bel to the pad
def add_pll(chip: Chip, db: chipdb, pad: PadInfo, ioloc: str):
    try:
        if ioloc in db.pad_pll:
            row, col, ttyp, bel_name = db.pad_pll[ioloc]
            pad.extra_data = PadExtraData(chip.strs.id(f'X{col}Y{row}'), chip.strs.id(bel_name), chip.strs.id(ttyp))
    except:
        return

# pinouts, packages...
_tbrlre = re.compile(r"IO([TBRL])(\d+)(\w)")
def create_packages(chip: Chip, db: chipdb):
    def ioloc_to_tile_bel(ioloc):
        side, num, bel_idx = _tbrlre.match(ioloc).groups()
        if side == 'T':
            row = 0
            col = int(num) - 1
        elif side == 'B':
            row = db.rows - 1
            col = int(num) - 1
        elif side == 'L':
            row = int(num) - 1
            col = 0
        elif side == 'R':
            row = int(num) - 1
            col = db.cols - 1
        return (f'X{col}Y{row}', f'IOB{bel_idx}')

    created_pkgs = set()
    for partno_spd, partdata in db.packages.items():
        pkgname, variant, spd = partdata
        partno = partno_spd.removesuffix(spd) # drop SPEED like 'C7/I6'
        if partno in created_pkgs:
            continue
        created_pkgs.add(partno)
        pkg = chip.create_package(partno)

        if variant in db.sip_cst and pkgname in db.sip_cst[variant]:
            pkg.extra_data = PackageExtraData(chip.strs, db.sip_cst[variant][pkgname])

        for pinno, pininfo in db.pinout[variant][pkgname].items():
            io_loc, cfgs = pininfo
            tile, bel = ioloc_to_tile_bel(io_loc)
            pad_func = ""
            for cfg in cfgs:
                pad_func += cfg + "/"
            pad_func = pad_func.rstrip('/')
            bank = int(db.pin_bank[io_loc])
            pad = pkg.create_pad(pinno, tile, bel, pad_func, bank)
            # add PLL if any is connected
            add_pll(chip, db, pad, io_loc)

# Extra chip data
def create_extra_data(chip: Chip, db: chipdb, chip_flags: int):
    chip.extra_data = ChipExtraData(chip.strs, chip_flags, None)
    chip.extra_data.create_bottom_io()
    for net_a, net_b in db.bottom_io[2]:
        chip.extra_data.add_bottom_io_cnd(net_a, net_b)
    for diff_type in db.diff_io_types:
        chip.extra_data.add_diff_io_type(diff_type)
    # create hclk wire->dhcen bel map
    for pip, bel in dhcen_bels.items():
        chip.extra_data.add_dhcen_bel(pip[0], pip[1], pip[2], bel[0], bel[1], bel[2], bel[3])
    # create spine->dqce bel map
    for spine, bel in dqce_bels.items():
        chip.extra_data.add_dqce_bel(spine, bel[0], bel[1], bel[2])
    # create spine->dcs bel map
    for spine, bel in dcs_bels.items():
        chip.extra_data.add_dcs_bel(spine, bel[0], bel[1], bel[2])
    # create iob->dlldly bel map
    for io, dlldly in io_dlldly_bels.items():
        chip.extra_data.add_io_dlldly_bel(io, dlldly)
    # create segments
    if hasattr(db, "segments"):
        for y_x_idx, seg in db.segments.items():
            _, x, idx = y_x_idx
            chip.extra_data.add_segment(x, idx, seg['min_x'], seg['min_y'], seg['max_x'], seg['max_y'],
                    seg['top_row'], seg['bottom_row'], seg['top_wire'], seg['bottom_wire'],
                    seg['top_gate_wire'], seg['bottom_gate_wire'])
            # add segment nodes
            lt_node = [NodeWire(x, seg['top_row'], seg['top_wire'])]
            lt_node.append(NodeWire(x, seg['bottom_row'], seg['bottom_wire']))
            for row in range(seg['min_y'], seg['max_y'] + 1):
                lt_node.append(NodeWire(x, row, f'LT0{1 + (idx // 4) * 3}'))
                node = [NodeWire(x, row, f'LBO{idx // 4}')]
                for col in range(seg['min_x'], seg['max_x'] + 1):
                    node.append(NodeWire(col, row, f'LB{idx}1'))
                chip.add_node(node)
            chip.add_node(lt_node)

def create_timing_info(chip: Chip, db: chipdb.Device):
    def group_to_timingvalue(group):
        # if himbaechel ever recognises unateness, this should match that order.
        ff = int(group[0] * 1000)
        fr = int(group[1] * 1000)
        rr = int(group[2] * 1000)
        rf = int(group[3] * 1000)
        return TimingValue(min(ff, fr, rf, rr), max(ff, fr, rf, rr))

    speed_grades = []
    for speed in db.timing.keys():
        speed_grades.append(speed)

    tmg = chip.set_speed_grades(speed_grades)

    for speed, groups in db.timing.items():
        for group, arc in groups.items():
            if group == "lut":
                lut = tmg.add_cell_variant(speed, "LUT4")
                lut.add_comb_arc("I0", "F", group_to_timingvalue(arc["a_f"]))
                lut.add_comb_arc("I1", "F", group_to_timingvalue(arc["b_f"]))
                lut.add_comb_arc("I2", "F", group_to_timingvalue(arc["c_f"]))
                lut.add_comb_arc("I3", "F", group_to_timingvalue(arc["d_f"]))
                mux5 = tmg.add_cell_variant(speed, "MUX2_LUT5")
                mux5.add_comb_arc("I0", "O", group_to_timingvalue(arc["m0_ofx0"]))
                mux5.add_comb_arc("I1", "O", group_to_timingvalue(arc["m1_ofx1"]))
                mux5.add_comb_arc("S0", "O", group_to_timingvalue(arc["fx_ofx1"]))
                mux6 = tmg.add_cell_variant(speed, "MUX2_LUT6")
                mux6.add_comb_arc("I0", "O", group_to_timingvalue(arc["m0_ofx0"]))
                mux6.add_comb_arc("I1", "O", group_to_timingvalue(arc["m1_ofx1"]))
                mux6.add_comb_arc("S0", "O", group_to_timingvalue(arc["fx_ofx1"]))
                mux7 = tmg.add_cell_variant(speed, "MUX2_LUT7")
                mux7.add_comb_arc("I0", "O", group_to_timingvalue(arc["m0_ofx0"]))
                mux7.add_comb_arc("I1", "O", group_to_timingvalue(arc["m1_ofx1"]))
                mux7.add_comb_arc("S0", "O", group_to_timingvalue(arc["fx_ofx1"]))
                mux8 = tmg.add_cell_variant(speed, "MUX2_LUT8")
                mux8.add_comb_arc("I0", "O", group_to_timingvalue(arc["m0_ofx0"]))
                mux8.add_comb_arc("I1", "O", group_to_timingvalue(arc["m1_ofx1"]))
                mux8.add_comb_arc("S0", "O", group_to_timingvalue(arc["fx_ofx1"]))
            elif group == "alu":
                alu = tmg.add_cell_variant(speed, "ALU")
                alu.add_comb_arc("I0", "SUM", group_to_timingvalue(arc["a_f"]))
                alu.add_comb_arc("I1", "SUM", group_to_timingvalue(arc["b_f"]))
                alu.add_comb_arc("I3", "SUM", group_to_timingvalue(arc["d_f"]))
                alu.add_comb_arc("CIN", "SUM", group_to_timingvalue(arc["fci_f0"]))
                alu.add_comb_arc("I0", "COUT", group_to_timingvalue(arc["a0_fco"]))
                alu.add_comb_arc("I1", "COUT", group_to_timingvalue(arc["b0_fco"]))
                alu.add_comb_arc("I3", "COUT", group_to_timingvalue(arc["d0_fco"]))
                alu.add_comb_arc("CIN", "COUT", group_to_timingvalue(arc["fci_fco"]))
            elif group == "sram":
                sram = tmg.add_cell_variant(speed, "RAM16SDP4")
                for do in range(4):
                    for rad in range(4):
                        sram.add_comb_arc(f"RAD[{rad}]", f"DO[{do}]", group_to_timingvalue(arc[f"rad{rad}_do"]))
                    sram.add_clock_out("CLK", f"DO[{do}]", ClockEdge.RISING, group_to_timingvalue(arc["clk_do"]))
                for di in range(4):
                    sram.add_setup_hold("CLK", f"DI[{di}", ClockEdge.RISING, group_to_timingvalue(arc["clk_di_set"]), group_to_timingvalue(arc["clk_di_hold"]))
                sram.add_setup_hold("CLK", "WRE", ClockEdge.RISING, group_to_timingvalue(arc["clk_wre_set"]), group_to_timingvalue(arc["clk_wre_hold"]))
                for wad in range(4):
                    sram.add_setup_hold("CLK", f"WAD[{wad}]", ClockEdge.RISING, group_to_timingvalue(arc[f"clk_wad{wad}_set"]), group_to_timingvalue(arc[f"clk_wad{wad}_hold"]))
            elif group == "dff":
                for reset_type in ('', 'P', 'C', 'S', 'R'):
                    for clock_enable in ('', 'E'):
                        cell_name = "DFF{}{}".format(reset_type, clock_enable)
                        dff = tmg.add_cell_variant(speed, cell_name)
                        dff.add_setup_hold("CLK", "D", ClockEdge.RISING, group_to_timingvalue(arc["di_clksetpos"]), group_to_timingvalue(arc["di_clkholdpos"]))
                        dff.add_setup_hold("CLK", "CE", ClockEdge.RISING, group_to_timingvalue(arc["ce_clksetpos"]), group_to_timingvalue(arc["ce_clkholdpos"]))
                        dff.add_clock_out("CLK", "Q", ClockEdge.RISING, group_to_timingvalue(arc["clk_qpos"]))

                        if reset_type in ('S', 'R'):
                            port = "RESET" if reset_type == 'R' else "SET"
                            dff.add_setup_hold("CLK", port, ClockEdge.RISING, group_to_timingvalue(arc["lsr_clksetpos_syn"]), group_to_timingvalue(arc["lsr_clkholdpos_syn"]))
                        elif reset_type in ('P', 'C'):
                            port = "CLEAR" if reset_type == 'C' else "PRESET"
                            dff.add_setup_hold("CLK", port, ClockEdge.RISING, group_to_timingvalue(arc["lsr_clksetpos_asyn"]), group_to_timingvalue(arc["lsr_clkholdpos_asyn"]))
                            dff.add_comb_arc(port, "Q", group_to_timingvalue(arc["lsr_q"]))

                        cell_name = "DFFN{}{}".format(reset_type, clock_enable)
                        dff = tmg.add_cell_variant(speed, cell_name)
                        dff.add_setup_hold("CLK", "D", ClockEdge.FALLING, group_to_timingvalue(arc["di_clksetneg"]), group_to_timingvalue(arc["di_clkholdneg"]))
                        dff.add_setup_hold("CLK", "CE", ClockEdge.FALLING, group_to_timingvalue(arc["ce_clksteneg"]), group_to_timingvalue(arc["ce_clkholdneg"])) # the DBs have a typo...
                        dff.add_clock_out("CLK", "Q", ClockEdge.FALLING, group_to_timingvalue(arc["clk_qneg"]))

                        if reset_type in ('S', 'R'):
                            port = "RESET" if reset_type == 'R' else "SET"
                            dff.add_setup_hold("CLK", port, ClockEdge.FALLING, group_to_timingvalue(arc["lsr_clksetneg_syn"]), group_to_timingvalue(arc["lsr_clkholdneg_syn"]))
                        elif reset_type in ('P', 'C'):
                            port = "CLEAR" if reset_type == 'C' else "PRESET"
                            dff.add_setup_hold("CLK", port, ClockEdge.FALLING, group_to_timingvalue(arc["lsr_clksetneg_asyn"]), group_to_timingvalue(arc["lsr_clkholdneg_asyn"]))
                            dff.add_comb_arc(port, "Q", group_to_timingvalue(arc["lsr_q"]))
            elif group == "bram":
                pass # TODO
            elif group == "fanout":
                pass # handled in "wire"
            elif group == "glbsrc":
                # no fanout delay for clock wires
                for name in ["CENT_SPINE_PCLK", "SPINE_TAP_PCLK", "TAP_BRANCH_PCLK"]:
                    tmg.set_pip_class(speed, name, group_to_timingvalue(arc[name]))
                tmg.set_pip_class(speed, 'GCLK_BRANCH', group_to_timingvalue(arc['BRANCH_PCLK']))
            elif group == "hclk":
                for name in ['HclkInMux', 'HclkHbrgMux', 'HclkOutMux', 'HclkDivMux']:
                    tmg.set_pip_class(speed, name, group_to_timingvalue(arc[name]))
            elif group == "iodelay":
                for name in ['GI_DO', 'SDTAP_DO', 'SETN_DO', 'VALUE_DO', 'SDTAP_DF', 'SETN_DF', 'VALUE_DF']:
                    tmg.set_pip_class(speed, name, group_to_timingvalue(arc[name]))
            elif group == "wire":
                # wires with delay and fanout delay
                for name in ["X0", "X2", "X8"]:
                    tmg.set_pip_class(speed, name, group_to_timingvalue(arc[name]), group_to_timingvalue(groups["fanout"][f"{name}Fan"]), TimingValue(round(1e6 / groups["fanout"][f"{name}FanNum"])))
                # wires with delay but no fanout delay
                for name in ["X0CTL", "X0CLK", "FX1"]:
                    tmg.set_pip_class(speed, name, group_to_timingvalue(arc[name]))
                # wires with presently-unknown delay
                for name in ["LUT_IN", "DI", "SEL", "CIN", "COUT", "VCC", "VSS", "LW_TAP", "LW_TAP_0", "LW_BRANCH", "LW_SPAN", "ISB"]:
                    tmg.set_pip_class(speed, name, TimingValue())
                # wires with fanout-only delay; used on cell output pips
                for name, mapping in [("LUT_OUT", "FFan"), ("FF_OUT", "QFan"), ("OF", "OFFan")]:
                    tmg.set_pip_class(speed, name, TimingValue(), group_to_timingvalue(groups["fanout"][mapping]), TimingValue(round(1e6 / groups["fanout"][f"{mapping}Num"])))

def main():
    parser = argparse.ArgumentParser(description='Make Gowin BBA')
    parser.add_argument('-d', '--device', required=True)
    parser.add_argument('-o', '--output', default="out.bba")

    args = parser.parse_args()

    device = args.device
    with gzip.open(importlib.resources.files("apycula").joinpath(f"{device}.pickle"), 'rb') as f:
        db = pickle.load(f)

    chip_flags = 0;
    # XXX compatibility
    if not hasattr(db, "chip_flags"):
        if device not in {"GW1NS-4", "GW1N-9"}:
            chip_flags |= CHIP_HAS_SP32;
    else:
        if "HAS_SP32" in db.chip_flags:
            chip_flags |= CHIP_HAS_SP32;
        if "NEED_SP_FIX" in db.chip_flags:
            chip_flags |= CHIP_NEED_SP_FIX;
        if "NEED_BSRAM_OUTREG_FIX" in db.chip_flags:
            chip_flags |= CHIP_NEED_BSRAM_OUTREG_FIX;
        if "NEED_BLKSEL_FIX" in db.chip_flags:
            chip_flags |= CHIP_NEED_BLKSEL_FIX;
        if "HAS_BANDGAP" in db.chip_flags:
            chip_flags |= CHIP_HAS_BANDGAP;
        if "HAS_PLL_HCLK" in db.chip_flags:
            chip_flags |= CHIP_HAS_PLL_HCLK;
        if "HAS_CLKDIV_HCLK" in db.chip_flags:
            chip_flags |= CHIP_HAS_CLKDIV_HCLK;
        if "HAS_PINCFG" in db.chip_flags:
            chip_flags |= CHIP_HAS_PINCFG;
        if "HAS_DFF67" in db.chip_flags:
            chip_flags |= CHIP_HAS_DFF67;

    X = db.cols;
    Y = db.rows;

    ch = Chip("gowin", device, X, Y)

    # Init constant ids
    ch.strs.read_constids(path.join(path.dirname(__file__), "constids.inc"))

    # packages from parntnumbers
    create_packages(ch, db)

    # The manufacturer distinguishes by externally identical tiles, so keep
    # these differences (in case it turns out later that there is a slightly
    # different routing or something like that).
    logic_tiletypes = db.tile_types['C']
    io_tiletypes = db.tile_types['I']
    ssram_tiletypes = db.tile_types['M']
    pll_tiletypes = db.tile_types['P']
    bsram_tiletypes = db.tile_types.get('B', set())
    dsp_tiletypes = db.tile_types.get('D', set())

    # Setup tile grid
    for x in range(X):
        for y in range(Y):
            ttyp = db.grid[y][x].ttyp
            if (x == 0 or x == X - 1) and (y == 0 or y == Y - 1):
                assert ttyp not in created_tiletypes, "Duplication of corner types"
                create_tiletype(create_corner_tiletype, ch, db, x, y, ttyp)
                continue
            elif ttyp in logic_tiletypes:
                create_tiletype(create_logic_tiletype, ch, db, x, y, ttyp)
            elif ttyp in ssram_tiletypes:
                create_tiletype(create_ssram_tiletype, ch, db, x, y, ttyp)
            elif ttyp in io_tiletypes:
                create_tiletype(create_io_tiletype, ch, db, x, y, ttyp)
            elif ttyp in pll_tiletypes:
                create_tiletype(create_pll_tiletype, ch, db, x, y, ttyp)
            elif ttyp in bsram_tiletypes:
                create_tiletype(create_bsram_tiletype, ch, db, x, y, ttyp)
            elif ttyp in dsp_tiletypes:
                create_tiletype(create_dsp_tiletype, ch, db, x, y, ttyp)
            else:
                create_tiletype(create_null_tiletype, ch, db, x, y, ttyp)

    # Create nodes between tiles
    create_nodes(ch, db)
    create_extra_data(ch, db, chip_flags)
    create_timing_info(ch, db)
    ch.write_bba(args.output)
if __name__ == '__main__':
    main()

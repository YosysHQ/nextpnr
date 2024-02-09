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
CHIP_HAS_SP32 = 0x1

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

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.tile_class.index)
        bba.u16(self.io16_x_off)
        bba.u16(self.io16_y_off)

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

@dataclass
class ChipExtraData(BBAStruct):
    strs: StringPool
    flags: int
    bottom_io: BottomIO
    diff_io_types: list[IdString] = field(default_factory = list)

    def create_bottom_io(self):
        self.bottom_io = BottomIO()

    def add_bottom_io_cnd(self, net_a: str, net_b: str):
        self.bottom_io.conditions.append(BottomIOCnd(self.strs.id(net_a), self.strs.id(net_b)))

    def add_diff_io_type(self, diff_type: str):
        self.diff_io_types.append(self.strs.id(diff_type))

    def serialise_lists(self, context: str, bba: BBAWriter):
        self.bottom_io.serialise_lists(f"{context}_bottom_io", bba)
        bba.label(f"{context}_diff_io_types")
        for i, diff_io_type in enumerate(self.diff_io_types):
            bba.u32(diff_io_type.index)

    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.flags)
        self.bottom_io.serialise(f"{context}_bottom_io", bba)
        bba.slice(f"{context}_diff_io_types", len(self.diff_io_types))

# Unique features of the tiletype
class TypeDesc:
    def __init__(self, dups, tiletype = '', extra_func = None, sfx = 0):
        self.tiletype = tiletype
        self.extra_func = extra_func
        self.dups = dups
        self.sfx = sfx
created_tiletypes = {}

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
        for y, x, wire in node:
            if wire_type:
                if not chip.tile_type_at(x, y).has_wire(wire):
                    chip.tile_type_at(x, y).create_wire(wire, wire_type)
                else:
                    chip.tile_type_at(x, y).set_wire_type(wire, wire_type)
            new_node = NodeWire(x, y, wire)
            gl_nodes = global_nodes.setdefault(node_name, [])
            if new_node not in gl_nodes:
                    gl_nodes.append(NodeWire(x, y, wire))

    for name, node in global_nodes.items():
        chip.add_node(node)

def create_switch_matrix(tt: TileType, db: chipdb, x: int, y: int):
    def get_wire_type(name):
        if name in {'XD0', 'XD1', 'XD2', 'XD3', 'XD4', 'XD5',}:
            return "X0"
        return ""

    for dst, srcs in db.grid[y][x].pips.items():
        if not tt.has_wire(dst):
            tt.create_wire(dst, get_wire_type(dst))
        for src in srcs.keys():
            if not tt.has_wire(src):
                tt.create_wire(src, get_wire_type(src))
            tt.create_pip(src, dst)

    # clock wires
    for dst, srcs in db.grid[y][x].pure_clock_pips.items():
        if not tt.has_wire(dst):
            tt.create_wire(dst, "GLOBAL_CLK")
        for src in srcs.keys():
            if not tt.has_wire(src):
                tt.create_wire(src, "GLOBAL_CLK")
            tt.create_pip(src, dst)

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
            tt.create_pip(src, dst)

def create_extra_funcs(tt: TileType, db: chipdb, x: int, y: int):
    if (y, x) not in db.extra_func:
        return
    for func, desc in db.extra_func[(y, x)].items():
        if func == 'osc':
            osc_type = desc['type']
            portmap = db.grid[y][x].bels[osc_type].portmap
            for port, wire in portmap.items():
                tt.create_wire(wire, port)
            bel = tt.create_bel(osc_type, osc_type, z = OSC_Z)
            for port, wire in portmap.items():
                if 'OUT' in port:
                    tt.add_bel_pin(bel, port, wire, PinType.OUTPUT)
                else:
                    tt.add_bel_pin(bel, port, wire, PinType.INPUT)
        elif func == 'gsr':
            wire = desc['wire']
            tt.create_wire(wire, "GSRI")
            bel = tt.create_bel("GSR", "GSR", z = GSR_Z)
            tt.add_bel_pin(bel, "GSRI", wire, PinType.INPUT)
        if func == 'io16':
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
        if func == 'buf':
            for buf_type, wires in desc.items():
                for i, wire in enumerate(wires):
                    if not tt.has_wire(wire):
                        tt.create_wire(wire, "TILE_CLK")
                    wire_out = f'{buf_type}{i}_O'
                    tt.create_wire(wire_out, "TILE_CLK")
                    # XXX make Z from buf_type
                    bel = tt.create_bel(f'{buf_type}{i}', buf_type, z = BUFG_Z + i)
                    bel.flags = BEL_FLAG_GLOBAL
                    tt.add_bel_pin(bel, "I", wire, PinType.INPUT)
                    tt.add_bel_pin(bel, "O", wire_out, PinType.OUTPUT)

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
        tt.create_wire('VSS', 'GND')
        gnd = tt.create_bel('GND', 'GND', z = GND_Z)
        tt.add_bel_pin(gnd, "G", "VSS", PinType.OUTPUT)
        # VCC is the logic high level generator
        tt.create_wire('VCC', 'VCC')
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
        if 'BOTTOM_IO_PORT_A' in portmap:
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
                    if port in {'CLK', 'PCLK'}:
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
            tt.create_wire(f"{inp_name}{i}", "LUT_INPUT")
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
        if i < 6:
            # FF data can come from LUT output, but we pretend that we can use
            # any LUT input
            tt.create_pip(f"F{i}", f"XD{i}")
            for inp_name in lut_inputs:
                tt.create_pip(f"{inp_name}{i}", f"XD{i}")
            # FF
            ff = tt.create_bel(f"DFF{i}", "DFF", z =(i * 2 + 1))
            tt.add_bel_pin(ff, "D", f"XD{i}", PinType.INPUT)
            tt.add_bel_pin(ff, "CLK", f"CLK{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "Q", f"Q{i}", PinType.OUTPUT)
            tt.add_bel_pin(ff, "SET", f"LSR{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "RESET", f"LSR{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "PRESET", f"LSR{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "CLEAR", f"LSR{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "CE", f"CE{i // 2}", PinType.INPUT)

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

    def add_port_wire(tt, bel, name, wire_type = "BSRAM_I", port_type = PinType.INPUT):
        wire = portmap[name]
        if not tt.has_wire(wire):
            if name.startswith('CLK'):
                tt.create_wire(wire, "TILE_CLK")
            else:
                tt.create_wire(wire, wire_type)
        tt.add_bel_pin(bel, name, wire, port_type)

    for sfx in {'', 'A', 'B'}:
        for inp in _bsram_inputs:
            add_port_wire(tt, bsram, f"{inp}{sfx}")
        for idx in range(3):
            add_port_wire(tt, bsram, f"BLKSEL{sfx}{idx}")
        for idx in range(14):
            add_port_wire(tt, bsram, f"AD{sfx}{idx}")
        for idx in range(18):
            add_port_wire(tt, bsram, f"DI{sfx}{idx}")
            add_port_wire(tt, bsram, f"DO{sfx}{idx}", "BSRAM_O", PinType.OUTPUT)
        if not sfx:
            for idx in range(18, 36):
                add_port_wire(tt, bsram, f"DI{idx}")
                add_port_wire(tt, bsram, f"DO{idx}", "BSRAM_O", PinType.OUTPUT)

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
        for pinno, pininfo in db.pinout[variant][pkgname].items():
            io_loc, cfgs = pininfo
            tile, bel = ioloc_to_tile_bel(io_loc)
            pad_func = ""
            for cfg in cfgs:
                pad_func += cfg + "/"
            pad_func = pad_func.rstrip('/')
            bank = int(db.pin_bank[io_loc])
            pad = pkg.create_pad(pinno, tile, bel, pad_func, bank)

# Extra chip data
def create_extra_data(chip: Chip, db: chipdb, chip_flags: int):
    chip.extra_data = ChipExtraData(chip.strs, chip_flags, None)
    chip.extra_data.create_bottom_io()
    for net_a, net_b in db.bottom_io[2]:
        chip.extra_data.add_bottom_io_cnd(net_a, net_b)
    for diff_type in db.diff_io_types:
        chip.extra_data.add_diff_io_type(diff_type)

def main():
    parser = argparse.ArgumentParser(description='Make Gowin BBA')
    parser.add_argument('-d', '--device', required=True)
    parser.add_argument('-o', '--output', default="out.bba")

    args = parser.parse_args()

    device = args.device
    with gzip.open(importlib.resources.files("apycula").joinpath(f"{device}.pickle"), 'rb') as f:
        db = pickle.load(f)

    chip_flags = 0;
    if device not in {"GW1NS-4", "GW1N-9"}:
        chip_flags &= CHIP_HAS_SP32;

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
            else:
                create_tiletype(create_null_tiletype, ch, db, x, y, ttyp)

    # Create nodes between tiles
    create_nodes(ch, db)
    create_extra_data(ch, db, chip_flags)
    ch.write_bba(args.output)
if __name__ == '__main__':
    main()

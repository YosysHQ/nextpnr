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

VCC_Z   = 277
GND_Z   = 278

@dataclass
class TileExtraData(BBAStruct):
    tile_class: IdString # The general functionality of the slightly different tiles,
                         # let's say the behavior of LUT+DFF in the tiles are completely identical,
                         # but one of them also contains clock-wire switches,
                         # then we assign them to the same LOGIC class.
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.tile_class.index)

created_tiletypes = set()

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
    for node_name, node in db.nodes.items():
        for y, x, wire in node:
            new_node = NodeWire(x, y, wire)
            gl_nodes = global_nodes.setdefault(node_name, [])
            if new_node not in gl_nodes:
                gl_nodes.append(NodeWire(x, y, wire))

    for name, node in global_nodes.items():
        chip.add_node(node)


# About X and Y as parameters - in some cases, the type of manufacturer's tile
# is not different, but some wires are not physically present, that is, routing
# depends on the location of otherwise identical tiles. There are many options
# for taking this into account, but for now we make a distinction here, by
# coordinates.
def create_switch_matrix(tt: TileType, db: chipdb, x: int, y: int):
    def get_wire_type(name):
        if name.startswith('GB') or name.startswith('GT'):
            return "GLOBAL_CLK"
        return ""

    for dst, srcs in db.grid[y][x].pips.items():
        if not tt.has_wire(dst):
            tt.create_wire(dst, get_wire_type(dst))
        for src in srcs.keys():
            if not tt.has_wire(src):
                tt.create_wire(src, get_wire_type(dst))
            tt.create_pip(src, dst)
    # clock wires
    for dst, srcs in db.grid[y][x].clock_pips.items():
        if not tt.has_wire(dst):
            tt.create_wire(dst, "GLOBAL_CLK")
        for src in srcs.keys():
            if not tt.has_wire(src):
                tt.create_wire(src, "GLOBAL_CLK")
            tt.create_pip(src, dst)

def create_null_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "NULL"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
    tt.extra_data = TileExtraData(chip.strs.id(typename))
    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

# responsible nodes, there will be IO banks, configuration, etc.
def create_corner_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "CORNER"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
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

    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

# simple IO - only A and B
def create_io_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "IO"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    for i in range(2):
        name = ['IOBA', 'IOBB'][i]
        # wires
        portmap = db.grid[y][x].bels[name].portmap
        tt.create_wire(portmap['I'], "IO_I")
        tt.create_wire(portmap['O'], "IO_O")
        # bels
        io = tt.create_bel(name, "IOB", z = i)
        tt.add_bel_pin(io, "I", portmap['I'], PinType.INPUT)
        tt.add_bel_pin(io, "O", portmap['O'], PinType.OUTPUT)
    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

# logic: luts, dffs, alu etc
def create_logic_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "LOGIC"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
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

    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

def create_ssram_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    # SSRAM is LUT based, so it's logic-like
    ttyp, tt = create_logic_tiletype(chip, db, x, y, ttyp)

    lut_inputs = ['A', 'B', 'C', 'D']
    ff = tt.create_bel(f"RAM16SDP4", "RAM16SDP4", z = RAMW_Z)
    for i in range(4):
        tt.add_bel_pin(ff, f"DI[{i}]", f"{lut_inputs[i]}5", PinType.INPUT)
        tt.add_bel_pin(ff, f"WAD[{i}]", f"{lut_inputs[i]}4", PinType.INPUT)
        # RAD[0] is RAD[0] is assumed to be connected to A3, A2, A1 and A0. But
        # for now we connect it only to A0, the others will be connected
        # directly during packing. RAD[1...3] - similarly.
        tt.add_bel_pin(ff, f"RAD[{i}]", f"{lut_inputs[i]}0", PinType.INPUT)
        tt.add_bel_pin(ff, f"DO[{i}]", f"F{i}", PinType.OUTPUT)


    tt.add_bel_pin(ff, f"CLK", "CLK2", PinType.INPUT)
    tt.add_bel_pin(ff, f"CE",  "CE2", PinType.INPUT)
    tt.add_bel_pin(ff, f"WRE", "LSR2", PinType.INPUT)
    return (ttyp, tt)

def main():
    parser = argparse.ArgumentParser(description='Make Gowin BBA')
    parser.add_argument('-d', '--device', required=True)
    parser.add_argument('-o', '--output', default="out.bba")

    args = parser.parse_args()

    device = args.device
    with gzip.open(importlib.resources.files("apycula").joinpath(f"{device}.pickle"), 'rb') as f:
        db = pickle.load(f)

    X = db.cols;
    Y = db.rows;

    ch = Chip("gowin", device, X, Y)

    # Init constant ids
    ch.strs.read_constids(path.join(path.dirname(__file__), "constids.inc"))

    # The manufacturer distinguishes by externally identical tiles, so keep
    # these differences (in case it turns out later that there is a slightly
    # different routing or something like that).
    logic_tiletypes = {12, 13, 14, 15, 16}
    io_tiletypes = {53, 55, 58, 59, 64, 65, 66}
    ssram_tiletypes = {17, 18, 19}
    # Setup tile grid
    for x in range(X):
        for y in range(Y):
            ttyp = db.grid[y][x].ttyp
            if (x == 0 or x == X - 1) and (y == 0 or y == Y - 1):
                assert ttyp not in created_tiletypes, "Duplication of corner types"
                ttyp, _ = create_corner_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"CORNER_{ttyp}")
                continue
            if ttyp in logic_tiletypes:
                ttyp, _ = create_logic_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"LOGIC_{ttyp}")
            elif ttyp in ssram_tiletypes:
                ttyp, _ = create_ssram_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"LOGIC_{ttyp}")
            elif ttyp in io_tiletypes:
                ttyp, _ = create_io_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"IO_{ttyp}")
            else:
                ttyp, _ = create_null_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"NULL_{ttyp}")

    # Create nodes between tiles
    create_nodes(ch, db)
    ch.write_bba(args.output)
if __name__ == '__main__':
    main()

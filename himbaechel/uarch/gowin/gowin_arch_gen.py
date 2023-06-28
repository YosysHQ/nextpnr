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

# XXX u-turn at the rim
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
    for y in range(Y):
        for x in range(X):
            nodes = []
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
            for node in nodes:
                print(node)
                chip.add_node(node)

# About X and Y as parameters - in some cases, the type of manufacturer's tile
# is not different, but some wires are not physically present, that is, routing
# depends on the location of otherwise identical tiles. There are many options
# for taking this into account, but for now we make a distinction here, by
# coordinates.
def create_switch_matrix(tt: TileType, db: chipdb, x: int, y: int):
    pips = db.grid[y][x].pips
    for dst, srcs in pips.items():
        if not tt.has_wire(dst):
            tt.create_wire(dst)
        for src in srcs.keys():
            if not tt.has_wire(src):
                tt.create_wire(src)
            tt.create_pip(dst, src)

def create_null_tiletype(chip: Chip, db: chipdb, x: int, y: int):
    tt = chip.create_tile_type(f"NULL_{db.grid[y][x].ttyp}")
    create_switch_matrix(tt, db, x, y)

# simple IO - only A and B
def create_io_tiletype(chip: Chip, db: chipdb, x: int, y: int):
    tt = chip.create_tile_type(f"IO_{db.grid[y][x].ttyp}")
    for i in range(2):
        name = ['IOBA', 'IOBB'][i]
        # wires
        portmap = db.grid[y][x].bels[name].portmap
        tt.create_wire(portmap['I'], "IO_I")
        tt.create_wire(portmap['O'], "IO_I")
        # bels
        io = tt.create_bel(name, "IOB", z=i)
        tt.add_bel_pin(io, "I", portmap['I'], PinType.INPUT)
        tt.add_bel_pin(io, "O", portmap['O'], PinType.OUTPUT)
    create_switch_matrix(tt, db, x, y)

# XXX 6 lut+dff only for now
def create_logic_tiletype(chip: Chip, db: chipdb, x: int, y: int):
    N = 6
    lut_inputs = {'A', 'B', 'C', 'D'}
    tt = chip.create_tile_type(f"LOGIC_{db.grid[y][x].ttyp}")
    # setup wires
    for i in range(N):
        for inp_name in lut_inputs:
            tt.create_wire(f"{inp_name}{i}", "LUT_INPUT")
        tt.create_wire(f"F{i}", "LUT_OUT")
        # experimental. the wire is false - it is assumed that DFF is always
        # connected to the LUT's output F{i}, but we can place primitives
        # arbitrarily and create a pass-through LUT afterwards.
        # just out of curiosity
        tt.create_wire(f"XD{i}", "FF_INPUT")
        tt.create_wire(f"Q{i}", "FF_OUT")
    for j in range(3):
        tt.create_wire(f"CLK{j}", "TILE_CLK")

    # create logic cells
    for i in range(N):
        # LUT
        lut = tt.create_bel(f"LUT{i}", "LUT4", z=(i*2 + 0))
        for j, inp_name in enumerate(lut_inputs):
            tt.add_bel_pin(lut, f"I[{j}]", f"{inp_name}{i}", PinType.INPUT)
        tt.add_bel_pin(lut, "F", f"F{i}", PinType.OUTPUT)
        # FF data can come from LUT output, but we pretend that we can use
        # any LUT input
        tt.create_pip(f"F{i}", f"XD{i}")
        for inp_name in lut_inputs:
            tt.create_pip(f"{inp_name}{i}", f"XD{i}")
        # FF
        ff = tt.create_bel(f"DFF{i}", "DFF", z=(i*2 + 1))
        tt.add_bel_pin(ff, "D", f"XD{i}", PinType.INPUT)
        tt.add_bel_pin(ff, "CLK", f"CLK{i // 2}", PinType.INPUT)
        tt.add_bel_pin(ff, "Q", f"Q{i}", PinType.OUTPUT)
    create_switch_matrix(tt, db, x, y)

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
    created_tiletypes = set()
    logic_tiletypes = {12, 13, 14, 15, 16, 17}
    io_tiletypes = {53, 58, 64} # Tangnano9k leds tiles and clock ;)
    # Setup tile grid
    for x in range(X):
        for y in range(Y):
            ttyp = db.grid[y][x].ttyp
            if ttyp in logic_tiletypes:
                if ttyp not in created_tiletypes:
                    create_logic_tiletype(ch, db, x, y)
                    created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"LOGIC_{ttyp}")
            elif ttyp in io_tiletypes:
                if ttyp not in created_tiletypes:
                    create_io_tiletype(ch, db, x, y)
                    created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"IO_{ttyp}")
            else:
                if ttyp not in created_tiletypes:
                    create_null_tiletype(ch, db, x, y)
                    created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"NULL_{ttyp}")

    # Create nodes between tiles
    create_nodes(ch, db)
    ch.write_bba(args.output)
if __name__ == '__main__':
    main()

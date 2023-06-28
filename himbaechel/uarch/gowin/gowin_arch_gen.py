from os import path
import sys

import importlib.resources
import pickle
import gzip
import argparse

sys.path.append(path.join(path.dirname(__file__), "../.."))
from himbaechel_dbgen.chip import *
from apycula import chipdb

def create_nodes(chip: Chip, db: chipdb):
    return

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
    tt = chip.create_tile_type(f"NULL{db.grid[y][x].ttyp}")

# XXX 6 lut+dff only for now
def create_logic_tiletype(chip: Chip, db: chipdb, x: int, y: int):
    N = 6
    lut_inputs = {'A', 'B', 'C', 'D'}
    tt = chip.create_tile_type(f"LOGIC{db.grid[y][x].ttyp}")
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
    # Setup tile grid
    for x in range(X):
        for y in range(Y):
            ttyp = db.grid[y][x].ttyp
            if ttyp in logic_tiletypes:
                if ttyp not in created_tiletypes:
                    create_logic_tiletype(ch, db, x, y)
                    created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"LOGIC{ttyp}")
            else:
                if ttyp not in created_tiletypes:
                    create_null_tiletype(ch, db, x, y)
                    created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"NULL{ttyp}")

    # Create nodes between tiles
    create_nodes(ch, db)
    ch.write_bba(args.output)
if __name__ == '__main__':
    main()

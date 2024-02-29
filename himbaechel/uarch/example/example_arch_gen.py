from os import path
import sys
sys.path.append(path.join(path.dirname(__file__), "../.."))
from himbaechel_dbgen.chip import *

# Grid size including IOBs at edges
X = 100
Y = 100
# LUT input count
K = 4
# SLICEs per tile
N = 8
# number of local wires
Wl = N * (K + 1) + 16
# 1/Fc for bel input wire pips; local wire pips and neighbour pips
Si = 6
Sq = 6
Sl = 1

dirs = [ # name, dx, dy
    ("N", 0, -1),
    ("NE", 1, -1),
    ("E", 1, 0),
    ("SE", 1, 1),
    ("S", 0, 1),
    ("SW", -1, 1),
    ("W", -1, 0),
    ("NW", -1, -1)
]

def create_switch_matrix(tt: TileType, inputs: list[str], outputs: list[str]):
    # FIXME: terrible routing matrix, just for a toy example...
    # constant wires
    tt.create_wire("GND", "GND", const_value="GND")
    tt.create_wire("VCC", "VCC", const_value="VCC")
    # switch wires
    for i in range(Wl):
        tt.create_wire(f"SWITCH{i}", "SWITCH")
    # neighbor wires
    for i in range(Wl):
        for d, dx, dy in dirs:
            tt.create_wire(f"{d}{i}", f"NEIGH_{d}")
    # input pips
    for i, w in enumerate(inputs):
        for j in range((i % Si), Wl, Si):
            tt.create_pip(f"SWITCH{j}", w, timing_class="SWINPUT")
    # output pips
    for i, w in enumerate(outputs):
        for j in range((i % Sq), Wl, Sq):
            tt.create_pip(w, f"SWITCH{j}", timing_class="SWINPUT")
    # constant pips
    for i in range(Wl):
        tt.create_pip("GND", f"SWITCH{i}")
        tt.create_pip("VCC", f"SWITCH{i}")
    # neighbour local pips
    for i in range(Wl):
        for j, (d, dx, dy) in enumerate(dirs):
            tt.create_pip(f"{d}{(i + j) % Wl}", f"SWITCH{i}", timing_class="SWNEIGH")
    # clock "ladder"
    if not tt.has_wire("CLK"):
        tt.create_wire(f"CLK", "TILE_CLK")
    tt.create_wire(f"CLK_PREV", "CLK_ROUTE")
    tt.create_pip(f"CLK_PREV", f"CLK")

def create_logic_tiletype(chip: Chip):
    tt = chip.create_tile_type("LOGIC")
    # setup wires
    inputs = []
    outputs = []
    for i in range(N):
        for j in range(K):
            inputs.append(f"L{i}_I{j}")
            tt.create_wire(f"L{i}_I{j}", "LUT_INPUT")
        tt.create_wire(f"L{i}_D", "FF_DATA")
        tt.create_wire(f"L{i}_O", "LUT_OUT")
        tt.create_wire(f"L{i}_Q", "FF_OUT")
        outputs += [f"L{i}_O", f"L{i}_Q"]
    tt.create_wire(f"CLK", "TILE_CLK")
    # create logic cells
    for i in range(N):
        # LUT
        lut = tt.create_bel(f"L{i}_LUT", "LUT4", z=(i*2 + 0))
        for j in range(K):
            tt.add_bel_pin(lut, f"I[{j}]", f"L{i}_I{j}", PinType.INPUT)
        tt.add_bel_pin(lut, "F", f"L{i}_O", PinType.OUTPUT)
        # FF data can come from LUT output or LUT I3
        tt.create_pip(f"L{i}_O", f"L{i}_D")
        tt.create_pip(f"L{i}_I{K-1}", f"L{i}_D")
        # FF
        ff = tt.create_bel(f"L{i}_FF", "DFF", z=(i*2 + 1))
        tt.add_bel_pin(ff, "D", f"L{i}_D", PinType.INPUT)
        tt.add_bel_pin(ff, "CLK", "CLK", PinType.INPUT)
        tt.add_bel_pin(ff, "Q", f"L{i}_Q", PinType.OUTPUT)
    create_switch_matrix(tt, inputs, outputs)
    return tt

N_io = 2

def create_io_tiletype(chip: Chip):
    tt = chip.create_tile_type("IO")
     # setup wires
    inputs = []
    outputs = []
    for i in range(N_io):
        tt.create_wire(f"IO{i}_T", "IO_T")
        tt.create_wire(f"IO{i}_I", "IO_I")
        tt.create_wire(f"IO{i}_O", "IO_O")
        tt.create_wire(f"IO{i}_PAD", "IO_PAD")
        inputs += [f"IO{i}_T", f"IO{i}_I"]
        outputs += [f"IO{i}_O", ]
    tt.create_wire(f"CLK", "TILE_CLK")
    for i in range(N_io):
        io = tt.create_bel(f"IO{i}", "IOB", z=i)
        tt.add_bel_pin(io, "I", f"IO{i}_I", PinType.INPUT)
        tt.add_bel_pin(io, "T", f"IO{i}_T", PinType.INPUT)
        tt.add_bel_pin(io, "O", f"IO{i}_O", PinType.OUTPUT)
        tt.add_bel_pin(io, "PAD", f"IO{i}_PAD", PinType.INOUT)
    # Actually used in top left IO only
    tt.create_wire("GCLK_OUT", "GCLK")
    tt.create_pip("IO0_O", "GCLK_OUT")
    create_switch_matrix(tt, inputs, outputs)
    return tt

def create_bram_tiletype(chip: Chip):
    Aw = 9
    Dw = 16

    tt = chip.create_tile_type("BRAM")
    inputs = [f"RAM_WA{i}" for i in range(Aw)]
    inputs += [f"RAM_RA{i}" for i in range(Aw)]
    inputs += [f"RAM_WE{i}" for i in range(Dw // 8)]
    inputs += [f"RAM_DI{i}" for i in range(Dw)]
    outputs = [f"RAM_DO{i}" for i in range(Dw)]
    for w in inputs:
        tt.create_wire(w, "RAM_IN")
    for w in outputs:
        tt.create_wire(w, "RAM_OUT")
    tt.create_wire(f"CLK", "TILE_CLK")
    ram = tt.create_bel(f"RAM", F"BRAM_{2**Aw}X{Dw}", z=0)
    tt.add_bel_pin(ram, "CLK", f"CLK", PinType.INPUT)
    for i in range(Aw):
        tt.add_bel_pin(ram, f"WA[{i}]", f"RAM_WA{i}", PinType.INPUT)
        tt.add_bel_pin(ram, f"RA[{i}]", f"RAM_RA{i}", PinType.INPUT)
    for i in range(Dw//8):
        tt.add_bel_pin(ram, f"WE[{i}]", f"RAM_WE{i}", PinType.INPUT)
    for i in range(Dw):
        tt.add_bel_pin(ram, f"DI[{i}]", f"RAM_DI{i}", PinType.INPUT)
        tt.add_bel_pin(ram, f"DO[{i}]", f"RAM_DO{i}", PinType.OUTPUT)
    create_switch_matrix(tt, inputs, outputs)
    return tt

def create_corner_tiletype(ch):
    tt = ch.create_tile_type("NULL")
    tt.create_wire(f"CLK", "TILE_CLK")
    tt.create_wire(f"CLK_PREV", "CLK_ROUTE")
    tt.create_pip(f"CLK_PREV", f"CLK")

    tt.create_wire(f"GND", "GND", const_value="GND")
    tt.create_wire(f"VCC", "VCC", const_value="VCC")

    gnd = tt.create_bel(f"GND_DRV", f"GND_DRV", z=0)
    tt.add_bel_pin(gnd, "GND", "GND", PinType.OUTPUT)
    vcc = tt.create_bel(f"VCC_DRV", f"VCC_DRV", z=1)
    tt.add_bel_pin(vcc, "VCC", "VCC", PinType.OUTPUT)

    return tt

def is_corner(x, y):
    return ((x == 0) or (x == (X-1))) and ((y == 0) or (y == (Y-1)))

def create_nodes(ch):
    for y in range(Y):
        #print(f"generating nodes for row {y}")
        for x in range(X):
            if not is_corner(x, y):
                # connect up actual neighbours
                local_nodes = [[NodeWire(x, y, f"SWITCH{i}")] for i in range(Wl)]
                for d, dx, dy in dirs:
                    x1 = x - dx
                    y1 = y - dy
                    if x1 < 0 or x1 >= X or y1 < 0 or y1 >= Y or is_corner(x1, y1):
                        continue
                    for i in range(Wl):
                        local_nodes[i].append(NodeWire(x1, y1, f"{d}{i}"))
                for n in local_nodes:
                    ch.add_node(n)
            # connect up clock ladder (not intended to be a sensible clock structure)
            if y != 1: # special case where the node has 3 wires
                if y == 0:
                    if x == 0:
                        # clock source: IO
                        clk_node = [NodeWire(1, 0, "GCLK_OUT")]
                    else:
                        # clock source: left
                        clk_node = [NodeWire(x-1, y, "CLK")]
                else:
                    # clock source: above
                    clk_node = [NodeWire(x, y-1, "CLK")]
                clk_node.append(NodeWire(x, y, "CLK_PREV"))
                if y == 0:
                    clk_node.append(NodeWire(x, y+1, "CLK_PREV"))
                ch.add_node(clk_node)

def set_timings(ch):
    speed = "DEFAULT"
    tmg = ch.set_speed_grades([speed])
    # --- Routing Delays ---
    # Notes: A simpler timing model could just use intrinsic delay and ignore R and Cs.
    # R and C values don't have to be physically realistic, just in agreement with themselves to provide
    # a meaningful scaling of delay with fanout. Units are subject to change.
    tmg.set_pip_class(grade=speed, name="SWINPUT",
        delay=TimingValue(80), # 80ps intrinstic delay
        in_cap=TimingValue(5000), # 5pF
        out_res=TimingValue(1000), # 1ohm
    )
    tmg.set_pip_class(grade=speed, name="SWOUTPUT",
        delay=TimingValue(100), # 100ps intrinstic delay
        in_cap=TimingValue(5000), # 5pF
        out_res=TimingValue(800), # 0.8ohm
    )
    tmg.set_pip_class(grade=speed, name="SWNEIGH",
        delay=TimingValue(120), # 120ps intrinstic delay
        in_cap=TimingValue(7000), # 7pF
        out_res=TimingValue(1200), # 1.2ohm
    )
    # TODO: also support node/wire delays and add an example of them

    # --- Cell delays ---
    lut = ch.timing.add_cell_variant(speed, "LUT4")
    for j in range(K):
        lut.add_comb_arc(f"I[{j}]", "F", TimingValue(150 + j * 15))
    dff = ch.timing.add_cell_variant(speed, "DFF")
    dff.add_setup_hold("CLK", "D", ClockEdge.RISING, TimingValue(150), TimingValue(25))
    dff.add_clock_out("CLK", "Q", ClockEdge.RISING, TimingValue(200))

def main():
    ch = Chip("example", "EX1", X, Y)
    # Init constant ids
    ch.strs.read_constids(path.join(path.dirname(__file__), "constids.inc"))
    logic = create_logic_tiletype(ch)
    io = create_io_tiletype(ch)
    bram = create_bram_tiletype(ch)
    null = create_corner_tiletype(ch)
    # Setup tile grid
    for x in range(X):
        for y in range(Y):
            if x == 0 or x == X-1: # left/right side IO
                if y == 0 or y == Y-1: # corner
                    ch.set_tile_type(x, y, "NULL")
                else:
                    ch.set_tile_type(x, y, "IO")
            elif y == 0 or y == Y-1: # top/bottom side IO
                ch.set_tile_type(x, y, "IO")
            elif (y % 15) == 7: # BRAM
                ch.set_tile_type(x, y, "BRAM")
            else:
                ch.set_tile_type(x, y, "LOGIC")
    # Create nodes between tiles
    create_nodes(ch)
    set_timings(ch)
    ch.write_bba(sys.argv[1])

if __name__ == '__main__':
    main()

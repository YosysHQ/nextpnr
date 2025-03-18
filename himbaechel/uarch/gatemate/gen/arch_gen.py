#
#  nextpnr -- Next Generation Place and Route
#
#  Copyright (C) 2024  The Project Peppercorn Authors.
#
#  Permission to use, copy, modify, and/or distribute this software for any
#  purpose with or without fee is hereby granted, provided that the above
#  copyright notice and this permission notice appear in all copies.
#
#  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
#  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
#  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
#  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
#  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
#  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
#  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

import os
from os import path
import sys
import argparse

sys.path.append(path.join(path.dirname(__file__), "../../.."))
from himbaechel_dbgen.chip import *

PIP_EXTRA_MUX = 1
PIP_EXTRA_CPE = 2

MUX_INVERT = 1
MUX_VISIBLE = 2
MUX_CONFIG = 4
MUX_CPE_INV = 8

BEL_EXTRA_GPIO_L = 1
BEL_EXTRA_GPIO_R = 2
BEL_EXTRA_GPIO_T = 4
BEL_EXTRA_GPIO_B = 8

parser = argparse.ArgumentParser()
parser.add_argument("--lib", help="Project Peppercorn python database script path", type=str, required=True)
parser.add_argument("--device", help="name of device to export", type=str, required=True)
parser.add_argument("--bba", help="bba file to write", type=str, required=True)
args = parser.parse_args()

sys.path.append(os.path.expanduser(args.lib))
sys.path += args.lib 
import chip
import die

@dataclass
class TileExtraData(BBAStruct):
    die : int = 0
    bit_x: int = 0
    bit_y: int = 0
    prim_id : int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u8(self.die)
        bba.u8(self.bit_x)
        bba.u8(self.bit_y)
        bba.u8(self.prim_id)

@dataclass
class PipExtraData(BBAStruct):
    pip_type: int
    name: IdString
    bits: int = 0
    value: int = 0
    invert: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        bba.u8(self.bits)
        bba.u8(self.value)
        bba.u8(self.invert)
        bba.u8(self.pip_type)

@dataclass
class BelExtraData(BBAStruct):
    flags: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.flags)

def set_timings(ch):
    speed = "DEFAULT"
    tmg = ch.set_speed_grades([speed])

    lut = ch.timing.add_cell_variant(speed, "CPE_HALF_L")
    #lut.add_comb_arc("IN1", "OUT1", TimingValue(455, 459))
    #lut.add_comb_arc("IN2", "OUT1", TimingValue(450, 457))
    #lut.add_comb_arc("IN3", "OUT1", TimingValue(427, 430))
    #lut.add_comb_arc("IN4", "OUT1", TimingValue(423, 424))
    lut.add_comb_arc("IN1", "OUT", TimingValue(416, 418)) # IN5 to OUT1
    lut.add_comb_arc("IN2", "OUT", TimingValue(413, 422)) # IN6 to OUT1
    lut.add_comb_arc("IN3", "OUT", TimingValue(372, 374)) # IN7 to OUT1
    lut.add_comb_arc("IN4", "OUT", TimingValue(275, 385)) # IN8 to OUT1

    lut = ch.timing.add_cell_variant(speed, "CPE_HALF_U")
    lut.add_comb_arc("IN1", "OUT", TimingValue(479, 484)) # to OUT2
    lut.add_comb_arc("IN2", "OUT", TimingValue(471, 488)) # to OUT2
    lut.add_comb_arc("IN3", "OUT", TimingValue(446, 449)) # to OUT2
    lut.add_comb_arc("IN4", "OUT", TimingValue(443, 453)) # to OUT2

    lut = ch.timing.add_cell_variant(speed, "CPE_HALF")
    lut.add_comb_arc("IN1", "OUT", TimingValue(479, 484)) # to OUT2
    lut.add_comb_arc("IN2", "OUT", TimingValue(471, 488)) # to OUT2
    lut.add_comb_arc("IN3", "OUT", TimingValue(446, 449)) # to OUT2
    lut.add_comb_arc("IN4", "OUT", TimingValue(443, 453)) # to OUT2

    dff = ch.timing.add_cell_variant(speed, "CPE_DFF")
    dff.add_setup_hold("CLK", "IN1", ClockEdge.RISING, TimingValue(60), TimingValue(50))
    dff.add_setup_hold("CLK", "IN2", ClockEdge.RISING, TimingValue(60), TimingValue(50))
    dff.add_setup_hold("CLK", "IN3", ClockEdge.RISING, TimingValue(60), TimingValue(50))
    dff.add_setup_hold("CLK", "IN4", ClockEdge.RISING, TimingValue(60), TimingValue(50))
    dff.add_clock_out("CLK", "OUT", ClockEdge.RISING, TimingValue(60))
    #dff.add_clock_out("CLK", "OUT2", ClockEdge.RISING, TimingValue(60))

def main():
    # Range needs to be +1, but we are adding +2 more to coordinates, since 
    # they are starting from -2 instead of zero required for nextpnr
    dev = chip.get_device(args.device)
    ch = Chip("gatemate", args.device, dev.max_col() + 3, dev.max_row() + 3)
    # Init constant ids
    ch.strs.read_constids(path.join(path.dirname(__file__), "..", "constids.inc"))
    ch.read_gfxids(path.join(path.dirname(__file__), "..", "gfxids.inc"))

    for type_name in sorted(die.get_tile_type_list()):
        tt = ch.create_tile_type(type_name)
        for group in sorted(die.get_groups_for_type(type_name)):
            tt.create_group(group.name, group.type)
        for wire in sorted(die.get_endpoints_for_type(type_name)):
            tt.create_wire(wire.name, wire.type)
        if "CPE" in type_name:
            for i in range(1,9):
                tt.create_wire(f"CPE.V_IN{i}_int", "CPE_VIRTUAL_WIRE")
        if "GPIO" in type_name:
            tt.create_wire("GPIO.DI", "CPE_VIRTUAL_WIRE")
        for prim in sorted(die.get_primitives_for_type(type_name)):
            bel = tt.create_bel(prim.name, prim.type, prim.z)
            if prim.name == "GPIO":
                flag = 0
                if "LES" in type_name:
                    flag = BEL_EXTRA_GPIO_L
                if "RES" in type_name:
                    flag = BEL_EXTRA_GPIO_R
                if "TES" in type_name:
                    flag = BEL_EXTRA_GPIO_T
                if "BES" in type_name:
                    flag = BEL_EXTRA_GPIO_B
                bel.extra_data = BelExtraData(flag)
                tt.add_bel_pin(bel, "DI", "GPIO.DI", PinType.INPUT)
            for pin in sorted(die.get_primitive_pins(prim.type)):
                tt.add_bel_pin(bel, pin.name, die.get_pin_connection_name(prim,pin), pin.dir)
        for mux in sorted(die.get_mux_connections_for_type(type_name)):
            if mux.dst.startswith("CPE.IN") and mux.dst.endswith("_int"):
                mux.dst = mux.dst.replace("CPE.IN", "CPE.V_IN")
            pp = tt.create_pip(mux.src, mux.dst)
            if mux.name:
                mux_flags = MUX_INVERT if mux.invert else 0
                mux_flags |= MUX_VISIBLE if mux.visible else 0
                mux_flags |= MUX_CONFIG if mux.config else 0
                pp.extra_data = PipExtraData(PIP_EXTRA_MUX, ch.strs.id(mux.name), mux.bits, mux.value, mux_flags)
        if "CPE" in type_name:
            for i in range(1,9):
                pp = tt.create_pip(f"CPE.V_IN{i}_int", f"CPE.IN{i}_int")
                pp = tt.create_pip(f"CPE.V_IN{i}_int", f"CPE.IN{i}_int")
                pp.extra_data = PipExtraData(PIP_EXTRA_MUX, ch.strs.id(f"CPE.IN{i}_INV"), 1, i, MUX_CPE_INV | MUX_INVERT)
            tt.create_wire("CPE.V_CLK", "CPE_VIRTUAL_WIRE")
            pp = tt.create_pip("CPE.V_CLK", "CPE.CLK")
            pp = tt.create_pip("CPE.V_CLK", "CPE.CLK")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX, ch.strs.id("CPE.CLK_INV"), 1, 1, MUX_CPE_INV| MUX_INVERT)
            tt.create_wire("CPE.V_EN", "CPE_VIRTUAL_WIRE")
            pp = tt.create_pip("CPE.V_EN", "CPE.EN")
            pp = tt.create_pip("CPE.V_EN", "CPE.EN")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX, ch.strs.id("CPE.EN_INV"), 1, 1, MUX_CPE_INV| MUX_INVERT)
            tt.create_wire("CPE.V_SR", "CPE_VIRTUAL_WIRE")
            pp = tt.create_pip("CPE.V_SR", "CPE.SR")
            pp = tt.create_pip("CPE.V_SR", "CPE.SR")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX, ch.strs.id("CPE.SR_INV"), 1, 1, MUX_CPE_INV| MUX_INVERT)
        if "GPIO" in type_name:
            tt.create_pip("GPIO.DI", "GPIO.IN1")
            tt.create_pip("GPIO.DI", "GPIO.IN2")

    # Setup tile grid
    for x in range(dev.max_col() + 3):
        for y in range(dev.max_row() + 3):
            ti = ch.set_tile_type(x, y, dev.get_tile_type(x - 2,y - 2))
            tileinfo = dev.get_tile_info(x - 2,y - 2)
            ti.extra_data = TileExtraData(tileinfo.die, tileinfo.bit_x, tileinfo.bit_y, tileinfo.prim_index)

    # Create nodes between tiles
    for _,nodes in dev.get_connections():
        node = []
        for conn in sorted(nodes):
            conn.name = conn.name.replace("CPE.CLK", "CPE.V_CLK")
            conn.name = conn.name.replace("CPE.EN", "CPE.V_EN")
            conn.name = conn.name.replace("CPE.SR", "CPE.V_SR")
            node.append(NodeWire(conn.x + 2, conn.y + 2, conn.name))
        ch.add_node(node)
    set_timings(ch)

    for package in dev.get_packages():
        pkg = ch.create_package(package)
        for pad in sorted(dev.get_package_pads(package)):
            pkg.create_pad(pad.name, f"X{pad.x+2}Y{pad.y+2}", pad.bel, pad.function, pad.bank, pad.flags)

    ch.write_bba(args.bba)

if __name__ == '__main__':
    main()

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

def set_timings(ch):
    speed = "DEFAULT"
    tmg = ch.set_speed_grades([speed])

def main():
    # Range needs to be +1, but we are adding +2 more to coordinates, since 
    # they are starting from -2 instead of zero required for nextpnr
    dev = chip.get_device(args.device)
    ch = Chip("gatemate", args.device, dev.max_col() + 3, dev.max_row() + 3)
    # Init constant ids
    ch.strs.read_constids(path.join(path.dirname(__file__), "..", "constids.inc"))
    ch.read_gfxids(path.join(path.dirname(__file__), "..", "gfxids.inc"))

    for type_name in die.get_tile_type_list():
        tt = ch.create_tile_type(type_name)
        for group in die.get_groups_for_type(type_name):
            tt.create_group(group.name, group.type)
        for wire in die.get_endpoints_for_type(type_name):
            tt.create_wire(wire.name, wire.type)
        for prim in die.get_primitives_for_type(type_name):
            bel = tt.create_bel(prim.name, prim.type, prim.z)
            for pin in die.get_primitive_pins(prim.type):
                tt.add_bel_pin(bel, pin.name, die.get_pin_connection_name(prim,pin), pin.dir)
        for mux in die.get_mux_connections_for_type(type_name):
            pp = tt.create_pip(mux.src, mux.dst)
            mux_flags = MUX_INVERT if mux.invert else 0
            mux_flags |= MUX_VISIBLE if mux.visible else 0
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX, ch.strs.id(mux.name), mux.bits, mux.value, mux_flags)
        if "CPE" in type_name:
            pp = tt.create_pip("CPE.IN1", "CPE.RAM_O2")
            pp.extra_data = PipExtraData(PIP_EXTRA_CPE,ch.strs.id("RAM_O2"))
        if "GPIO" in type_name:
            tt.create_wire("GPIO.OUT_D1", "WIRE_INTERNAL")
            tt.create_wire("GPIO.OUT_D2", "WIRE_INTERNAL")
            #tt.create_wire("GPIO.OUT_Q1", "WIRE_INTERNAL")
            #tt.create_wire("GPIO.OUT_Q2", "WIRE_INTERNAL")
            #tt.create_wire("GPIO.OUT_CLK","WIRE_INTERNAL")
            #tt.create_wire("GPIO.CLK_INT","WIRE_INTERNAL")

            pp = tt.create_pip("GPIO.OUT1", "GPIO.OUT_D1")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT1_4"), 1, 0, MUX_VISIBLE)
            pp = tt.create_pip("GPIO.OUT4", "GPIO.OUT_D1")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT1_4"), 1, 1, MUX_VISIBLE)

            pp = tt.create_pip("GPIO.OUT2", "GPIO.OUT_D2")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT2_3"), 1, 0, MUX_VISIBLE)
            pp = tt.create_pip("GPIO.OUT3", "GPIO.OUT_D2")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT2_3"), 1, 1, MUX_VISIBLE)

            pp = tt.create_pip("GPIO.OUT_D1","GPIO.DO")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT23_14_SEL"), 1, 0, MUX_VISIBLE)
            pp = tt.create_pip("GPIO.OUT_D2","GPIO.DO")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT23_14_SEL"), 1, 1, MUX_VISIBLE)


            pp = tt.create_pip("GPIO.OUT2","GPIO.OE")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OE_SIGNAL"), 2, 1, MUX_VISIBLE)
            pp = tt.create_pip("GPIO.OUT3","GPIO.OE")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OE_SIGNAL"), 2, 2, MUX_VISIBLE)
            pp = tt.create_pip("GPIO.OUT4","GPIO.OE")
            pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OE_SIGNAL"), 2, 3, MUX_VISIBLE)

            #pp = tt.create_pip("GPIO.OUT4", "GPIO.CLK_INT")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.CLK_1_4"), 1, 0, MUX_VISIBLE)
            #pp = tt.create_pip("GPIO.OUT1", "GPIO.CLK_INT")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.CLK_1_4"), 1, 1, MUX_VISIBLE)

            #pp = tt.create_pip("GPIO.CLK_INT", "GPIO.OUT_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.SEL_OUT_CLOCK"), 1, 1, MUX_VISIBLE)
            #pp = tt.create_pip("GPIO.CLOCK1", "GPIO.OUT_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT_CLOCK"), 2, 0, MUX_VISIBLE)
            #pp = tt.create_pip("GPIO.CLOCK2", "GPIO.OUT_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT_CLOCK"), 2, 1, MUX_VISIBLE)
            #pp = tt.create_pip("GPIO.CLOCK3", "GPIO.OUT_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT_CLOCK"), 2, 2, MUX_VISIBLE)
            #pp = tt.create_pip("GPIO.CLOCK4", "GPIO.OUT_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.OUT_CLOCK"), 2, 3, MUX_VISIBLE)


            #tt.create_wire("GPIO.IN_D1", "WIRE_INTERNAL")
            #tt.create_wire("GPIO.IN_D2", "WIRE_INTERNAL")
            #tt.create_wire("GPIO.IN_Q1", "WIRE_INTERNAL")
            #tt.create_wire("GPIO.IN_Q2", "WIRE_INTERNAL")
            #tt.create_wire("GPIO.IN_CLK","WIRE_INTERNAL")

            #pp = tt.create_pip("GPIO.CLK_INT", "GPIO.IN_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.SEL_IN_CLOCK"), 1, 1, MUX_VISIBLE)
            #pp = tt.create_pip("GPIO.CLOCK1", "GPIO.IN_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.IN_CLOCK"), 2, 0, MUX_VISIBLE)
            #pp = tt.create_pip("GPIO.CLOCK2", "GPIO.IN_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.IN_CLOCK"), 2, 1, MUX_VISIBLE)
            #pp = tt.create_pip("GPIO.CLOCK3", "GPIO.IN_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.IN_CLOCK"), 2, 2, MUX_VISIBLE)
            #pp = tt.create_pip("GPIO.CLOCK4", "GPIO.IN_CLK")
            #pp.extra_data = PipExtraData(PIP_EXTRA_MUX,ch.strs.id("GPIO.IN_CLOCK"), 2, 3, MUX_VISIBLE)

            tt.create_pip("GPIO.DI", "GPIO.IN1")
            tt.create_pip("GPIO.DI", "GPIO.IN2")

    # Setup tile grid
    for x in range(dev.max_col() + 3):
        for y in range(dev.max_row() + 3):
            ch.set_tile_type(x, y, dev.get_tile_type(x - 2,y - 2))
    # Create nodes between tiles
    for _,nodes in dev.get_connections():
        node = []
        for conn in nodes:
            node.append(NodeWire(conn.x + 2, conn.y + 2, conn.name))
        ch.add_node(node)
    set_timings(ch)

    for package in dev.get_packages():
        pkg = ch.create_package(package)
        for pad in dev.get_package_pads(package):
            pkg.create_pad(pad.name, f"X{pad.x+2}Y{pad.y+2}", pad.bel, pad.function, pad.bank)

    ch.write_bba(args.bba)

if __name__ == '__main__':
    main()

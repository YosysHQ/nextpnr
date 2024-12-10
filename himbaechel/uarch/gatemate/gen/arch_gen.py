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


parser = argparse.ArgumentParser()
parser.add_argument("--lib", help="Project Peppercorn python database script path", type=str, required=True)
parser.add_argument("--device", help="name of device to export", type=str, required=True)
parser.add_argument("--bba", help="bba file to write", type=str, required=True)
args = parser.parse_args()

sys.path.append(os.path.expanduser(args.lib))
sys.path += args.lib 
import die

@dataclass
class PipExtraData(BBAStruct):
    name: IdString
    bits: int = 0
    value: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        bba.u16(self.bits)
        bba.u16(self.value)

def set_timings(ch):
    speed = "DEFAULT"
    tmg = ch.set_speed_grades([speed])

def main():
    # Range needs to be +1, but we are adding +2 more to coordinates, since 
    # they are starting from -2 instead of zero required for nextpnr
    ch = Chip("gatemate", args.device, die.max_col() + 3, die.max_row() + 3)
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
            for pin in die.get_primitive_pins(prim.name):
                tt.add_bel_pin(bel, pin.name, f"{prim.name}.{pin.name}", pin.dir)
        for mux in die.get_mux_connections_for_type(type_name):
            pp = tt.create_pip(mux.src, mux.dst)
            pp.extra_data = PipExtraData(ch.strs.id(mux.name), mux.bits, mux.value)

    # Setup tile grid
    for x in range(die.max_col() + 3):
        for y in range(die.max_row() + 3):
            ch.set_tile_type(x, y, die.get_tile_type(x - 2,y - 2))
    # Create nodes between tiles
    for _,nodes in die.get_connections():
        node = []
        for conn in nodes:
            node.append(NodeWire(conn.x + 2, conn.y + 2, conn.name))
        ch.add_node(node)
    set_timings(ch)
    ch.write_bba(args.bba)

if __name__ == '__main__':
    main()

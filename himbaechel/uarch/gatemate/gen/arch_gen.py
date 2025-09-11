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
import re
import sys
import argparse

sys.path.append(path.join(path.dirname(__file__), "../../.."))
from himbaechel_dbgen.chip import *

PIP_EXTRA_MUX = 1

MUX_INVERT = 1
MUX_VISIBLE = 2
MUX_CONFIG = 4
MUX_ROUTING = 8

parser = argparse.ArgumentParser()
parser.add_argument("--lib", help="Project Peppercorn python database script path", type=str, required=True)
parser.add_argument("--device", help="name of device to export", type=str, required=True)
parser.add_argument("--bba", help="bba file to write", type=str, required=True)
args = parser.parse_args()

sys.path.append(os.path.expanduser(args.lib))
sys.path += args.lib 
import chip
import die

pip_tmg_names = set()
node_tmg_names = set()

@dataclass
class TileExtraData(BBAStruct):
    die : int = 0
    bit_x: int = 0
    bit_y: int = 0
    tile_x: int = 0
    tile_y: int = 0
    prim_id : int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u8(self.die)
        bba.u8(self.bit_x)
        bba.u8(self.bit_y)
        bba.u8(self.tile_x)
        bba.u8(self.tile_y)
        bba.u8(self.prim_id)
        bba.u16(0)

@dataclass
class PipExtraData(BBAStruct):
    pip_type: int
    name: IdString
    bits: int = 0
    value: int = 0
    invert: int = 0
    plane: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        bba.u8(self.bits)
        bba.u8(self.value)
        bba.u8(self.invert)
        bba.u8(self.pip_type)
        bba.u8(self.plane)
        bba.u8(0)
        bba.u16(0)

@dataclass
class BelPinConstraint(BBAStruct):
    index: int
    pin_name: IdString
    constr_x: int = 0
    constr_y: int = 0
    constr_z: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.pin_name.index)
        bba.u16(self.constr_x)
        bba.u16(self.constr_y)
        bba.u16(self.constr_z)
        bba.u16(0)

@dataclass
class BelExtraData(BBAStruct):
    constraints: list[BelPinConstraint] = field(default_factory = list)

    def add_constraints(self, pin: IdString, x: int, y: int, z:int):
        item = BelPinConstraint(len(self.constraints),pin,x,y,z)
        self.constraints.append(item)

    def serialise_lists(self, context: str, bba: BBAWriter):
        self.constraints.sort(key=lambda p: p.pin_name.index)
        bba.label(f"{context}_constraints")
        for i, t in enumerate(self.constraints):
            t.serialise(f"{context}_constraint{i}", bba)
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.slice(f"{context}_constraints", len(self.constraints))

@dataclass
class PadExtraData(BBAStruct):
    x: int = 0
    y: int = 0
    z: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u16(self.x)
        bba.u16(self.y)
        bba.u16(self.z)
        bba.u16(0)

@dataclass
class TimingExtraData(BBAStruct):
    name: IdString
    delay: TimingValue = field(default_factory=TimingValue)

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        self.delay.serialise(context, bba)

@dataclass
class SpeedGradeExtraData(BBAStruct):
    timings: list[TimingExtraData] = field(default_factory = list)

    def add_timing(self, name: IdString, delay: TimingValue):
        item = TimingExtraData(name,delay)
        self.timings.append(item)

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_timings")
        for i, t in enumerate(self.timings):
            t.serialise(f"{context}_timing{i}", bba)
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.slice(f"{context}_timings", len(self.timings))

def convert_timing(tim):
    return TimingValue(tim.rise.min, tim.rise.max, tim.fall.min, tim.fall.max)

def set_timings(ch):
    speed_grades = ["best_lpr", "best_eco", "best_spd",
                    "typ_lpr", "typ_eco", "typ_spd",
                    "worst_lpr", "worst_eco", "worst_spd"]
    rename_table = {
        "CINX_OUT1": "_ARBLUT_CINX_OUT1",
        "CINX_OUT2": "_ARBLUT_CINX_OUT2",
        "PINX_OUT1": "_ARBLUT_PINX_OUT1",
        "PINX_OUT2": "_ARBLUT_PINX_OUT2",
        "PINY1_OUT1": "_ARBLUT_PINY1_OUT1",
        "PINY1_OUT2": "_ARBLUT_PINY1_OUT2",
    }
    tmg = ch.set_speed_grades(speed_grades)
    for speed in speed_grades:
        print(f"Loading timings for {speed}...")
        timing = dict(sorted(chip.get_timings(speed).items()))
        tmg.get_speed_grade(speed).extra_data = SpeedGradeExtraData()
        for name, val in timing.items():
            if name.startswith(("sb_del_t", "im_x", "om_x", "sb_rim_xy", "edge_xy")):
                continue
            name = "timing_" + re.sub(r"[-= >]", "_", rename_table.get(name, name))
            tmg.get_speed_grade(speed).extra_data.add_timing(name=ch.strs.id(name), delay=convert_timing(val))
        #for k in node_tmg_names:
        #    assert k in timing, f"node class {k} not found in timing data"
        #    tmg.set_node_class(grade=speed, name=k, delay=convert_timing(timing[k]))
        #for k in pip_tmg_names:
        #    assert k in timing, f"pip class {k} not found in timing data"
        #    tmg.set_pip_class(grade=speed, name=k, delay=convert_timing(timing[k]))

EXPECTED_VERSION = 1.7

def main():
    # Range needs to be +1, but we are adding +2 more to coordinates, since 
    # they are starting from -2 instead of zero required for nextpnr
    dev = chip.get_device(args.device)
    ch = Chip("gatemate", args.device, dev.max_col() + 3, dev.max_row() + 3)
    # Init constant ids
    ch.strs.read_constids(path.join(path.dirname(__file__), "..", "constids.inc"))
    ch.read_gfxids(path.join(path.dirname(__file__), "..", "gfxids.inc"))
    try:
        if chip.get_version()!=EXPECTED_VERSION:
            print("==============================================================================")
            print(f"ERROR: Expected v{EXPECTED_VERSION} and current v{chip.get_version()} chip database mismatch")
            print("       Please update prjpeppercorn and/or nextpnr")
            print("==============================================================================")
            os._exit(-1)
    except AttributeError:
        print("==============================================================================")
        print("ERROR: Unable to determine prjpepercorn version")
        print("       Please update prjpeppercorn and/or nextpnr")
        print("==============================================================================")
        os._exit(-1)


    if not chip.check_dly_available():
        print("==============================================================================")
        print("ERROR: Delay files not, found")
        print("       Run delay.sh in prjpeppercorn to download needed files")
        print("==============================================================================")
        os._exit(-1)

    new_wires = dict()
    for _,nodes in dev.get_connections():
        for conn in nodes:
            if conn.endpoint:
                t_name = dev.get_tile_type(conn.x,conn.y)
                if t_name not in new_wires:
                    new_wires[t_name] = set()
                new_wires[t_name].add(conn.name)

    for type_name in sorted(die.get_tile_type_list()):
        tt = ch.create_tile_type(type_name)
        for group in sorted(die.get_groups_for_type(type_name)):
            tt.create_group(group.name, group.type)
        for wire in sorted(die.get_endpoints_for_type(type_name)):
            tt.create_wire(wire.name, wire.type)
        if type_name in new_wires:
            for wire in sorted(new_wires[type_name]):
                tt.create_wire(wire+"_n", "NODE_WIRE")
        for prim in sorted(die.get_primitives_for_type(type_name)):
            bel = tt.create_bel(prim.name, prim.type, prim.z)
            if (prim.name in ["CPE_LT_FULL", "CPE_BRIDGE"]):
                bel.flags |= BEL_FLAG_HIDDEN     
            extra = BelExtraData()
            for constr in sorted(die.get_pins_constraint(type_name, prim.name, prim.type)):
                extra.add_constraints(ch.strs.id(constr.name),constr.rel_x,constr.rel_y,4 if constr.pin_num==2 else 5)
            bel.extra_data = extra
            for pin in sorted(die.get_primitive_pins(prim.type)):
                tt.add_bel_pin(bel, pin.name, die.get_pin_connection_name(prim,pin), pin.dir)
        for mux in sorted(die.get_mux_connections_for_type(type_name)):
            if len(mux.delay)>0:
                pip_tmg_names.add(mux.delay)
            pp = tt.create_pip(mux.src, mux.dst, mux.delay)
            if mux.name:
                mux_flags = MUX_INVERT if mux.invert else 0
                mux_flags |= MUX_VISIBLE if mux.visible else 0
                mux_flags |= MUX_CONFIG if mux.config else 0
                plane = 0
                if mux.name.startswith("IM"):
                    plane = int(mux.name[4:6])
                if mux.name.startswith("SB_SML") or mux.name.startswith("SB_BIG"):
                    plane = int(mux.name[8:10])
                if mux.name.startswith("SB_DRIVE"):
                    plane = int(mux.name[10:12])
                if mux.name == "CPE.C_SN":
                    mux_flags |= MUX_ROUTING
                pp.extra_data = PipExtraData(PIP_EXTRA_MUX, ch.strs.id(mux.name), mux.bits, mux.value, mux_flags, plane)
        if type_name in new_wires:
            for wire in sorted(new_wires[type_name]):
                pp = tt.create_pip(wire+"_n", wire)

    # Setup tile grid
    for x in range(dev.max_col() + 3):
        for y in range(dev.max_row() + 3):
            ti = ch.set_tile_type(x, y, dev.get_tile_type(x - 2,y - 2))
            tileinfo = dev.get_tile_info(x - 2,y - 2)
            ti.extra_data = TileExtraData(tileinfo.die, tileinfo.bit_x, tileinfo.bit_y, tileinfo.tile_x, tileinfo.tile_y, tileinfo.prim_index)

    # Create nodes between tiles
    for _,nodes in dev.get_connections():
        node = []
        timing = ""
        for conn in sorted(nodes):
            node.append(NodeWire(conn.x + 2, conn.y + 2, (conn.name + "_n") if conn.endpoint else conn.name))
            # for now update to last one we have defined
            if len(conn.delay)>0:
                timing = conn.delay
                node_tmg_names.add(conn.delay)
        ch.add_node(node, timing)
    set_timings(ch)

    for package in dev.get_packages():
        pkg = ch.create_package(package)
        for pad in sorted(dev.get_package_pads(package)):
            pp = pkg.create_pad(pad.name, f"X{pad.x+2}Y{pad.y+2}", pad.bel, pad.function, pad.bank, pad.flags)
            pp.extra_data = PadExtraData(pad.ddr.x+2, pad.ddr.y+2, 4 if pad.ddr.z==0 else 5)

    ch.write_bba(args.bba)

if __name__ == '__main__':
    main()

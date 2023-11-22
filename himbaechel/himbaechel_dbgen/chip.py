from dataclasses import dataclass, field
from .bba import BBAWriter
from enum import Enum
from typing import Optional
import abc
import struct, hashlib

"""
This provides a semi-flattened routing graph that is built into a deduplicated one.

There are two key elements:
 - Tile Types:
      these represent a unique kind of grid location in terms of its contents:
       - bels (logic functionality like LUTs, FFs, IOs, IP, etc)
       - internal wires (excluding connectivity to other tiles)
       - pips that switch internal wires
 - Nodes
      these merge tile-internal wires across wires to create inter-tile connectivity
      so, for example, a length-4 wire might connect (x, y, "E4AI") and (x+3, y, "E4AO")
"""

class BBAStruct(abc.ABC):
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        pass

@dataclass(eq=True, order=True, frozen=True)
class IdString:
    index: int = 0

class StringPool:
    def __init__(self):
        self.strs = {"": 0}
        self.known_id_count = 1

    def read_constids(self, file: str):
        idx = 1
        with open(file, "r") as f:
            for line in f:
                l = line.strip()
                if not l.startswith("X("):
                    continue
                l = l[2:]
                assert l.endswith(")"), l
                l = l[:-1].strip()
                i = self.id(l)
                assert i.index == idx, (i, idx, l)
                idx += 1
        self.known_id_count = idx

    def id(self, val: str):
        if val in self.strs:
            return IdString(self.strs[val])
        else:
            idx = len(self.strs)
            self.strs[val] = idx
            return IdString(idx)

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_strs")
        for s, idx in sorted(self.strs.items(), key=lambda x: x[1]): # sort by index
            if idx < self.known_id_count:
                continue
            bba.str(s)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.known_id_count)
        bba.slice(f"{context}_strs", len(self.strs) - self.known_id_count)

@dataclass
class PinType(Enum):
    INPUT = 0
    OUTPUT = 1
    INOUT = 2

@dataclass
class BelPin(BBAStruct):
    name: IdString
    wire: int
    dir: PinType

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        bba.u32(self.wire)
        bba.u32(self.dir.value)

BEL_FLAG_GLOBAL = 0x01
BEL_FLAG_HIDDEN = 0x02

@dataclass
class BelData(BBAStruct):
    index: int
    name: IdString
    bel_type: IdString
    z: int

    flags: int = 0
    site: int = 0
    checker_idx: int = 0

    pins: list[BelPin] = field(default_factory=list)
    extra_data: object = None

    def serialise_lists(self, context: str, bba: BBAWriter):
        # sort pins for fast binary search lookups
        self.pins.sort(key=lambda p: p.name.index)
        # write pins array
        bba.label(f"{context}_pins")
        for i, pin in enumerate(self.pins):
            pin.serialise(f"{context}_pin{i}", bba)
        # extra data (optional)
        if self.extra_data is not None:
            self.extra_data.serialise_lists(f"{context}_extra_data", bba)
            bba.label(f"{context}_extra_data")
            self.extra_data.serialise(f"{context}_extra_data", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        bba.u32(self.bel_type.index)
        bba.u16(self.z)
        bba.u16(0)
        bba.u32(self.flags)
        bba.u32(self.site)
        bba.u32(self.checker_idx)
        bba.slice(f"{context}_pins", len(self.pins))
        if self.extra_data is not None:
            bba.ref(f"{context}_extra_data")
        else:
            bba.u32(0)

@dataclass
class BelPinRef(BBAStruct):
    bel: int
    pin: IdString
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.bel)
        bba.u32(self.pin.index)

@dataclass
class TileWireData:
    index: int
    name: IdString
    wire_type: IdString
    const_value: IdString = field(default_factory=list)
    flags: int = 0
    timing_idx: int = -1

    # these crossreferences will be updated by finalise(), no need to manually update
    pips_uphill: list[int] = field(default_factory=list)
    pips_downhill: list[int] = field(default_factory=list)
    bel_pins: list[BelPinRef] = field(default_factory=list)

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_pips_uh")
        for pip_idx in self.pips_uphill:
            bba.u32(pip_idx)
        bba.label(f"{context}_pips_dh")
        for pip_idx in self.pips_downhill:
            bba.u32(pip_idx)
        bba.label(f"{context}_bel_pins")
        for i, bel_pin in enumerate(self.bel_pins):
            bel_pin.serialise(f"{context}_bp{i}", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        bba.u32(self.wire_type.index)
        bba.u32(self.const_value.index)
        bba.u32(self.flags)
        bba.u32(self.timing_idx)
        bba.slice(f"{context}_pips_uh", len(self.pips_uphill))
        bba.slice(f"{context}_pips_dh", len(self.pips_downhill))
        bba.slice(f"{context}_bel_pins", len(self.bel_pins))

@dataclass
class PipData(BBAStruct):
    index: int
    src_wire: int
    dst_wire: int
    pip_type: IdString = field(default_factory=IdString)
    flags: int = 0
    timing_idx: int = -1
    extra_data: object = None

    def serialise_lists(self, context: str, bba: BBAWriter):
        # extra data (optional)
        if self.extra_data is not None:
            self.extra_data.serialise_lists(f"{context}_extra_data", bba)
            bba.label(f"{context}_extra_data")
            self.extra_data.serialise(f"{context}_extra_data", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.src_wire)
        bba.u32(self.dst_wire)
        bba.u32(self.pip_type.index)
        bba.u32(self.flags)
        bba.u32(self.timing_idx)
        if self.extra_data is not None:
            bba.ref(f"{context}_extra_data")
        else:
            bba.u32(0)
@dataclass
class TileType(BBAStruct):
    strs: StringPool
    tmg: "TimingPool"
    type_name: IdString
    bels: list[BelData] = field(default_factory=list)
    pips: list[PipData] = field(default_factory=list)
    wires: list[TileWireData] = field(default_factory=list)

    _wire2idx: dict[IdString, int] = field(default_factory=dict)

    extra_data: object = None

    def create_bel(self, name: str, type: str, z: int):
        # Create a new bel of a given name, type and z (index within the tile) in the tile type
        bel = BelData(index=len(self.bels),
            name=self.strs.id(name),
            bel_type=self.strs.id(type),
            z=z)
        self.bels.append(bel)
        return bel
    def add_bel_pin(self, bel: BelData, pin: str, wire: str, dir: PinType):
        # Add a pin with associated wire to a bel. The wire should exist already.
        pin_id = self.strs.id(pin)
        wire_idx = self._wire2idx[self.strs.id(wire)]
        bel.pins.append(BelPin(pin_id, wire_idx, dir))
        self.wires[wire_idx].bel_pins.append(BelPinRef(bel.index, pin_id))

    def create_wire(self, name: str, type: str="", const_value: str=""):
        # Create a new tile wire of a given name and type (optional) in the tile type
        wire = TileWireData(index=len(self.wires),
            name=self.strs.id(name),
            wire_type=self.strs.id(type),
            const_value=self.strs.id(const_value))
        self._wire2idx[wire.name] = wire.index
        self.wires.append(wire)
        return wire
    def create_pip(self, src: str, dst: str, timing_class: str=""):
        # Create a pip between two tile wires in the tile type. Both wires should exist already.
        src_idx = self._wire2idx[self.strs.id(src)]
        dst_idx = self._wire2idx[self.strs.id(dst)]
        pip = PipData(index=len(self.pips), src_wire=src_idx, dst_wire=dst_idx,
            timing_idx=self.tmg.pip_class_idx(timing_class))
        self.wires[src_idx].pips_downhill.append(pip.index)
        self.wires[dst_idx].pips_uphill.append(pip.index)
        self.pips.append(pip)
        return pip
    def has_wire(self, wire: str):
        # Check if a wire has already been created
        return self.strs.id(wire) in self._wire2idx
    def set_wire_type(self, wire: str, type: str):
        # wire type change
        self.wires[self._wire2idx[self.strs.id(wire)]].wire_type = self.strs.id(type)
    def serialise_lists(self, context: str, bba: BBAWriter):
        # list children of members
        for i, bel in enumerate(self.bels):
            bel.serialise_lists(f"{context}_bel{i}", bba)
        for i, wire in enumerate(self.wires):
            wire.serialise_lists(f"{context}_wire{i}", bba)
        for i, pip in enumerate(self.pips):
            pip.serialise_lists(f"{context}_pip{i}", bba)
        # lists of members
        bba.label(f"{context}_bels")
        for i, bel in enumerate(self.bels):
            bel.serialise(f"{context}_bel{i}", bba)
        bba.label(f"{context}_wires")
        for i, wire in enumerate(self.wires):
            wire.serialise(f"{context}_wire{i}", bba)
        bba.label(f"{context}_pips")
        for i, pip in enumerate(self.pips):
            pip.serialise(f"{context}_pip{i}", bba)
        # extra data (optional)
        if self.extra_data is not None:
            self.extra_data.serialise_lists(f"{context}_extra_data", bba)
            bba.label(f"{context}_extra_data")
            self.extra_data.serialise(f"{context}_extra_data", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.type_name.index)
        bba.slice(f"{context}_bels", len(self.bels))
        bba.slice(f"{context}_wires", len(self.wires))
        bba.slice(f"{context}_pips", len(self.pips))
        if self.extra_data is not None:
            bba.ref(f"{context}_extra_data")
        else:
            bba.u32(0)

# Pre deduplication (nodes flattened, absolute coords)
@dataclass
class NodeWire:
    x: int
    y: int
    wire: str


@dataclass
class NodeShape(BBAStruct):
    wires: list[int] = field(default_factory=list)
    timing_index: int = -1

    def key(self):
        m = hashlib.md5()
        m.update(struct.pack("h"*len(self.wires), *self.wires))
        m.update(struct.pack("i", self.timing_index))
        return m.digest()

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_wires")
        for w in self.wires:
            bba.u16(w)
        if len(self.wires) % 2 != 0:
            bba.u16(0) # alignment
    def serialise(self, context: str, bba: BBAWriter):
        bba.slice(f"{context}_wires", len(self.wires)//3)
        bba.u32(self.timing_index) # timing index (not yet used)

MODE_TILE_WIRE = 0x7000
MODE_IS_ROOT = 0x7001
MODE_ROW_CONST = 0x7002
MODE_GLB_CONST = 0x7003

@dataclass
class RelNodeRef(BBAStruct):
    dx_mode: int = MODE_TILE_WIRE
    dy: int = 0
    wire: int = 0
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u16(self.dx_mode)
        bba.u16(self.dy)
        bba.u16(self.wire)

@dataclass
class TileRoutingShape(BBAStruct):
    wire_to_node: list[int] = field(default_factory=list)
    def key(self):
        m = hashlib.md5()
        m.update(struct.pack("h"*len(self.wire_to_node), *self.wire_to_node))
        return m.digest()

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_w2n")
        for x in self.wire_to_node:
            bba.u16(x)
        if len(self.wire_to_node) % 2 != 0:
            bba.u16(0) # alignment
    def serialise(self, context: str, bba: BBAWriter):
        bba.slice(f"{context}_w2n", len(self.wire_to_node)//3)
        bba.u32(-1) # timing index

@dataclass
class TileInst(BBAStruct):
    x: int
    y: int
    type_idx: Optional[int] = None
    name_prefix: IdString = field(default_factory=IdString)
    loc_type: int = 0
    shape: TileRoutingShape = field(default_factory=TileRoutingShape)
    shape_idx: int = -1
    extra_data: object = None

    def serialise_lists(self, context: str, bba: BBAWriter):
        if self.extra_data is not None:
            self.extra_data.serialise_lists(f"{context}_extra_data", bba)
            bba.label(f"{context}_extra_data")
            self.extra_data.serialise(f"{context}_extra_data", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name_prefix.index)
        bba.u32(self.type_idx)
        bba.u32(self.shape_idx)
        if self.extra_data is not None:
            bba.ref(f"{context}_extra_data")
        else:
            bba.u32(0)

@dataclass
class PadInfo(BBAStruct):
    # package pin name
    package_pin: IdString
    # reference to corresponding bel
    tile: IdString
    bel: IdString
    # function name
    pad_function: IdString
    # index of pin bank
    pad_bank: int
    # extra pad flags
    flags: int
    extra_data: object = None

    def serialise_lists(self, context: str, bba: BBAWriter):
        if self.extra_data is not None:
            self.extra_data.serialise_lists(f"{context}_extra_data", bba)
            bba.label(f"{context}_extra_data")
            self.extra_data.serialise(f"{context}_extra_data", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.package_pin.index)
        bba.u32(self.tile.index)
        bba.u32(self.bel.index)
        bba.u32(self.pad_function.index)
        bba.u32(self.pad_bank)
        bba.u32(self.flags)
        if self.extra_data is not None:
            bba.ref(f"{context}_extra_data")
        else:
            bba.u32(0)

@dataclass
class PackageInfo(BBAStruct):
    strs: StringPool
    name: IdString
    pads: list[int] = field(default_factory=list)

    def create_pad(self, package_pin: str, tile: str, bel: str, pad_function: str, pad_bank: int, flags: int = 0):
        pad = PadInfo(package_pin = self.strs.id(package_pin), tile = self.strs.id(tile), bel = self.strs.id(bel),
                pad_function = self.strs.id(pad_function), pad_bank = pad_bank, flags = flags)
        self.pads.append(pad)
        return pad

    def serialise_lists(self, context: str, bba: BBAWriter):
        for i, pad in enumerate(self.pads):
            pad.serialise_lists(f"{context}_pad{i}", bba)
        bba.label(f"{context}_pads")
        for i, pad in enumerate(self.pads):
            pad.serialise(f"{context}_pad{i}", bba)

    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index)
        bba.slice(f"{context}_pads", len(self.pads))

class TimingValue(BBAStruct):
    def __init__(self, fast_min=0, fast_max=None, slow_min=None, slow_max=None):
        self.fast_min = fast_min
        self.fast_max = fast_max or fast_min
        self.slow_min = slow_min or self.fast_min
        self.slow_max = slow_max or self.fast_max

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
            bba.u32(self.fast_min)
            bba.u32(self.fast_max)
            bba.u32(self.slow_min)
            bba.u32(self.slow_max)

@dataclass
class PipTiming(BBAStruct):
    int_delay: TimingValue = field(default_factory=TimingValue) # internal fixed delay in ps
    in_cap: TimingValue = field(default_factory=TimingValue) # internal capacitance in notional femtofarads
    out_res: TimingValue = field(default_factory=TimingValue) # drive/output resistance in notional milliohms
    flags: int = 0 # is_buffered etc
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        self.int_delay.serialise(context, bba)
        self.in_cap.serialise(context, bba)
        self.out_res.serialise(context, bba)
        bba.u32(self.flags)

@dataclass
class NodeTiming(BBAStruct):
    res: TimingValue = field(default_factory=TimingValue) # wire resistance in notional milliohms
    cap: TimingValue = field(default_factory=TimingValue) # wire capacitance in notional femtofarads
    delay: TimingValue = field(default_factory=TimingValue) # fixed wire delay in ps
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        self.res.serialise(context, bba)
        self.cap.serialise(context, bba)
        self.delay.serialise(context, bba)

@dataclass
class ClockEdge(Enum):
    RISING = 0
    FALLING = 1

@dataclass
class CellPinRegArc(BBAStruct):
    clock: int
    edge: ClockEdge
    setup: TimingValue = field(default_factory=TimingValue) # setup time in ps
    hold: TimingValue = field(default_factory=TimingValue) # hold time in ps
    clk_q: TimingValue = field(default_factory=TimingValue) # clock to output time in ps
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.clock.index)
        bba.u32(self.edge.value)
        self.setup.serialise(context, bba)
        self.hold.serialise(context, bba)
        self.clk_q.serialise(context, bba)

@dataclass
class CellPinCombArc(BBAStruct):
    from_pin: int
    delay: TimingValue = field(default_factory=TimingValue)
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.from_pin.index)
        self.delay.serialise(context, bba)

@dataclass
class CellPinTiming(BBAStruct):
    pin: int
    flags: int = 0
    comb_arcs: list[CellPinCombArc] = field(default_factory=list) # sorted by from_pin ID index
    reg_arcs: list[CellPinRegArc] = field(default_factory=list) # sorted by clock ID index

    def set_clock(self):
        self.flags |= 1

    def finalise(self):
        self.comb_arcs.sort(key=lambda a: a.from_pin)
        self.reg_arcs.sort(key=lambda a: a.clock)

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_comb")
        for i, a in enumerate(self.comb_arcs):
            a.serialise(f"{context}_comb{i}", bba)
        bba.label(f"{context}_reg")
        for i, a in enumerate(self.reg_arcs):
            a.serialise(f"{context}_reg{i}", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.pin.index) # pin idstring
        bba.u32(self.flags)
        bba.slice(f"{context}_comb", len(self.comb_arcs))
        bba.slice(f"{context}_reg", len(self.reg_arcs))

class CellTiming(BBAStruct):
    def __init__(self, strs: StringPool, type_variant: str):
        self.strs = strs
        self.type_variant = strs.id(type_variant)
        self.pin_data = {}

    # combinational timing through a cell (like a LUT delay)
    def add_comb_arc(self, from_pin: str, to_pin: str, delay: TimingValue):
        if to_pin not in self.pin_data:
            self.pin_data[to_pin] = CellPinTiming(pin=self.strs.id(to_pin))
        self.pin_data[to_pin].comb_arcs.append(CellPinCombArc(from_pin=self.strs.id(from_pin), delay=delay))

    # register input style timing (like a DFF input)
    def add_setup_hold(self, clock: str, input_pin: str, edge: ClockEdge, setup: TimingValue, hold: TimingValue):
        if input_pin not in self.pin_data:
            self.pin_data[input_pin] = CellPinTiming(pin=self.strs.id(input_pin))
        if clock not in self.pin_data:
            self.pin_data[clock] = CellPinTiming(pin=self.strs.id(clock))
        self.pin_data[input_pin].reg_arcs.append(CellPinRegArc(clock=self.strs.id(clock), edge=edge, setup=setup, hold=hold))
        self.pin_data[clock].set_clock()

    # register output style timing (like a DFF output)
    def add_clock_out(self, clock: str, output_pin: str, edge: ClockEdge, delay: TimingValue):
        if output_pin not in self.pin_data:
            self.pin_data[output_pin] = CellPinTiming(pin=self.strs.id(output_pin))
        if clock not in self.pin_data:
            self.pin_data[clock] = CellPinTiming(pin=self.strs.id(clock))
        self.pin_data[output_pin].reg_arcs.append(CellPinRegArc(clock=self.strs.id(clock), edge=edge, clk_q=delay))
        self.pin_data[clock].set_clock()

    def finalise(self):
        self.pins = list(self.pin_data.values())
        self.pins.sort(key=lambda p: p.pin)
        for pin in self.pins:
            pin.finalise()

    def serialise_lists(self, context: str, bba: BBAWriter):
        for i, p in enumerate(self.pins):
            p.serialise_lists(f"{context}_pin{i}", bba)
        bba.label(f"{context}_pins")
        for i, p in enumerate(self.pins):
            p.serialise(f"{context}_pin{i}", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.type_variant.index) # type idstring
        bba.slice(f"{context}_pins", len(self.pins))

@dataclass
class SpeedGrade(BBAStruct):
    name: int
    pip_classes: list[Optional[PipTiming]] = field(default_factory=list)
    node_classes: list[Optional[NodeTiming]] = field(default_factory=list)
    cell_types: list[CellTiming] = field(default_factory=list) # sorted by (cell_type, variant) ID tuple

    def finalise(self):
        self.cell_types.sort(key=lambda ty: ty.type_variant)
        for ty in self.cell_types:
            ty.finalise()

    def serialise_lists(self, context: str, bba: BBAWriter):
        for i, t in enumerate(self.cell_types):
            t.serialise_lists(f"{context}_cellty{i}", bba)
        bba.label(f"{context}_pip_classes")
        for i, p in enumerate(self.pip_classes):
            p.serialise(f"{context}_pipc{i}", bba)
        bba.label(f"{context}_node_classes")
        for i, n in enumerate(self.node_classes):
            n.serialise(f"{context}_nodec{i}", bba)
        bba.label(f"{context}_cell_types")
        for i, t in enumerate(self.cell_types):
            t.serialise(f"{context}_cellty{i}", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name.index) # speed grade idstring
        bba.slice(f"{context}_pip_classes", len(self.pip_classes))
        bba.slice(f"{context}_node_classes", len(self.node_classes))
        bba.slice(f"{context}_cell_types", len(self.cell_types))

class TimingPool(BBAStruct):
    def __init__(self, strs: StringPool):
        self.strs = strs
        self.speed_grades = []
        self.speed_grade_idx = {}
        self.pip_classes = {}
        self.node_classes = {}

    def set_speed_grades(self, speed_grades: list):
        assert len(self.speed_grades) == 0
        self.speed_grades = [SpeedGrade(name=self.strs.id(g)) for g in speed_grades]
        self.speed_grade_idx = {g: i for i, g in enumerate(speed_grades)}

    def pip_class_idx(self, name: str):
        if name == "":
            return -1
        elif name in self.pip_classes:
            return self.pip_classes[name]
        else:
            idx = len(self.pip_classes)
            self.pip_classes[name] = idx
            return idx

    def node_class_idx(self, name: str):
        if name == "":
            return -1
        elif name in self.node_classes:
            return self.node_classes[name]
        else:
            idx = len(self.node_classes)
            self.node_classes[name] = idx
            return idx

    def set_pip_class(self, grade: str, name: str, delay: TimingValue,
            in_cap: Optional[TimingValue]=None, out_res: Optional[TimingValue]=None,
            is_buffered=True):
        idx = self.pip_class_idx(name)
        sg = self.speed_grades[self.speed_grade_idx[grade]]
        if idx >= len(sg.pip_classes):
            sg.pip_classes += [None for i in range(1 + idx - len(sg.pip_classes))]
        assert sg.pip_classes[idx] is None, f"attempting to set pip class {name} in speed grade {grade} twice"
        sg.pip_classes[idx] = PipTiming(int_delay=delay,
            in_cap=in_cap or TimingValue(),
            out_res=out_res or TimingValue(),
            flags=(1 if is_buffered else 0)
        )

    def set_bel_pin_class(self, grade: str, name: str, delay: TimingValue,
            in_cap: Optional[TimingValue]=None, out_res: Optional[TimingValue]=None):
        # bel pin classes are shared with pip classes, but this alias adds a bit of extra clarity
        set_pip_class(self, grade, name, delay, in_cap, out_res, is_buffered=True)

    def set_node_class(self, grade: str, name: str,  delay: TimingValue,
            res: Optional[TimingValue]=None, cap: Optional[TimingValue]=None):
        idx = self.node_class_idx(name)
        sg = self.speed_grades[self.speed_grade_idx[grade]]
        if idx >= len(sg.node_classes):
            sg.node_classes += [None for i in range(1 + idx - len(sg.node_classes))]
        assert sg.node_classes[idx] is None, f"attempting to set node class {name} in speed grade {grade} twice"
        sg.node_classes[idx] = NodeTiming(delay=delay, res=res or TimingValue(), cap=cap or TimingValue())

    def add_cell_variant(self, speed_grade: str, name: str):
        cell = CellTiming(self.strs, name)
        self.speed_grades[self.speed_grade_idx[speed_grade]].cell_types.append(cell)
        return cell

    def finalise(self):
        for sg in self.speed_grades:
            sg.finalise()

class Chip:
    def __init__(self, uarch: str, name: str, width: int, height: int):
        self.strs = StringPool()
        self.uarch = uarch
        self.name = name
        self.width = width
        self.height = height
        self.tile_types = []
        self.tiles = [[TileInst(x, y) for x in range(width)] for y in range(height)]
        self.tile_type_idx = dict()
        self.node_shapes = []
        self.node_shape_idx = dict()
        self.tile_shapes = []
        self.tile_shapes_idx = dict()
        self.packages = []
        self.extra_data = None
        self.timing = TimingPool(self.strs)
    def create_tile_type(self, name: str):
        tt = TileType(self.strs, self.timing, self.strs.id(name))
        self.tile_type_idx[name] = len(self.tile_types)
        self.tile_types.append(tt)
        return tt
    def set_tile_type(self, x: int, y: int, type: str):
        self.tiles[y][x].type_idx = self.tile_type_idx[type]
        return self.tiles[y][x]
    def tile_type_at(self, x: int, y: int):
        assert self.tiles[y][x].type_idx is not None, f"tile type at ({x}, {y}) must be set"
        return self.tile_types[self.tiles[y][x].type_idx]
    def set_speed_grades(self, speed_grades: list):
        self.timing.set_speed_grades(speed_grades)
        return self.timing
    def add_node(self, wires: list[NodeWire], timing_class=""):
        # encode a 0..65535 unsigned value into -32768..32767 signed value so struct.pack doesn't complain
        # (we use the same field as signed and unsigned in different modes)
        def _twos(x):
            if x & 0x8000:
                x = x - 0x10000
            return x
        # add a node - joining between multiple tile wires into a single connection (from nextpnr's point of view)
        # all the tile wires must exist, and the tile types must be set, first
        x0 = wires[0].x
        y0 = wires[0].y
        # compute node shape
        shape = NodeShape(timing_index=self.timing.node_class_idx(timing_class))
        for w in wires:
            if isinstance(w.wire, int):
                wire_index = w.wire
            else:
                wire_id = w.wire if w.wire is IdString else self.strs.id(w.wire)
                wire_index = self.tile_type_at(w.x, w.y)._wire2idx[wire_id]
            shape.wires += [w.x-x0, w.y-y0, wire_index]
        # deduplicate node shapes
        key = shape.key()
        if key in self.node_shape_idx:
            shape_idx = self.node_shape_idx[key]
        else:
            shape_idx = len(self.node_shapes)
            self.node_shape_idx[key] = shape_idx
            self.node_shapes.append(shape)
        # update tile wire to node ref
        for i, w in enumerate(wires):
            inst = self.tiles[w.y][w.x]
            wire_idx = shape.wires[i*3+2]
            # make sure there's actually enough space; first
            while 3*wire_idx >= len(inst.shape.wire_to_node):
                inst.shape.wire_to_node += [MODE_TILE_WIRE, 0, 0]
            if i == 0:
                # root of the node. we don't need to back-reference anything because the node is based here
                # so we re-use the structure to store the index of the node shape, instead
                assert inst.shape.wire_to_node[3*wire_idx+0] == MODE_TILE_WIRE, "attempting to add wire to multiple nodes!"
                inst.shape.wire_to_node[3*wire_idx+0] = MODE_IS_ROOT
                inst.shape.wire_to_node[3*wire_idx+1] = _twos(shape_idx & 0xFFFF)
                inst.shape.wire_to_node[3*wire_idx+2] = ((shape_idx >> 16) & 0xFFFF)
            else:
                # back-reference to the root of the node
                dx = x0 - w.x
                dy = y0 - w.y
                assert dx < MODE_TILE_WIRE, "dx range causes overlap with magic values!"
                assert inst.shape.wire_to_node[3*wire_idx+0] == MODE_TILE_WIRE, "attempting to add wire to multiple nodes!"
                inst.shape.wire_to_node[3*wire_idx+0] = dx
                inst.shape.wire_to_node[3*wire_idx+1] = dy
                inst.shape.wire_to_node[3*wire_idx+2] = shape.wires[0*3+2]

    def flatten_tile_shapes(self):
        print("Deduplicating tile shapes...")
        for row in self.tiles:
            for tile in row:
                key = tile.shape.key()
                if key in self.tile_shapes_idx:
                    tile.shape_idx = self.tile_shapes_idx[key]
                else:
                    tile.shape_idx = len(self.tile_shapes)
                    self.tile_shapes.append(tile.shape)
                    self.tile_shapes_idx[key] = tile.shape_idx
        print(f"{len(self.tile_shapes)} unique tile routing shapes")

    def create_package(self, name: str):
        pkg = PackageInfo(self.strs, self.strs.id(name))
        self.packages.append(pkg)
        return pkg

    def serialise(self, bba: BBAWriter):
        self.flatten_tile_shapes()
        # TODO: preface, etc
        # Lists that make up the database
        for i, tt in enumerate(self.tile_types):
            tt.serialise_lists(f"tt{i}", bba)
        for i, shp in enumerate(self.node_shapes):
            shp.serialise_lists(f"nshp{i}", bba)
        for i, tsh in enumerate(self.tile_shapes):
            tsh.serialise_lists(f"tshp{i}", bba)
        for i, pkg in enumerate(self.packages):
            pkg.serialise_lists(f"pkg{i}", bba)
        for y, row in enumerate(self.tiles):
            for x, tinst in enumerate(row):
                tinst.serialise_lists(f"tinst_{x}_{y}", bba)
        for i, sg in enumerate(self.timing.speed_grades):
            sg.serialise_lists(f"sg{i}", bba)
        self.strs.serialise_lists(f"constids", bba)
        if self.extra_data is not None:
            self.extra_data.serialise_lists("extra_data", bba)
            bba.label("extra_data")
            self.extra_data.serialise("extra_data", bba)

        bba.label(f"tile_types")
        for i, tt in enumerate(self.tile_types):
            tt.serialise(f"tt{i}", bba)
        bba.label(f"node_shapes")
        for i, shp in enumerate(self.node_shapes):
            shp.serialise(f"nshp{i}", bba)
        bba.label(f"tile_shapes")
        for i, tsh in enumerate(self.tile_shapes):
            tsh.serialise(f"tshp{i}", bba)
        bba.label(f"packages")
        for i, pkg in enumerate(self.packages):
            pkg.serialise(f"pkg{i}", bba)
        bba.label(f"tile_insts")
        for y, row in enumerate(self.tiles):
            for x, tinst in enumerate(row):
                tinst.serialise(f"tinst_{x}_{y}", bba)
        bba.label(f"speed_grades")
        for i, sg in enumerate(self.timing.speed_grades):
            sg.serialise(f"sg{i}", bba)
        bba.label(f"constids")
        self.strs.serialise(f"constids", bba)

        bba.label("chip_info")
        bba.u32(0x00ca7ca7) # magic
        bba.u32(3) # version
        bba.u32(self.width)
        bba.u32(self.height)

        bba.str(self.uarch)
        bba.str(self.name)
        bba.str("python_dbgen") # generator

        bba.slice("tile_types", len(self.tile_types))
        bba.slice("tile_insts", self.width*self.height)
        bba.slice("node_shapes", len(self.node_shapes))
        bba.slice("tile_shapes", len(self.tile_shapes))
        # packages
        bba.slice("packages", len(self.packages))
        # speed grades: not yet used
        bba.slice("speed_grades", len(self.timing.speed_grades))
        # db-defined constids
        bba.ref("constids")
        # extra data
        if self.extra_data is not None:
            bba.ref("extra_data")
        else:
            bba.u32(0)

    def write_bba(self, filename):
        self.timing.finalise()
        with open(filename, "w") as f:
            bba = BBAWriter(f)
            bba.pre('#include \"nextpnr.h\"')
            bba.pre('NEXTPNR_NAMESPACE_BEGIN')
            bba.post('NEXTPNR_NAMESPACE_END')
            bba.push('chipdb_blob')
            bba.ref('chip_info')
            self.serialise(bba)
            bba.pop()

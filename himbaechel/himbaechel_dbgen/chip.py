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

@dataclass(eq=True, frozen=True)
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
    flags: int = 0

    # not serialised, but used to build the global constant networks
    const_val: int = -1

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
        bba.u32(self.flags)
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
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.src_wire)
        bba.u32(self.dst_wire)
        bba.u32(self.pip_type.index)
        bba.u32(self.flags)
        bba.u32(self.timing_idx)

@dataclass
class TileType(BBAStruct):
    strs: StringPool
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

    def create_wire(self, name: str, type: str=""):
        # Create a new tile wire of a given name and type (optional) in the tile type
        wire = TileWireData(index=len(self.wires),
            name=self.strs.id(name),
            wire_type=self.strs.id(type))
        self._wire2idx[wire.name] = wire.index
        self.wires.append(wire)
        return wire
    def create_pip(self, src: str, dst: str):
        # Create a pip between two tile wires in the tile type. Both wires should exist already.
        src_idx = self._wire2idx[self.strs.id(src)]
        dst_idx = self._wire2idx[self.strs.id(dst)]
        pip = PipData(index=len(self.pips), src_wire=src_idx, dst_wire=dst_idx)
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

# Post deduplication (node shapes merged, relative coords)
@dataclass
class TileWireRef(BBAStruct):
    dx: int
    dy: int
    wire: int

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u16(self.dx)
        bba.u16(self.dy)
        bba.u16(self.wire)

@dataclass
class NodeShape(BBAStruct):
    wires: list[TileWireRef] = field(default_factory=list)
    def key(self):
        m = hashlib.sha1()
        for wire in self.wires:
            m.update(wire.dx.to_bytes(2, 'little', signed=True))
            m.update(wire.dy.to_bytes(2, 'little', signed=True))
            m.update(wire.wire.to_bytes(2, 'little'))
        return m.digest()

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_wires")
        for i, w in enumerate(self.wires):
            w.serialise(f"{context}_w{i}", bba)
        if len(self.wires) % 2 != 0:
            bba.u16(0) # alignment
    def serialise(self, context: str, bba: BBAWriter):
        bba.slice(f"{context}_wires", len(self.wires))
        bba.u32(-1) # timing index (not yet used)

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
    wire_to_node: list[RelNodeRef] = field(default_factory=list)
    def key(self):
        m = hashlib.sha1()
        for wire in self.wire_to_node:
            m.update(wire.dx_mode.to_bytes(2, 'little', signed=True))
            m.update(wire.dy.to_bytes(2, 'little', signed=(wire.dy < 0)))
            m.update(wire.wire.to_bytes(2, 'little', signed=True))
        return m.digest()

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_w2n")
        for i, w in enumerate(self.wire_to_node):
            w.serialise(f"{context}_w{i}", bba)
        if len(self.wire_to_node) % 2 != 0:
            bba.u16(0) # alignment
    def serialise(self, context: str, bba: BBAWriter):
        bba.slice(f"{context}_w2n", len(self.wire_to_node))
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
    def create_tile_type(self, name: str):
        tt = TileType(self.strs, self.strs.id(name))
        self.tile_type_idx[name] = len(self.tile_types)
        self.tile_types.append(tt)
        return tt
    def set_tile_type(self, x: int, y: int, type: str):
        self.tiles[y][x].type_idx = self.tile_type_idx[type]
    def tile_type_at(self, x: int, y: int):
        assert self.tiles[y][x].type_idx is not None, f"tile type at ({x}, {y}) must be set"
        return self.tile_types[self.tiles[y][x].type_idx]
    def add_node(self, wires: list[NodeWire]):
        # add a node - joining between multiple tile wires into a single connection (from nextpnr's point of view)
        # all the tile wires must exist, and the tile types must be set, first
        x0 = wires[0].x
        y0 = wires[0].y
        # compute node shape
        shape = NodeShape()
        for w in wires:
            wire_id = w.wire if w.wire is IdString else self.strs.id(w.wire)
            shape.wires.append(TileWireRef(
                dx=w.x-x0, dy=w.y-y0,
                wire=self.tile_type_at(w.x, w.y)._wire2idx[wire_id]
            ))
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
            wire_idx = shape.wires[i].wire
            # make sure there's actually enough space; first
            if wire_idx >= len(inst.shape.wire_to_node):
                inst.shape.wire_to_node += [RelNodeRef() for k in range(len(inst.shape.wire_to_node), wire_idx+1)]
            if i == 0:
                # root of the node. we don't need to back-reference anything because the node is based here
                # so we re-use the structure to store the index of the node shape, instead
                assert inst.shape.wire_to_node[wire_idx].dx_mode == MODE_TILE_WIRE, "attempting to add wire to multiple nodes!"
                inst.shape.wire_to_node[wire_idx] = RelNodeRef(MODE_IS_ROOT, (shape_idx & 0xFFFF), ((shape_idx >> 16) & 0xFFFF))
            else:
                # back-reference to the root of the node
                dx = x0 - w.x
                dy = y0 - w.y
                assert dx < MODE_TILE_WIRE, "dx range causes overlap with magic values!"
                assert inst.shape.wire_to_node[wire_idx].dx_mode == MODE_TILE_WIRE, "attempting to add wire to multiple nodes!"
                inst.shape.wire_to_node[wire_idx] = RelNodeRef(dx, dy, shape.wires[0].wire)

    def flatten_tile_shapes(self):
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

        bba.label(f"constids")
        self.strs.serialise(f"constids", bba)

        bba.label("chip_info")
        bba.u32(0x00ca7ca7) # magic
        bba.u32(1) # version (TODO)
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
        bba.u32(0)
        bba.u32(0)
        # db-defined constids
        bba.ref("constids")
        # extra data
        if self.extra_data is not None:
            bba.ref("extra_data")
        else:
            bba.u32(0)

    def write_bba(self, filename):
        with open(filename, "w") as f:
            bba = BBAWriter(f)
            bba.pre('#include \"nextpnr.h\"')
            bba.pre('NEXTPNR_NAMESPACE_BEGIN')
            bba.post('NEXTPNR_NAMESPACE_END')
            bba.push('chipdb_blob')
            bba.ref('chip_info')
            self.serialise(bba)
            bba.pop()

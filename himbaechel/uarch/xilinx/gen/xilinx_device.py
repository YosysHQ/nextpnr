import json
import os
import re
from enum import Enum
from tileconn import apply_tileconn
from parse_sdf import parse_sdf_file
from dataclasses import dataclass
from typing import Optional
# Represents Xilinx device data from PrjXray etc

@dataclass
class WireData:
    index: int
    name: str
    intent: str = ""
    tied_value: Optional[int] = None
    resistance: float = 0
    capacitance: float = 0

@dataclass
class PIPData:
    index: int
    from_wire: int
    to_wire: int
    is_bidi: bool = False
    is_route_thru: bool = False
    is_buffered: bool = False
    min_delay: float = 0
    max_delay: float = 0
    resistance: float = 0
    capacitance: float = 0

@dataclass
class SiteWireData:
    name: str
    is_pin: bool = False

@dataclass
class SiteBELPinData:
    name: str
    pindir: str
    site_wire_idx: int

@dataclass
class SiteBELData:
    name: str
    bel_type: str
    bel_class: str
    pins: list[SiteBELPinData]

@dataclass
class SitePIPData:
    bel_idx: int
    bel_input: str
    from_wire_idx: int
    to_wire_idx: int

@dataclass
class SitePinData:
    name: str
    pindir: str
    site_wire_idx: int
    prim_pin_name: str

class SiteData:
    def __init__(self, site_type):
        self.site_type = site_type
        self.wires = []
        self.bels = []
        self.pips = []
        self.pins = []
        self.variants = {}

class TileSitePinData:
    def __init__(self, wire_idx):
        self.wire_idx = wire_idx
        self.min_delay = 0
        self.max_delay = 0
        self.resistance = 0
        self.capacitance = 0

class TileData:
    def __init__(self, tile_type):
        self.tile_type = tile_type
        self.wires = []
        self.wires_by_name = {}
        self.pips = []
        self.sitepin_data = {} # (type, relxy, pin) -> TileSitePinData
        self.cell_timing = None

class PIP:
    def __init__(self, tile, index):
        self.tile = tile
        self.index = index
        self.data = tile.get_pip_data(index)
    def src_wire(self):
        return Wire(self.tile, self.data.from_wire)
    def dst_wire(self):
        return Wire(self.tile, self.data.to_wire)
    def is_route_thru(self):
        return self.data.is_route_thru
    def is_bidi(self):
        return self.data.is_bidi
    def is_buffered(self):
        return self.data.is_buffered
    def min_delay(self):
        return self.data.min_delay
    def max_delay(self):
        return self.data.max_delay
    def resistance(self):
        return self.data.resistance
    def capacitance(self):
        return self.data.capacitance

class Wire:
    def __init__(self, tile, index):
        self.tile = tile
        self.index = index
        self.data = tile.get_wire_data(index)
    def name(self):
        return self.data.name
    def intent(self):
        return self.data.intent
    def node(self):
        if self.index not in self.tile.wire_to_node:
            self.tile.wire_to_node[self.index] = Node(self.tile, [self])
        return self.tile.wire_to_node[self.index]
    def is_gnd(self):
        return "GND_WIRE" in self.name()
    def is_vcc(self):
        return "VCC_WIRE" in self.name()
    def resistance(self):
        return self.data.resistance
    def capacitance(self):
        return self.data.capacitance

class SiteWire:
    def __init__(self, site, index):
        self.site = site
        self.index = index
        self.data = self.site.get_wire_data(index)
    def name(self):
        return self.data.name

class SiteBELPin:
    def __init__(self, bel, name):
        self.bel = bel
        self.name = name
        self.data = self.bel.data.pins[name]
    def name(self):
        return self.name
    def dir(self):
        return self.data.pindir
    def site_wire(self):
        return SiteWire(self.bel.site, self.data.site_wire_idx)

class SiteBEL:
    def __init__(self, site, index):
        self.site = site
        self.index = index
        self.data = site.get_bel_data(index)
    def name(self):
        return self.data.name
    def bel_type(self):
        return self.data.bel_type
    def bel_class(self):
        return self.data.bel_class
    def pins(self):
        return (SiteBELPin(self, n) for n in self.data.pins.keys())

class SitePIP:
    def __init__(self, site, index):
        self.site = site
        self.index = index
        self.data = site.get_pip_data(index)
    def bel(self):
        return SiteBEL(self.site, self.data.bel_idx)
    def bel_input(self):
        return self.data.bel_input
    def src_wire(self):
        return SiteWire(self.site, self.data.from_wire_idx)
    def dst_wire(self):
        return SiteWire(self.site, self.data.to_wire_idx)

class SitePin:
    def __init__(self, site, index):
        self.site = site
        self.index = index
        self.data = site.data.pins[index]
    def name(self):
        return self.data.name
    def dir(self):
        return self.data.pindir
    def site_wire(self):
        return SiteWire(self.site, self.data.site_wire_idx)
    def tile_wire(self):
        return self.site.tile.site_pin_wire(self.site.primary.site_type(), self.site.rel_xy(), self.data.prim_pin_name)
    def min_delay(self):
        return self.site.tile.site_pin_timing(self.site.primary.site_type(), self.site.rel_xy(), self.data.prim_pin_name).min_delay
    def max_delay(self):
        return self.site.tile.site_pin_timing(self.site.primary.site_type(), self.site.rel_xy(), self.data.prim_pin_name).max_delay
    def resistance(self):
        return self.site.tile.site_pin_timing(self.site.primary.site_type(), self.site.rel_xy(), self.data.prim_pin_name).resistance
    def capacitance(self):
        return self.site.tile.site_pin_timing(self.site.primary.site_type(), self.site.rel_xy(), self.data.prim_pin_name).capacitance

class Site:
    def __init__(self, tile, name, index, grid_xy, data, primary=None):
        self.tile = tile
        self.name = name
        self.index = index
        self.prefix = name[0:name.rfind('_')]
        self.grid_xy = grid_xy
        self.data = data
        self.primary = primary if primary is not None else self
        self._rel_xy = None # filled later
        self._variants = None #filled later
    def get_bel_data(self, index):
        return self.data.bels[index]
    def get_wire_data(self, index):
        return self.data.wires[index]
    def get_pip_data(self, index):
        return self.data.pips[index]
    def site_type(self):
        return self.data.site_type
    def rel_xy(self):
        if self._rel_xy is None:
            base_x = 999999
            base_y = 999999
            for site in self.tile.sites():
                if site.prefix != self.prefix:
                    continue
                base_x = min(base_x, site.grid_xy[0])
                base_y = min(base_y, site.grid_xy[1])
            self._rel_xy = (self.grid_xy[0] - base_x, self.grid_xy[1] - base_y)
        return self._rel_xy
    def bels(self):
        return (SiteBEL(self, i) for i in range(len(self.data.bels)))
    def wires(self):
        return (SiteWire(self, i) for i in range(len(self.data.wires)))
    def pips(self):
        return (SitePIP(self, i) for i in range(len(self.data.pips)))
    def pins(self):
        return (SitePin(self, i) for i in range(len(self.data.pins)))
    def pin(self, p):
        for i in range(len(self.data.pins)):
            if self.data.pins[i].name == p:
                return SitePin(self, i)
        return None
    def available_variants(self):
        # Make sure primary type is first
        if self._variants is None:
            self._variants = []
            self._variants.append(self.site_type())
            for var in sorted(self.data.variants.keys()):
                if var != self.site_type():
                    self._variants.append(var)
        return self._variants
    def variant(self, vtype):
        vsite = Site(self.tile, self.name, self.index, self.grid_xy, self.data.variants[vtype], self)
        return vsite
    def rel_name(self):
        x, y = self.rel_xy()
        return f"{self.prefix}_X{x}Y{y}"

class Tile:
    def __init__(self, x, y, name, data, interconn_xy, site_insts):
        self.x = x
        self.y = y
        self.name = name
        self.data = data
        self.interconn_xy = interconn_xy
        self.site_insts = site_insts
        self.wire_to_node = {}
        self.node_autoidx = 0
        self.used_wires = None
    def get_pip_data(self, i):
        return self.data.pips[i]
    def get_wire_data(self, i):
        return self.data.wires[i]
    def tile_type(self):
        return self.data.tile_type
    def wires(self):
        return (Wire(self, i) for i in range(len(self.data.wires)))
    def wire(self, name):
        return Wire(self, self.data.wires_by_name[name].index)
    def pips(self):
        return (PIP(self, i) for i in range(len(self.data.pips)))
    def sites(self):
        return self.site_insts
    def site_pin_wire(self, sitetype, rel_xy, pin):
        wire_idx = self.data.sitepin_data[(sitetype, rel_xy, pin)].wire_idx
        return Wire(self, wire_idx) if wire_idx is not None else None
    def site_pin_timing(self, sitetype, rel_xy, pin):
        return self.data.sitepin_data[(sitetype, rel_xy, pin)]
    def cell_timing(self):
        return self.data.cell_timing
    def used_wire_indices(self):
        if self.used_wires is None:
            self.used_wires = set()
            for pip in self.pips():
                self.used_wires.add(pip.src_wire().index)
                self.used_wires.add(pip.dst_wire().index)
            for site in self.sites():
                for v in site.available_variants():
                    variant = site.variant(v)
                    for pin in variant.pins():
                        if pin.tile_wire() is not None:
                            self.used_wires.add(pin.tile_wire().index)
        return self.used_wires
    def split_name(self):
        prefix, xy = self.name.rsplit("_", 1)
        xy_m = re.match(r"X(\d+)Y(\d+)", xy)
        return prefix, int(xy_m.group(1)), int(xy_m.group(2))

class Node:
    def __init__(self, tile, wires=[]):
        self.tile = tile
        self.index = tile.node_autoidx
        tile.node_autoidx += 1
        self.wires = wires
    def unique_index(self):
        return (self.tile.y << 48) | (self.tile.x << 32) | self.index
    def is_vcc(self):
        for wire in self.wires:
            if wire.is_vcc():
                return True
        return False
    def is_gnd(self):
        for wire in self.wires:
            if wire.is_gnd():
                return True
        return False

class Package:
    def __init__(self, name):
        self.name = name
        self.pin_map = {}

class Device:
    def __init__(self, name):
        self.name = name
        self.tiles = []
        self.tiles_by_name = {}
        self.tiles_by_xy = {}
        self.sites_by_name = {}
        self.width = 0
        self.height = 0
        self.packages = {}
    def tile(self, name):
        return self.tiles_by_name[name]
    def site(self, name):
        return self.sites_by_name[name]

def import_device(fabricname, prjxray_root, metadata_root):
    site_type_cache = {}
    tile_type_cache = {}
    tile_json_cache = {}
    def parse_xy(xy):
        xpos = xy.rfind("X")
        ypos = xy.rfind("Y")
        return int(xy[xpos+1:ypos]), int(xy[ypos+1:])

    def get_site_type_data(sitetype):
        if sitetype not in site_type_cache:
            sd = SiteData(sitetype)
            sp = metadata_root + "/site_type_" + sitetype + ".json"
            if os.path.exists(sp):
                with open(sp, "r") as jf:
                    sj = json.load(jf)
                for vtype, vdata in sorted(sj.items()): # Consider all site variants
                    if vtype == sitetype:
                        vd = sd # primary variant
                    else:
                        vd = SiteData(vtype)
                    site_wire_by_name = {}
                    def wire_index(name):
                        if name not in site_wire_by_name:
                            idx = len(vd.wires)
                            vd.wires.append(SiteWireData(name=name))
                            site_wire_by_name[name] = idx
                        return site_wire_by_name[name]
                    # Import bels
                    bel_idx_by_name = {}
                    for bel, beldata in sorted(vdata["bels"].items()):
                        belpins = {}
                        for pin, pindata in sorted(beldata["pins"].items()):
                            belpins[pin] = SiteBELPinData(name=pin, pindir=pindata["dir"], site_wire_idx=wire_index(pindata["wire"]))
                        bd = SiteBELData(name=bel, bel_type=beldata["type"], bel_class=beldata["class"], pins=belpins)
                        bel_idx_by_name[bel] = len(vd.bels)
                        vd.bels.append(bd)
                    # Import pips
                    for pipdata in vdata["pips"]:
                        bel_idx = bel_idx_by_name[pipdata["bel"]]
                        bel_data = vd.bels[bel_idx]
                        vd.pips.append(SitePIPData(bel_idx=bel_idx_by_name[pipdata["bel"]], bel_input=pipdata["from_pin"],
                            from_wire_idx=bel_data.pins[pipdata["from_pin"]].site_wire_idx,
                            to_wire_idx=bel_data.pins[pipdata["to_pin"]].site_wire_idx))
                    # Import pins
                    for pin, pindata in sorted(vdata["pins"].items()):
                        vd.pins.append(SitePinData(name=pin, pindir=pindata["dir"], site_wire_idx=wire_index(pindata["wire"]),
                            prim_pin_name=pindata["primary"]))
                    sd.variants[vtype] = vd
            else:
                sd.variants[sitetype] = sd
            site_type_cache[sitetype] = sd
        return site_type_cache[sitetype]

    def read_tile_type_json(tiletype):
        if tiletype not in tile_json_cache:
            if not os.path.exists(prjxray_root + "/tile_type_" + tiletype + ".json"):
                tile_json_cache[tiletype] = dict(wires={}, pips={}, sites=[])
            else:
                with open(prjxray_root + "/tile_type_" + tiletype + ".json", "r") as jf:
                    tile_json_cache[tiletype] = json.load(jf)
        return tile_json_cache[tiletype]

    def get_tile_type_data(tiletype):
        if tiletype not in tile_type_cache:
            td = TileData(tiletype)
            # Import wires and pips
            tj = read_tile_type_json(tiletype)
            for wire, wire_data in sorted(tj["wires"].items()):
                wire_id = len(td.wires)
                wd = WireData(index=wire_id, name=wire, tied_value=None) # FIXME: tied_value
                wd.intent = get_wire_intent(tiletype, wire)
                if wire_data is not None:
                    if "res" in wire_data:
                        wd.resistance = float(wire_data["res"])
                    if "cap" in wire_data:
                        wd.capacitance = float(wire_data["cap"])
                td.wires.append(wd)
                td.wires_by_name[wire] = wd
            for pip, pipdata in sorted(tj["pips"].items()):
                # FIXME: pip/wire delays
                pip_id = len(td.pips)
                pd = PIPData(index=pip_id,
                    from_wire=td.wires_by_name[pipdata["src_wire"]].index, to_wire=td.wires_by_name[pipdata["dst_wire"]].index,
                    is_bidi=(not bool(int(pipdata["is_directional"]))), is_route_thru=bool(int(pipdata["is_pseudo"])))
                if "is_pass_transistor" in pipdata:
                    pd.is_buffered = (not bool(int(pipdata["is_pass_transistor"])))
                if "src_to_dst" in pipdata:
                    s2d = pipdata["src_to_dst"]
                    if "delay" in s2d and s2d["delay"] is not None:
                        pd.min_delay = min(float(s2d["delay"][0]), float(s2d["delay"][1]))
                        pd.max_delay = max(float(s2d["delay"][2]), float(s2d["delay"][3]))
                    if "res" in s2d and s2d["res"] is not None:
                        pd.resistance = float(s2d["res"])
                    if "in_cap" in s2d and s2d["in_cap"] is not None:
                        pd.capacitance = float(s2d["in_cap"])
                td.pips.append(pd)
            for sitedata in tj["sites"]:
                rel_xy = parse_xy(sitedata["name"])
                sitetype = sitedata["type"]
                for sitepin, pindata in sorted(sitedata["site_pins"].items()):
                    if pindata is None:
                        tspd = TileSitePinData(None)
                    else:
                        pinwire = td.wires_by_name[pindata["wire"]].index
                        tspd = TileSitePinData(pinwire)
                        if "delay" in pindata:
                            tspd.min_delay = min(float(pindata["delay"][0]), float(pindata["delay"][1]))
                            tspd.max_delay = max(float(pindata["delay"][2]), float(pindata["delay"][3]))
                        if "res" in pindata:
                            tspd.resistance = float(pindata["res"])
                        if "cap" in pindata:
                            tspd.capacitance = float(pindata["cap"])
                    td.sitepin_data[(sitetype, rel_xy, sitepin)] = tspd
            if os.path.exists(prjxray_root + "/timings/" + tiletype + ".sdf"):
                td.cell_timing = parse_sdf_file(prjxray_root + "/timings/" + tiletype + ".sdf")

            tile_type_cache[tiletype] = td

        return tile_type_cache[tiletype]

    def get_wire_intent(tiletype, wirename):
        if tiletype not in ij["tiles"]:
            return "GENERIC"
        if wirename not in ij["tiles"][tiletype]:
            return "GENERIC"
        return ij["intents"][str(ij["tiles"][tiletype][wirename])]

    d = Device(fabricname)

    # Load intent JSON
    with open(metadata_root + "/wire_intents.json", "r") as ijf:
        ij = json.load(ijf)
    with open(prjxray_root + "/" + fabricname + "/tilegrid.json") as gf:
        tgj = json.load(gf)
    for tile, tiledata in sorted(tgj.items()):
        x = int(tiledata["grid_x"])
        y = int(tiledata["grid_y"])
        d.width = max(d.width, x + 1)
        d.height = max(d.height, y + 1)
        tiletype = tiledata["type"]
        t = Tile(x, y, tile, get_tile_type_data(tiletype), (-1, -1), [])
        for idx, (site, sitetype) in enumerate(sorted(tiledata["sites"].items())):
                si = Site(t, site, idx, parse_xy(site), get_site_type_data(sitetype))
                t.site_insts.append(si)
                d.sites_by_name[site] = si
        d.tiles_by_name[tile] = t
        d.tiles_by_xy[x, y] = t
        d.tiles.append(t)

    # Resolve interconnect tile coordinates
    for t in d.tiles:
        for delta in range(0, 30):
            if t.interconn_xy != (-1, -1):
                break # found, done
            for direction in (-1, +1):
                nxy = (t.x + direction * delta, t.y)
                if nxy not in d.tiles_by_xy:
                    continue
                if d.tiles_by_xy[nxy].tile_type not in ("INT", "INT_L", "INT_R"):
                    continue
                t.interconn_xy = nxy
                break
    # Read package pins
    for entry in os.scandir(prjxray_root):
        if not entry.is_dir() or not entry.name.startswith(fabricname):
            continue
        device_postfix = entry.name[len(fabricname):]
        if len(device_postfix) == 0:
            continue
        package_name = device_postfix.split("-")[0]
        if package_name in d.packages:
            continue # already seen in a different speed grade
        with open(prjxray_root + "/" + entry.name + "/package_pins.csv") as ppf:
            pkg = Package(name=package_name)
            for line in ppf:
                sl = line.strip().split(",")
                if len(sl) < 3:
                    continue
                if sl[2] == "site":
                    continue # header
                pkg.pin_map[sl[0]] = sl[2]
            d.packages[package_name] = pkg
    with open(prjxray_root + "/" + fabricname + "/tileconn.json", "r") as tcf:
        apply_tileconn(tcf, d)
    return d

if __name__ == '__main__':
    import sys
    import_device(*sys.argv[1:])

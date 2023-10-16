from os import path
import sys
import argparse
import xilinx_device
import filters
import struct
sys.path.append(path.join(path.dirname(__file__), "../../.."))
from himbaechel_dbgen.chip import *

def lookup_port_type(t):
    if t == "INPUT": return PinType.INPUT
    elif t == "OUTPUT": return PinType.OUTPUT
    elif t == "BIDIR": return PinType.INOUT
    else: assert False

def gen_bel_name(site, bel_name):
    prim_st = site.primary.site_type()
    if prim_st in ("IOB33M", "IOB33S", "IOB33", "IOB18M", "IOB18S", "IOB18"):
        return f"{site.site_type()}.{bel_name}"
    else:
        return f"{bel_name}"

@dataclass
class PipClass(Enum):
    TILE_ROUTING = 0
    SITE_ENTRANCE = 1
    SITE_EXIT = 2
    SITE_INTERNAL = 3
    LUT_PERMUTATION = 4
    LUT_ROUTETHRU = 5
    CONST_DRIVER = 6

@dataclass
class PipExtraData(BBAStruct):
    site_key: int = -1
    bel_name: IdString = field(default_factory=IdString)
    pip_config: int = 0

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.site_key)
        bba.u32(self.bel_name.index)
        bba.u32(self.pip_config)

@dataclass
class BelExtraData(BBAStruct):
    name_in_site: IdString

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name_in_site.index)

@dataclass
class SiteInst(BBAStruct):
    name_prefix: IdString
    site_x: int
    site_y: int
    rel_x: int
    rel_y: int
    int_x: int
    int_y: int
    variants: list[IdString] = field(default_factory=list)

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_variants")
        for i, variant in enumerate(self.variants):
            bba.u32(variant.index)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name_prefix.index)
        bba.u16(self.site_x)
        bba.u16(self.site_y)
        bba.u16(self.rel_x)
        bba.u16(self.rel_y)
        bba.u16(self.int_x)
        bba.u16(self.int_y)
        bba.slice(f"{context}_variants", len(self.variants))

@dataclass
class TileExtraData(BBAStruct):
    name_prefix: IdString
    tile_x: int
    tile_y: int
    sites: list[SiteInst] = field(default_factory=list)

    def serialise_lists(self, context: str, bba: BBAWriter):
        for i, site in enumerate(self.sites):
            site.serialise_lists(f"{context}_si{i}", bba)
        bba.label(f"{context}_sites")
        for i, site in enumerate(self.sites):
            site.serialise(f"{context}_si{i}", bba)
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.name_prefix.index)
        bba.u16(self.tile_x)
        bba.u16(self.tile_y)
        bba.slice(f"{context}_sites", len(self.sites))

def timing_pip_classs(pip: xilinx_device.PIP):
    return f"{'buf' if pip.is_buffered() else 'sw'}_d{pip.max_delay()}_r{pip.resistance()}_c{pip.capacitance()}"

seen_pip_timings = set()
seen_node_timings = set()

def import_tiletype(ch: Chip, tile: xilinx_device.Tile):
    tile_type = tile.tile_type()
    if tile.x == 0 and tile.y == 0:
        assert tile_type == "NULL"
        tile_type = "NULL_CORNER"
    tt = ch.create_tile_type(tile_type)
    # import tile wires
    for wire in tile.wires():
        nw = tt.create_wire(wire.name(), wire.intent())
        nw.flags = -1 # not a site wire
        if wire.is_gnd():
            nw.const_value = ch.strs.id("GND")
        if wire.is_vcc():
            nw.const_value = ch.strs.id("VCC")
    if tile_type == "NULL_CORNER":
        # pseudo ground bels
        tt.create_wire(f"GND", "GND", const_value="GND")
        tt.create_wire(f"VCC", "VCC", const_value="VCC")

        gnd = tt.create_bel(f"PSEUDO_GND", f"PSEUDO_GND", z=0)
        gnd.site = -1
        gnd.extra_data = BelExtraData(name_in_site=ch.strs.id("PSEUDO_GND"))
        tt.add_bel_pin(gnd, "Y", "GND", PinType.OUTPUT)

        vcc = tt.create_bel(f"PSEUDO_VCC", f"PSEUDO_VCC", z=1)
        vcc.site = -1
        vcc.extra_data = BelExtraData(name_in_site=ch.strs.id("PSEUDO_VCC"))
        tt.add_bel_pin(vcc, "Y", "VCC", PinType.OUTPUT)
    def add_pip(src_wire, dst_wire, pip_class=PipClass.TILE_ROUTING, timing="",
        site_key=-1, bel_name="", pip_config=0):
        np = tt.create_pip(src_wire, dst_wire, timing)
        np.flags = pip_class.value
        np.extra_data = PipExtraData(site_key=site_key, bel_name=ch.strs.id(bel_name),
            pip_config=pip_config)

    def lookup_site_wire(sw):
        canon_name = f"{sw.site.rel_name()}.{sw.name()}"
        if not tt.has_wire(canon_name):
            nw = tt.create_wire(name=canon_name,
                type="INTENT_SITE_GND" if sw.name() == "GND_WIRE" else "INTENT_SITE_WIRE")
            nw.flags = sw.site.primary.index
        return canon_name

    def add_site_io_pip(pin):
        pn = pin.name()
        s = pin.site
        site_key = (s.index << 8)
        if pin.tile_wire() is None:
            return None
        # TODO: timing class
        if pin.dir() in ("OUTPUT", "BIDIR"):
            if s.primary.site_type() == "IPAD" and pn == "O":
                return None
            # if s.rel_xy()[0] == 0 and s.primary.site_type() == "SLICEL" and pin.site_wire().name() == "A":
            #     # Add ground pip
            #     # self.add_pseudo_pip(self.row_gnd_wire_index, self.sitewire_to_tilewire(pin.site_wire()),
            #     #    pip_type=NextpnrPipType.CONST_DRIVER)
            #     pass
            add_pip(lookup_site_wire(pin.site_wire()), pin.tile_wire().name(),
                pip_class=PipClass.SITE_EXIT, timing="SITE_NULL")
        else:
            if s.site_type() in ("SLICEL", "SLICEM"):
                # Add permuation pseudo-pips for LUT inputs
                swn = pin.site_wire().name()
                if len(swn) == 2 and swn[0] in "ABCDEFGH" and swn[1] in "123456":
                    i = int(swn[1])
                    for j in range(1, 7):
                        if (i == 6) != (j == 6):
                            continue # don't allow permutation of input 6
                        pip_config = ("ABCDEFGH".index(swn[0]) << 8) | ((j - 1) << 4) | (i - 1)
                        if s.rel_xy()[0] == 1:
                            pip_config |= (4 << 8)
                        add_pip(s.pin(f"{swn[0]}{j}").tile_wire().name(), lookup_site_wire(pin.site_wire()),
                            pip_class=PipClass.LUT_PERMUTATION,
                            pip_config=pip_config,
                            site_key=(s.index << 8),
                            timing="SITE_NULL"
                        )
                    return
            add_pip(pin.tile_wire().name(), lookup_site_wire(pin.site_wire()),
                pip_class=PipClass.SITE_ENTRANCE, timing="SITE_NULL")

    # TODO: ground/vcc
    tile_wire_count = len(tt.wires)
    for site in tile.sites():
        seen_pins = set()
        for variant_idx, variant in enumerate(site.available_variants()):
            if variant in ("FIFO36E1", ): #unsupported atm
                continue
            sv = site.variant(variant)
            variant_key = (site.index << 8) | (variant_idx & 0xFF)
            # Import site bels
            for bel in sv.bels():
                z = filters.get_bel_z_override(bel, len(tt.bels))
                # Overriden z of -1 means we skip this bel
                if z == -1:
                    continue
                bel_name = gen_bel_name(sv, bel.name())
                nb = tt.create_bel(name=f"{site.rel_name()}.{bel_name}", type=filters.get_bel_type_override(bel.bel_type()), z=z)
                nb.site = variant_key
                nb.extra_data = BelExtraData(name_in_site=ch.strs.id(bel_name))
                if bel.bel_class() == "RBEL": nb.flags |= 2
                # TODO: extra data (site etc)
                for pin in bel.pins():
                    tt.add_bel_pin(nb, pin.name, lookup_site_wire(pin.site_wire()), lookup_port_type(pin.dir()))
            # Import site pins
            for pin in sv.pins():
                pin_key = (pin.name(), pin.site_wire().name())
                if pin_key not in seen_pins:
                    add_site_io_pip(pin)
                    seen_pins.add(pin_key)
            # Import site pips
            for site_pip in sv.pips():
                if "LUT" in site_pip.bel().bel_type():
                    continue # ignore site LUT route-throughs
                bel_name = site_pip.bel().name()
                bel_pin = site_pip.bel_input()
                if (bel_name == "ADI1MUX" and bel_pin == "BDI1") or \
                    (bel_name == "BDI1MUX" and bel_pin == "DI") or \
                    (bel_name == "CDI1MUX" and bel_pin == "DI") or \
                    (bel_name.startswith("TFBUSED")) or \
                    (bel_name == "OMUX"):
                    continue
                add_pip(lookup_site_wire(site_pip.src_wire()),
                    lookup_site_wire(site_pip.dst_wire()),
                    pip_class=PipClass.SITE_INTERNAL,
                    site_key=variant_key,
                    bel_name=bel_name,
                    pip_config=ch.strs.id(bel_pin).index,
                    timing="SITE_NULL"
                )
    # Import tile pips
    for pip in tile.pips():
        if not filters.include_pip(tile.tile_type(), pip):
            continue
        tcls = timing_pip_classs(pip)
        if tcls not in seen_pip_timings:
            ch.timing.set_pip_class(f"DEFAULT", tcls,
                delay=TimingValue(int(pip.min_delay()*1000), int(pip.max_delay()*1000)), # ps
                in_cap=TimingValue(int(pip.capacitance()*1000)), # fF
                out_res=TimingValue(int(pip.resistance())), # mohm
                is_buffered=pip.is_buffered())
            seen_pip_timings.add(tcls)
        add_pip(pip.src_wire().name(), pip.dst_wire().name(), pip_class=PipClass.TILE_ROUTING, timing=tcls,
            pip_config=1 if pip.is_route_thru() else 0)
        # TODO: extra data, route-through flag
        if pip.is_bidi():
            add_pip(pip.dst_wire().name(), pip.src_wire().name(), pip_class=PipClass.TILE_ROUTING, timing=tcls,
                pip_config=1 if pip.is_route_thru() else 0)
def main():
    xlbase = path.join(path.dirname(path.realpath(__file__)), "..")

    parser = argparse.ArgumentParser()
    parser.add_argument("--xray", help="Project X-Ray device database path for current family (e.g. ../prjxray-db/artix7)", type=str, required=True)
    parser.add_argument("--metadata", help="nextpnr-xilinx site metadata root", type=str, default=path.join(xlbase, "meta", "artix7"))
    parser.add_argument("--device", help="name of device to export", type=str, required=True)
    parser.add_argument("--constids", help="name of nextpnr constids file to read", type=str, default=path.join(xlbase, "constids.inc"))
    parser.add_argument("--bba", help="bba file to write", type=str, required=True)
    args = parser.parse_args()

    # Init database paths
    metadata_root = args.metadata
    xraydb_root = args.xray
    if "xc7z" in args.device:
        metadata_root = metadata_root.replace("artix7", "zynq7")
        xraydb_root = xraydb_root.replace("artix7", "zynq7")
    if "xc7k" in args.device:
        metadata_root = metadata_root.replace("artix7", "kintex7")
        xraydb_root = xraydb_root.replace("artix7", "kintex7")
    if "xc7s" in args.device:
        metadata_root = metadata_root.replace("artix7", "spartan7")
        xraydb_root = xraydb_root.replace("artix7", "spartan7")
    # Load prjxray device data
    d = xilinx_device.import_device(args.device, xraydb_root, metadata_root)
    # Init constant ids
    ch = Chip("xilinx", args.device, d.width, d.height)
    ch.strs.read_constids(path.join(path.dirname(__file__), args.constids))
    ch.set_speed_grades(["DEFAULT", ]) # TODO: figure out how speed grades are supposed to work in prjxray
    # Import tile types
    for tile in d.tiles:
        tile_type = tile.tile_type()
        if tile.x == 0 and tile.y == 0:
            assert tile_type == "NULL"
            # location for pseudo gnd/vcc bels
            tile_type = "NULL_CORNER"
        if tile_type not in ch.tile_type_idx:
            import_tiletype(ch, tile)
        ti = ch.set_tile_type(tile.x, tile.y, tile_type)
        prefix, tx, ty = tile.split_name()
        ti.extra_data = TileExtraData(ch.strs.id(prefix), tx, ty)
        for site in tile.sites():
            ti.extra_data.sites.append(SiteInst(
                name_prefix=ch.strs.id(site.prefix),
                site_x=site.grid_xy[0], site_y=site.grid_xy[1],
                rel_x=site.rel_xy()[0], rel_y=site.rel_xy()[1],
                int_x=tile.interconn_xy[0], int_y=tile.interconn_xy[1],
                variants=[ch.strs.id(v) for v in site.available_variants()]
            ))
    # Import nodes
    seen_nodes = set()
    tt_used_wires = {}
    print("Processing nodes...")
    for row in range(d.height):
        # TODO: GND and VCC row connectivity...
        for col in range(d.width):
            t = d.tiles_by_xy[col, row]
            for w in t.wires():
                n = w.node()
                uid = n.unique_index()
                if uid in seen_nodes:
                    continue
                if len(n.wires) > 1:
                    node_wires = []
                    node_cap = 0
                    node_res = 0
                    # Add interconnect tiles first for better delay estimates in nextpnr
                    for j in range(2):
                        for w in n.wires:
                            if (w.tile.tile_type() in ("INT", "INT_L", "INT_R")) != (j == 0):
                                continue
                            if w.tile.tile_type() not in tt_used_wires:
                                tt_used_wires[w.tile.tile_type()] = w.tile.used_wire_indices()
                            node_cap += int(w.capacitance() * 1000)
                            node_res += int(w.resistance())
                            # prune wires from nodes that just pass through a tile without connecting to
                            # bels/pips, because nextpnr doesn't care about these
                            if w.index not in tt_used_wires[w.tile.tile_type()]:
                                continue
                            node_wires.append(NodeWire(w.tile.x, w.tile.y, w.index))
                    if len(node_wires) > 1:
                        timing_class = f"node_c{node_cap}_c{node_res}"
                        ch.add_node(node_wires, timing_class=timing_class)
                        if timing_class not in seen_node_timings:
                            ch.timing.set_node_class("DEFAULT", timing_class, delay=TimingValue(0), cap=TimingValue(node_cap), res=TimingValue(node_res))
                            seen_node_timings.add(timing_class)
                    del node_wires
                seen_nodes.add(uid)
    # Stub timing class
    ch.timing.set_pip_class("DEFAULT", "SITE_NULL", delay=TimingValue(20))
    # Stub bel timings
    lut = ch.timing.add_cell_variant("DEFAULT", "SLICE_LUTX")
    for i in range(1, 6+1):
        lut.add_comb_arc(f"A{i}", "O6", TimingValue(100, 125))
        if i <= 5: lut.add_comb_arc(f"A{i}", "O5", TimingValue(120, 150))
    for i in range(1, 8+1):
        lut.add_setup_hold("CLK", f"WA{i}", ClockEdge.RISING, TimingValue(100, 500), TimingValue(100, 200))
    lut.add_setup_hold("CLK", "WE", ClockEdge.RISING, TimingValue(100, 600), TimingValue(100, 100))
    lut.add_setup_hold("CLK", "DI1", ClockEdge.RISING, TimingValue(100, 100), TimingValue(100, 100))
    ff = ch.timing.add_cell_variant("DEFAULT", "SLICE_FFX")
    ff.add_setup_hold("CK", "CE", ClockEdge.RISING, TimingValue(100, 100), TimingValue(0, 0))
    ff.add_setup_hold("CK", "SR", ClockEdge.RISING, TimingValue(100, 100), TimingValue(0, 0))
    ff.add_setup_hold("CK", "D", ClockEdge.RISING, TimingValue(100, 100), TimingValue(200, 200))
    ff.add_clock_out("CK", "Q", ClockEdge.RISING, TimingValue(300, 350))
    # Import package pins
    for package_name, package in sorted(d.packages.items(), key=lambda x:x[0]):
        pkg = ch.create_package(package_name)
        for pin, site in sorted(package.pin_map.items(), key=lambda x:x[0]):
            site_data = d.sites_by_name[site]
            bel_name = gen_bel_name(site_data, "PAD")
            tile_type = ch.tile_type_at(site_data.tile.x, site_data.tile.y)
            pkg.create_pad(pin, f"X{site_data.tile.x}Y{site_data.tile.y}",
            f"{site_data.rel_name()}.{bel_name}",
            "", 0) # TODO: bank
    ch.write_bba(args.bba)

if __name__ == '__main__':
    main()

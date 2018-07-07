#!/usr/bin/env python3
import pytrellis
import database

location_types = dict()
type_at_location = dict()
tiletype_names = dict()


def is_global(loc):
    return loc.x == -2 and loc.y == -2


# Get the index for a tiletype
def get_tiletype_index(name):
    if name in tiletype_names:
        return tiletype_names[name]
    idx = len(tiletype_names)
    tiletype_names[name] = idx
    return idx


loc_wire_indices = dict()
loc_wires = dict()

loc_bels = dict()
wire_bel_pins_uphill = dict()
wire_bel_pins_downhill = dict()


# Import all wire names at all locations
def import_location_wires(rg, x, y):
    loc_wire_indices[x, y] = dict()
    loc_wires[x, y] = list()
    wire_bel_pins_uphill[x, y] = list()
    wire_bel_pins_downhill[x, y] = list()
    rtile = rg.tiles[pytrellis.Location(x, y)]
    for wire in rtile.wires:
        name = rg.to_str(wire.key())
        idx = len(loc_wires[x, y])
        loc_wires[x, y].append(name)
        loc_wire_indices[x, y][name] = idx
        wire_bel_pins_uphill[x, y].append([])
        wire_bel_pins_downhill[x, y].append([])


# Take a RoutingId from Trellis and make into a (relx, rely, name) tuple
def resolve_wirename(rg, rid, cur_x, cur_y):
    if is_global(rid.loc):
        return (cur_x, cur_y, rg.to_str(rid.id))
    else:
        x = rid.loc.x
        y = rid.loc.y
        widx = loc_wire_indices[x, y][rg.to_str(rid.id)]
        return (x - cur_x, y - cur_y, widx)


loc_arc_indices = dict()  # Map RoutingId index to nextpnr index
loc_arcs = dict()


# Import all arc indices at a location
def index_location_arcs(rg, x, y):
    loc_arc_indices[x, y] = dict()
    loc_arcs[x, y] = list()
    rtile = rg.tiles[pytrellis.Location(x, y)]
    for arc in rtile.arcs:
        idx = len(loc_arcs)
        trid = arc.key()
        loc_arcs[x, y].append(trid)
        loc_arc_indices[x, y][trid] = idx


def add_bel_input(bel_x, bel_y, bel_idx, bel_pin, wire_x, wire_y, wire_name):
    loc_bels[bel_x, bel_y][bel_idx][2].append((bel_pin, (wire_x, wire_y, loc_wire_indices[wire_x, wire_y][wire_name])))
    wire_bel_pins_downhill[wire_x, wire_y][loc_wire_indices[wire_x, wire_y][wire_name]].append((
        (bel_x, bel_y, bel_idx), bel_pin))


def add_bel_output(bel_x, bel_y, bel_idx, bel_pin, wire_x, wire_y, wire_name):
    loc_bels[bel_x, bel_y][bel_idx][2].append((bel_pin, (wire_x, wire_y, loc_wire_indices[wire_x, wire_y][wire_name])))
    wire_bel_pins_uphill[wire_x, wire_y][loc_wire_indices[wire_x, wire_y][wire_name]].append((
        (bel_x, bel_y, bel_idx), bel_pin))


def add_slice(x, y, z):
    idx = len(loc_bels[x, y])
    l = ("A", "B", "C", "D")[z]
    name = "SLICE" + l
    loc_bels[x, y].append((name, "SLICE", []))
    lc0 = z * 2
    lc1 = z * 2 + 1
    add_bel_input(x, y, idx, "A0", x, y, "A{}_SLICE".format(lc0))
    add_bel_input(x, y, idx, "B0", x, y, "B{}_SLICE".format(lc0))
    add_bel_input(x, y, idx, "C0", x, y, "C{}_SLICE".format(lc0))
    add_bel_input(x, y, idx, "D0", x, y, "D{}_SLICE".format(lc0))
    add_bel_input(x, y, idx, "M0", x, y, "M{}_SLICE".format(lc0))

    add_bel_input(x, y, idx, "A1", x, y, "A{}_SLICE".format(lc1))
    add_bel_input(x, y, idx, "B1", x, y, "B{}_SLICE".format(lc1))
    add_bel_input(x, y, idx, "C1", x, y, "C{}_SLICE".format(lc1))
    add_bel_input(x, y, idx, "D1", x, y, "D{}_SLICE".format(lc1))
    add_bel_input(x, y, idx, "M1", x, y, "M{}_SLICE".format(lc1))

    add_bel_input(x, y, idx, "FCI", x, y, "FCI{}_SLICE".format(l if z > 0 else ""))
    add_bel_input(x, y, idx, "FXA", x, y, "FXA{}_SLICE".format(l))
    add_bel_input(x, y, idx, "FXB", x, y, "FXB{}_SLICE".format(l))

    add_bel_input(x, y, idx, "CLK", x, y, "CLK{}_SLICE".format(z))
    add_bel_input(x, y, idx, "LSR", x, y, "LSR{}_SLICE".format(z))
    add_bel_input(x, y, idx, "CE", x, y, "CE{}_SLICE".format(z))

    add_bel_output(x, y, idx, "F0", x, y, "F{}_SLICE".format(lc0))
    add_bel_output(x, y, idx, "Q0", x, y, "Q{}_SLICE".format(lc0))

    add_bel_output(x, y, idx, "F1", x, y, "F{}_SLICE".format(lc1))
    add_bel_output(x, y, idx, "Q1", x, y, "Q{}_SLICE".format(lc1))

    add_bel_output(x, y, idx, "FCO", x, y, "FCO{}_SLICE".format(l if z < 3 else ""))


def add_pio(x, y, z):
    idx = len(loc_bels[x, y])
    l = ("A", "B", "C", "D")[z]
    name = "PIO" + l
    loc_bels[x, y].append((name, "PIO", []))
    add_bel_input(x, y, idx, "I", x, y, "PADDO{}_PIO".format(l))
    add_bel_input(x, y, idx, "T", x, y, "PADDT{}_PIO".format(l))
    add_bel_output(x, y, idx, "O", x, y, "JPADDI{}_PIO".format(l))


def add_bels(chip, x, y):
    loc_bels[x, y] = []
    tiles = chip.get_tiles_by_position(y, x)
    num_slices = 0
    num_pios = 0
    for tile in tiles:
        tt = tile.info.type
        if tt == "PLC2":
            num_slices = 4
        elif "PICL0" in tt or "PICR0" in tt:
            num_pios = 4
        elif "PIOT0" in tt or "PIOB0" in tt:
            num_pios = 2
    for i in range(num_slices):
        add_slice(x, y, i)
    for i in range(num_pios):
        add_pio(x, y, i)


# Import a location, deduplicating if appropriate
def import_location(rg, x, y):
    rtile = rg.tiles[pytrellis.Location(x, y)]
    arcs = []  # (src, dst, configurable, tiletype)
    wires = []  # (name, uphill, downhill, belpin_uphill, belpins_downhill)
    bels = []  # (name, [(pin, wire)])
    for name in loc_wires[x, y]:
        w = rtile.wires[rg.ident(name)]
        arcs_uphill = []
        arcs_downhill = []
        belpins_uphill = []
        belpins_downhill = []
        for uh in w.uphill:
            arcidx = loc_arc_indices[uh.loc.x, uh.loc.y][uh.id]
            arcs_uphill.append((uh.loc.x - x, uh.loc.y - y, arcidx))
        for dh in w.downhill:
            arcidx = loc_arc_indices[dh.loc.x, dh.loc.y][dh.id]
            arcs_downhill.append((dh.loc.x - x, dh.loc.y - y, arcidx))
        for bp in wire_bel_pins_uphill[x, y][loc_wire_indices[x, y][name]]:
            bel, pin = bp
            bel_x, bel_y, bel_idx = bel
            belpins_uphill.append(((bel_x - x, bel_y - y, bel_idx), pin))
        for bp in wire_bel_pins_downhill[x, y][loc_wire_indices[x, y][name]]:
            bel, pin = bp
            bel_x, bel_y, bel_idx = bel
            belpins_downhill.append(((bel_x - x, bel_y - y, bel_idx), pin))
        assert len(belpins_uphill) <= 1
        wires.append((name, tuple(arcs_downhill), tuple(arcs_uphill), tuple(belpins_uphill), tuple(belpins_downhill)))

    for bel in loc_bels[x, y]:
        name, beltype, pins = bel
        xformed_pins = tuple((p[0], (p[1][0] - x, p[1][1] - y, p[1][2])) for p in pins)
        bels.append((name, beltype, xformed_pins))

    for arcidx in loc_arcs[x, y]:
        a = rtile.arcs[arcidx]
        source_wire = resolve_wirename(rg, a.source, x, y)
        dest_wire = resolve_wirename(rg, a.sink, x, y)
        arcs.append((source_wire, dest_wire, a.configurable, get_tiletype_index(rg.to_str(a.tiletype))))

    tile_data = (tuple(wires), tuple(arcs), tuple(bels))
    if tile_data in location_types:
        type_at_location[x, y] = location_types[tile_data]
    else:
        idx = len(location_types)
        location_types[tile_data] = idx
        type_at_location[x, y] = idx


class BinaryBlobAssembler:
    def __init__(self, cname, endianness, nodebug=False):
        assert endianness in ["le", "be"]
        self.cname = cname
        self.endianness = endianness
        self.finalized = False
        self.data = bytearray()
        self.comments = dict()
        self.labels = dict()
        self.exports = set()
        self.labels_byaddr = dict()
        self.ltypes_byaddr = dict()
        self.strings = dict()
        self.refs = dict()
        self.nodebug = nodebug

    def l(self, name, ltype=None, export=False):
        assert not self.finalized
        assert name not in self.labels
        assert len(self.data) not in self.labels_byaddr
        self.labels[name] = len(self.data)
        if ltype is not None:
            self.ltypes_byaddr[len(self.data)] = ltype
        self.labels_byaddr[len(self.data)] = name
        if export:
            assert ltype is not None
            self.exports.add(len(self.data))

    def r(self, name, comment):
        assert not self.finalized
        assert len(self.data) % 4 == 0
        assert len(self.data) not in self.refs
        if self.nodebug:
            comment = None
        if name is not None:
            self.refs[len(self.data)] = (name, comment)
        self.data.append(0)
        self.data.append(0)
        self.data.append(0)
        self.data.append(0)
        if (name is None) and (comment is not None):
            self.comments[len(self.data)] = comment + " (null reference)"

    def s(self, s, comment):
        assert not self.finalized
        if self.nodebug:
            comment = None
        if s not in self.strings:
            index = len(self.strings)
            self.strings[s] = index
        else:
            index = self.strings[s]
        if comment is not None:
            self.r("str%d" % index, '%s: "%s"' % (comment, s))
        else:
            self.r("str%d" % index, None)

    def u8(self, v, comment):
        assert not self.finalized
        if self.nodebug:
            comment = None
        self.data.append(v)
        if comment is not None:
            self.comments[len(self.data)] = comment

    def u16(self, v, comment):
        assert not self.finalized
        assert len(self.data) % 2 == 0
        if self.nodebug:
            comment = None
        if self.endianness == "le":
            self.data.append(v & 255)
            self.data.append((v >> 8) & 255)
        elif self.endianness == "be":
            self.data.append((v >> 8) & 255)
            self.data.append(v & 255)
        else:
            assert 0
        if comment is not None:
            self.comments[len(self.data)] = comment

    def s16(self, v, comment):
        assert not self.finalized
        assert len(self.data) % 2 == 0
        if self.nodebug:
            comment = None
        c2val = (~v + 1) if v < 0 else v
        if self.endianness == "le":
            self.data.append(c2val & 255)
            self.data.append((c2val >> 8) & 255)
        elif self.endianness == "be":
            self.data.append((c2val >> 8) & 255)
            self.data.append(c2val & 255)
        else:
            assert 0
        if comment is not None:
            self.comments[len(self.data)] = comment

    def u32(self, v, comment):
        assert not self.finalized
        assert len(self.data) % 4 == 0
        if self.nodebug:
            comment = None
        if self.endianness == "le":
            self.data.append(v & 255)
            self.data.append((v >> 8) & 255)
            self.data.append((v >> 16) & 255)
            self.data.append((v >> 24) & 255)
        elif self.endianness == "be":
            self.data.append((v >> 24) & 255)
            self.data.append((v >> 16) & 255)
            self.data.append((v >> 8) & 255)
            self.data.append(v & 255)
        else:
            assert 0
        if comment is not None:
            self.comments[len(self.data)] = comment

    def finalize(self):
        assert not self.finalized
        for s, index in self.strings.items():
            self.l("str%d" % index, "char")
            for c in s:
                self.data.append(ord(c))
            self.data.append(0)
        self.finalized = True
        cursor = 0
        while cursor < len(self.data):
            if cursor in self.refs:
                v = self.labels[self.refs[cursor][0]] - cursor
                if self.endianness == "le":
                    self.data[cursor + 0] = (v & 255)
                    self.data[cursor + 1] = ((v >> 8) & 255)
                    self.data[cursor + 2] = ((v >> 16) & 255)
                    self.data[cursor + 3] = ((v >> 24) & 255)
                elif self.endianness == "be":
                    self.data[cursor + 0] = ((v >> 24) & 255)
                    self.data[cursor + 1] = ((v >> 16) & 255)
                    self.data[cursor + 2] = ((v >> 8) & 255)
                    self.data[cursor + 3] = (v & 255)
                else:
                    assert 0
                cursor += 4
            else:
                cursor += 1

    def write_verbose_c(self, f, ctype="const unsigned char"):
        assert self.finalized
        print("%s %s[%d] = {" % (ctype, self.cname, len(self.data)), file=f)
        cursor = 0
        bytecnt = 0
        while cursor < len(self.data):
            if cursor in self.comments:
                if bytecnt == 0:
                    print(" ", end="", file=f)
                print(" // %s" % self.comments[cursor], file=f)
                bytecnt = 0
            if cursor in self.labels_byaddr:
                if bytecnt != 0:
                    print(file=f)
                if cursor in self.exports:
                    print("#define %s ((%s*)(%s+%d))" % (
                    self.labels_byaddr[cursor], self.ltypes_byaddr[cursor], self.cname, cursor), file=f)
                else:
                    print("  // [%d] %s" % (cursor, self.labels_byaddr[cursor]), file=f)
                bytecnt = 0
            if cursor in self.refs:
                if bytecnt != 0:
                    print(file=f)
                print(" ", end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor + 0]), end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor + 1]), end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor + 2]), end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor + 3]), end="", file=f)
                print(" // [%d] %s (reference to %s)" % (cursor, self.refs[cursor][1], self.refs[cursor][0]), file=f)
                bytecnt = 0
                cursor += 4
            else:
                if bytecnt == 0:
                    print(" ", end="", file=f)
                print(" %-4s" % ("%d," % self.data[cursor]), end=("" if bytecnt < 15 else "\n"), file=f)
                bytecnt = (bytecnt + 1) & 15
                cursor += 1
        if bytecnt != 0:
            print(file=f)
        print("};", file=f)

    def write_compact_c(self, f, ctype="const unsigned char"):
        assert self.finalized
        print("%s %s[%d] = {" % (ctype, self.cname, len(self.data)), file=f)
        column = 0
        for v in self.data:
            if column == 0:
                print("  ", end="", file=f)
                column += 2
            s = "%d," % v
            print(s, end="", file=f)
            column += len(s)
            if column > 75:
                print(file=f)
                column = 0
        if column != 0:
            print(file=f)
        for cursor in self.exports:
            print("#define %s ((%s*)(%s+%d))" % (
            self.labels_byaddr[cursor], self.ltypes_byaddr[cursor], self.cname, cursor), file=f)
        print("};", file=f)

    def write_uint64_c(self, f, ctype="const uint64_t"):
        assert self.finalized
        print("%s %s[%d] = {" % (ctype, self.cname, (len(self.data) + 7) // 8), file=f)
        column = 0
        for i in range((len(self.data) + 7) // 8):
            v0 = self.data[8 * i + 0] if 8 * i + 0 < len(self.data) else 0
            v1 = self.data[8 * i + 1] if 8 * i + 1 < len(self.data) else 0
            v2 = self.data[8 * i + 2] if 8 * i + 2 < len(self.data) else 0
            v3 = self.data[8 * i + 3] if 8 * i + 3 < len(self.data) else 0
            v4 = self.data[8 * i + 4] if 8 * i + 4 < len(self.data) else 0
            v5 = self.data[8 * i + 5] if 8 * i + 5 < len(self.data) else 0
            v6 = self.data[8 * i + 6] if 8 * i + 6 < len(self.data) else 0
            v7 = self.data[8 * i + 7] if 8 * i + 7 < len(self.data) else 0
            if self.endianness == "le":
                v = v0 << 0
                v |= v1 << 8
                v |= v2 << 16
                v |= v3 << 24
                v |= v4 << 32
                v |= v5 << 40
                v |= v6 << 48
                v |= v7 << 56
            elif self.endianness == "be":
                v = v7 << 0
                v |= v6 << 8
                v |= v5 << 16
                v |= v4 << 24
                v |= v3 << 32
                v |= v2 << 40
                v |= v1 << 48
                v |= v0 << 56
            else:
                assert 0
            if column == 3:
                print(" 0x%016x," % v, file=f)
                column = 0
            else:
                if column == 0:
                    print(" ", end="", file=f)
                print(" 0x%016x," % v, end="", file=f)
                column += 1
        if column != 0:
            print("", file=f)
        print("};", file=f)

    def write_string_c(self, f, ctype="const char"):
        assert self.finalized
        assert self.data[len(self.data) - 1] == 0
        print("%s %s[%d] =" % (ctype, self.cname, len(self.data)), file=f)
        print("  \"", end="", file=f)
        column = 0
        for i in range(len(self.data) - 1):
            if (self.data[i] < 32) or (self.data[i] > 126):
                print("\\%03o" % self.data[i], end="", file=f)
                column += 4
            elif self.data[i] == ord('"') or self.data[i] == ord('\\'):
                print("\\" + chr(self.data[i]), end="", file=f)
                column += 2
            else:
                print(chr(self.data[i]), end="", file=f)
                column += 1
            if column > 70 and (i != len(self.data) - 2):
                print("\"\n  \"", end="", file=f)
                column = 0
        print("\";", file=f)

    def write_binary(self, f):
        assert self.finalized
        assert self.data[len(self.data) - 1] == 0
        f.buffer.write(self.data)


def write_database(bba):
    def write_loc(x, y, sym_name):
        bba.s16(x, "%s_x" % sym_name)
        bba.s16(y, "%s_y" % sym_name)

    for loctype, idx in sorted(location_types.items(), key=lambda x: x[1]):
        wires, arcs, bels = loctype
        bba.l("loc%d_pips" % idx, "PipInfoPOD")
        for arc in arcs:
            src_wire, dst_wire, configurable, tile_type = arc
            write_loc(src_wire[0], src_wire[1], "src")
            write_loc(dst_wire[0], dst_wire[1], "dst")
            bba.u32(src_wire[2], "src_idx")
            bba.u32(dst_wire[2], "dst_idx")
            bba.u32(1, "delay")  # TODO:delay
            bba.u16(tile_type, "tile_type")
            bba.u8(1 if not configurable else 0, "pip_type")
            bba.u8(0, "padding")
        for wire_idx in range(len(wires)):
            wire = wires[wire_idx]
            name, downpips, uppips, downbels, upbels = wire
            if len(downpips) > 0:
                bba.l("loc%d_wire%d_downpips" % (idx, wire_idx), "PipLocatorPOD")
                for dp in downpips:
                    write_loc(dp[0], dp[1], "rel_loc")
                    bba.u32(dp[2], "idx")
            if len(uppips) > 0:
                bba.l("loc%d_wire%d_uppips" % (idx, wire_idx), "PipLocatorPOD")
                for up in uppips:
                    write_loc(up[0], up[1], "rel_loc")
                    bba.u32(up[2], "idx")


def main():
    pytrellis.load_database(database.get_db_root())
    print("Initialising chip...")
    chip = pytrellis.Chip("LFE5U-25F")
    print("Building routing graph...")
    rg = chip.get_routing_graph()
    max_row = chip.get_max_row()
    max_col = chip.get_max_col()
    print("Indexing wires...")
    for y in range(0, max_row + 1):
        for x in range(0, max_col + 1):
            import_location_wires(rg, x, y)
    print("Indexing arcs...")
    for y in range(0, max_row + 1):
        for x in range(0, max_col + 1):
            index_location_arcs(rg, x, y)
    print("Adding bels...")
    for y in range(0, max_row + 1):
        for x in range(0, max_col + 1):
            add_bels(chip, x, y)
    print("Importing tiles...")
    for y in range(0, max_row + 1):
        for x in range(0, max_col + 1):
            print("     At R{}C{}".format(y, x))
            import_location(rg, x, y)


if __name__ == "__main__":
    main()

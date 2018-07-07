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

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

# Import all wire names at all locations
def import_location_wires(rg, x, y):
    loc_wire_indices[x, y] = dict()
    loc_wires[x, y] = list()
    rtile = rg.tiles[pytrellis.Location(x, y)]
    for wire in rtile.wires:
        name = rg.to_str(wire.key())
        idx = len(loc_wires[x, y])
        loc_wires[x, y].append(name)
        loc_wire_indices[x, y][name] = idx


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

# Import a location, deduplicating if appropriate
def import_location(rg, x, y):
    rtile = rg.tiles[pytrellis.Location(x, y)]
    arcs = []  # (src, dst, configurable, tiletype)
    wires = []  # (name, uphill, downhill, belpin_uphill, belpins_downhill)
    bels = []  # (name, (pin, wire))
    for name in loc_wires[x, y]:
        w = rtile.wires[rg.ident(name)]
        arcs_uphill = []
        arcs_downhill = []
        for uh in w.uphill:
            arcidx = loc_arc_indices[uh.loc.x, uh.loc.y][uh.id]
            arcs_uphill.append((uh.loc.x - x, uh.loc.y - y, arcidx))
        for dh in w.downhill:
            arcidx = loc_arc_indices[dh.loc.x, dh.loc.y][dh.id]
            arcs_downhill.append((dh.loc.x - x, dh.loc.y - y, arcidx))
        # TODO: Bel pins
        wires.append((name, tuple(arcs_downhill), tuple(arcs_uphill), tuple(), tuple()))

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
    chip = pytrellis.Chip("LFE5U-45F")
    print("Building routing graph...")
    rg = chip.get_routing_graph()
    max_row = chip.get_max_row()
    max_col = chip.get_max_col()
    print("Indexing wires...")
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            import_location_wires(rg, x, y)
    print("Indexing arcs...")
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            index_location_arcs(rg, x, y)
    print("Importing tiles...")
    for y in range(0, max_row+1):
        for x in range(0, max_col+1):
            print("     At R{}C{}".format(y, x))
            import_location(rg, x, y)

if __name__ == "__main__":
    main()

# Himbächel - a series of bigger arches

[Viaduct](./viaduct.md) enables custom architectures to be easily prototyped using a C++ API to build in-memory databases; with most of the flexibility of nextpnr's validity checking capabilities to hand. This is the recommended way to get started quickly with FPGAs up to about 20k logic elements, where no other particularly unusal requirements exist.

However, building the routing graph in-memory at every startup and storing it flat doesn't scale at all well with larger FPGAs (say, 100k LEs or bigger).
So, we take advantage of nextpnr's support for complex, non-flat routing graph structures and define a deduplication approach that's designed to work better even for very large fabrics - the database size is unlikely to exceed about 100MB even for million-LUT scale devices, compared to multiple gigabytes for a flat database.

Python scripting is defined that allows the user to describe a semi-flattened routing graph and build the deduplicated database binary _at compile time_. Pips - routing switches - are described per tile type rather than flat; however, the connectivity between tiles ("nodes") are described flat and automatically deduplicated during database build.

## Getting Started

Most of what's written in the [viaduct docs](./viaduct.md) also applies to bootstrapping a Himbächel arch - this also provides a migration path for an existing Viaduct architecture. Just replace `viaduct` with `himbaechel` and `ViaductAPI` with `HimbaechelAPI` - the set of validity checking and custom flow "hooks" that you have access to is designed to be otherwise as close as possible.

However, the key difference is that you will need to generate a "binary blob" chip database. `himbaechel_dbgen/bba.py` provides a framework for this. The typical steps for using this API would be as follows:
 - Create a `Chip` instance
 - For each unique "tile type" in the design (e.g. logic, BRAM, IO - in some cases multiple variants of these may be multiple tile types):
     - Create it using `Chip.create_tile_type`
     - Add local wires (connectivity between tiles is dealt with later) using `TileType.create_wire`
     - Add bels (like LUTs and FFs) using `TileType.create_bel`, and pins to those bels using `TileType.add_bel_pin`
     - Add pips using `TileType.create_pip`
 - For each grid location, use `Chip.set_tile_type` to set its tile type. Every location must have a tile type set, even if it's just an empty "NULL" tile
 - Whenever wires span multiple tiles (i.e. all wires with a length greater than zero), combine the per-tile local wires into a single node using `Chip.add_node` for each case. 
 - Write out the `.bba` file using `Chip.write_bba`
 - Compile it into a binary that nextpnr can load using `./bba/bbasm --l my_chipdb.bba my_chipdb.bin`

An example Python generator to copy from is located in `uarch/example/example_arch_gen.py`.

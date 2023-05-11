# nextpnr Coding Notes

This document aims to provide an overview into the philosophy behind nextpnr's codebase and some tips and tricks for developers.

## See also

 - [FAQ](faq.md) - overview of terminology
 - [Arch API](archapi.md) - reference for the using and implementing the Architecture API
 - [Netlist Structure](netlist.md) - reference for the netlist data structures
 - [Python API](python.md) - Python API overview for netlist access, constraints, etc
 - [Generic Architecture](generic.md) - using the Python API to create architectures

## nextpnr Architectures

An architecture in nextpnr is described first and foremost as code. The exact details are given in the [Arch API reference](archapi.md); this aims to explain the core concept.

By choosing this approach; this gives architectures significant flexibility to use more advanced database representations than a simple flat database - for example deduplicated approaches that store similar tiles only once.

Architectures can also implement custom algorithms for packing (or other initial netlist transformations) and specialized parts of placement and routing such as global clock networks. This is because architectures provide the `pack()`, `place()` and `route()` functions, although the latter two will normally use generic algorithms (such as HeAP and router1) to do most of the work.

Another important function provided by architectures is placement validity checking. This allows the placer to check whether or not a given cell placement is valid. An example of this is for iCE40, where 8 logic cells in a tile share one clock signal - this is checked here.

This function allows architectures in nextpnr to do significantly less packing than in traditional FPGA CAD flows. Although `pack()` could pack entire tiles, it is more idiomatic to pack to logic cells (e.g. single LUT and FF or two LUTFFs) or even smaller units like the LUTs and FFs themselves (in this case all the "packer" would do is convert LUTs and FFs to a common type if needed.)

Additionally to this; architectures provide functions for checking the availability and conflicts between resources (e.g. `checkBelAvail`, `checkPipAvail`, etc). This enables arbitrary constraints between resource availability to be defined, for example:

 - where a group of pips share bitstream bits, only one can be used at a time
 - Pips that represent LUT permutation are not available when the LUT is in memory mode
 - only a certain total number of pips in a switchbox can be used at once due to power supply limitations

As well as implementing all of the standard [Arch API](archapi.md) functions; arches may include their own helper functions for various purposes. By convention these are in `snake_case`, to distinguish them from the `camelCase` Arch API functions.

## `IdString`s

To avoid the high cost of using strings as identifiers directly; almost all "string" identifiers in nextpnr (such as cell names and types) use an indexed string pool type named `IdString`. Unlike Yosys, which has a global garbage collected pool, nextpnr has a per-Context pool without any garbage collection.

`IdString`s can be created in two ways. Architectures can add `IdString`s with constant indices - allowing `IdString` constants to be provided too - using `initialize_add` at startup. See how `constids.inc` is used in iCE40 for an example of this. The main way to create `IdString`s, however, is at runtime using the `id` member function of `BaseCtx` given the string to create from (if an `IdString` of that string already exists, the existing `IdString` will be returned).

Note that `IdString`s need a `Context` (or `BaseCtx`) pointer to convert them back to regular strings, due to the pool being per-context as described above.

## Developing CAD algorithms - packing

Packing in nextpnr could be done in two ways (if significant packing is done at all):
 - replacing multiple cells with a single larger cell that corresponds to a bel
 - combining smaller cells using relative placement constraints

The packer will also have to add relative constraints for fixed structures such as carry chains or LUT-MUX cascades.

There are several helper functions that are useful for developing packers and other passes that perform significant netlist modifications in `util.h`. It is often preferable to use these compared to direct modification of the netlist structures due to the "double linking" nextpnr does - e.g. when connecting a port you must update both the `net` field of the ports and the `driver`/`users` of the net.

### Cell to bel mapping

There is an Arch API choice when it comes to representing the relationship
between cell types and bel types.  One option is to transform cells into
common types that correspond to a bel (e.g. convert multiple flipflop
primitives to a single common type with some extra parameters). In Arch APIs
designed like this, packer transformations are required to convert input cell
types into nextpnr specific cell types that have a 1 to 1 relationship with
bel types.

For Arch APIs of this type, the method `isValidBelForCellType` reduces to:

```
bool isValidBelForCellType(IdString cell_type, BelId bel) const {
    return cell_type == getBelType(bel);
}
```

The alternative is to implement a fast `isValidBelForCellType` method that
determines if this cell type can be bound to this bel.

## Developing CAD algorithms - placement

The job of the placer in nextpnr is to find a suitable bel for each cell in the design; while respecting legality and relative constraints.

Placers might want to create their own indices of bels (for example, bels by type and location) to speed up the search.

As nextpnr allows arbitrary constraints on bels for more advanced packer-free flows and complex real-world architectures; placements must be checked for legality using `isBelLocationValid` (after placement) and the placement rejected if invalid. For analytical placement algorithms; after creating a spread-out AP solution the legality of placing each cell needs to be checked. In practice, the cost of this is fairly low as the architecture should ensure these functions are as fast as possible.

There are several routes for timing information in the placer:
 - sink ports can have a criticality (value between 0 and 1 where 1 is the critical path) associated with them by using `get_criticalities` and a `NetCriticalityMap`
 - `predictDelay` and its derivative `predictArcDelay` returns an estimated delay for a sink port based on placement information


### Bel Buckets

The bel Bucket Arch APIs can be used by an analytical placer (AP) for getting
groups of bels and cell types placed together.  This grouping is important for
algorithms like HeAP which typically want to do operate on subsets of the
design for some portions of the placement.

The HeAP implementation allows for multiple bel buckets to be placed on
together, see the "cellGroups" field.

## Routing

The job of the router is to ensure that the `wires` map for each net contains a complete routing tree; populated using the Arch functions to bind wires and pips. The ripup invariants in the [FAQ](faq.md) are important to bear in mind; as there may be complex constraints on the usage of wires and pips in some architectures.

`estimateDelay` is intended for use as an A* metric to guide routing.

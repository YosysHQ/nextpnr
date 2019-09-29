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

 - where a group of Pips share bitstream bits, only one can be used at a time
 - Pips that represent LUT permutation are not available when the LUT is in memory mode
 - only a certain total number of Pips in a switchbox can be used at once due to power supply limitations

## `IdString`s

To avoid the high cost of using strings as identifiers directly; almost all "string" identifiers in nextpnr (such as cell names and types) use an indexed string pool type named `IdString`. Unlike Yosys, which has a global garbage collected pool, nextpnr has a per-Context pool without any garbage collection. 

`IdString`s can be created in two ways. Architectures can add `IdString`s with constant indicies - allowing `IdString` constants to be provided too - using `initialize_add` at startup. See how `constids.inc` is used in iCE40 for an example of this. The main way to create `IdString`s, however, is at runtime using the `id` member function of `BaseCtx` given the string to create from (if an `IdString` of that string already exists, the existing `IdString` will be returned).

Note that `IdString`s need a `Context` (or `BaseCtx`) pointer to convert them back to regular strings, due to the pool being per-context as described above.

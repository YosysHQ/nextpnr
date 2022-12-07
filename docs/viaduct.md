# Viaduct - a series of small arches

Viaduct is a C++-based successor to the Python generic API that gets most of the benefits of a full-custom nextpnr architecture, with the simplicity of a harness to build from and a predefined flat set of data structures for the placement and routing resources.

Like the Python generic API, the routing graph can be built programmatically, or loaded from an external data source at startup. However, the Viaduct framework provides considerably improved startup times by relying less on strings and eliminating the C++/Python boundary; and also enables more complex architectures to be modeled with arbitrary place-and-route time constraints implemented as code, in the spirit of nextpnr.

A Viaduct implementation is called a 'uarch' (microarch), because it's smaller than a full architecture.

Viaduct implementations, including some examples, are located as subfolders of `generic/viaduct/`.

## Viaduct API Reference

### Initialisation

A Viaduct uarch must override `ViaductAPI` - see `generic/viaduct_api.h`. This contains virtual methods to be optionally overridden, in most cases only a small number of these need be.

```c++
void init(Context *ctx)
```

This should perform device resources initialisation. uarches should always call the superclass `ViaductAPI::init(ctx)` first, too.

Bels (placement locations), wires ('metal' interconnect) and pips (programmable switches that connect wires) can be dynamically created by calling the methods of `Context` described in the [generic arch docs](coding.md) - the most important methods to start with are:

```c++
ctx->addWire(IdStringList name, IdString type, int x, int y);
ctx->addPip(IdStringList name, IdString type, WireId srcWire, WireId dstWire, float delay, Loc loc);
ctx->addBel(IdStringList name, IdString type, Loc loc, bool gb, bool hidden);
ctx->addBelInput(BelId bel, IdString name, WireId wire);
ctx->addBelOutput(BelId bel, IdString name, WireId wire);
ctx->addBelInout(BelId bel, IdString name, WireId wire);
```

### Helpers

nextpnr uses an indexed, interned string type for performance and object names (for bels, wires and pips) are based on lists of these. To performantly build these; you can add a `ViaductHelpers` instance to your uarch, call `init(ctx)` on it, and then use the `xy_id(x, y, base)` member functions of this. For example:

```c++
ViaductHelpers h;
h.init(ctx);
ctx->addWire(h.xy_id(13, 45, ctx->id("CLK0")), ctx->id("CLK"), 13, 45);
```

To create a wire named `X13/Y45/CLK0`.

### Constant IDs

In some cases, such as during packing and validity checks, `IdString`s for strings such as common port names will be needed a large number of times. To avoid the string hash and compare associated with `ctx->id("string")`, you can use the "constids" support. To use this:

 - create a 'constids.inc' file in your uarch folder containing one ID per line; inside X( ). For example:
```
X(LUT4)
X(DFF)
X(CLK)
X(D)
X(F)
```
  -  set the `VIADUCT_CONSTIDS` macro to the path to this file relative to the generic arch base
  -  in the same file as `init` is implemented; also define the `GEN_INIT_CONSTIDS` macro before the `viaduct_constids.h` include to create `init_uarch_constids`, which you should call in your `init` implementation.
  -  in any file you need the constant `IdString`s, include `viaduct_constids.h` and the ids will be accessible as constants named like `id_LUT4`.

### Constraints

```c++
bool checkBelAvail(BelId bel) const;
bool isValidBelForCellType(IdString cell_type, BelId bel) const;
bool isBelLocationValid(BelId bel, bool explain_invalid = false) const;
bool checkWireAvail(WireId wire) const;
bool checkPipAvail(PipId pip) const;
bool checkPipAvailForNet(PipId pip, NetInfo *net) const;
```

These can be overriden, if needed to implement nextpnr's system of arbitrary,architecture-defined constraints on legal placements and the availability of placement and routing resources. These could be used to implement placement rules inside tiles (like clocks that are shared between flipflops); or disable one routing resource when a conflicting one is used. They only need to be overriden, and return false, where a resource is unavailable due to a specific, custom constraint and not just because that resource itself is occupied.

For more information on terminology, see [FAQ](faq.md); for references of these functions see the [Arch API](archapi.md) docs; and for some general hints see the [Coding Tips](coding.md).

uarches may update internal, constraint-related structures based on placement and routing updates by optionally overriding the 'hook' functions called whenever bindings are changed.

```c++
void notifyBelChange(BelId bel, CellInfo *cell);
void notifyWireChange(WireId wire, NetInfo *net);
void notifyPipChange(PipId pip, NetInfo *net);
```

These will be called with `cell` or `net` pointing to the object the resource is being bound to for a bind; or `nullptr` for an unbind.

### Packing

Although arches can implement as much or as little packing as they like, nextpnr leans towards doing minimal pre-placement packing and leaving the combination of LUTs and flipflops into tiles, and similar tasks, down to placement validity checks (`checkBelAvail`).

Any packing tasks that do need to be done; for example cleaning up top level IO pairing cells that should always stay together using relative constraints, should be done by overriding the `pack` method:

```c++
void pack();
```


There are also hooks to perform custom transformations or steps in-between and after placement and routing:

```c++
void prePlace();
void postPlace();
void preRoute();
void postRoute();
```

The most common use for this would be to implement a custom bitstream generation step (or similar intermediate format) inside `postRoute` on the final design. Another example use case would be to implement a custom global clock routing pass in `preRoute`.

### ViaductArch

As well as creating the uarch class that derives from `ViaductAPI`, you also need to create a factory for it by creating a singleton of a class that derives from `ViaductArch`. This should, in its constructor, construct `ViaductArch` with the arch name, and also implement the `create` function to return a new instance of your `ViaductAPI` implementation. For example:

```c++
struct ExampleArch : ViaductArch
{
    ExampleArch() : ViaductArch("example"){};
    std::unique_ptr<ViaductAPI> create(const dict<std::string, std::string> &args)
    {
        return std::make_unique<ExampleImpl>();
    }
} exampleArch;
```

### Adding a new uarch

The reference above provides an overview of what a Viaduct uarch must implement, it's also recommended to look at the `generic` and `okami` examples in `generic/viaduct`. New uarches should have their source contained in subfolders of `generic/viaduct`; and added to `VIADUCT_UARCHES` list in `generic/family.cmake`.

Once you've implemented `ViaductAPI` and created the `ViaductArch` singleton, you should be able to run nextpnr with the arch by running `nextpnr-generic --uarch <name>`.




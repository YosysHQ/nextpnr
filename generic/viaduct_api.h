/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifndef VIADUCT_API_H
#define VIADUCT_API_H

#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

/*
Viaduct -- a series of small arches

Viaduct is a framework that provides an 'inbetween' step between nextpnr-generic
using Python bindings and a full-custom arch.

It allows an arch to programmatically build a set of bels (placement locations)
and a routing graph in-memory at startup; and then hook into nextpnr's flow
and validity checking rules at runtime with custom C++ code.

To create a Viaduct 'uarch', the following are required:
 - an implementation of ViaductAPI. At a minimum; you will need to use ctx->addBel, ctx->addWire and ctx->addPip to
create the graph of placement and routing resources in-memory. Also implement any placement validity checking required -
like rules for how LUTs and FFs can be placed together in a SLICE.
 - an instance of a struct deriving from ViaductArch - this is how the uarch is discovered. Override create(args) to
create an instance of your ViaductAPI implementation.
 - these should be within C++ files in a new subfolder of 'viaduct'. Add the name of this subfolder to the list of
VIADUCT_UARCHES in family.cmake if building in-tree.

For an example of how these pieces fit together; see 'viaduct/example' which implements a small synthetic architecture
using this framework.

*/

struct Context;

struct ViaductAPI
{
    virtual void init(Context *ctx);
    Context *ctx;
    bool with_gui = false;

    // --- Bel functions ---
    // Called when a bel is placed/unplaced (with cell=nullptr for a unbind)
    virtual void notifyBelChange(BelId bel, CellInfo *cell) {}
    // This only needs to return false if a bel is disabled for a microarch-specific reason and not just because it's
    // bound (which the base generic will deal with)
    virtual bool checkBelAvail(BelId bel) const { return true; }
    // Mirror the ArchAPI functions - see archapi.md
    virtual std::vector<IdString> getCellTypes() const;
    virtual BelBucketId getBelBucketForBel(BelId bel) const;
    virtual BelBucketId getBelBucketForCellType(IdString cell_type) const;
    virtual bool isValidBelForCellType(IdString cell_type, BelId bel) const;
    virtual bool isBelLocationValid(BelId bel, bool explain_invalid = false) const { return true; }

    // --- Wire and pip functions ---
    // Called when a wire/pip is placed/unplaced (with net=nullptr for a unbind)
    virtual void notifyWireChange(WireId wire, NetInfo *net) {}
    virtual void notifyPipChange(PipId pip, NetInfo *net) {}
    // These only need to return false if a wire/pip is disabled for a microarch-specific reason and not just because
    // it's bound (which the base arch will deal with)
    virtual bool checkWireAvail(WireId wire) const { return true; }
    virtual bool checkPipAvail(PipId pip) const { return true; }
    virtual bool checkPipAvailForNet(PipId pip, const NetInfo *net) const { return checkPipAvail(pip); };

    // --- Route lookahead ---
    virtual delay_t estimateDelay(WireId src, WireId dst) const;
    virtual delay_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const;
    virtual BoundingBox getRouteBoundingBox(WireId src, WireId dst) const;

    // --- Flow hooks ---
    virtual void pack(){}; // replaces the pack function
    // Called before and after main placement and routing
    virtual void prePlace(){};
    virtual void postPlace(){};
    virtual void preRoute(){};
    virtual void postRoute(){};

    virtual ~ViaductAPI(){};
};

struct ViaductArch
{
    static ViaductArch *list_head;
    ViaductArch *list_next = nullptr;

    std::string name;
    ViaductArch(const std::string &name);
    ~ViaductArch(){};
    virtual std::unique_ptr<ViaductAPI> create(const dict<std::string, std::string> &args) = 0;

    static std::string list();
    static std::unique_ptr<ViaductAPI> create(const std::string &name, const dict<std::string, std::string> &args);
};

NEXTPNR_NAMESPACE_END

#endif

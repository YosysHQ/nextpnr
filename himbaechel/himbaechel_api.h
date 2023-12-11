/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-23  gatecat <gatecat@ds0.me>
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

#ifndef HIMBAECHEL_API_H
#define HIMBAECHEL_API_H

#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

/*
Himbaechel -- a series of bigger arches

Himbaechel extends on the existing Viaduct API in nextpnr-generic for smaller, lower-impact architectures by a
deduplicated BBA chipdb format as well as API hooks more suited to such size and complexity devices.

It allows an arch to programmatically build a set of bels (placement locations) and a routing graph at compile time into
a space-efficient (both disk and runtime RAM) deduplicated database with fast lookups; and then hook into nextpnr's flow
and validity checking rules at runtime with custom C++ code.

To create a Himbaechel 'uarch', the following are required:
 - an implementation of HimbaechelAPI. This should define things like rules for how LUTs and FFs can be placed together
in a SLICE.
 - "ahead-of-time" Python scripts to programmatically build a routing graph for the device as well as list of placement
locations, in a way that will become space-efficient
 - an instance of a struct deriving from HimbaechelArch - this is how the uarch is discovered. Override create(args) to
create an instance of your HimabaechelAPI implementation.
 - these should be within C++ files in a new subfolder of 'himbaechel/uarches/'. Add the name of this subfolder to the
list of HIMBAECHEL_UARCHES in family.cmake if building in-tree.

For an example of how these pieces fit together; see 'himbaechel/uarches/example' which implements a small synthetic
architecture using this framework.

*/

struct Arch;
struct Context;

struct PlacerHeapCfg;

struct HimbaechelAPI
{
    virtual void init(Context *ctx);
    // If constids are being used, this is used to set them up early
    // then it is responsible for loading the db blob with arch->load_chipdb()
    virtual void init_database(Arch *arch) = 0;
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

    // Cell->bel pin mapping
    virtual bool map_cell_bel_pins(CellInfo *cell) const { return false; }

    // Cluster
    virtual CellInfo *getClusterRootCell(ClusterId cluster) const;
    virtual BoundingBox getClusterBounds(ClusterId cluster) const;
    virtual Loc getClusterOffset(const CellInfo *cell) const;
    virtual bool isClusterStrict(const CellInfo *cell) const;
    virtual bool getClusterPlacement(ClusterId cluster, BelId root_bel,
                                     std::vector<std::pair<CellInfo *, BelId>> &placement) const;
    // --- Flow hooks ---
    virtual void pack(){}; // replaces the pack function
    // Called before and after main placement and routing
    virtual void prePlace(){};
    virtual void postPlace(){};
    virtual void preRoute(){};
    virtual void postRoute(){};

    // For custom placer configuration
    virtual void configurePlacerHeap(PlacerHeapCfg &cfg){};

    virtual ~HimbaechelAPI(){};
};

struct HimbaechelArch
{
    static HimbaechelArch *list_head;
    HimbaechelArch *list_next = nullptr;

    std::string name;
    HimbaechelArch(const std::string &name);
    ~HimbaechelArch(){};
    virtual bool match_device(const std::string &device) = 0;
    virtual std::unique_ptr<HimbaechelAPI> create(const std::string &device,
                                                  const dict<std::string, std::string> &args) = 0;

    static std::string list();
    static HimbaechelArch *find_match(const std::string &device);
};

NEXTPNR_NAMESPACE_END

#endif

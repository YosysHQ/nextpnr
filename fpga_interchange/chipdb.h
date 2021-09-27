/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#ifndef CHIPDB_H
#define CHIPDB_H

#include "archdefs.h"
#include "nextpnr_namespaces.h"
#include "relptr.h"

NEXTPNR_NAMESPACE_BEGIN

/* !!! Everything in this section must be kept in sync !!!
 * !!! with fpga_interchange/chip_info.py              !!!
 *
 * When schema changes, bump version number in chip_info.py and
 * kExpectedChipInfoVersion
 */

static constexpr int32_t kExpectedChipInfoVersion = 15;

NPNR_PACKED_STRUCT(struct BelConnectedPinsPOD {
    int32_t pin1;
    int32_t pin2;
});

// Flattened site indexing.
//
// To enable flat BelId.z spaces, every tile and sites within that tile are
// flattened.
//
// This has implications on BelId's, WireId's and PipId's.
// The flattened site space works as follows:
//  - Objects that belong to the tile are first.  BELs are always part of Sites,
//    so no BEL objects are in this category.
//  - All site alternative modes are exposed as a "full" site.
//  - Each site appends it's BEL's, wires (site wires) and PIP's.
//   - Sites add two types of pips.  Sites will add pip data first for site
//     pips, and then for site pin edges.
//     1. The first type is site pips, which connect site wires to other site
//        wires.
//     2. The second type is site pin edges, which connect site wires to tile
//        wires (or vise-versa).

NPNR_PACKED_STRUCT(struct BelInfoPOD {
    int32_t name;       // bel name (in site) constid
    int32_t type;       // Type name constid
    int32_t bel_bucket; // BEL bucket constid.

    int32_t num_bel_wires;
    RelPtr<int32_t> ports; // port name constid
    RelPtr<int32_t> types; // port type (IN/OUT/BIDIR)
    RelPtr<int32_t> wires; // connected wire index in tile, or -1 if NA

    int16_t site;
    int16_t site_variant; // some sites have alternative types
    int16_t category;
    int8_t synthetic;
    int8_t lut_element;

    RelPtr<int32_t> pin_map; // Index into CellMapPOD::cell_bel_map

    // If this BEL is a site routing BEL with inverting pins, these values
    // will be [0, num_bel_wires).  If this BEL is either not a site routing
    // BEL or this site routing has no inversion capabilities, then these will
    // both be -1.
    int8_t non_inverting_pin;
    int8_t inverting_pin;

    int16_t padding;

    RelSlice<BelConnectedPinsPOD> connected_pins;
});

enum BELCategory
{
    // BEL is a logic element
    BEL_CATEGORY_LOGIC = 0,
    // BEL is a site routing mux
    BEL_CATEGORY_ROUTING = 1,
    // BEL is a site port, e.g. boundry between site and routing graph.
    BEL_CATEGORY_SITE_PORT = 2
};

NPNR_PACKED_STRUCT(struct BelPortPOD {
    int32_t bel_index;
    int32_t port;
});

NPNR_PACKED_STRUCT(struct TileWireInfoPOD {
    int32_t name; // wire name constid

    // Pip index inside tile
    RelSlice<int32_t> pips_uphill;

    // Pip index inside tile
    RelSlice<int32_t> pips_downhill;

    // Bel index inside tile
    RelSlice<BelPortPOD> bel_pins;

    int16_t site;         // site index in tile
    int16_t site_variant; // site variant index in tile
});

NPNR_PACKED_STRUCT(struct PipInfoPOD {
    int32_t src_index, dst_index;
    int16_t site;         // site index in tile
    int16_t site_variant; // site variant index in tile
    int16_t bel;          // BEL this pip belongs to if site pip.
    int16_t extra_data;
    RelSlice<int32_t> pseudo_cell_wires;
});

NPNR_PACKED_STRUCT(struct ConstraintTagPOD {
    int32_t tag_prefix;       // constid
    int32_t default_state;    // constid
    RelSlice<int32_t> states; // constid
});

NPNR_PACKED_STRUCT(struct LutBelPOD {
    uint32_t name;          // constid
    RelSlice<int32_t> pins; // constid
    uint32_t low_bit;
    uint32_t high_bit;
    int32_t out_pin; // constid
});

NPNR_PACKED_STRUCT(struct LutElementPOD {
    int32_t width;
    RelSlice<LutBelPOD> lut_bels;
});

NPNR_PACKED_STRUCT(struct TileTypeInfoPOD {
    int32_t name; // Tile type constid

    RelSlice<BelInfoPOD> bel_data;

    RelSlice<TileWireInfoPOD> wire_data;

    RelSlice<PipInfoPOD> pip_data;

    RelSlice<ConstraintTagPOD> tags;

    RelSlice<LutElementPOD> lut_elements;

    RelSlice<int32_t> site_types; // constid
});

NPNR_PACKED_STRUCT(struct SiteInstInfoPOD {
    RelPtr<char> name;
    RelPtr<char> site_name;

    // Which site type is this site instance?
    // constid
    int32_t site_type;
});

NPNR_PACKED_STRUCT(struct TileInstInfoPOD {
    // Name of this tile.
    RelPtr<char> name;

    // Index into root.tile_types.
    int32_t type;

    // This array is root.tile_types[type].site_types.size() long.
    // Index into root.sites
    RelSlice<int32_t> sites;

    // Number of tile wires; excluding any site-internal wires
    // which come after general wires and are not stored here
    // as they will never be nodal
    // -1 if a tile-local wire; node index if nodal wire
    RelSlice<int32_t> tile_wire_to_node;

    // Index into wire_types
    RelSlice<int16_t> tile_wire_to_type;
});

NPNR_PACKED_STRUCT(struct TileWireRefPOD {
    int32_t tile;
    int32_t index;
});

NPNR_PACKED_STRUCT(struct NodeInfoPOD { RelSlice<TileWireRefPOD> tile_wires; });

NPNR_PACKED_STRUCT(struct CellBelPinPOD {
    int32_t cell_pin; // constid
    int32_t bel_pin;  // constid
});

NPNR_PACKED_STRUCT(struct ParameterPinsPOD {
    int32_t key;   // constid
    int32_t value; // constid
    RelSlice<CellBelPinPOD> pins;
});

NPNR_PACKED_STRUCT(struct CellConstraintPOD {
    int32_t tag;              // Tag index
    int32_t constraint_type;  // Constraint::ConstraintType
    RelSlice<int32_t> states; // State indicies
});

// Cell parameters metadata
NPNR_PACKED_STRUCT(struct CellParameterPOD {
    int32_t cell_type;     // constid
    int32_t parameter;     // constid
    int32_t format;        // ParameterFormat enum
    int32_t default_value; // constid
});

NPNR_PACKED_STRUCT(struct CellBelMapPOD {
    RelSlice<CellBelPinPOD> common_pins;
    RelSlice<ParameterPinsPOD> parameter_pins;
    RelSlice<CellConstraintPOD> constraints;
});

NPNR_PACKED_STRUCT(struct LutCellPOD {
    int32_t cell;                 // constid
    RelSlice<int32_t> input_pins; // constids
    int32_t parameter;
});

NPNR_PACKED_STRUCT(struct CellMapPOD {
    // Cell names supported in this arch.
    RelSlice<int32_t> cell_names; // constids

    // BEL names that are global buffers.
    RelSlice<int32_t> global_buffers; // constids

    // Name of BelBuckets.
    RelSlice<int32_t> cell_bel_buckets; // constids

    RelSlice<CellBelMapPOD> cell_bel_map;

    RelSlice<LutCellPOD> lut_cells;
    RelSlice<CellParameterPOD> cell_parameters;
});

NPNR_PACKED_STRUCT(struct PackagePinPOD {
    int32_t package_pin; // constid
    int32_t site;        // constid
    int32_t bel;         // constid
});

NPNR_PACKED_STRUCT(struct PackagePOD {
    int32_t package; // constid
    RelSlice<PackagePinPOD> pins;
});

enum CellPinValue
{
    // leave floating
    PIN_VALUE_FLOAT = 0,
    // connect to ground
    PIN_VALUE_GND = 1,
    // connect to vcc
    PIN_VALUE_VCC = 2,
};

NPNR_PACKED_STRUCT(struct DefaultCellConnPOD {
    int32_t pin_name; // constid
    int32_t value;    // CellPinValue
});

NPNR_PACKED_STRUCT(struct DefaultCellConnsPOD {
    int32_t cell_type; // constid
    RelSlice<DefaultCellConnPOD> pins;
});

NPNR_PACKED_STRUCT(struct ConstantsPOD {
    // Cell type and port for the GND and VCC global source.
    int32_t gnd_cell_name; // constid
    int32_t gnd_cell_port; // constid

    int32_t vcc_cell_name; // constid
    int32_t vcc_cell_port; // constid

    int32_t gnd_bel_tile;
    int32_t gnd_bel_index;
    int32_t gnd_bel_pin; // constid

    int32_t vcc_bel_tile;
    int32_t vcc_bel_index;
    int32_t vcc_bel_pin; // constid

    // Name to use for the global GND constant net
    int32_t gnd_net_name; // constid

    // Name to use for the global VCC constant net
    int32_t vcc_net_name; // constid

    // If a choice is available, which constant net should be used?
    // Can be ''/0 if either constant net are equivilent.
    int32_t best_constant_net; // constid

    // Default cell pin connections
    RelSlice<DefaultCellConnsPOD> default_conns;
});

enum WireCategory
{
    WIRE_CAT_GENERAL = 0,
    WIRE_CAT_SPECIAL = 1,
    WIRE_CAT_GLOBAL = 2,
};

NPNR_PACKED_STRUCT(struct WireTypePOD {
    int32_t name;     // constid
    int32_t category; // WireCategory
});

NPNR_PACKED_STRUCT(struct GlobalCellPinPOD {
    int32_t name;     // constid
    int16_t max_hops; // max routing hops to try
    int8_t guide_placement;
    int8_t force_routing;
});

NPNR_PACKED_STRUCT(struct GlobalCellPOD {
    int32_t cell_type;
    RelSlice<GlobalCellPinPOD> pins;
});

NPNR_PACKED_STRUCT(struct MacroParameterPOD {
    int32_t key;   // constid
    int32_t value; // constid
});

enum MacroParamRuleType
{
    PARAM_MAP_COPY = 0,  // copy parameter value
    PARAM_MAP_SLICE = 1, // take a slice of bits
    PARAM_MAP_TABLE = 2, // lookup strings in table
};

NPNR_PACKED_STRUCT(struct MacroParamMapRulePOD {
    // name of parameter on parent primitive
    int32_t prim_param; // constid
    // name of instance to set parameter on
    int32_t inst_name; // constid
    // name of parameter on macro expansion instance
    int32_t inst_param; // constid
    // type of mapping to use to derive new value
    int32_t rule_type; // MacroParamRuleType
    // for slice mappings, the bits to collect
    RelSlice<uint32_t> slice_bits;
    // for table mappings, the lookup table to use
    RelSlice<MacroParameterPOD> map_table;
});

NPNR_PACKED_STRUCT(struct MacroCellInstPOD {
    int32_t name; // instance name constid
    int32_t type; // instance type constid
    // parameters to set on cell
    RelSlice<MacroParameterPOD> parameters;
});

NPNR_PACKED_STRUCT(struct MacroPortInstPOD {
    // name of the cell instance the port is on; or 0/'' for top level ports
    int32_t instance;
    // name of the port
    int32_t port;
    // direction of the port
    int32_t dir;
});

NPNR_PACKED_STRUCT(struct MacroNetPOD {
    // name of the net
    int32_t name;
    // ports on the net
    RelSlice<MacroPortInstPOD> ports;
});

NPNR_PACKED_STRUCT(struct MacroPOD {
    // macro name
    int32_t name;
    // cell instances inside macro
    RelSlice<MacroCellInstPOD> cell_insts;
    // nets inside macro
    RelSlice<MacroNetPOD> nets;
});

NPNR_PACKED_STRUCT(struct MacroExpansionPOD {
    // primitive name to match
    int32_t prim_name;
    // macro name to expand to
    int32_t macro_name;
    // list of parameters to (optionally) match
    RelSlice<MacroParameterPOD> param_matches;
    // how to derive parameters for expansion instances
    RelSlice<MacroParamMapRulePOD> param_rules;
});

NPNR_PACKED_STRUCT(struct ClusterCellPortPOD {
    uint32_t cell;
    uint32_t port;
});

NPNR_PACKED_STRUCT(struct ChainablePortPOD {
    uint32_t cell_source;
    uint32_t cell_sink;
    uint32_t bel_source;
    uint32_t bel_sink;
    int16_t avg_x_offset;
    int16_t avg_y_offset;
});

NPNR_PACKED_STRUCT(struct ClusterRequiredCellPOD {
    uint32_t name;
    uint32_t count;
});

NPNR_PACKED_STRUCT(struct ClusterUsedPortPOD { uint32_t name; });

NPNR_PACKED_STRUCT(struct ClusterEdgePOD {
    uint32_t dir;
    uint32_t cell_pin;
    uint32_t other_cell_pin;
    uint32_t other_cell_type;
});

NPNR_PACKED_STRUCT(struct ClusterConnectionsPOD {
    uint32_t target_idx;
    RelSlice<ClusterEdgePOD> edges;
});

NPNR_PACKED_STRUCT(struct ClusterConnectionGraphPOD {
    uint32_t idx;
    uint32_t cell_type;
    RelSlice<ClusterConnectionsPOD> connections;
    RelSlice<ClusterUsedPortPOD> used_ports;
});

NPNR_PACKED_STRUCT(struct ClusterPhysicalPlacementEntryPOD { RelSlice<uint32_t> bels; });

NPNR_PACKED_STRUCT(struct ClusterPhysicalPlacementsPOD {
    uint32_t site_type;
    RelSlice<ClusterPhysicalPlacementEntryPOD> places;
});

NPNR_PACKED_STRUCT(struct ClusterPOD {
    uint32_t name;
    RelSlice<uint32_t> root_cell_types;
    RelSlice<ChainablePortPOD> chainable_ports;
    RelSlice<ClusterCellPortPOD> cluster_cells_map;
    RelSlice<ClusterRequiredCellPOD> required_cells;
    RelSlice<ClusterConnectionGraphPOD> connection_graph;
    RelSlice<ClusterPhysicalPlacementsPOD> physical_placements;
    uint32_t out_of_site_clusters;
    uint32_t disallow_other_cells;
    uint32_t from_macro;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    RelPtr<char> name;
    RelPtr<char> generator;

    int32_t version;
    int32_t width, height;

    RelSlice<TileTypeInfoPOD> tile_types;
    RelSlice<SiteInstInfoPOD> sites;
    RelSlice<TileInstInfoPOD> tiles;
    RelSlice<NodeInfoPOD> nodes;
    RelSlice<PackagePOD> packages;
    RelSlice<WireTypePOD> wire_types;
    RelSlice<GlobalCellPOD> global_cells;

    // Macro related data
    RelSlice<MacroPOD> macros;
    RelSlice<MacroExpansionPOD> macro_rules;

    RelSlice<ClusterPOD> clusters;

    // BEL bucket constids.
    RelSlice<int32_t> bel_buckets;

    RelPtr<CellMapPOD> cell_map;
    RelPtr<ConstantsPOD> constants;

    // Constid string data.
    RelPtr<RelSlice<RelPtr<char>>> constids;
});

/************************ End of chipdb section. ************************/

inline const TileTypeInfoPOD &tile_info(const ChipInfoPOD *chip_info, int32_t tile)
{
    return chip_info->tile_types[chip_info->tiles[tile].type];
}

template <typename Id> const TileTypeInfoPOD &loc_info(const ChipInfoPOD *chip_info, Id &id)
{
    return chip_info->tile_types[chip_info->tiles[id.tile].type];
}

NPNR_ALWAYS_INLINE inline const BelInfoPOD &bel_info(const ChipInfoPOD *chip_info, BelId bel)
{
    NPNR_ASSERT(bel != BelId());
    return loc_info(chip_info, bel).bel_data[bel.index];
}

inline const PipInfoPOD &pip_info(const ChipInfoPOD *chip_info, PipId pip)
{
    NPNR_ASSERT(pip != PipId());
    return loc_info(chip_info, pip).pip_data[pip.index];
}

inline const SiteInstInfoPOD &site_inst_info(const ChipInfoPOD *chip_info, int32_t tile, int32_t site)
{
    return chip_info->sites[chip_info->tiles[tile].sites[site]];
}

inline const ClusterPOD &cluster_info(const ChipInfoPOD *chip_info, int32_t cluster)
{
    return chip_info->clusters[cluster];
}

enum SyntheticType
{
    NOT_SYNTH = 0,
    SYNTH_SIGNAL = 1,
    SYNTH_GND = 2,
    SYNTH_VCC = 3,
};

NEXTPNR_NAMESPACE_END

#endif /* CHIPDB_H */

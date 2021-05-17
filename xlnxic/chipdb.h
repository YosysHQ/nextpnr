
/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021 gatecat <gatecat@ds0.me>
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

NPNR_PACKED_STRUCT(struct BelPinPOD {
    int32_t name;
    int32_t wire;
    int32_t type;
});

// Structures for encoding/decoding bel's 'place_idx' fields

struct LogicBelIdx
{
    enum LogicBel : uint32_t
    {
        // General bels
        LUT6 = 0,
        LUT5 = 1,
        FF = 2,
        FF2 = 3,
        CARRY = 4, // CARRY4/CARRY8/LOOKAHEAD8
                   // 7-series/UltraScale+
        F7MUX = 5,
        F8MUX = 6,
        F9MUX = 7,
        // Versal
        IMR_1 = 5,
        IMR_2 = 6,
        IMR_3 = 7,
        IMR_4 = 8,
        IMR_5 = 9,
        IMR_6 = 10,
        IMR_I = 11,
        IMR_X = 12,
        IMR_CE_WE = 13,
        IMR_SR = 14,
        CLK_MOD = 15,
    };
    explicit LogicBelIdx(uint32_t idx) : idx(idx){};
    // eigth is the index within the site for LUT/FF bels [0..7]
    LogicBelIdx(uint32_t eighth, LogicBel bel) : idx((eighth << 4) | uint32_t(bel)){};

    uint32_t eighth() const { return (idx >> 4) & 0x7; }
    LogicBel bel() const { return LogicBel(idx & 0xF); }

    uint32_t idx;
};

struct BramDspBelIdx
{
    enum BramDspBel : uint32_t
    {
        // 7/US+/Versal BRAM
        RAMB36 = 0,
        RAMB18_L = 1,
        RAMB18_U = 2,
        // US+/Versal DSP
        DSP_PREADD_DATA = 0,
        DSP_PREADD = 1,
        DSP_A_B_DATA = 2,
        DSP_MULTIPLIER = 3,
        DSP_C_DATA = 4,
        DSP_M_DATA = 5,
        DSP_ALU = 6,
        DSP_OUTPUT = 7,
        // Versal DSP
        DSP_ALUADD = 8,
        DSP_ALUMUX = 9,
        DSP_ALUREG = 10,
        DSP_CAS_DELAY = 11,
        DSP_DFX = 12,
        DSP_PATDET = 13,
        // Versal DSP - FP
        DSP_FP_ADDER = 0,
        DSP_FP_CAS_DELAY = 1,
        DSP_FP_INMUX = 2,
        DSP_FP_INREG = 3,
        DSP_FP_OUTPUT = 4,
        DSP_FPA_CREG = 5,
        DSP_FPA_OPM_REG = 6,
        DSP_FPM_PIPEREG = 7,
        DSP_FPM_STAGE0 = 8,
        DSP_FPM_STAGE1 = 9,
        // Versal DSP - complex
        DSP_CPLX_STAGE0 = 0,
        DSP_CPLX_STAGE1 = 1,
    };

    explicit BramDspBelIdx(uint32_t idx) : idx(idx){};
    // site is the site index in tile [0..1]
    // eigth is the index within the site for LUT/FF bels [0..7]
    BramDspBelIdx(uint32_t site, BramDspBel bel) : idx((site << 4) | uint32_t(bel)){};

    uint32_t site() const { return (idx >> 4) & 0x3; }
    uint32_t bel() const { return BramDspBel(idx & 0xF); }

    uint32_t idx;
};

NPNR_PACKED_STRUCT(struct BelCellMapPOD {
    int32_t cell_type;
    int32_t pin_map_idx;
});

NPNR_PACKED_STRUCT(struct BelDataPOD {
    int32_t name;
    int32_t bel_type;
    int16_t site;
    int16_t site_variant;
    int32_t flags;
    int32_t z;
    int32_t place_idx; // index used for bels that need to be referenced for validity checks
    RelSlice<BelPinPOD> pins;
    RelSlice<BelCellMapPOD> placements;

    static constexpr uint32_t FLAG_RBEL = 0x1000;
    static constexpr uint32_t FLAG_PAD = 0x2000;
});

NPNR_PACKED_STRUCT(struct BelPinRefPOD {
    int32_t bel;
    int32_t pin;
});

NPNR_PACKED_STRUCT(struct TileWireDataPOD {
    int32_t name;
    int16_t site;
    int16_t site_variant;
    int32_t intent;
    int32_t flags;
    RelSlice<int32_t> pips_uphill;
    RelSlice<int32_t> pips_downhill;
    RelSlice<BelPinRefPOD> bel_pins;
});

NPNR_PACKED_STRUCT(struct PseudoPipPinPOD {
    int32_t bel_index;
    int32_t pin_name;
});

NPNR_PACKED_STRUCT(struct PipDataPOD {
    int32_t src_wire;
    int32_t dst_wire;

    int16_t type;
    uint16_t flags;
    int16_t site;
    int16_t site_variant;

    NPNR_PACKED_STRUCT(union {
        NPNR_PACKED_STRUCT(struct {
            int16_t bel;
            int16_t from_pin;
            int16_t to_pin;
            int16_t padding;
        } site_pip);
        NPNR_PACKED_STRUCT(struct {
            int32_t port_name;
            int32_t padding;
        } site_port);
        RelSlice<PseudoPipPinPOD> pseudo_pip;
    });

    static constexpr uint16_t TILE_ROUTING = 0;
    static constexpr uint16_t SITE_ENTRANCE = 1;
    static constexpr uint16_t SITE_EXIT = 2;
    static constexpr uint16_t SITE_INTERNAL = 3;
    static constexpr uint16_t LUT_PERMUTATION = 4;
    static constexpr uint16_t LUT_ROUTETHRU = 5;
    static constexpr uint16_t CONST_DRIVER = 6;

    static constexpr uint16_t FLAG_CAN_INV = 0x400;
    static constexpr uint16_t FLAG_FIXED_INV = 0x800;

    static constexpr uint16_t FLAG_PSEUDO = 0x1000;
    static constexpr uint16_t FLAG_SYNTHETIC = 0x2000;
    static constexpr uint16_t FLAG_REVERSED = 0x4000;
});

NPNR_PACKED_STRUCT(struct TileSitePOD {
    int32_t site_prefix;
    RelSlice<int32_t> variant_types;
    uint16_t dx;
    uint16_t dy;
});

NPNR_PACKED_STRUCT(struct RelTileWireRefPOD {
    int16_t dx;
    int16_t dy;
    int16_t wire;
});

NPNR_PACKED_STRUCT(struct NodeShapePOD { RelSlice<RelTileWireRefPOD> tile_wires; });

NPNR_PACKED_STRUCT(struct TileTypePOD {
    int32_t type_name;
    RelSlice<BelDataPOD> bels;
    RelSlice<TileWireDataPOD> wires;
    RelSlice<PipDataPOD> pips;
    RelSlice<TileSitePOD> sites;
});

NPNR_PACKED_STRUCT(struct RelNodeRefPOD {
    static constexpr int16_t MODE_TILE_WIRE = 0x7000;
    static constexpr int16_t MODE_IS_ROOT = 0x7001;
    static constexpr int16_t MODE_ROW_CONST = 0x7002;
    static constexpr int16_t MODE_GLB_CONST = 0x7003;
    int16_t dx_mode; // relative X-coord, or a special value
    int16_t dy;      // normally, relative Y-coord
    uint16_t wire;   // normally, node index in tile (x+dx, y+dy)
});

NPNR_PACKED_STRUCT(struct TileShapePOD { RelSlice<RelNodeRefPOD> wire_to_node; });

NPNR_PACKED_STRUCT(struct SiteInstInfoPOD {
    int32_t site_prefix;
    uint16_t site_x, site_y;
    uint16_t inter_x, inter_y;
});

NPNR_PACKED_STRUCT(struct TileInstPOD {
    int32_t type;
    int32_t shape;

    int32_t prefix;
    uint16_t tile_x, tile_y;
    int16_t clock_x, clock_y;
    uint16_t slr_index;
    uint16_t padding;

    // Site names must be per tile instance,
    // at least for now, due to differing coordinate systems
    RelSlice<SiteInstInfoPOD> site_insts;
});

NPNR_PACKED_STRUCT(struct ConstIDDataPOD {
    int32_t known_id_count;
    RelSlice<RelPtr<char>> bba_ids;
});

NPNR_PACKED_STRUCT(struct ParameterPOD {
    int32_t key;   // constid
    int32_t value; // constid
});

NPNR_PACKED_STRUCT(struct MacroCellInstPOD {
    int32_t name; // instance name constid
    int32_t type; // instance type constid
    // parameters to set on cell
    RelSlice<ParameterPOD> parameters;
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

NPNR_PACKED_STRUCT(struct PinMapEntryPOD {
    int32_t log_pin;
    RelSlice<int32_t> phys_pins;
});

NPNR_PACKED_STRUCT(struct ParameterPinMapPOD {
    RelSlice<ParameterPOD> param_matches;
    RelSlice<PinMapEntryPOD> pins;
});

NPNR_PACKED_STRUCT(struct PinMapPOD {
    RelSlice<PinMapEntryPOD> common_pins;
    RelSlice<ParameterPinMapPOD> param_pins;
});

NPNR_PACKED_STRUCT(struct CellPinDefaultPOD {
    int32_t pin_name;
    uint32_t value;
    static constexpr uint32_t ZERO = 0;
    static constexpr uint32_t ONE = 1;
    static constexpr uint32_t DISCONN = 2;
});

NPNR_PACKED_STRUCT(struct CellInversionPOD {
    int32_t pin_name;
    int32_t parameter;
});

NPNR_PACKED_STRUCT(struct CellLogicalPortPOD {
    int32_t name;
    int32_t dir;
    int32_t bus_start;
    int32_t bus_end;
});

enum ParameterFormat : uint32_t
{
    PARAM_FMT_STRING = 0,
    PARAM_FMT_BOOLEAN = 1,
    PARAM_FMT_INTEGER = 2,
    PARAM_FMT_FLOAT = 3,
    PARAM_FMT_VBIN = 4,
    PARAM_FMT_VHEX = 5,
    PARAM_FMT_CBIN = 6,
    PARAM_FMT_CHEX = 7,
};

NPNR_PACKED_STRUCT(struct CellParameterPOD {
    int32_t name;
    int32_t format;
    int32_t default_value;
    int32_t width;
});

NPNR_PACKED_STRUCT(struct CellTypePOD {
    int32_t cell_type;
    int32_t library;
    RelSlice<CellPinDefaultPOD> defaults;
    RelSlice<CellInversionPOD> inversions;
    RelSlice<CellLogicalPortPOD> logical_ports;
    RelSlice<CellParameterPOD> parameters;
});

NPNR_PACKED_STRUCT(struct PadInfoPOD {
    // package pin name
    int32_t package_pin;
    // tile and site indexes
    int32_t tile;
    int32_t site;
    // pad bel name
    int32_t bel_name;
    // site type name
    int32_t site_type_name;
    // function name
    int32_t pad_function;
    // index of differential complementary pin
    int32_t pad_complement;
    // index of pin bank
    int32_t pad_bank;
    // extra pad flags
    uint32_t flags;

    static constexpr uint32_t DIFF_SIG = 0x0001;
    static constexpr uint32_t GENERAL_PURPOSE = 0x0002;
    static constexpr uint32_t GLOBAL_CLK = 0x0004;
    static constexpr uint32_t LOW_CAP = 0x0008;
    static constexpr uint32_t VREF = 0x0010;
    static constexpr uint32_t VRN = 0x0020;
    static constexpr uint32_t VRP = 0x0040;
});

NPNR_PACKED_STRUCT(struct PackageInfoPOD {
    int32_t name;
    RelSlice<PadInfoPOD> pads;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    int32_t name;

    int32_t version;
    int32_t width, height;
    RelSlice<TileTypePOD> tile_types;
    RelSlice<TileInstPOD> tile_insts;
    RelSlice<NodeShapePOD> node_shapes;
    RelSlice<TileShapePOD> tile_shapes;

    RelSlice<PinMapPOD> pin_maps;
    RelSlice<CellTypePOD> cell_types;
    RelSlice<MacroPOD> macros;

    RelSlice<PackageInfoPOD> packages;

    RelPtr<ConstIDDataPOD> extra_constids;
});

NEXTPNR_NAMESPACE_END

#endif

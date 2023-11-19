#ifndef XILINX_EXTRA_DATA_H
#define XILINX_EXTRA_DATA_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

NPNR_PACKED_STRUCT(struct XlnxBelExtraDataPOD { int32_t name_in_site; });

struct BelSiteKey
{
    int16_t site;
    int16_t site_variant;
    inline static BelSiteKey unpack(uint32_t s)
    {
        BelSiteKey result;
        result.site_variant = (s & 0xFF);
        result.site = (s >> 8) & 0xFFFF;
        return result;
    };
};

enum XlnxPipType
{
    PIP_TILE_ROUTING = 0,
    PIP_SITE_ENTRY = 1,
    PIP_SITE_EXIT = 2,
    PIP_SITE_INTERNAL = 3,
    PIP_LUT_PERMUTATION = 4,
    PIP_LUT_ROUTETHRU = 5,
    PIP_CONST_DRIVER = 6,
};

NPNR_PACKED_STRUCT(struct XlnxPipExtraDataPOD {
    int32_t site_key;
    int32_t bel_name;
    int32_t pip_config;
});

enum class PipClass
{
    PIP_TILE_ROUTING = 0,
    PIP_SITE_ENTRY = 1,
    PIP_SITE_EXIT = 2,
    PIP_SITE_INTERNAL = 3,
    PIP_LUT_PERMUTATION = 4,
    PIP_LUT_ROUTETHRU = 5,
    PIP_CONST_DRIVER = 6,
};

NPNR_PACKED_STRUCT(struct SiteInstPOD {
    int32_t name_prefix;
    int16_t site_x, site_y; // absolute site coords
    int16_t rel_x, rel_y;   // coords in tiles
    int16_t int_x, int_y;   // associated interconnect coords
    RelSlice<int32_t> variants;
});

NPNR_PACKED_STRUCT(struct XlnxTileInstExtraDataPOD {
    int32_t name_prefix;
    int16_t tile_x, tile_y;
    RelSlice<SiteInstPOD> sites;
});

// LSnibble of Z: function
// MSnibble of Z: index in CLB (A-H)
enum LogicBelTypeZ
{
    BEL_6LUT = 0x0,
    BEL_5LUT = 0x1,
    BEL_FF = 0x2,
    BEL_FF2 = 0x3,
    BEL_FFMUX1 = 0x4,
    BEL_FFMUX2 = 0x5,
    BEL_OUTMUX = 0x6,
    BEL_F7MUX = 0x7,
    BEL_F8MUX = 0x8,
    BEL_F9MUX = 0x9,
    BEL_CARRY8 = 0xA,
    BEL_CLKINV = 0xB,
    BEL_RSTINV = 0xC,
    BEL_HARD0 = 0xD,
    BEL_CARRY4 = 0xF
};

enum BRAMBelTypeZ
{
    BEL_RAMFIFO36 = 0,
    BEL_RAM36 = 1,
    BEL_FIFO36 = 2,

    BEL_RAM18_U = 5,

    BEL_RAMFIFO18_L = 8,
    BEL_RAM18_L = 9,
    BEL_FIFO18_L = 10,
};

enum DSP48E1BelTypeZ
{
    BEL_LOWER_DSP = 6,
    BEL_UPPER_DSP = 25,
};

NEXTPNR_NAMESPACE_END

#endif

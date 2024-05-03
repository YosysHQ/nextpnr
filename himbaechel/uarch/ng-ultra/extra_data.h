#ifndef NGULTRA_EXTRA_DATA_H
#define NGULTRA_EXTRA_DATA_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

NPNR_PACKED_STRUCT(struct NGUltraTileInstExtraDataPOD {
    int32_t name;
    uint8_t lobe;
    uint8_t dummy1;
    uint16_t dummy2;
});

NPNR_PACKED_STRUCT(struct NGUltraPipExtraDataPOD {
    int32_t name;
    uint16_t type;
    uint8_t input;
    uint8_t output;
});

NPNR_PACKED_STRUCT(struct NGUltraBelExtraDataPOD { int32_t flags; });
   
enum TILETypeZ
{
    BEL_LUT_Z   = 0,
    BEL_LUT_MAX_Z = 31,
    BEL_CY_Z    = 32,
    BEL_XLUT_Z  = BEL_CY_Z + 4,
    BEL_RF_Z    = BEL_XLUT_Z + 8,
    BEL_XRF_Z   = BEL_RF_Z + 2,
    BEL_FIFO_Z  = BEL_XRF_Z + 1,
    BEL_XFIFO_Z = BEL_FIFO_Z + 2,
    BEL_CDC_Z   = BEL_XFIFO_Z + 1,
    BEL_XCDC_Z  = BEL_CDC_Z + 2
};

enum ClusterPlacement
{
    PLACE_CY_CHAIN = 1024,
    PLACE_CY_FE1,
    PLACE_CY_FE2,
    PLACE_CY_FE3,
    PLACE_CY_FE4,
};

enum PipExtra
{
    PIP_EXTRA_CROSSBAR = 1,
    PIP_EXTRA_MUX = 2,
    PIP_EXTRA_BYPASS = 3,
    PIP_EXTRA_LUT_PERMUTATION = 4,
    PIP_EXTRA_INTERCONNECT = 5,
    PIP_EXTRA_VIRTUAL = 6,
};

enum BelExtra
{
    BEL_EXTRA_FE_CSC = 1,
    BEL_EXTRA_FE_SCC = 2,
};

NEXTPNR_NAMESPACE_END

#endif

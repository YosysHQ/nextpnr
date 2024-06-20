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
    PLACE_XRF_I1,
    PLACE_XRF_I2,
    PLACE_XRF_I3,
    PLACE_XRF_I4,
    PLACE_XRF_I5,
    PLACE_XRF_I6,
    PLACE_XRF_I7,
    PLACE_XRF_I8,
    PLACE_XRF_I9,
    PLACE_XRF_I10,
    PLACE_XRF_I11,
    PLACE_XRF_I12,
    PLACE_XRF_I13,
    PLACE_XRF_I14,
    PLACE_XRF_I15,
    PLACE_XRF_I16,
    PLACE_XRF_I17,
    PLACE_XRF_I18,
    PLACE_XRF_I19,
    PLACE_XRF_I20,
    PLACE_XRF_I21,
    PLACE_XRF_I22,
    PLACE_XRF_I23,
    PLACE_XRF_I24,
    PLACE_XRF_I25,
    PLACE_XRF_I26,
    PLACE_XRF_I27,
    PLACE_XRF_I28,
    PLACE_XRF_I29,
    PLACE_XRF_I30,
    PLACE_XRF_I31,
    PLACE_XRF_I32,
    PLACE_XRF_I33,
    PLACE_XRF_I34,
    PLACE_XRF_I35,
    PLACE_XRF_I36,
    PLACE_XRF_RA1,
    PLACE_XRF_RA2,
    PLACE_XRF_RA3,
    PLACE_XRF_RA4,
    PLACE_XRF_RA5,
    PLACE_XRF_RA6,
    PLACE_XRF_RA7,
    PLACE_XRF_RA8,
    PLACE_XRF_RA9,
    PLACE_XRF_RA10,
    PLACE_XRF_WA1,
    PLACE_XRF_WA2,
    PLACE_XRF_WA3,
    PLACE_XRF_WA4,
    PLACE_XRF_WA5,
    PLACE_XRF_WA6,
    PLACE_XRF_WE,
    PLACE_XRF_WEA,
    PLACE_DSP_CHAIN,
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

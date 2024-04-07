#ifndef GOWIN_UTILS_H
#define GOWIN_UTILS_H

#include "idstringlist.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

namespace BelFlags {
static constexpr uint32_t FLAG_SIMPLE_IO = 0x100;
}

struct GowinUtils
{
    Context *ctx;

    GowinUtils() {}

    void init(Context *ctx) { this->ctx = ctx; }

    // tile
    IdString get_tile_class(int x, int y);
    Loc get_tile_io16_offs(int x, int y);

    // pin functions: GCLKT_4, SSPI_CS, READY etc
    IdStringList get_pin_funcs(BelId io_bel);

    // PLL pads (type - CLKIN, FeedBack, etc)
    BelId get_pll_bel(BelId io_bel, IdString type);

    // Bels and pips
    bool is_simple_io_bel(BelId bel);
    Loc get_pair_iologic_bel(Loc loc);
    BelId get_io_bel_from_iologic(BelId bel);

    // DSP
    inline int get_dsp_18_z(int z) const { return z & (~3); }
    inline int get_dsp_9_idx(int z) const { return z & 3; }
    inline int get_dsp_18_idx(int z) const { return z & 4; }
    inline int get_dsp_paired_9(int z) const { return (3 - get_dsp_9_idx(z)) | (z & (~3)); }
    inline int get_dsp_mult_from_padd(int padd_z) const { return padd_z + 8; }
    inline int get_dsp_padd_from_mult(int mult_z) const { return mult_z - 8; }
    inline int get_dsp_next_macro(int z) const { return z + 32; }
    inline int get_dsp(int z) const { return BelZ::DSP_Z; }
    inline int get_dsp_macro(int z) const { return (z & 0x20) + BelZ::DSP_0_Z; }
    inline int get_dsp_macro_num(int z) const { return (z & 0x20) >> 5; }
    Loc get_dsp_next_9_in_chain(Loc from) const;
    Loc get_dsp_next_macro_in_chain(Loc from) const;
    Loc get_dsp_next_in_chain(Loc from, IdString dsp_type) const;

    // check bus.
    // This is necessary to find the head in the DSP chain - these buses are
    // not switched in the hardware, but in software you can leave them
    // unconnected or connect them to VCC or VSS, which is the same - as I
    // already said, they are hard-wired and we are only discovering the fact
    // that they are not connected to another DSP in the chain.
    CellInfo *dsp_bus_src(const CellInfo *ci, const char *bus_prefix, int wire_num) const;
    CellInfo *dsp_bus_dst(const CellInfo *ci, const char *bus_prefix, int wire_num) const;

    bool is_diff_io_supported(IdString type);
    bool have_bottom_io_cnds(void);
    IdString get_bottom_io_wire_a_net(int8_t condition);
    IdString get_bottom_io_wire_b_net(int8_t condition);

    // wires
    inline bool is_wire_type_default(IdString wire_type) { return wire_type == IdString(); }
    // If wire is an important part of the global network (like SPINExx)
    inline bool is_global_wire(WireId wire) const
    {
        return ctx->getWireName(wire)[1].in(
                id_SPINE0, id_SPINE1, id_SPINE2, id_SPINE3, id_SPINE4, id_SPINE5, id_SPINE6, id_SPINE7, id_SPINE8,
                id_SPINE9, id_SPINE10, id_SPINE11, id_SPINE12, id_SPINE13, id_SPINE14, id_SPINE15, id_SPINE16,
                id_SPINE17, id_SPINE18, id_SPINE19, id_SPINE20, id_SPINE21, id_SPINE22, id_SPINE23, id_SPINE24,
                id_SPINE25, id_SPINE26, id_SPINE27, id_SPINE28, id_SPINE29, id_SPINE30, id_SPINE31);
    }

    // pips
    inline bool is_global_pip(PipId pip) const
    {
        return is_global_wire(ctx->getPipSrcWire(pip)) || is_global_wire(ctx->getPipDstWire(pip));
    }

    // chip dependent
    bool have_SP32(void);

    // make cell but do not include it in the list of chip cells.
    std::unique_ptr<CellInfo> create_cell(IdString name, IdString type);
};

NEXTPNR_NAMESPACE_END

#endif

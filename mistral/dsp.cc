// SPDX-License-Identifier: ISC
#include "nextpnr.h"

#include "dsp.h"

NEXTPNR_NAMESPACE_BEGIN

void Arch::create_dsp(int x, int y)
{
    // The dedicated TCLK inputs are the normal path for global clocks and
    // clears. Cyclone V also exposes fabric alternatives on GOUT; keep both
    // BEL pins available so the packer can select the routable resource for a
    // non-global control net.
    const IdString clk_fabric = id("CLK_FABRIC");
    const IdString aclr_fabric = id("ACLR_FABRIC");
    auto add_control_pins = [&](BelId bel) {
        add_bel_pin(bel, id_CLK, PORT_IN, get_port(CycloneV::DSP, x, y, -1, CycloneV::CLKIN, 0));
        add_bel_pin(bel, clk_fabric, PORT_IN, get_port(CycloneV::DSP, x, y, -1, CycloneV::CLKIN, 3));
        add_bel_pin(bel, id_ACLR, PORT_IN, get_port(CycloneV::DSP, x, y, -1, CycloneV::ACLR, 0));
        add_bel_pin(bel, aclr_fabric, PORT_IN, get_port(CycloneV::DSP, x, y, -1, CycloneV::ACLR, 2));
        add_bel_pin(bel, id_ENA, PORT_IN, get_port(CycloneV::DSP, x, y, -1, CycloneV::ENABLE, 0));
        add_bel_pin(bel, id_ACCUMULATE, PORT_IN,
                    get_port(CycloneV::DSP, x, y, -1, CycloneV::ACCUMULATE, -1));
        add_bel_pin(bel, id_SUB, PORT_IN, get_port(CycloneV::DSP, x, y, -1, CycloneV::SUB, -1));
        add_bel_pin(bel, id_NEGATE, PORT_IN, get_port(CycloneV::DSP, x, y, -1, CycloneV::NEGATE, -1));
        add_bel_pin(bel, id_LOADCONST, PORT_IN,
                    get_port(CycloneV::DSP, x, y, -1, CycloneV::LOADCONST, -1));
    };

    for (size_t lane_idx = 0; lane_idx < mistral_dsp_lanes.size(); ++lane_idx) {
        const auto &lane = mistral_dsp_lanes.at(lane_idx);
        BelId bel = add_bel(x, y, id_MISTRAL_MUL9X9, id_MISTRAL_MUL9X9);
        for (int bit = 0; bit < 9; ++bit) {
            add_bel_pin(bel, idf("A[%d]", bit), PORT_IN,
                        get_port(CycloneV::DSP, x, y, lane.a_group, CycloneV::DATAIN, bit));
            add_bel_pin(bel, idf("B[%d]", bit), PORT_IN,
                        get_port(CycloneV::DSP, x, y, lane.b_group, CycloneV::DATAIN, bit));
            add_bel_pin(bel, idf("Z[%d]", bit), PORT_IN,
                        get_port(CycloneV::DSP, x, y, mistral_dsp_preadder_z_groups.at(lane_idx),
                                 CycloneV::DATAIN, bit));
        }
        for (int bit = 0; bit < 18; ++bit)
            add_bel_pin(bel, idf("Y[%d]", bit), PORT_OUT,
                        get_port(CycloneV::DSP, x, y, -1, CycloneV::RESULT, lane.result_offset + bit));
        add_control_pins(bel);
    }

    BelId mul18 = add_bel(x, y, id_MISTRAL_MUL18X18, id_MISTRAL_MUL18X18);
    for (int bit = 0; bit < 18; ++bit) {
        add_bel_pin(mul18, idf("A[%d]", bit), PORT_IN,
                    get_port(CycloneV::DSP, x, y, mistral_dsp_18x18_a_groups.at(bit / 9), CycloneV::DATAIN,
                             bit % 9));
        add_bel_pin(mul18, idf("B[%d]", bit), PORT_IN,
                    get_port(CycloneV::DSP, x, y, mistral_dsp_18x18_b_groups.at(bit / 9), CycloneV::DATAIN,
                             bit % 9));
    }
    for (int bit = 0; bit < 36; ++bit)
        add_bel_pin(mul18, idf("C[%d]", bit), PORT_IN,
                    get_port(CycloneV::DSP, x, y, mistral_dsp_18x18_c_groups.at(bit / 9), CycloneV::DATAIN,
                             bit % 9));
    for (int bit = 0; bit < 36; ++bit)
        add_bel_pin(mul18, idf("Y[%d]", bit), PORT_OUT,
                    get_port(CycloneV::DSP, x, y, -1, CycloneV::RESULT, bit));
    add_control_pins(mul18);

    BelId mul27 = add_bel(x, y, id_MISTRAL_MUL27X27, id_MISTRAL_MUL27X27);
    for (int bit = 0; bit < 27; ++bit) {
        int slice = bit / 9;
        add_bel_pin(mul27, idf("A[%d]", bit), PORT_IN,
                    get_port(CycloneV::DSP, x, y, mistral_dsp_27x27_a_groups.at(slice), CycloneV::DATAIN,
                             bit % 9));
        add_bel_pin(mul27, idf("B[%d]", bit), PORT_IN,
                    get_port(CycloneV::DSP, x, y, mistral_dsp_27x27_b_groups.at(slice), CycloneV::DATAIN,
                             bit % 9));
    }
    for (int bit = 0; bit < 54; ++bit)
        add_bel_pin(mul27, idf("Y[%d]", bit), PORT_OUT,
                    get_port(CycloneV::DSP, x, y, -1, CycloneV::RESULT, mistral_dsp_27x27_result_port(bit)));
    add_control_pins(mul27);
}

NEXTPNR_NAMESPACE_END

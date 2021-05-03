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

#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// This file contains functions related to our custom LAB structure, including creating the LAB bels; checking the
// legality of LABs; and manipulating LUT inputs and equations
namespace {
static void create_alm(Arch *arch, int x, int y, int z, uint32_t lab_idx)
{
    auto &lab = arch->labs.at(lab_idx);
    // Create the combinational part of ALMs.
    // There are two of these, for the two LUT outputs, and these also contain the carry chain and associated logic
    // Each one has all 8 ALM inputs as input pins. In many cases only a subset of these are used; depending on mode;
    // and the bel-cell pin mappings are used to handle this post-placement without losing flexibility
    for (int i = 0; i < 2; i++) {
        // Carry/share wires are a bit tricky due to all the different permutations
        WireId carry_in, share_in;
        WireId carry_out, share_out;
        if (z == 0 && i == 0) {
            if (y == 0) {
                // Base case
                carry_in = arch->add_wire(x, y, id_CARRY_START);
                share_in = arch->add_wire(x, y, id_CARRY_START);
            } else {
                // Output of last tile
                carry_in = arch->add_wire(x, y - 1, id_COUT);
                share_in = arch->add_wire(x, y - 1, id_SHAREOUT);
            }
        } else {
            // Output from last combinational unit
            carry_in = arch->add_wire(x, y, arch->id(stringf("CARRY[%d]", (z * 2 + i) - 1)));
            share_in = arch->add_wire(x, y, arch->id(stringf("SHARE[%d]", (z * 2 + i) - 1)));
        }
        if (z == 9 && i == 1) {
            carry_out = arch->add_wire(x, y, id_COUT);
            share_out = arch->add_wire(x, y, id_SHAREOUT);
        } else {
            carry_out = arch->add_wire(x, y, arch->id(stringf("CARRY[%d]", z * 2 + i)));
            share_out = arch->add_wire(x, y, arch->id(stringf("SHARE[%d]", z * 2 + i)));
        }

        BelId bel = arch->add_bel(x, y, arch->id(stringf("ALM%d_COMB%d", z, i)), id_MISTRAL_COMB);
        // LUT/MUX inputs
        arch->add_bel_pin(bel, id_A, PORT_IN, arch->get_port(CycloneV::LAB, x, y, z, CycloneV::A));
        arch->add_bel_pin(bel, id_B, PORT_IN, arch->get_port(CycloneV::LAB, x, y, z, CycloneV::B));
        arch->add_bel_pin(bel, id_C, PORT_IN, arch->get_port(CycloneV::LAB, x, y, z, CycloneV::C));
        arch->add_bel_pin(bel, id_D, PORT_IN, arch->get_port(CycloneV::LAB, x, y, z, CycloneV::D));
        arch->add_bel_pin(bel, id_E0, PORT_IN, arch->get_port(CycloneV::LAB, x, y, z, CycloneV::E0));
        arch->add_bel_pin(bel, id_E1, PORT_IN, arch->get_port(CycloneV::LAB, x, y, z, CycloneV::E1));
        arch->add_bel_pin(bel, id_F0, PORT_IN, arch->get_port(CycloneV::LAB, x, y, z, CycloneV::F0));
        arch->add_bel_pin(bel, id_F1, PORT_IN, arch->get_port(CycloneV::LAB, x, y, z, CycloneV::F1));
        // Carry/share chain
        arch->add_bel_pin(bel, id_CIN, PORT_IN, carry_in);
        arch->add_bel_pin(bel, id_SHAREIN, PORT_IN, share_in);
        arch->add_bel_pin(bel, id_CIN, PORT_OUT, carry_in);
        arch->add_bel_pin(bel, id_SHAREIN, PORT_OUT, share_out);
        // Combinational output
        WireId comb_out = arch->add_wire(x, y, arch->id(stringf("COMBOUT[%d]", z * 2 + i)));
        arch->add_bel_pin(bel, id_COMBOUT, PORT_OUT, comb_out);
        // Assign indexing
        lab.alms.at(z).lut_bels.at(i) = bel;
        auto &b = arch->bel_data(bel);
        b.lab_data.lab = lab_idx;
        b.lab_data.alm = z;
        b.lab_data.idx = i;
    }
    // Create the control set and E/F selection - which is per pair of FF
    std::array<WireId, 2> sel_clk, sel_ena, sel_aclr, sel_ef;
    for (int i = 0; i < 2; i++) {
        // Wires
        sel_clk[i] = arch->add_wire(x, y, arch->id(stringf("CLK%c[%d]", i ? 'B' : 'T', z)));
        sel_ena[i] = arch->add_wire(x, y, arch->id(stringf("ENA%c[%d]", i ? 'B' : 'T', z)));
        sel_aclr[i] = arch->add_wire(x, y, arch->id(stringf("ACLR%c[%d]", i ? 'B' : 'T', z)));
        sel_ef[i] = arch->add_wire(x, y, arch->id(stringf("%cEF[%d]", i ? 'B' : 'T', z)));
        // Muxes - three CLK/ENA per LAB, two ACLR
        for (int j = 0; j < 3; i++) {
            arch->add_pip(lab.clk_wires[j], sel_clk[i]);
            arch->add_pip(lab.ena_wires[j], sel_ena[i]);
            if (j < 2)
                arch->add_pip(lab.aclr_wires[j], sel_aclr[i]);
        }
        // E/F pips
        arch->add_pip(arch->get_port(CycloneV::LAB, x, y, z, i ? CycloneV::E1 : CycloneV::E0), sel_ef[i]);
        arch->add_pip(arch->get_port(CycloneV::LAB, x, y, z, i ? CycloneV::F1 : CycloneV::F0), sel_ef[i]);
    }

    // Create the flipflops and associated routing
    const CycloneV::port_type_t outputs[4] = {CycloneV::FFT0, CycloneV::FFT1, CycloneV::FFB0, CycloneV::FFB1};
    const CycloneV::port_type_t l_outputs[4] = {CycloneV::FFT1L, CycloneV::FFB1L};

    for (int i = 0; i < 4; i++) {
        // FF input, selected by *PKREG*
        WireId comb_out = arch->add_wire(x, y, arch->id(stringf("COMBOUT[%d]", (z * 2) + (i / 2))));
        WireId ff_in = arch->add_wire(x, y, arch->id(stringf("FFIN[%d]", (z * 4) + i)));
        arch->add_pip(comb_out, ff_in);
        arch->add_pip(sel_ef[i / 2], ff_in);
        // FF bel
        BelId bel = arch->add_bel(x, y, arch->id(stringf("ALM%d_FF%d", z, i)), id_MISTRAL_FF);
        arch->add_bel_pin(bel, id_CLK, PORT_IN, sel_clk[i / 2]);
        arch->add_bel_pin(bel, id_ENA, PORT_IN, sel_ena[i / 2]);
        arch->add_bel_pin(bel, id_ACLR, PORT_IN, sel_aclr[i / 2]);
        arch->add_bel_pin(bel, id_SCLR, PORT_IN, lab.sclr_wire);
        arch->add_bel_pin(bel, id_SLOAD, PORT_IN, lab.sload_wire);
        arch->add_bel_pin(bel, id_DATAIN, PORT_IN, ff_in);
        arch->add_bel_pin(bel, id_SDATA, PORT_IN, sel_ef[i / 2]);

        // FF output
        WireId ff_out = arch->add_wire(x, y, arch->id(stringf("FFOUT[%d]", (z * 4) + i)));
        arch->add_bel_pin(bel, id_Q, PORT_OUT, ff_out);
        // Output mux (*DFF*)
        WireId out = arch->get_port(CycloneV::LAB, x, y, z, outputs[i]);
        arch->add_pip(ff_out, out);
        arch->add_pip(comb_out, out);
        // 'L' output mux where applicable
        if (i == 1 || i == 3) {
            WireId l_out = arch->get_port(CycloneV::LAB, x, y, z, l_outputs[i / 2]);
            arch->add_pip(ff_out, l_out);
            arch->add_pip(comb_out, l_out);
        }

        lab.alms.at(z).ff_bels.at(i) = bel;
        auto &b = arch->bel_data(bel);
        b.lab_data.lab = lab_idx;
        b.lab_data.alm = z;
        b.lab_data.idx = i;
    }
}
} // namespace

void Arch::create_lab(int x, int y)
{
    uint32_t lab_idx = labs.size();
    labs.emplace_back();

    auto &lab = labs.back();

    // Create common control set configuration. This is actually a subset of what's possible, but errs on the side of
    // caution due to incomplete documentation

    // Clocks - hardcode to CLKA choices, as both CLKA and CLKB coming from general routing causes unexpected
    // permutations
    for (int i = 0; i < 3; i++) {
        lab.clk_wires[i] = add_wire(x, y, id(stringf("CLK%d", i)));
        add_pip(get_port(CycloneV::LAB, x, y, -1, CycloneV::CLKIN, 0), lab.clk_wires[i]);  // dedicated routing
        add_pip(get_port(CycloneV::LAB, x, y, -1, CycloneV::DATAIN, 0), lab.clk_wires[i]); // general routing
    }

    // Enables - while it looks from the config like there are choices for these, it seems like EN0_SEL actually selects
    // SCLR not ENA0 and EN1_SEL actually selects SLOAD?
    lab.ena_wires[0] = get_port(CycloneV::LAB, x, y, -1, CycloneV::DATAIN, 2);
    lab.ena_wires[1] = get_port(CycloneV::LAB, x, y, -1, CycloneV::DATAIN, 3);
    lab.ena_wires[2] = get_port(CycloneV::LAB, x, y, -1, CycloneV::DATAIN, 0);

    // ACLRs - only consider general routing for now
    lab.aclr_wires[0] = get_port(CycloneV::LAB, x, y, -1, CycloneV::DATAIN, 3);
    lab.aclr_wires[1] = get_port(CycloneV::LAB, x, y, -1, CycloneV::DATAIN, 2);

    // SCLR and SLOAD - as above it seems like these might be selectable using the "EN*_SEL" bits but play it safe for
    // now
    lab.sclr_wire = get_port(CycloneV::LAB, x, y, -1, CycloneV::DATAIN, 3);
    lab.sload_wire = get_port(CycloneV::LAB, x, y, -1, CycloneV::DATAIN, 1);

    for (int i = 0; i < 10; i++) {
        create_alm(this, x, y, i, lab_idx);
    }
}

NEXTPNR_NAMESPACE_END
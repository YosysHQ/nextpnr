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
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// This file contains functions related to our custom LAB structure, including creating the LAB bels; checking the
// legality of LABs; and manipulating LUT inputs and equations

// LAB/ALM structure creation functions
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
        arch->add_bel_pin(bel, id_COUT, PORT_OUT, carry_out);
        arch->add_bel_pin(bel, id_SHAREOUT, PORT_OUT, share_out);
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
        for (int j = 0; j < 3; j++) {
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

// Cell handling and annotation functions
namespace {
ControlSig get_ctrlsig(const CellInfo *cell, IdString port)
{
    ControlSig result;
    result.net = get_net_or_empty(cell, port);
    if (cell->pin_data.count(port))
        result.inverted = cell->pin_data.at(port).state == PIN_INV;
    else
        result.inverted = false;
    return result;
}
} // namespace

bool Arch::is_comb_cell(IdString cell_type) const
{
    // Return true if a cell is a combinational cell type, to be a placed at a MISTRAL_COMB location
    switch (cell_type.index) {
    case ID_MISTRAL_ALUT6:
    case ID_MISTRAL_ALUT5:
    case ID_MISTRAL_ALUT4:
    case ID_MISTRAL_ALUT3:
    case ID_MISTRAL_ALUT2:
    case ID_MISTRAL_NOT:
    case ID_MISTRAL_CONST:
    case ID_MISTRAL_ALUT_ARITH:
        return true;
    default:
        return false;
    }
}

void Arch::assign_comb_info(CellInfo *cell) const
{
    cell->combInfo.is_carry = false;
    cell->combInfo.is_shared = false;
    cell->combInfo.is_extended = false;

    if (cell->type == id_MISTRAL_ALUT_ARITH) {
        cell->combInfo.is_carry = true;
        cell->combInfo.lut_input_count = 5;
        cell->combInfo.lut_bits_count = 32;
        // This is a special case in terms of naming
        int i = 0;
        for (auto pin : {id_A, id_B, id_C, id_D0, id_D1}) {
            cell->combInfo.lut_in[i++] = get_net_or_empty(cell, pin);
        }
        cell->combInfo.comb_out = get_net_or_empty(cell, id_SO);
    } else {
        cell->combInfo.lut_input_count = 0;
        switch (cell->type.index) {
        case ID_MISTRAL_ALUT6:
            ++cell->combInfo.lut_input_count;
            cell->combInfo.lut_in[5] = get_net_or_empty(cell, id_F);
            [[fallthrough]];
        case ID_MISTRAL_ALUT5:
            ++cell->combInfo.lut_input_count;
            cell->combInfo.lut_in[4] = get_net_or_empty(cell, id_E);
            [[fallthrough]];
        case ID_MISTRAL_ALUT4:
            ++cell->combInfo.lut_input_count;
            cell->combInfo.lut_in[3] = get_net_or_empty(cell, id_D);
            [[fallthrough]];
        case ID_MISTRAL_ALUT3:
            ++cell->combInfo.lut_input_count;
            cell->combInfo.lut_in[2] = get_net_or_empty(cell, id_C);
            [[fallthrough]];
        case ID_MISTRAL_ALUT2:
            ++cell->combInfo.lut_input_count;
            cell->combInfo.lut_in[1] = get_net_or_empty(cell, id_B);
            [[fallthrough]];
        case ID_MISTRAL_BUF: // used to route through to FFs etc
        case ID_MISTRAL_NOT: // used for inverters that map to LUTs
            ++cell->combInfo.lut_input_count;
            cell->combInfo.lut_in[0] = get_net_or_empty(cell, id_A);
            [[fallthrough]];
        case ID_MISTRAL_CONST:
            // MISTRAL_CONST is a nextpnr-inserted cell type for 0-input, constant-generating LUTs
            break;
        default:
            log_error("unexpected combinational cell type %s\n", getCtx()->nameOf(cell->type));
        }
        // Note that this relationship won't hold for extended mode, when that is supported
        cell->combInfo.lut_bits_count = (1 << cell->combInfo.lut_input_count);
    }
    cell->combInfo.used_lut_input_count = 0;
    for (int i = 0; i < cell->combInfo.lut_input_count; i++)
        if (cell->combInfo.lut_in[i])
            ++cell->combInfo.used_lut_input_count;
}

void Arch::assign_ff_info(CellInfo *cell) const
{
    cell->ffInfo.ctrlset.clk = get_ctrlsig(cell, id_CLK);
    cell->ffInfo.ctrlset.ena = get_ctrlsig(cell, id_ENA);
    cell->ffInfo.ctrlset.aclr = get_ctrlsig(cell, id_ACLR);
    cell->ffInfo.ctrlset.sclr = get_ctrlsig(cell, id_SCLR);
    cell->ffInfo.ctrlset.sload = get_ctrlsig(cell, id_SLOAD);
    cell->ffInfo.sdata = get_net_or_empty(cell, id_SDATA);
    cell->ffInfo.datain = get_net_or_empty(cell, id_DATAIN);
}

// Validity checking functions
bool Arch::is_alm_legal(uint32_t lab, uint8_t alm) const
{
    auto &alm_data = labs.at(lab).alms.at(alm);
    // Get cells into an array for fast access
    std::array<const CellInfo *, 2> luts{getBoundBelCell(alm_data.lut_bels[0]), getBoundBelCell(alm_data.lut_bels[1])};
    std::array<const CellInfo *, 4> ffs{getBoundBelCell(alm_data.ff_bels[0]), getBoundBelCell(alm_data.ff_bels[1]),
                                        getBoundBelCell(alm_data.ff_bels[2]), getBoundBelCell(alm_data.ff_bels[3])};
    int used_lut_bits = 0;

    int total_lut_inputs = 0;
    // TODO: for more complex modes like extended/arithmetic, it might not always be possible for any LUT input to map
    // to any of the ALM half inputs particularly shared and extended mode will need more thought and probably for this
    // to be revisited
    for (int i = 0; i < 2; i++) {
        if (!luts[i])
            continue;
        total_lut_inputs += luts[i]->combInfo.lut_input_count;
        used_lut_bits += luts[i]->combInfo.lut_bits_count;
    }
    // An ALM only has 64 bits of storage. In theory some of these cases might be legal because of overlap between the
    // two functions, but the current placer is unlikely to stumble upon these cases frequently without anything to
    // guide it, and the cost of checking them here almost certainly outweighs any marginal benefit in supporting them,
    // at least for now.
    if (used_lut_bits > 64)
        return false;

    if (total_lut_inputs > 8) {
        NPNR_ASSERT(luts[0] && luts[1]); // something has gone badly wrong if this fails!
        // Make sure that LUT inputs are not overprovisioned
        int shared_lut_inputs = 0;
        // Even though this N^2 search looks inefficient, it's unlikely a set lookup or similar is going to be much
        // better given the low N.
        for (int i = 0; i < luts[1]->combInfo.lut_input_count; i++) {
            const NetInfo *sig = luts[1]->combInfo.lut_in[i];
            for (int j = 0; j < luts[0]->combInfo.lut_input_count; j++) {
                if (sig == luts[0]->combInfo.lut_in[j]) {
                    ++shared_lut_inputs;
                    break;
                }
            }
        }
        if ((total_lut_inputs - shared_lut_inputs) > 8)
            return false;
    }

    // For each ALM half; check FF control set sharing and input routeability
    for (int i = 0; i < 2; i++) {
        // There are two ways to route from the fabric into FF data - either routing through a LUT or using the E/F
        // signals and SLOAD=1 (*PKREF*)
        bool route_thru_lut_avail = !luts[i] && (total_lut_inputs < 8) && (used_lut_bits < 64);
        // E/F is available if this LUT is using 3 or fewer inputs - this is conservative and sharing can probably
        // improve this situation
        bool ef_available = (!luts[i] || luts[i]->combInfo.used_lut_input_count <= 3);
        // Control set checking
        bool found_ff = false;

        FFControlSet ctrlset;
        for (int j = 0; j < 2; j++) {
            const CellInfo *ff = ffs[i * 2 + j];
            if (!ff)
                continue;
            if (found_ff) {
                // Two FFs in the same half with an incompatible control set
                if (ctrlset != ff->ffInfo.ctrlset)
                    return false;
            } else {
                ctrlset = ff->ffInfo.ctrlset;
            }
            // SDATA must use the E/F input
            // TODO: rare case of two FFs with the same SDATA in the same ALM half
            if (ff->ffInfo.sdata) {
                if (!ef_available)
                    return false;
                ef_available = false;
            }
            // Find a way of routing the input through fabric, if it's not driven by the LUT
            if (ff->ffInfo.datain && (!luts[i] || (ff->ffInfo.datain != luts[i]->combInfo.comb_out))) {
                if (route_thru_lut_avail)
                    route_thru_lut_avail = false;
                else if (ef_available)
                    ef_available = false;
                else
                    return false;
            }
            found_ff = true;
        }
    }

    return true;
}

bool Arch::is_lab_ctrlset_legal(uint32_t lab) const
{
    // TODO
    return true;
}

// This default cell-bel pin mapping is used to provide estimates during placement only. It will have errors and
// overlaps and a correct mapping will be resolved twixt placement and routing
const std::unordered_map<IdString, IdString> Arch::comb_pinmap = {
        {id_A, id_F0}, // fastest input first
        {id_B, id_E0}, {id_C, id_D}, {id_D, id_C},       {id_D0, id_C},       {id_D1, id_B},
        {id_E, id_B},  {id_F, id_A}, {id_Q, id_COMBOUT}, {id_SO, id_COMBOUT},
};

NEXTPNR_NAMESPACE_END
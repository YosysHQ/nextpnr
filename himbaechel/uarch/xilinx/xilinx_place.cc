/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019-2023  gatecat <gatecat@ds0.me>
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

#include <boost/algorithm/string.hpp>
#include <regex>

#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "xilinx.h"

#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

// #define DEBUG_VALIDITY
#ifdef DEBUG_VALIDITY
#define DBG() log_info("invalid: %s %d\n", __FILE__, __LINE__)
#else
#define DBG()
#endif

bool XilinxImpl::xc7_logic_tile_valid(IdString tile_type, const LogicTileStatus &lts) const
{
    bool is_slicem = (tile_type == id_CLBLM_L) || (tile_type == id_CLBLM_R);
    bool tile_is_memory = false;
    if (lts.cells[(3 << 4) | BEL_6LUT] != nullptr && get_tags(lts.cells[(3 << 4) | BEL_6LUT])->lut.is_memory)
        tile_is_memory = true;
    bool small_memory = false;
    if (lts.cells[(3 << 4) | BEL_5LUT] != nullptr && get_tags(lts.cells[(3 << 4) | BEL_5LUT])->lut.is_memory)
        small_memory = true;
    NetInfo *wclk = nullptr;
    // Check eight-tiles (mostly LUT-related validity)
    for (int i = 0; i < 8; i++) {
        if (lts.eights[i].dirty) {
            lts.eights[i].dirty = false;
            lts.eights[i].valid = false;

            auto lut6 = get_tags(lts.cells[(i << 4) | BEL_6LUT]);
            auto lut5 = get_tags(lts.cells[(i << 4) | BEL_5LUT]);

            // Check 6LUT
            if (lut6) {
                if (!is_slicem && (lut6->lut.is_memory || lut6->lut.is_srl)) {
                    DBG();
                    return false;
                } // Memory and SRLs only valid in SLICEMs
                if (lut6->lut.is_srl && (i >= 4)) {
                    DBG();
                    return false;
                }
                if (lut6->lut.is_memory || lut6->lut.is_srl) {
                    if (wclk == nullptr)
                        wclk = lut6->lut.wclk;
                    else if (lut6->lut.wclk != wclk) {
                        DBG();
                        return false;
                    }
                }
                if (lut5) {
                    // Can't mix memory and non-memory
                    if (lut6->lut.is_memory != lut5->lut.is_memory || lut6->lut.is_srl != lut5->lut.is_srl) {
                        DBG();
                        return false;
                    }
                    // If all 6 inputs or 2 outputs are used, 5LUT can't also be present
                    if (lut6->lut.input_count == 6 || lut6->lut.output_count == 2) {
                        DBG();
                        return false;
                    }
                    // If more than 5 total inputs are used, need to check number of shared input
                    if ((lut6->lut.input_count + lut5->lut.input_count) > 5) {
                        int shared = 0, need_shared = (lut6->lut.input_count + lut5->lut.input_count - 5);
                        for (int j = 0; j < lut6->lut.input_count; j++) {
                            for (int k = 0; k < lut5->lut.input_count; k++) {
                                if (lut6->lut.input_sigs[j] == lut5->lut.input_sigs[k])
                                    shared++;
                                if (shared >= need_shared)
                                    break;
                            }
                        }
                        if (shared < need_shared) {
                            DBG();
                            return false;
                        }
                    }
                }
            }
            if (lut5 != nullptr) {
                if (!is_slicem && (lut5->lut.is_memory || lut5->lut.is_srl)) {
                    DBG();
                    return false; // Memory and SRLs only valid in SLICEMs
                }
                if (lut5->lut.is_srl) {
                    if (wclk == nullptr)
                        wclk = lut5->lut.wclk;
                    else if (lut5->lut.wclk != wclk) {
                        DBG();
                        return false;
                    }
                }
                // 5LUT can use at most 5 inputs and 1 output
                if (lut5->lut.input_count > 5 || lut5->lut.output_count == 2) {
                    DBG();
                    return false; // Memory and SRLs only valid in SLICEMs
                }
            }

            // Check (over)usage ofX inputs
            NetInfo *x_net = nullptr;
            if (lut6) {
                x_net = lut6->lut.di2_net;
            }

            CellInfo *mux_cell = nullptr;
            // Eights A, C, E, G: F7MUX uses X input
            if (i == 0 || i == 2 || i == 4 || i == 6)
                mux_cell = lts.cells[i << 4 | BEL_F7MUX];
            // Eights B, F: F8MUX uses X input
            if (i == 1 || i == 5)
                mux_cell = lts.cells[(i - 1) << 4 | BEL_F8MUX];
            auto mux = get_tags(mux_cell);
            if (mux) {
                if (x_net)
                    x_net = mux->mux.sel;
                else if (x_net != mux->mux.sel) {
                    DBG();
                    return false; // Memory and SRLs only valid in SLICEMs
                }
            }

            CellInfo *out_fmux_cell = nullptr;
            // Subslices A, C: F7MUX connects to F7F8 out
            if (i == 0 || i == 2 || i == 4 || i == 6)
                out_fmux_cell = lts.cells[(i << 4) | BEL_F7MUX];
            // Subslices B: F8MUX connects to F7F8 out
            if (i == 1 || i == 5)
                out_fmux_cell = lts.cells[(i - 1) << 4 | BEL_F8MUX];

            auto carry4 = get_tags(lts.cells[((i / 4) << 6) | BEL_CARRY4]);

            if (carry4 != nullptr && carry4->carry.x_sigs[i % 4] != nullptr) {
                if (x_net == nullptr)
                    x_net = carry4->carry.x_sigs[i % 4];
                else if (x_net != carry4->carry.x_sigs[i % 4]) {
                    DBG();
                    return false;
                }
            }

            // FF1 might use X, if it isn't driven directly
            auto ff1 = get_tags(lts.cells[i << 4 | BEL_FF]);
            if (ff1 != nullptr && ff1->ff.d != nullptr && ff1->ff.d->driver.cell != nullptr) {
                auto &drv = ff1->ff.d->driver;
                if ((drv.cell == lts.cells[(i << 4) | BEL_6LUT] && drv.port != id_MC31) ||
                    drv.cell == lts.cells[(i << 4) | BEL_5LUT] || drv.cell == out_fmux_cell) {
                    // Direct, OK
                } else {
                    // Indirect, must use X input
                    if (x_net == nullptr)
                        x_net = ff1->ff.d;
                    else if (x_net != ff1->ff.d) {
                        DBG();
                        return false;
                    }
                }
            }

            // FF2 might use X, if it isn't driven directly
            auto ff2 = get_tags(lts.cells[i << 4 | BEL_FF2]);
            if (ff2 != nullptr && ff2->ff.d != nullptr && ff2->ff.d->driver.cell != nullptr) {
                auto &drv = ff2->ff.d->driver;
                if (drv.cell == lts.cells[(i << 4) | BEL_5LUT]) {
                    // Direct, OK
                } else {
                    // Indirect, must use X input
                    if (x_net == nullptr)
                        x_net = ff2->ff.d;
                    else if (x_net != ff2->ff.d) {
#ifdef DEBUG_VALIDITY
                        log_info("%s %s %s %s %s\n", nameOf(lut6), nameOf(ff1), nameOf(lut5), nameOf(ff2),
                                 nameOf(drv.cell));
#endif
                        DBG();
                        return false;
                    }
                }
            }

            // collision with top address bits
            if (tile_is_memory && !small_memory) {
                auto top_lut = get_tags(lts.cells[(3 << 4) | BEL_6LUT]);
                if (top_lut) {
                    if ((i == 2) && x_net != top_lut->lut.address_msb[0]) {
                        DBG();
                        return false;
                    }
                    if ((i == 1) && x_net != top_lut->lut.address_msb[1]) {
                        DBG();
                        return false;
                    }
                }
            }

            bool mux_output_used = false;
            NetInfo *out5 = nullptr;
            if (lut6 != nullptr && lut6->lut.output_count == 2)
                out5 = lut6->lut.output_sigs[1];
            else if (lut5 != nullptr && !lut5->lut.only_drives_carry)
                out5 = lut5->lut.output_sigs[0];
            if (out5 != nullptr && (out5->users.entries() > 1 ||
                                    ((ff1 == nullptr || out5 != ff1->ff.d) && (ff2 == nullptr || out5 != ff2->ff.d)))) {
                mux_output_used = true;
            }

            if (carry4 != nullptr && carry4->carry.out_sigs[i % 4] != nullptr) {
                // FIXME: direct connections to FF
                if (mux_output_used) {
                    DBG();
                    return false; // Memory and SRLs only valid in SLICEMs
                }
                mux_output_used = true;
            }
            if (out_fmux_cell != nullptr) {
                auto out_fmux = get_tags(out_fmux_cell);
                NetInfo *f7f8 = out_fmux->mux.out;
                if (f7f8 != nullptr && (f7f8->users.entries() > 1 || ((ff1 == nullptr || f7f8 != ff1->ff.d)))) {
                    if (mux_output_used) {
                        DBG();
                        return false; // Memory and SRLs only valid in SLICEMs
                    }
                    mux_output_used = true;
                }
            }
            if (ff2 != nullptr) {
                if (mux_output_used) {
                    DBG();
                    return false; // Memory and SRLs only valid in SLICEMs
                }
                mux_output_used = true;
            }

            lts.eights[i].valid = true;
        } else if (!lts.eights[i].valid) {
            DBG();
            return false;
        }
    }
    // Check half-tiles
    for (int i = 0; i < 2; i++) {
        if (lts.halfs[i].dirty) {
            lts.halfs[i].valid = false;
            bool found_ff[2] = {false, false};
            if (i == 0 && wclk == nullptr) {
                // Need to check wclk too
                for (int z = 4 * i; z < 4 * (i + 1); z++) {
                    for (int k = 0; k < 2; k++) {
                        auto lut = get_tags(lts.cells[z << 4 | (BEL_6LUT + k)]);
                        if (!lut)
                            continue;
                        if (!lut->lut.is_memory && !lut->lut.is_srl)
                            continue;
                        if (lut->lut.wclk != nullptr) {
                            wclk = lut->lut.wclk;
                            break;
                        }
                    }
                }
            }
            NetInfo *clk = nullptr, *sr = nullptr, *ce = nullptr;
            bool clkinv = false, srinv = false, islatch = false, ffsync = false;
            for (int z = 4 * i; z < 4 * (i + 1); z++) {
                for (int k = 0; k < 2; k++) {
                    auto ff = get_tags(lts.cells[z << 4 | (BEL_FF + k)]);
                    if (ff == nullptr)
                        continue;
                    if (ff->ff.is_latch && k == 1) {
                        DBG();
                        return false;
                    }
                    if (found_ff[0] || found_ff[1]) {
                        if (ff->ff.clk != clk) {
                            DBG();
                            return false;
                        }
                        if (ff->ff.sr != sr) {
                            DBG();
                            return false;
                        }
                        if (ff->ff.ce != ce) {
                            DBG();
                            return false;
                        }
                        if (ff->ff.is_clkinv != clkinv) {
                            DBG();
                            return false;
                        }
                        if (ff->ff.is_srinv != srinv) {
                            DBG();
                            return false;
                        }
                        if (ff->ff.is_latch != islatch) {
                            DBG();
                            return false;
                        }
                        if (ff->ff.ffsync != ffsync) {
                            DBG();
                            return false;
                        }
                    } else {
                        clk = ff->ff.clk;
                        if (i == 0 && wclk != nullptr && clk != wclk) {
                            DBG();
                            return false;
                        }
                        sr = ff->ff.sr;
                        ce = ff->ff.ce;
                        clkinv = ff->ff.is_clkinv;
                        srinv = ff->ff.is_srinv;
                        islatch = ff->ff.is_latch;
                        ffsync = ff->ff.ffsync;
                    }
                    found_ff[k] = true;
                }
            }
            lts.halfs[i].valid = true;
        } else if (!lts.halfs[i].valid) {
            DBG();
            return false;
        }
    }
    return true;
}

bool XilinxImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    if (is_logic_tile(bel)) {
        if (!tile_status.at(bel.tile).lts)
            return true;
        return xc7_logic_tile_valid(bel_tile_type(bel), *tile_status.at(bel.tile).lts);
    } else if (is_bram_tile(bel)) {
        const auto &bts = tile_status.at(bel.tile).bts;
        if (!bts)
            return true;
        auto onehot = [&](CellInfo *a, CellInfo *b, CellInfo *c) {
            return (((a != nullptr) ? 1 : 0) + ((b != nullptr) ? 1 : 0) + ((c != nullptr) ? 1 : 0)) <= 1;
        };
        // Only one type of BRAM cell at any given location
        if (!onehot(bts->cells[BEL_RAMFIFO36], bts->cells[BEL_RAM36], bts->cells[BEL_FIFO36])) {
            DBG();
            return false;
        }
        if (!onehot(bts->cells[BEL_RAMFIFO18_L], bts->cells[BEL_RAM18_L], bts->cells[BEL_FIFO18_L])) {
            DBG();
            return false;
        }
        // 18-bit BRAMs cannot be used whilst 36-bit is used
        if (bts->cells[BEL_RAMFIFO36] || bts->cells[BEL_RAM36] || bts->cells[BEL_FIFO36]) {
            for (int i = 4; i < 12; i++)
                if (bts->cells[i]) {
                    DBG();
                    return false;
                }
        }
    }
    return true;
}

void XilinxImpl::fixup_placement()
{
    log_info("Running post-placement legalisation...\n");
    for (auto &ts : tile_status) {
        if (!ts.lts)
            continue;
        auto &lt = *(ts.lts);
        for (int z = 0; z < 8; z++) {
            // Fixup LUT connectivity - applies whenever a LUT5 is used
            CellInfo *lut5 = lt.cells[z << 4 | BEL_5LUT];
            if (!lut5)
                continue;
            auto l5_tags = get_tags(lut5);
            dict<IdString, std::vector<int>> lut5Inputs, lut6Inputs;
            for (int i = 0; i < l5_tags->lut.input_count; i++)
                if (l5_tags->lut.input_sigs[i])
                    lut5Inputs[l5_tags->lut.input_sigs[i]->name].push_back(i);
            CellInfo *lut6 = lt.cells[z << 4 | BEL_6LUT];
            if (lut6) {
                auto l6_tags = get_tags(lut6);
                for (int i = 0; i < l6_tags->lut.input_count; i++)
                    if (l6_tags->lut.input_sigs[i])
                        lut6Inputs[l6_tags->lut.input_sigs[i]->name].push_back(i);
            }
            if (l5_tags->lut.is_memory || l5_tags->lut.is_srl) {
                if (lut6) {
                    if (!lut6->ports.count(id_A6)) {
                        lut6->addInput(id_A6);
                    }
                    lut6->connectPort(id_A6, ctx->nets.at(ctx->id("$PACKER_VCC_NET")).get());
                }
                continue;
            }
            std::set<IdString> uniqueInputs;
            for (auto i5 : lut5Inputs)
                uniqueInputs.insert(i5.first);
            for (auto i6 : lut6Inputs)
                uniqueInputs.insert(i6.first);
            // Disconnect LUT inputs, and re-connect them to not overlap
            IdString ports[6] = {id_A1, id_A2, id_A3, id_A4, id_A5, id_A6};
            for (auto p : ports) {
                lut5->disconnectPort(p);
                lut5->attrs.erase(ctx->idf("X_ORIG_PORT_%s", p.c_str(ctx)));
                if (lut6) {
                    lut6->attrs.erase(ctx->idf("X_ORIG_PORT_%s", p.c_str(ctx)));
                    lut6->disconnectPort(p);
                }
            }
            int index = 0;
            for (auto i : uniqueInputs) {
                if (lut5Inputs.count(i)) {
                    if (!lut5->ports.count(ports[index])) {
                        lut5->ports[ports[index]].name = ports[index];
                        lut5->ports[ports[index]].type = PORT_IN;
                    }
                    lut5->connectPort(ports[index], ctx->nets.at(i).get());
                    lut5->attrs[ctx->idf("X_ORIG_PORT_%s", ports[index].c_str(ctx))] = std::string("");
                    bool first = true;
                    for (auto inp : lut5Inputs[i]) {
                        lut5->attrs[ctx->idf("X_ORIG_PORT_%s", ports[index].c_str(ctx))].str +=
                                (first ? "I" : " I") + std::to_string(inp);
                        first = false;
                    }
                }
                if (lut6 && lut6Inputs.count(i)) {
                    if (!lut6->ports.count(ports[index])) {
                        lut6->ports[ports[index]].name = ports[index];
                        lut6->ports[ports[index]].type = PORT_IN;
                    }
                    lut6->connectPort(ports[index], ctx->nets.at(i).get());

                    lut6->attrs[ctx->idf("X_ORIG_PORT_%s", ports[index].c_str(ctx))] = std::string("");
                    bool first = true;
                    for (auto inp : lut6Inputs[i]) {
                        lut6->attrs[ctx->idf("X_ORIG_PORT_%s", ports[index].c_str(ctx))].str +=
                                (first ? "I" : " I") + std::to_string(inp);
                        first = false;
                    }
                }
                ++index;
            }
            lut5->renamePort(id_O6, id_O5);
            lut5->attrs.erase(id_X_ORIG_PORT_O6);
            lut5->attrs[id_X_ORIG_PORT_O5] = std::string("O");

            if (lut6) {
                if (!lut6->ports.count(id_A6)) {
                    lut6->addInput(id_A6);
                }
                lut6->connectPort(id_A6, ctx->nets.at(ctx->id("$PACKER_VCC_NET")).get());
            }
        }
    }
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_PS7_PS7) {
            log_info("Tieing unused PS7 inputs to constants...\n");
            for (IdString pname : ctx->getBelPins(ci->bel)) {
                if (ci->ports.count(pname) && ci->ports.at(pname).net != nullptr &&
                    ci->ports.at(pname).net->driver.cell != nullptr)
                    continue;
                if (ctx->getBelPinType(ci->bel, pname) != PORT_IN)
                    continue;
                std::string name = pname.str(ctx);
                if (name.find("_PAD_") != std::string::npos)
                    continue;
                if (boost::starts_with(name, "TEST") || boost::starts_with(name, "DEBUGSELECT") ||
                    boost::starts_with(name, "MIO") || boost::starts_with(name, "DDR"))
                    continue;
                bool constval = false;
                ci->ports[pname].name = pname;
                ci->ports[pname].type = PORT_IN;
                if (ci->ports[pname].net != nullptr) {
                    ci->disconnectPort(pname);
                    ci->attrs.erase(ctx->idf("X_ORIG_PORT_", name.c_str()));
                }
                ci->connectPort(pname,
                                ctx->nets.at(constval ? ctx->id("$PACKER_VCC_NET") : ctx->id("$PACKER_GND_NET")).get());
            }
        }
    }
}

void XilinxImpl::fixup_routing()
{
    log_info("Running post-routing legalisation...\n");
    /*
     * Convert LUT permutation into correct physical connections (i.e. effectively eliminating the permutation pips),
     * then specifying the permutation as a new physical-to-logical mapping using X_ORIG_PORT. This keeps RapidWright
     * and Vivado happy, preserving the original logical netlist
     */
    dict<int, std::vector<int>> used_perm_pips; // tile -> [extra_data] for LUT perm pips

    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        for (auto &wire : ni->wires) {
            PipId pip = wire.second.pip;
            if (pip == PipId())
                continue;
            auto &pd = chip_pip_info(ctx->chip_info, pip);
            if (pd.flags != PIP_LUT_PERMUTATION)
                continue;
            const auto &extra_data = *reinterpret_cast<const XlnxPipExtraDataPOD *>(pd.extra_data.get());
            used_perm_pips[pip.tile].push_back(extra_data.pip_config);
        }
    }

    for (size_t ti = 0; ti < tile_status.size(); ti++) {
        if (!used_perm_pips.count(int(ti)))
            continue;
        auto &ts = tile_status.at(ti);
        if (!ts.lts)
            continue;

        auto &lt = *ts.lts;
        for (int z = 0; z < 8; z++) {
            CellInfo *lut5 = lt.cells[z << 4 | BEL_5LUT];
            CellInfo *lut6 = lt.cells[z << 4 | BEL_6LUT];
            if (lut5 == nullptr && lut6 == nullptr)
                continue;
            auto &pp = used_perm_pips.at(ti);
            // from -> to
            dict<IdString, std::vector<IdString>> new_connections;
            IdString ports[6] = {id_A1, id_A2, id_A3, id_A4, id_A5, id_A6};
            for (auto pip : pp) {
                if (((pip >> 8) & 0xF) != z)
                    continue;
                new_connections[ports[(pip >> 4) & 0xF]].push_back(ports[pip & 0xF]);
            }
            dict<IdString, NetInfo *> orig_nets;
            dict<IdString, std::string> orig_ports_l6, orig_ports_l5;
            for (int i = 0; i < 6; i++) {
                NetInfo *l6net = lut6 ? lut6->getPort(ports[i]) : nullptr;
                NetInfo *l5net = lut5 ? lut5->getPort(ports[i]) : nullptr;
                orig_nets[ports[i]] = (l6net ? l6net : l5net);
                if (lut6)
                    orig_ports_l6[ports[i]] =
                            str_or_default(lut6->attrs, ctx->idf("X_ORIG_PORT_%s", ports[i].c_str(ctx)));
                if (lut5)
                    orig_ports_l5[ports[i]] =
                            str_or_default(lut5->attrs, ctx->idf("X_ORIG_PORT_%s", ports[i].c_str(ctx)));
            }
            for (auto &nc : new_connections) {
                if (lut6)
                    lut6->disconnectPort(nc.first);
                if (lut5)
                    lut5->disconnectPort(nc.first);
                for (auto &dst : nc.second) {
                    if (lut6)
                        lut6->disconnectPort(dst);
                    if (lut5)
                        lut5->disconnectPort(dst);
                }
            }
            for (int i = 0; i < 6; i++) {
                if (lut6)
                    lut6->attrs.erase(ctx->idf("X_ORIG_PORT_%s", ports[i].c_str(ctx)));
                if (lut5)
                    lut5->attrs.erase(ctx->idf("X_ORIG_PORT_%s", ports[i].c_str(ctx)));
            }
            for (int i = 0; i < 6; i++) {
                auto p = ports[i];
                if (!new_connections.count(p) || new_connections.at(p).empty())
                    continue;
                if (lut6) {
                    if (!lut6->ports.count(p)) {
                        lut6->addInput(p);
                    }
                    lut6->connectPort(p, orig_nets[new_connections.at(p).front()]);
                    lut6->attrs[ctx->idf("X_ORIG_PORT_%s", p.c_str(ctx))] = std::string("");
                    auto &orig_attr = lut6->attrs[ctx->idf("X_ORIG_PORT_%s", p.c_str(ctx))].str;
                    bool first = true;
                    for (auto &nc : new_connections.at(p)) {
                        orig_attr += orig_ports_l6[nc] + (first ? "" : " ");
                        first = false;
                    }
                    if (orig_attr.empty())
                        lut6->attrs.erase(ctx->idf("X_ORIG_PORT_%s", p.c_str(ctx)));
                }
                if (lut5) {
                    if (!lut5->ports.count(p)) {
                        lut5->addInput(p);
                    }
                    lut5->connectPort(p, orig_nets[new_connections.at(p).front()]);
                    lut5->attrs[ctx->idf("X_ORIG_PORT_%s", p.c_str(ctx))] = std::string("");
                    auto &orig_attr = lut5->attrs[ctx->idf("X_ORIG_PORT_%s", p.c_str(ctx))].str;
                    bool first = true;
                    for (auto &nc : new_connections.at(p)) {
                        orig_attr += orig_ports_l5[nc] + (first ? "" : " ");
                        first = false;
                    }
                    if (orig_attr.empty())
                        lut5->attrs.erase(ctx->idf("X_ORIG_PORT_%s", p.c_str(ctx)));
                }
            }
        }
    }

    /*
     * Legalise route through OSERDESE3s
     */
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_OSERDESE3) {
            if (ci->getPort(id_T_OUT) != nullptr)
                continue;
            ci->params[id_OSERDES_T_BYPASS] = std::string("TRUE");
        }
    }
}

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "cell_transform.h"
#include "design_utils.h"
#include "nextpnr.h"
#include "util.h"
#include "validity.h"

NEXTPNR_NAMESPACE_BEGIN
namespace {
struct LogicPacker
{
    Context *ctx;
    LogicPacker(Context *ctx) : ctx(ctx){};
    bool is_root_cell(CellInfo *cell, IdString in_port, IdString out_port)
    {
        NetInfo *ci = cell->getPort(in_port);
        // Root MUXCYs have CI either driven by a non-MUXCY; or a MUXCY but are not the first user MUXCY user (which is
        // the one that is chained)
        if (!ci->driver.cell)
            return true; // undriven CI
        if (ci->driver.cell->type != cell->type || ci->driver.port != out_port)
            return true; // CI not driven by MUXCY
        for (auto &other_usr : ci->users) {
            if (other_usr.cell == cell && other_usr.port == in_port)
                return false; // first MUXCY user in chain
            if (other_usr.cell->type == cell->type && other_usr.port == in_port)
                return true; // not the first MUXCY user in chain
        }
        NPNR_ASSERT_FALSE("unreachable");
    }
    void combine_split_carries()
    {
        // Combine MUXCY+XORCY chains into CARRY4/CARRY8 chains
        IdString prim = (ctx->family == ArchFamily::XC7) ? id_CARRY4 : id_CARRY8;
        pool<IdString> processed_cells;
        for (auto muxcy : ctx->get_cells_by_type(id_MUXCY)) {
            if (processed_cells.count(muxcy->name))
                continue;
            if (!is_root_cell(muxcy, id_CI, id_O))
                continue;
            int index = 0;
            CellInfo *cursor = muxcy;
            CellInfo *packed = ctx->create_lib_cell(ctx->derive_name(muxcy->name, prim), prim);
            NetInfo *last_carry = muxcy->getPort(id_CI);
            muxcy->movePortTo(id_CI, packed, (prim == id_CARRY4) ? id_CYINIT : id_CI);
            while (true) {
                if (index == (prim == id_CARRY4 ? 4 : 8)) {
                    // Next chunk
                    CellInfo *next = ctx->create_lib_cell(ctx->derive_name(cursor->name, prim), prim);
                    // External carry between chunks
                    packed->connectPorts((prim == id_CARRY4) ? ctx->id("CO[3]") : ctx->id("CO[7]"), next, id_CI);
                    packed = next;
                    index = 0;
                }
                NPNR_ASSERT(
                        !processed_cells.count(cursor->name)); // check we don't process the same cell more than once
                processed_cells.insert(cursor->name);
                // Replace S/DI connections
                IdString di_bit = ctx->id(stringf("DI[%d]", index)), s_bit = ctx->id(stringf("S[%d]", index)),
                         co_bit = ctx->id(stringf("CO[%d]", index));
                muxcy->movePortTo(id_DI, packed, di_bit);
                muxcy->movePortTo(id_S, packed, s_bit);
                // Replace output connection
                last_carry = muxcy->getPort(id_O);
                muxcy->movePortTo(id_O, packed, co_bit);
                // Look for an XORCY that can be combined, too
                NetInfo *s_net = packed->getPort(s_bit);
                if (s_net) {
                    // Search for XORCYs based on S-LI shared connectivity
                    for (auto &s_usr : s_net->users) {
                        if (s_usr.cell->type == id_XORCY && s_usr.port == id_LI &&
                            !processed_cells.count(s_usr.cell->name)) {
                            CellInfo *xorcy = s_usr.cell;
                            // Check CI connectivity matches, too
                            NetInfo *xorcy_ci = xorcy->getPort(id_CI);
                            if (xorcy_ci != last_carry)
                                continue;
                            // Pack into CARRY4/8
                            processed_cells.insert(xorcy->name);
                            xorcy->disconnectPort(id_CI);
                            xorcy->disconnectPort(id_LI);
                            xorcy->movePortTo(id_O, packed, ctx->id(stringf("O[%d]", index)));
                        }
                    }
                }
                cursor = nullptr;
                // Find the next MUXCY in the chain, if there is one
                if (last_carry) {
                    for (auto &usr : last_carry->users) {
                        if (usr.cell->type == id_MUXCY && usr.port == id_CI) {
                            cursor = usr.cell;
                            break;
                        }
                    }
                }
                if (!cursor)
                    break;
                index++;
            }
        }
        // Remove the cells we packed
        for (IdString cell_name : processed_cells) {
            for (auto &p : ctx->cells.at(cell_name)->ports) {
                NPNR_ASSERT(!p.second.net); // check we didn't leave any dangling connections
            }
            ctx->cells.erase(cell_name);
        }
        // Any XORCYs left behind now are not part of a chain and can safely be blasted to soft LUT2s
        dict<IdString, XFormRule> softlogic_rules;
        softlogic_rules[ctx->id("XORCY")].new_type = ctx->id("LUT2");
        softlogic_rules[ctx->id("XORCY")].port_xform[ctx->id("CI")] = ctx->id("I0");
        softlogic_rules[ctx->id("XORCY")].port_xform[ctx->id("LI")] = ctx->id("I1");
        softlogic_rules[ctx->id("XORCY")].set_params.emplace_back(ctx->id("INIT"), Property(0x6));
        for (auto xorcy : ctx->get_cells_by_type(id_XORCY))
            transform_cell(ctx, softlogic_rules, xorcy);
    }
    void carry4_to_carry8()
    {
        // Upgrade 7-series CARRY4s to UltraScale+ CARRY8s
        pool<IdString> processed_cells;
        for (auto carry4 : ctx->get_cells_by_type(id_CARRY4)) {
            if (processed_cells.count(carry4->name))
                continue;
            if (!is_root_cell(carry4, id_CI, ctx->id("CO[3]")))
                continue;
            CellInfo *base = carry4;
            CellInfo *cursor = base;
            bool is_47 = false;
            while (true) {
                if (is_47) {
                    // Is the top bits of a CARRY4
                    processed_cells.insert(cursor->name);
                    for (int i = 0; i < 4; i++) {
                        cursor->movePortTo(ctx->id(stringf("DI[%d]", i)), base, ctx->id(stringf("DI[%d]", i + 4)));
                        cursor->movePortTo(ctx->id(stringf("S[%d]", i)), base, ctx->id(stringf("S[%d]", i + 4)));
                        cursor->movePortTo(ctx->id(stringf("CO[%d]", i)), base, ctx->id(stringf("CO[%d]", i + 4)));
                        cursor->movePortTo(ctx->id(stringf("O[%d]", i)), base, ctx->id(stringf("O[%d]", i + 4)));
                    }
                    cursor->disconnectPort(id_CI);
                    cursor->disconnectPort(id_CYINIT);
                } else {
                    // Is the bottom bits of a CARRY4
                    base = cursor;
                    base->type = id_CARRY8;
                    base->params[id_CARRY_TYPE] = std::string("SINGLE_CY8");
                    // CARRY4 has these separate; CARRY8 has them combined
                    NetInfo *cyinit = cursor->getPort(id_CYINIT);
                    if (!cyinit || cyinit->name == id_GLOBAL_LOGIC0) {
                        // Nothing to do, just disconnect CYINIT at the end
                    } else {
                        // Move CYINIT to CI
                        if (!cursor->ports.count(id_CI))
                            cursor->addInput(id_CI);
                        NetInfo *ci = cursor->getPort(id_CI);
                        NPNR_ASSERT(!ci || ci->name == id_GLOBAL_LOGIC0); // can't have both CI and CYINIT used!
                        if (ci)
                            cursor->disconnectPort(id_CI);
                        cursor->connectPort(id_CI, cyinit);
                    }
                    cursor->disconnectPort(id_CYINIT);
                    cursor->ports.erase(id_CYINIT);
                }
                NetInfo *out = base->getPort(is_47 ? ctx->id("CO[7]") : ctx->id("CO[3]"));
                if (!out)
                    break;
                CellInfo *next = nullptr;
                for (auto &usr : out->users)
                    if (usr.cell->type == id_CARRY4 && usr.port == id_CI) {
                        next = usr.cell;
                        break;
                    }
                if (!next)
                    break;
                cursor = next;
                is_47 = !is_47;
            }
        }
        // Remove the cells we packed
        for (IdString cell_name : processed_cells) {
            for (auto &p : ctx->cells.at(cell_name)->ports) {
                NPNR_ASSERT(!p.second.net); // check we didn't leave any dangling connections
            }
            ctx->cells.erase(cell_name);
        }
    }
    // Insert a route-thru LUT where a LUT can't be packed to drive the carry chain.
    // While we could rely on route-thru pips and validity checking, this makes the macro much more explicit to the
    // placer
    CellInfo *insert_route_in_lut(NetInfo *net, CellInfo *usr_cell, IdString usr_port)
    {
        // Move port onto a new net
        usr_cell->disconnectPort(usr_port);
        NetInfo *routed_thru_lut = ctx->createNet(ctx->derive_name(net->name, id_ROUTETHRU, true));
        usr_cell->connectPort(usr_port, routed_thru_lut);
        // Create LUT
        CellInfo *lut = ctx->create_lib_cell(ctx->derive_name(net->name, id_ROUTETHRU_LUT), id_LUT1);
        lut->connectPort(id_O, routed_thru_lut);
        lut->connectPort(id_I0, net);
        lut->params[id_INIT] = Property(2, 2);
        return lut;
    }
    // Insert a route-thru latch to legalise a combined sum-out and carry-out that has no other legal routing
    // possibilities
    CellInfo *insert_route_thru_latch(NetInfo *net)
    {
        // Move driver port onto a new net that drivers the latch
        PortRef driver = net->driver;
        NPNR_ASSERT(driver.cell != nullptr);
        driver.cell->disconnectPort(driver.port);
        NetInfo *latch_data = ctx->createNet(ctx->derive_name(net->name, id_ROUTETHRU, true));
        driver.cell->connectPort(driver.port, latch_data);
        // Create the latch
        CellInfo *latch = ctx->create_lib_cell(ctx->derive_name(net->name, id_ROUTETHRU_LATCH), id_LDCE);
        latch->connectPort(id_D, latch_data);
        latch->connectPort(id_Q, net);
        // Latch is always-enabled
        latch->connectPort(id_G, ctx->nets.at(id_GLOBAL_LOGIC1).get());
        latch->connectPort(id_GE, ctx->nets.at(id_GLOBAL_LOGIC1).get());
        latch->connectPort(id_CLR, ctx->nets.at(id_GLOBAL_LOGIC0).get());

        return latch;
    }

    bool is_packable_lut(const CellInfo *ci)
    {
        return ci->type.in(id_LUT1, id_LUT2, id_LUT3, id_LUT4, id_LUT5, id_LUT6);
    }

    void get_lut_inputs(const CellInfo *ci, pool<IdString> &result)
    {
        for (auto port : {id_I0, id_I1, id_I2, id_I3, id_I4, id_I5}) {
            const NetInfo *net = ci->getPort(port);
            if (!net)
                continue;
            result.insert(net->name);
        }
    }

    CellInfo *get_packable_lut(NetInfo *ni, bool is_s)
    {
        if (!ni)
            return nullptr;
        if (!ni->driver.cell || !is_packable_lut(ni->driver.cell))
            return nullptr;
        // S can route through 06 and drive other signals, DI can't
        if (!is_s && ni->users.entries() > 1)
            return nullptr;
        // DI can't be a LUT6
        if (!is_s && ni->driver.cell->type == id_LUT6)
            return nullptr;
        // If it's already been clustered, no good
        if (ni->driver.cell->cluster != ClusterId())
            return nullptr;
        return ni->driver.cell;
    }

    void constrain_carry_chains()
    {
        const IdString carry_prim =
                (ctx->family == ArchFamily::VERSAL)
                        ? id_LOOKAHEAD8
                        : ((ctx->family == ArchFamily::XCUP || ctx->family == ArchFamily::XCU) ? id_CARRY8 : id_CARRY4);
        const IdString carry_in_port = (ctx->family == ArchFamily::VERSAL) ? id_CIN : id_CI;
        const IdString carry_out_port =
                (ctx->family == ArchFamily::VERSAL)
                        ? id_COUTH
                        : ((ctx->family == ArchFamily::XCUP || ctx->family == ArchFamily::XCU) ? ctx->id("CO[7]")
                                                                                               : ctx->id("CO[3]"));
        const int carry_height = (ctx->family == ArchFamily::XC7) ? 4 : 8;
        std::vector<CellInfo *> root_cells;
        for (auto &cell : ctx->cells) {
            CellInfo *root = cell.second.get();
            if (root->type != carry_prim)
                continue;
            if (!is_root_cell(root, carry_in_port, carry_out_port))
                continue;
            root_cells.push_back(root);
        }
        for (auto root : root_cells) {
            int offset = 0;
            CellInfo *cursor = root;
            auto do_constrain = [&](CellInfo *entry, uint32_t eighth, LogicBelIdx::LogicBel bel) {
                entry->cluster = root->name;
                root->cluster_info.cluster_cells.push_back(entry);
                auto &c = entry->cluster_info;
                c.site_dx = c.site_dy = 0;
                c.tile_dx = 0;
                c.tile_dy = offset;
                c.type = ClusterInfo::ABS_PLACE_IDX;
                c.place_idx = LogicBelIdx(eighth, bel).idx;
            };
            while (true) {
                do_constrain(cursor, 0, LogicBelIdx::CARRY);
                // Deal with CYINIT
                bool ax_used = false;
                if (ctx->family == ArchFamily::XC7) {
                    const NetInfo *cyinit = cursor->getPort(id_CYINIT);
                    const NetInfo *ci = cursor->getPort(id_CI);
                    if (offset != 0) {
                        // Middle of chain
                        if (cyinit && cyinit->name != id_GLOBAL_LOGIC0)
                            log_error("Found illegal CYINIT connection '%s' in middle-of-chain cell '%s'\n",
                                      ctx->nameOf(cyinit), ctx->nameOf(cursor));
                        cursor->disconnectPort(id_CYINIT);
                    } else {
                        // Start of chain
                        if (cyinit && ci && ci->name == id_GLOBAL_LOGIC0)
                            cursor->disconnectPort(id_CI);
                        ax_used = (cyinit && cyinit->name != id_GLOBAL_LOGIC0 && cyinit->name != id_GLOBAL_LOGIC1);
                    }
                } else if (ctx->family != ArchFamily::VERSAL && offset == 0) {
                    const NetInfo *cin = cursor->getPort(id_CI);
                    ax_used = (cin && cin->name != id_GLOBAL_LOGIC0);
                }
                // Pack LUTs driving carry chain too
                for (int i = 0; i < carry_height; i++) {
                    if (ctx->family == ArchFamily::VERSAL) {
                        static const std::string indices = "ABCDEFGH";
                        NetInfo *ge = cursor->getPort(ctx->id(stringf("GE%c", indices.at(i))));
                        NetInfo *prop = cursor->getPort(ctx->id(stringf("PROP%c", indices.at(i))));
                        if (ge && ge->driver.cell) {
                            if (ge->driver.cell->type != id_LUTCY2)
                                log_error("Expected LUTCY2 driving GE signal '%s', got '%s'\n", ctx->nameOf(ge),
                                          ctx->nameOf(ge->driver.cell->type));
                            do_constrain(ge->driver.cell, i, LogicBelIdx::LUT6);
                        }
                        if (prop && prop->driver.cell) {
                            if (prop->driver.cell->type != id_LUTCY1)
                                log_error("Expected LUTCY1 driving PROP signal '%s', got '%s'\n", ctx->nameOf(prop),
                                          ctx->nameOf(prop->driver.cell->type));
                            do_constrain(prop->driver.cell, i, LogicBelIdx::LUT5);
                        }
                    } else {
                        IdString s_port = ctx->id(stringf("S[%d]", i)), di_port = ctx->id(stringf("DI[%d]", i));
                        NetInfo *s = cursor->getPort(s_port);
                        NetInfo *di = cursor->getPort(di_port);
                        bool s_driven = (s && s->driver.cell), di_driven = (di && di->driver.cell);
                        CellInfo *s_lut = get_packable_lut(s, true);
                        CellInfo *di_lut = get_packable_lut(di, false);
                        // Check input count packability
                        if (s_lut && di_lut) {
                            pool<IdString> input_nets;
                            get_lut_inputs(s_lut, input_nets);
                            get_lut_inputs(di_lut, input_nets);
                            // Failed, set di_lut to nullptr to force route-thru
                            if (input_nets.size() > 5)
                                di_lut = nullptr;
                        }
                        // If we can't pack a LUT into DI _and_ the X input is taken for something else, we need to
                        // force a route-through there, too
                        if (i == 0 && ax_used && di_driven && !di_lut) {
                            // We might need to force a route-through on S if we'd hit input count issues otherwise
                            if (s_lut && (s_lut->type == id_LUT5 || s_lut->type == id_LUT6))
                                s_lut = nullptr;
                            di_lut = insert_route_in_lut(di, cursor, di_port);
                        }
                        // Insert a S route-through if one is required
                        if (s_driven && !s_lut)
                            s_lut = insert_route_in_lut(s, cursor, s_port);
                        // Constrain the LUTs, if they exist
                        if (s_lut)
                            do_constrain(s_lut, i, LogicBelIdx::LUT6);
                        if (di_lut)
                            do_constrain(di_lut, i, LogicBelIdx::LUT5);
                    }
                }

                // 'MUX' output contention means we can't always route both O[i] and CO[i] to fabric
                // This can be fixed either by forcing a FF to a particular location, or, failing that, inserting route
                // through latches
                if (ctx->family != ArchFamily::VERSAL) {
                    LogicSiteStatus ff_checker(ctx->family);
                    bool requires_latches = false;
                    std::vector<std::pair<int, NetInfo *>> candidate_route_thru_co;
                    for (int i = 0; i < carry_height; i++) {
                        NetInfo *o = cursor->getPort(ctx->id(stringf("O[%d]", i)));
                        NetInfo *co = cursor->getPort(ctx->id(stringf("CO[%d]", i)));
                        if (!o || !co || o->users.empty() || co->users.empty())
                            continue; // O and CO not used at once; always legal
                        bool is_legal_co = true;
                        // Check that CO users don't exceed a relevant carry in or one (1) packable FF
                        bool ci_found = false;
                        for (auto &usr : co->users) {
                            if (i == (carry_height - 1) && usr.cell->type == carry_prim && usr.port == carry_in_port &&
                                !ci_found) {
                                // Carry in; part of the chain
                                ci_found = true;
                                continue;
                            }
                            if (ctx->getBelBucketForCellType(usr.cell->type) == id_FF && usr.port == id_D &&
                                !requires_latches) {
                                LogicBelIdx bel_idx(i, LogicBelIdx::FF);
                                if (!ff_checker.bound.at(bel_idx.idx)) {
                                    // Not already used the FF slot
                                    ff_checker.bound.at(bel_idx.idx) = usr.cell;
                                    continue;
                                }
                            }
                            is_legal_co = false;
                            break;
                        }
                        // If we have any users on CO other than CI, then this will require a route-thru latch if we
                        // give up packing FFs
                        if (!ci_found || co->users.entries() > 1) {
                            candidate_route_thru_co.emplace_back(i, co);
                        }
                        // We've found a solution for CO; nothing more to do
                        if (is_legal_co)
                            continue;
                        // Still hope we can constrain an FF for O[i] and use the 'MUX' output for CO[i]
                        bool is_legal_o = true;
                        for (auto &usr : o->users) {
                            if (ctx->getBelBucketForCellType(usr.cell->type) == id_FF && usr.port == id_D &&
                                !requires_latches) {
                                LogicBelIdx bel_idx(i, LogicBelIdx::FF);
                                if (!ff_checker.bound.at(bel_idx.idx)) {
                                    // Not already used the FF slot
                                    ff_checker.bound.at(bel_idx.idx) = usr.cell;
                                    continue;
                                }
                            }
                            is_legal_o = false;
                            break;
                        }
                        if (is_legal_o)
                            continue;
                        // At this point a latch is the only option
                        requires_latches = true;
                    }
                    // Check if constraining all FFs thus would still be legal
                    for (int i = 0; i < (carry_height / 4); i++)
                        if (!check_ff_ctrlset(ff_checker, ctx->family, i))
                            requires_latches = true;
                    if (!check_tile_ctrlset(ff_checker, ctx->family))
                        requires_latches = true;
                    // Insert latches if required
                    if (requires_latches) {
                        // Remove all candidate FFs
                        std::fill(ff_checker.bound.begin(), ff_checker.bound.end(), nullptr);
                        // Insert route thrus
                        for (auto entry : candidate_route_thru_co)
                            ff_checker.bound.at(LogicBelIdx(entry.first, LogicBelIdx::FF).idx) =
                                    insert_route_thru_latch(entry.second);
                    }
                    // Constrain FFs/latches
                    for (int i = 0; i < carry_height; i++) {
                        CellInfo *ci = ff_checker.get_cell(i, LogicBelIdx::FF);
                        if (!ci)
                            continue;
                        do_constrain(ci, i, LogicBelIdx::FF);
                    }
                }

                NetInfo *chain_net = cursor->getPort(carry_out_port);
                if (!chain_net)
                    break;
                cursor = nullptr;
                for (auto &usr : chain_net->users) {
                    if (usr.cell->type == carry_prim && usr.port == carry_in_port) {
                        cursor = usr.cell;
                        break;
                    }
                }
                if (!cursor)
                    break;
                --offset; // Xilinx carries go downwards in tile coordinates
            }
        }
    }
    bool is_lut_output(PortRef &p)
    {
        return p.port == id_O &&
               p.cell->type.in(id_LUT1, id_LUT2, id_LUT3, id_LUT4, id_LUT5, id_LUT6, id_RAMD32, id_RAMD64E, id_RAMD64E5,
                               id_RAMS32, id_RAMS64E, id_RAMS64E1, id_RAMS64E5);
    }
    // Label LUTs and intermediate muxes in a mux tree with their eihgth-offset in the slice
    void label_muxtree(CellInfo *root, dict<IdString, int> &labels, int offset = 0)
    {
        labels[root->name] = offset;
        auto process_mux = [&](IdString next_type, int delta) {
            for (int i = 0; i < 2; i++) {
                NetInfo *inp = root->getPort((i == 1) ? id_I1 : id_I0);
                if (!inp || !inp->driver.cell)
                    continue;
                if (inp->driver.cell->type != next_type || inp->driver.port != id_O)
                    continue;
                label_muxtree(inp->driver.cell, labels, offset + (1 - i) * delta); // (1-i) because input 0 is higher
            }
        };
        if (root->type == id_MUXF9)
            process_mux(id_MUXF8, 4);
        else if (root->type == id_MUXF8)
            process_mux(id_MUXF7, 2);
        else {
            NPNR_ASSERT(root->type == id_MUXF7);
            for (int i = 0; i < 2; i++) {
                NetInfo *inp = root->getPort((i == 1) ? id_I1 : id_I0);
                if (!inp || !inp->driver.cell)
                    continue;
                if (!is_lut_output(inp->driver))
                    continue;
                labels[inp->driver.cell->name] = offset + (1 - i);
            }
        }
    }
    bool is_lutram_type(IdString type)
    {
        return type.in(id_RAMD32, id_RAMD32M64, id_RAMD64E, id_RAMD64E5, id_RAMS32, id_RAMS64E, id_RAMS64E1,
                       id_RAMS64E5);
    }
    bool is_spram_type(IdString type) { return type.in(id_RAMS32, id_RAMS64E, id_RAMS64E1, id_RAMS64E5); }
    bool is_ram32_type(IdString type) { return type.in(id_RAMS32, id_RAMD32, id_RAMD32M64); }
    // Find macros that are LUTRAM
    std::vector<IdString> find_lutram_macros()
    {
        std::vector<IdString> result;
        for (auto &macro : ctx->expanded_macros) {
            bool is_lutram = false;
            for (auto &cell : macro.second.expanded_cells) {
                if (!ctx->cells.count(cell))
                    continue;
                if (!is_lutram_type(ctx->cells.at(cell)->type))
                    continue;
                is_lutram = true;
                break;
            }
            if (!is_lutram)
                continue;
            result.push_back(macro.first);
        }
        return result;
    }
    // Gets the key of a RAM32 based on its read port
    std::array<IdString, 6> get_ram32_key(const CellInfo *cell)
    {
        std::array<IdString, 6> result;
        static const std::array<IdString, 5> dpr_inputs{id_RADR0, id_RADR1, id_RADR2, id_RADR3, id_RADR4};
        static const std::array<IdString, 5> spr_inputs{id_ADR0, id_ADR1, id_ADR2, id_ADR3, id_ADR4};
        for (int i = 0; i < 5; i++) {
            IdString pin = (cell->type == id_RAMS32) ? spr_inputs.at(i) : dpr_inputs.at(i);
            const NetInfo *net = cell->getPort(pin);
            result[i] = net ? net->name : IdString();
        }
        result[5] = (cell->type == id_RAMD32M64) ? id_RAMD32 : cell->type;
        return result;
    }
    // Gets the bel for a mux cell type
    LogicBelIdx::LogicBel get_mux_bel(IdString type, LogicBelIdx::LogicBel def = LogicBelIdx::LUT6)
    {
        switch (type.index) {
        case ID_MUXF7:
            return LogicBelIdx::F7MUX;
        case ID_MUXF8:
            return LogicBelIdx::F8MUX;
        case ID_MUXF9:
            return LogicBelIdx::F9MUX;
        default:
            return def;
        }
    }
    void constrain_lutram_macro(IdString macro)
    {
        // Create map of cells in macro
        dict<IdString, CellInfo *> macro_cells;
        for (auto &cell : ctx->expanded_macros.at(macro).expanded_cells) {
            if (!ctx->cells.count(cell))
                continue;
            macro_cells[cell] = ctx->cells.at(cell).get();
        }
        // If we have RAM32s in the macro; aet up a fast index by read port
        idict<std::array<IdString, 6>> ram32_groups;
        std::vector<pool<IdString>> ram32_by_group;
        for (auto cell : macro_cells) {
            if (!is_ram32_type(cell.second->type))
                continue;
            auto key = get_ram32_key(cell.second);
            int group = ram32_groups(key);
            if (group >= int(ram32_by_group.size()))
                ram32_by_group.resize(group + 1);
            ram32_by_group.at(group).insert(cell.first);
        }
        // Group cells into chunks; starting with the largest mux and working down to cells not associated with a mux at
        // all
        std::vector<dict<IdString, int>> groups;
        pool<IdString> grouped_cells;
        for (IdString type : {id_MUXF9, id_MUXF8, id_MUXF7, id_RAMD32M64, IdString()}) {
            for (auto cell : macro_cells) {
                if (grouped_cells.count(cell.first))
                    continue;
                if ((type == IdString() && is_lutram_type(cell.second->type)) ||
                    ((type == id_RAMD32M64 && cell.second->type == type))) {
                    // For cells not part of a mux cascade
                    groups.emplace_back();
                    groups.back()[cell.first] = 0;
                    grouped_cells.insert(cell.first);
                    if (is_ram32_type(cell.second->type)) {
                        // Also try and add another RAM32 to the cascade
                        auto key = get_ram32_key(cell.second);
                        for (auto ram32 : ram32_by_group.at(ram32_groups(key))) {
                            if (ram32 != cell.first && !grouped_cells.count(ram32) &&
                                (type != id_RAMD32M64 || macro_cells.at(ram32)->type == id_RAMD32)) {
                                groups.back()[ram32] = 1;
                                grouped_cells.insert(ram32);
                                break;
                            }
                        }
                    }
                } else if (type != IdString() && cell.second->type == type) {
                    groups.emplace_back();
                    auto &group = groups.back();
                    label_muxtree(cell.second, group);
                    for (auto group_cell : group)
                        grouped_cells.insert(group_cell.first);
                }
            }
        }
        // Arbitrarily pick the first RAM cell as the cluster root, we are using abs place idx so it doesn't matter
        CellInfo *root = nullptr;
        for (auto cell : macro_cells) {
            if (is_lutram_type(cell.second->type)) {
                root = cell.second;
                break;
            }
        }
        auto do_constrain = [&](CellInfo *entry, uint32_t eighth, LogicBelIdx::LogicBel bel) {
            entry->cluster = root->name;
            root->cluster_info.cluster_cells.push_back(entry);
            auto &c = entry->cluster_info;
            c.site_dx = c.site_dy = 0;
            c.tile_dx = c.tile_dy = 0;
            c.type = ClusterInfo::ABS_PLACE_IDX;
            c.place_idx = LogicBelIdx(eighth, bel).idx;
        };
        int eighth = (ctx->family == ArchFamily::XC7) ? 4 : 8; // work downwards from the top
        auto constrain_group = [&](const dict<IdString, int> &group) {
            int height = 1;
            bool is_ram32 = false;
            for (auto entry : group) {
                height = std::max(height, entry.second + 1);
                is_ram32 |= is_ram32_type(macro_cells.at(entry.first)->type);
            }
            if (is_ram32)
                height = 1; // group offsets in RAM32 groups are not eighths but LUT5/LUT6
            int start_eighth = eighth - height;
            eighth -= height;
            // Do the placing itself
            for (auto entry : group) {
                if (is_ram32) {
                    do_constrain(macro_cells.at(entry.first), start_eighth,
                                 (entry.second == 1) ? LogicBelIdx::LUT5 : LogicBelIdx::LUT6);
                } else {
                    do_constrain(macro_cells.at(entry.first), start_eighth + entry.second,
                                 get_mux_bel(macro_cells.at(entry.first)->type));
                }
            }
        };
        // First constrain the write port at the highest position (mode=0) then everything else (mode=1)
        for (int mode = 0; mode < 2; mode++) {
            for (auto &group : groups) {
                bool is_wport = false;
                for (auto entry : group) {
                    if (is_spram_type(macro_cells.at(entry.first)->type) ||
                        (macro_cells.at(entry.first)->macro_inst.str(ctx).find("SP") != std::string::npos)) {
                        is_wport = true;
                        break;
                    }
                }
                if (is_wport != (mode == 0))
                    continue;
                constrain_group(group);
            }
        }
        // Copy WCLK inversion
        // TODO: split up INIT params too; for Vivado flows this will be resolved downstream from the unexpanded macro
        // in the logical netlist but if we do bitgen ourselves that doesn't help...
        for (auto &entry : macro_cells) {
            if (!is_lutram_type(entry.second->type))
                continue;
            entry.second->params[id_IS_CLK_INVERTED] =
                    bool_or_default(ctx->expanded_macros.at(macro).params, id_IS_CLK_INVERTED);
        }
    }
    void constrain_lutram()
    {
        for (IdString macro : find_lutram_macros())
            constrain_lutram_macro(macro);
    }
    void run()
    {
        combine_split_carries();
        if (ctx->family != ArchFamily::XC7)
            carry4_to_carry8();
        constrain_carry_chains();
        constrain_lutram();
    }
};
} // namespace

void Arch::pack_logic()
{
    LogicPacker packer(getCtx());
    packer.run();
}

NEXTPNR_NAMESPACE_END

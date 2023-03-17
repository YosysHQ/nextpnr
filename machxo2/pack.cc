/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018-19  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
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

#include <algorithm>
#include <iterator>
#include "cells.h"
#include "design_utils.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// Pack LUTs and LUT-FF pairs
static void pack_lut_lutffs(Context *ctx)
{
    log_info("Packing LUT-FFs..\n");

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ci->name.c_str(ctx), ci->type.c_str(ctx));
        if (is_lut(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_machxo2_cell(ctx, id_TRELLIS_SLICE, ci->name.str(ctx) + "_LC");
            for (auto &attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;

            packed_cells.insert(ci->name);
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ci->name.c_str(ctx), packed->name.c_str(ctx));
            // See if we can pack into a DFF. Both LUT4 and FF outputs are
            // available for a given slice, so we can pack a FF even if the
            // LUT4 drives more than one FF.
            NetInfo *o = ci->ports.at(id_Z).net;
            CellInfo *dff = net_only_drives(ctx, o, is_ff, id_DI, false);
            auto lut_bel = ci->attrs.find(id_BEL);
            bool packed_dff = false;

            if (dff) {
                if (ctx->verbose)
                    log_info("found attached dff %s\n", dff->name.c_str(ctx));
                auto dff_bel = dff->attrs.find(id_BEL);
                if (lut_bel != ci->attrs.end() && dff_bel != dff->attrs.end() && lut_bel->second != dff_bel->second) {
                    // Locations don't match, can't pack
                } else {
                    lut_to_lc(ctx, ci, packed.get(), false);
                    dff_to_lc(ctx, dff, packed.get(), LutType::Normal);
                    if (dff_bel != dff->attrs.end())
                        packed->attrs[id_BEL] = dff_bel->second;
                    packed_cells.insert(dff->name);
                    if (ctx->verbose)
                        log_info("packed cell %s into %s\n", dff->name.c_str(ctx), packed->name.c_str(ctx));
                    packed_dff = true;
                }
            }
            if (!packed_dff) {
                lut_to_lc(ctx, ci, packed.get(), true);
            }
            new_cells.push_back(std::move(packed));
        }
    }

    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

static void pack_remaining_ffs(Context *ctx)
{
    log_info("Packing remaining FFs..\n");

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();

        if (is_ff(ctx, ci)) {
            if (ctx->verbose)
                log_info("cell '%s' of type '%s remains unpacked'\n", ci->name.c_str(ctx), ci->type.c_str(ctx));

            std::unique_ptr<CellInfo> packed = create_machxo2_cell(ctx, id_TRELLIS_SLICE, ci->name.str(ctx) + "_LC");
            for (auto &attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;

            auto dff_bel = ci->attrs.find(id_BEL);

            dff_to_lc(ctx, ci, packed.get(), LutType::None);

            if (dff_bel != ci->attrs.end())
                packed->attrs[id_BEL] = dff_bel->second;
            packed_cells.insert(ci->name);
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ci->name.c_str(ctx), packed->name.c_str(ctx));

            new_cells.push_back(std::move(packed));
        }
    }

    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

// Merge a net into a constant net
static void set_net_constant(Context *ctx, NetInfo *orig, NetInfo *constnet, bool constval)
{
    (void)constval;

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    orig->driver.cell = nullptr;
    for (auto user : orig->users) {
        if (user.cell != nullptr) {
            CellInfo *uc = user.cell;
            if (ctx->verbose)
                log_info("%s user %s\n", orig->name.c_str(ctx), uc->name.c_str(ctx));

            if (uc->type == id_TRELLIS_FF && user.port == id_DI) {
                log_info("TRELLIS_FF %s is driven by a constant\n", uc->name.c_str(ctx));

                std::unique_ptr<CellInfo> lc = create_machxo2_cell(ctx, id_TRELLIS_SLICE, uc->name.str(ctx) + "_CONST");
                for (auto &attr : uc->attrs)
                    lc->attrs[attr.first] = attr.second;

                dff_to_lc(ctx, uc, lc.get(), LutType::PassThru);
                packed_cells.insert(uc->name);

                lc->ports[id_A0].net = constnet;
                user.cell = lc.get();
                user.port = id_A0;

                new_cells.push_back(std::move(lc));
            } else {
                uc->ports[user.port].net = constnet;
            }

            user.cell->ports.at(user.port).user_idx = constnet->users.add(user);
        }
    }
    orig->users.clear();

    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

// Pack constants (based on simple implementation in generic).
// VCC/GND cells provided by nextpnr automatically.
static void pack_constants(Context *ctx)
{
    log_info("Packing constants..\n");

    std::unique_ptr<CellInfo> const_cell = create_machxo2_cell(ctx, id_TRELLIS_SLICE, "$PACKER_CONST");
    const_cell->params[id_LUT0_INITVAL] = Property(0, 16);
    const_cell->params[id_LUT1_INITVAL] = Property(0xFFFF, 16);

    NetInfo *gnd_net = ctx->createNet(ctx->id("$PACKER_GND_NET"));
    gnd_net->driver.cell = const_cell.get();
    gnd_net->driver.port = id_F0;
    const_cell->ports.at(id_F0).net = gnd_net;

    NetInfo *vcc_net = ctx->createNet(ctx->id("$PACKER_VCC_NET"));
    vcc_net->name = ctx->id("$PACKER_VCC_NET");
    vcc_net->driver.cell = const_cell.get();
    vcc_net->driver.port = id_F1;
    const_cell->ports.at(id_F1).net = vcc_net;

    std::vector<IdString> dead_nets;

    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        if (ni->driver.cell != nullptr && ni->driver.cell->type == id_GND) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, gnd_net, false);
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        } else if (ni->driver.cell != nullptr && ni->driver.cell->type == id_VCC) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, vcc_net, true);
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        }
    }

    ctx->cells[const_cell->name] = std::move(const_cell);

    for (auto dn : dead_nets) {
        ctx->nets.erase(dn);
    }
}

static bool is_nextpnr_iob(Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("$nextpnr_ibuf") || cell->type == ctx->id("$nextpnr_obuf") ||
           cell->type == ctx->id("$nextpnr_iobuf");
}

static bool is_trellis_iob(const Context *ctx, const CellInfo *cell) { return cell->type == id_TRELLIS_IO; }

static bool nextpnr_iob_connects_only_trellis_iob(Context *ctx, CellInfo *iob, NetInfo *&top)
{
    NPNR_ASSERT(is_nextpnr_iob(ctx, iob));

    if (iob->type == ctx->id("$nextpnr_ibuf")) {
        NetInfo *o = iob->ports.at(id_O).net;
        top = o;

        CellInfo *fio = net_only_drives(ctx, o, is_trellis_iob, id_B, true);
        return fio != nullptr;
    } else if (iob->type == ctx->id("$nextpnr_obuf")) {
        NetInfo *i = iob->ports.at(id_I).net;
        top = i;

        // If connected to a TRELLIS_IO PAD, the net attached to an I port of an
        // $nextpnr_obuf will not have a driver, only users; an inout port
        // like PAD cannot be a driver in nextpnr. So net_driven_by won't
        // return anything. We exclude the IOB as one of the two users because
        // we already know that the net drives the $nextpnr_obuf.
        CellInfo *fio = net_only_drives(ctx, i, is_trellis_iob, id_B, true, iob);
        return fio != nullptr;
    } else if (iob->type == ctx->id("$nextpnr_iobuf")) {
        NetInfo *o = iob->ports.at(id_O).net;
        top = o;

        // When split_io is enabled in a frontend (it is for JSON), the I and O
        // ports of a $nextpnr_iobuf are split; the I port connects to the
        // driver of the original net before IOB insertion, and the O port
        // connects everything else. Because TRELLIS_IO PADs cannot be a driver
        // in nextpnr, the we can safely ignore the I port of an $nextpnr_iobuf
        // for any JSON input we're interested in accepting.
        CellInfo *fio_o = net_only_drives(ctx, o, is_trellis_iob, id_B, true);
        return fio_o != nullptr;
    }

    // Unreachable!
    NPNR_ASSERT(false);
}

// Pack IO buffers- Right now, all this does is remove $nextpnr_[io]buf cells.
// User is expected to manually instantiate TRELLIS_IO with BEL/IO_TYPE
// attributes.
static void pack_io(Context *ctx)
{
    pool<IdString> packed_cells;

    log_info("Packing IOs..\n");

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_nextpnr_iob(ctx, ci)) {
            NetInfo *top;

            if (!nextpnr_iob_connects_only_trellis_iob(ctx, ci, top))
                log_error("Top level net '%s' is not connected to a TRELLIS_IO PAD port.\n", top->name.c_str(ctx));

            if (ctx->verbose)
                log_info("Removing top-level IOBUF '%s' of type '%s'\n", ci->name.c_str(ctx), ci->type.c_str(ctx));

            for (auto &p : ci->ports)
                ci->disconnectPort(p.first);
            packed_cells.insert(ci->name);
        } else if (is_trellis_iob(ctx, ci)) {
            // If TRELLIS_IO has LOC attribute, convert the LOC (pin) to a BEL
            // attribute and place TRELLIS_IO at resulting BEL location. A BEL
            // attribute already on a TRELLIS_IO is an error. Attributes on
            // the pin attached to the PAD of TRELLIS_IO are ignored by this
            // packing phase.
            auto loc_attr_cell = ci->attrs.find(id_LOC);
            auto bel_attr_cell = ci->attrs.find(id_BEL);

            if (loc_attr_cell != ci->attrs.end()) {
                if (bel_attr_cell != ci->attrs.end()) {
                    log_error("IO buffer %s has both a BEL attribute and LOC attribute.\n", ci->name.c_str(ctx));
                }

                log_info("found LOC attribute on IO buffer %s.\n", ci->name.c_str(ctx));
                std::string pin = loc_attr_cell->second.as_string();

                BelId pinBel = ctx->getPackagePinBel(pin);
                if (pinBel == BelId()) {
                    log_error("IO buffer '%s' constrained to pin '%s', which does not exist for package '%s'.\n",
                              ci->name.c_str(ctx), pin.c_str(), ctx->package_name);
                } else {
                    log_info("pin '%s' constrained to Bel '%s'.\n", ci->name.c_str(ctx), ctx->nameOfBel(pinBel));
                }
                ci->attrs[id_BEL] = ctx->getBelName(pinBel).str(ctx);
            }
        }
    }

    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
}

// Main pack function
bool Arch::pack()
{
    Context *ctx = getCtx();
    try {
        log_break();
        pack_constants(ctx);
        pack_io(ctx);
        pack_lut_lutffs(ctx);
        pack_remaining_ffs(ctx);
        ctx->settings[id_pack] = 1;
        ctx->assignArchInfo();
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END

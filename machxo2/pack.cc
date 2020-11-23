/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018-19  David Shah <david@symbioticeda.com>
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
#include <unordered_set>
#include "cells.h"
#include "design_utils.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// Merge a net into a constant net
static void set_net_constant(const Context *ctx, NetInfo *orig, NetInfo *constnet, bool constval)
{
    (void) constval;

    orig->driver.cell = nullptr;
    for (auto user : orig->users) {
        if (user.cell != nullptr) {
            CellInfo *uc = user.cell;
            if (ctx->verbose)
                log_info("%s user %s\n", orig->name.c_str(ctx), uc->name.c_str(ctx));

            uc->ports[user.port].net = constnet;
            constnet->users.push_back(user);
        }
    }
    orig->users.clear();
}


// Pack constants (based on simple implementation in generic).
// VCC/GND cells provided by nextpnr automatically.
static void pack_constants(Context *ctx)
{
    log_info("Packing constants..\n");

    std::unique_ptr<CellInfo> const_cell = create_machxo2_cell(ctx, id_FACADE_SLICE, "$PACKER_CONST");
    const_cell->params[id_LUT0_INITVAL] = Property(0, 16);
    const_cell->params[id_LUT1_INITVAL] = Property(0xFFFF, 16);

    std::unique_ptr<NetInfo> gnd_net = std::unique_ptr<NetInfo>(new NetInfo);
    gnd_net->name = ctx->id("$PACKER_GND_NET");
    gnd_net->driver.cell = const_cell.get();
    gnd_net->driver.port = id_F0;
    const_cell->ports.at(id_F0).net = gnd_net.get();

    std::unique_ptr<NetInfo> vcc_net = std::unique_ptr<NetInfo>(new NetInfo);
    vcc_net->name = ctx->id("$PACKER_VCC_NET");
    vcc_net->driver.cell = const_cell.get();
    vcc_net->driver.port = id_F1;
    const_cell->ports.at(id_F1).net = vcc_net.get();

    std::vector<IdString> dead_nets;

    for (auto net : sorted(ctx->nets)) {
        NetInfo *ni = net.second;
        if (ni->driver.cell != nullptr && ni->driver.cell->type == ctx->id("GND")) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, gnd_net.get(), false);
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        } else if (ni->driver.cell != nullptr && ni->driver.cell->type == ctx->id("VCC")) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, vcc_net.get(), true);
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        }
    }

    ctx->cells[const_cell->name] = std::move(const_cell);
    ctx->nets[gnd_net->name] = std::move(gnd_net);
    ctx->nets[vcc_net->name] = std::move(vcc_net);

    for (auto dn : dead_nets) {
        ctx->nets.erase(dn);
    }
}


// Main pack function
bool Arch::pack()
{
    Context *ctx = getCtx();
    log_info("Packing implementation goes here..");
    try {
        log_break();
        pack_constants(ctx);
        ctx->settings[ctx->id("pack")] = 1;
        ctx->assignArchInfo();
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END

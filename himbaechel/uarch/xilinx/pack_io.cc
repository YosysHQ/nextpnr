/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019-23  Myrtle Shah <gatecat@ds0.me>
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
#include <boost/algorithm/string.hpp>
#include <queue>
#include <unordered_set>
#include "design_utils.h"
#include "extra_data.h"
#include "log.h"
#include "nextpnr.h"
#include "pack.h"
#include "pins.h"

#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

CellInfo *XilinxPacker::insert_obuf(IdString name, IdString type, NetInfo *i, NetInfo *o, NetInfo *tri)
{
    auto obuf = create_cell(type, name);
    obuf->connectPort(id_I, i);
    obuf->connectPort(id_T, tri);
    obuf->connectPort(id_O, o);
    return obuf;
}

CellInfo *XilinxPacker::insert_outinv(IdString name, NetInfo *i, NetInfo *o)
{
    auto inv = create_cell(id_INV, name);
    inv->connectPort(id_I, i);
    inv->connectPort(id_O, o);
    return inv;
}

CellInfo *XC7Packer::insert_ibuf(IdString name, IdString type, NetInfo *i, NetInfo *o)
{
    auto inbuf = create_cell(type, name);
    inbuf->connectPort(id_I, i);
    inbuf->connectPort(id_O, o);
    return inbuf;
}

CellInfo *XC7Packer::insert_diffibuf(IdString name, IdString type, const std::array<NetInfo *, 2> &i, NetInfo *o)
{
    auto inbuf = create_cell(type, name);
    inbuf->connectPort(id_I, i[0]);
    inbuf->connectPort(id_IB, i[1]);
    inbuf->connectPort(id_O, o);
    return inbuf;
}

NetInfo *XilinxPacker::invert_net(NetInfo *toinv)
{
    if (toinv == nullptr)
        return nullptr;
    // If net is driven by an inverter, don't double-invert, which could cause problems with timing
    // and IOLOGIC packing
    if (toinv->driver.cell != nullptr && toinv->driver.cell->type == id_LUT1 &&
        int_or_default(toinv->driver.cell->params, id_INIT, 0) == 1) {
        NetInfo *preinv = toinv->driver.cell->getPort(id_I0);
        // If only one user, also sweep the inversion LUT to avoid packing issues
        if (toinv->users.entries() == 1) {
            packed_cells.insert(toinv->driver.cell->name);
            toinv->driver.cell->disconnectPort(id_I0);
            toinv->driver.cell->disconnectPort(id_O);
        }
        return preinv;
    } else {
        NetInfo *inv = ctx->createNet(ctx->idf("%s$inverted%d", toinv->name.c_str(ctx), autoidx++));
        create_lut(inv->name.str(ctx) + "$lut", {toinv}, inv, Property(1));
        return inv;
    }
}

std::pair<CellInfo *, PortRef> XilinxPacker::insert_pad_and_buf(CellInfo *npnr_io)
{
    // Given a nextpnr IO buffer, create a PAD instance and insert an IO buffer if one isn't already present
    std::pair<CellInfo *, PortRef> result;
    std::unique_ptr<CellInfo> pad_cell = std::make_unique<CellInfo>(ctx, npnr_io->name, id_PAD);
    pad_cell->addInout(id_PAD);
    // Copy IO attributes to pad
    for (auto &attr : npnr_io->attrs)
        pad_cell->attrs[attr.first] = attr.second;
    NetInfo *ionet = nullptr;
    PortRef iobuf;
    iobuf.cell = nullptr;
    if (npnr_io->type == ctx->id("$nextpnr_obuf") || npnr_io->type == ctx->id("$nextpnr_iobuf")) {
        ionet = npnr_io->getPort(id_I);
        if (ionet != nullptr && ionet->driver.cell != nullptr)
            if (toplevel_ports.count(ionet->driver.cell->type) &&
                toplevel_ports.at(ionet->driver.cell->type).count(ionet->driver.port)) {
                if (ionet->users.entries() > 1)
                    log_error("IO buffer '%s' is connected to more than a single top level IO pin.\n",
                              ionet->driver.cell->name.c_str(ctx));
                iobuf = ionet->driver;
            }
        pad_cell->attrs[id_X_IO_DIR] = std::string(npnr_io->type == ctx->id("$nextpnr_obuf") ? "OUT" : "INOUT");
    }
    if (npnr_io->type == ctx->id("$nextpnr_ibuf") || npnr_io->type == ctx->id("$nextpnr_iobuf")) {
        ionet = npnr_io->getPort(id_O);
        if (ionet != nullptr)
            for (auto &usr : ionet->users)
                if (toplevel_ports.count(usr.cell->type) && toplevel_ports.at(usr.cell->type).count(usr.port)) {
                    if (ionet->users.entries() > 1)
                        log_error("IO buffer '%s' is connected to more than a single top level IO pin.\n",
                                  usr.cell->name.c_str(ctx));
                    iobuf = usr;
                }
        pad_cell->attrs[id_X_IO_DIR] = std::string(npnr_io->type == ctx->id("$nextpnr_ibuf") ? "IN" : "INOUT");
    }

    if (!iobuf.cell) {
        // No IO buffer, need to create one
        log_error(
                "   IO port '%s' is missing an IO buffer, do you need to remove -noiopad from your Yosys arguments?\n",
                npnr_io->name.c_str(ctx));
    } else {
        log_info("    IO port '%s' driven by %s '%s'\n", npnr_io->name.c_str(ctx), iobuf.cell->type.c_str(ctx),
                 iobuf.cell->name.c_str(ctx));
    }

    NPNR_ASSERT(ionet != nullptr);

    for (auto &port : npnr_io->ports)
        npnr_io->disconnectPort(port.first);

    pad_cell->connectPort(id_PAD, ionet);
    if (iobuf.cell->ports.at(iobuf.port).net != ionet) {
        iobuf.cell->disconnectPort(iobuf.port);
        iobuf.cell->connectPort(iobuf.port, ionet);
    }

    result.first = pad_cell.get();
    result.second = iobuf;
    // delete the npnr io and then add the pad, to avoid a name conflict
    for (auto &port : npnr_io->ports) {
        npnr_io->disconnectPort(port.first);
    }
    ctx->cells.erase(npnr_io->name);

    ctx->cells[result.first->name] = std::move(pad_cell);

    return result;
}

void XC7Packer::decompose_iob(CellInfo *xil_iob, bool is_hr, const std::string &iostandard)
{
    bool is_se_ibuf = xil_iob->type.in(id_IBUF, id_IBUF_IBUFDISABLE, id_IBUF_INTERMDISABLE);
    bool is_se_iobuf = xil_iob->type.in(id_IOBUF, id_IOBUF_DCIEN, id_IOBUF_INTERMDISABLE);
    bool is_se_obuf = xil_iob->type.in(id_OBUF, id_OBUFT);

    auto pad_site = [&](NetInfo *n) {
        for (auto user : n->users)
            if (user.cell->type == id_PAD)
                return uarch->get_bel_site(ctx->getBelByNameStr(user.cell->attrs[id_BEL].as_string()));
        NPNR_ASSERT_FALSE(("can't find PAD for net " + n->name.str(ctx)).c_str());
    };

    if (is_se_ibuf || is_se_iobuf) {
        log_info("Generating input buffer for '%s'\n", xil_iob->name.c_str(ctx));
        NetInfo *pad_net = xil_iob->getPort(is_se_iobuf ? id_IO : id_I);
        NPNR_ASSERT(pad_net != nullptr);
        auto site = pad_site(pad_net);
        if (!is_se_iobuf)
            xil_iob->disconnectPort(id_I);

        NetInfo *top_out = xil_iob->getPort(id_O);
        xil_iob->disconnectPort(id_O);

        IdString ibuf_type = id_IBUF;
        if (xil_iob->type.in(id_IBUF_IBUFDISABLE, id_IOBUF_DCIEN))
            ibuf_type = id_IBUF_IBUFDISABLE;
        if (xil_iob->type.in(id_IBUF_INTERMDISABLE, id_IOBUF_INTERMDISABLE))
            ibuf_type = id_IBUF_INTERMDISABLE;

        CellInfo *inbuf = insert_ibuf(int_name(xil_iob->name, "IBUF", is_se_iobuf), ibuf_type, pad_net, top_out);
        std::string tile = ctx->get_tile_type(site.tile).str(ctx);
        if (boost::starts_with(tile, "RIOB18"))
            ctx->bindBel(uarch->get_site_bel(site, ctx->id("IOB18.INBUF_DCIEN")), inbuf, STRENGTH_LOCKED);
        else
            ctx->bindBel(uarch->get_site_bel(site, ctx->id("IOB33.INBUF_EN")), inbuf, STRENGTH_LOCKED);
        xil_iob->movePortTo(id_IBUFDISABLE, inbuf, id_IBUFDISABLE);
        xil_iob->movePortTo(id_INTERMDISABLE, inbuf, id_INTERMDISABLE);
    }

    if (is_se_obuf || is_se_iobuf) {
        log_info("Generating output buffer for '%s'\n", xil_iob->name.c_str(ctx));
        NetInfo *pad_net = xil_iob->getPort(is_se_iobuf ? id_IO : id_O);
        NPNR_ASSERT(pad_net != nullptr);
        auto site = pad_site(pad_net);
        xil_iob->disconnectPort(is_se_iobuf ? id_IO : id_O);
        bool has_dci = xil_iob->type == id_IOBUF_DCIEN;
        CellInfo *obuf = insert_obuf(
                int_name(xil_iob->name, (is_se_iobuf || xil_iob->type == id_OBUFT) ? "OBUFT" : "OBUF", !is_se_obuf),
                is_se_iobuf ? (has_dci ? id_OBUFT_DCIEN : id_OBUFT) : xil_iob->type, xil_iob->getPort(id_I), pad_net,
                xil_iob->getPort(id_T));
        std::string tile = ctx->get_tile_type(site.tile).str(ctx);
        if (boost::starts_with(tile, "RIOB18"))
            ctx->bindBel(uarch->get_site_bel(site, ctx->id("IOB18.OUTBUF_DCIEN")), obuf, STRENGTH_LOCKED);
        else
            ctx->bindBel(uarch->get_site_bel(site, ctx->id("IOB33.OUTBUF")), obuf, STRENGTH_LOCKED);
        xil_iob->movePortTo(id_DCITERMDISABLE, obuf, id_DCITERMDISABLE);
    }

    bool is_diff_ibuf = xil_iob->type.in(id_IBUFDS, id_IBUFDS_INTERMDISABLE, id_IBUFDS);
    bool is_diff_iobuf = xil_iob->type.in(id_IOBUFDS, id_IOBUFDS_DCIEN);
    bool is_diff_out_iobuf =
            xil_iob->type.in(id_IOBUFDS_DIFF_OUT, id_IOBUFDS_DIFF_OUT_DCIEN, id_IOBUFDS_DIFF_OUT_INTERMDISABLE);
    bool is_diff_obuf = xil_iob->type.in(id_OBUFDS, id_OBUFTDS);

    if (is_diff_ibuf || is_diff_iobuf) {
        NetInfo *pad_p_net = xil_iob->getPort((is_diff_iobuf || is_diff_out_iobuf) ? id_IO : id_I);
        NPNR_ASSERT(pad_p_net != nullptr);
        auto site_p = pad_site(pad_p_net);
        NetInfo *pad_n_net = xil_iob->getPort((is_diff_iobuf || is_diff_out_iobuf) ? id_IOB : id_IB);
        NPNR_ASSERT(pad_n_net != nullptr);
        std::string tile_p = ctx->get_tile_type(site_p.tile).str(ctx);
        bool is_riob18 = boost::starts_with(tile_p, "RIOB18");

        if (!is_diff_iobuf && !is_diff_out_iobuf) {
            xil_iob->disconnectPort(id_I);
            xil_iob->disconnectPort(id_IB);
        }

        NetInfo *top_out = xil_iob->getPort(id_O);
        xil_iob->disconnectPort(id_O);

        IdString ibuf_type = id_IBUFDS;
        CellInfo *inbuf = insert_diffibuf(int_name(xil_iob->name, "IBUF", is_se_iobuf), ibuf_type,
                                          {pad_p_net, pad_n_net}, top_out);
        if (is_riob18) {
            ctx->bindBel(uarch->get_site_bel(site_p, ctx->id("IOB18M.INBUF_DCIEN")), inbuf, STRENGTH_LOCKED);
            inbuf->attrs[id_X_IOB_SITE_TYPE] = std::string("IOB18M");
        } else {
            ctx->bindBel(uarch->get_site_bel(site_p, ctx->id("IOB33M.INBUF_EN")), inbuf, STRENGTH_LOCKED);
            inbuf->attrs[id_X_IOB_SITE_TYPE] = std::string("IOB33M");
        }
    }

    if (is_diff_obuf || is_diff_out_iobuf || is_diff_iobuf) {
        // FIXME: true diff outputs
        NetInfo *pad_p_net = xil_iob->getPort((is_diff_iobuf || is_diff_out_iobuf) ? id_IO : id_O);
        NPNR_ASSERT(pad_p_net != nullptr);
        auto site_p = pad_site(pad_p_net);
        NetInfo *pad_n_net = xil_iob->getPort((is_diff_iobuf || is_diff_out_iobuf) ? id_IOB : id_OB);
        NPNR_ASSERT(pad_n_net != nullptr);
        auto site_n = pad_site(pad_n_net);
        std::string tile_p = ctx->get_tile_type(site_p.tile).str(ctx);
        bool is_riob18 = boost::starts_with(tile_p, "RIOB18");

        xil_iob->disconnectPort((is_diff_iobuf || is_diff_out_iobuf) ? id_IO : id_O);
        xil_iob->disconnectPort((is_diff_iobuf || is_diff_out_iobuf) ? id_IOB : id_OB);

        NetInfo *inv_i = create_internal_net(xil_iob->name, is_diff_obuf ? "I_B" : "OBUFTDS$subnet$I_B");
        CellInfo *inv = insert_outinv(int_name(xil_iob->name, is_diff_obuf ? "INV" : "OBUFTDS$subcell$INV"),
                                      xil_iob->getPort(id_I), inv_i);
        if (is_riob18) {
            ctx->bindBel(uarch->get_site_bel(site_n, ctx->id("IOB18S.O_ININV")), inv, STRENGTH_LOCKED);
            inv->attrs[id_X_IOB_SITE_TYPE] = std::string("IOB18S");
        } else {
            ctx->bindBel(uarch->get_site_bel(site_n, ctx->id("IOB33S.O_ININV")), inv, STRENGTH_LOCKED);
            inv->attrs[id_X_IOB_SITE_TYPE] = std::string("IOB33S");
        }

        bool has_dci = xil_iob->type.in(id_IOBUFDS_DCIEN, id_IOBUFDSE3);

        CellInfo *obuf_p = insert_obuf(int_name(xil_iob->name, is_diff_obuf ? "P" : "OBUFTDS$subcell$P"),
                                       (is_diff_iobuf || is_diff_out_iobuf || (xil_iob->type == id_OBUFTDS))
                                               ? (has_dci ? id_OBUFT_DCIEN : id_OBUFT)
                                               : id_OBUF,
                                       xil_iob->getPort(id_I), pad_p_net, xil_iob->getPort(id_T));

        if (is_riob18) {
            ctx->bindBel(uarch->get_site_bel(site_p, ctx->id("IOB18M.OUTBUF_DCIEN")), obuf_p, STRENGTH_LOCKED);
            obuf_p->attrs[id_X_IOB_SITE_TYPE] = std::string("IOB18M");
        } else {
            ctx->bindBel(uarch->get_site_bel(site_p, ctx->id("IOB33M.OUTBUF")), obuf_p, STRENGTH_LOCKED);
            obuf_p->attrs[id_X_IOB_SITE_TYPE] = std::string("IOB33M");
        }
        obuf_p->connectPort(id_DCITERMDISABLE, xil_iob->getPort(id_DCITERMDISABLE));

        CellInfo *obuf_n = insert_obuf(int_name(xil_iob->name, is_diff_obuf ? "N" : "OBUFTDS$subcell$N"),
                                       (is_diff_iobuf || is_diff_out_iobuf || (xil_iob->type == id_OBUFTDS))
                                               ? (has_dci ? id_OBUFT_DCIEN : id_OBUFT)
                                               : id_OBUF,
                                       inv_i, pad_n_net, xil_iob->getPort(id_T));

        if (is_riob18) {
            ctx->bindBel(uarch->get_site_bel(site_n, ctx->id("IOB18S.OUTBUF_DCIEN")), obuf_n, STRENGTH_LOCKED);
            obuf_n->attrs[id_X_IOB_SITE_TYPE] = std::string("IOB18S");
        } else {
            ctx->bindBel(uarch->get_site_bel(site_n, ctx->id("IOB33S.OUTBUF")), obuf_n, STRENGTH_LOCKED);
            obuf_n->attrs[id_X_IOB_SITE_TYPE] = std::string("IOB33S");
        }
        obuf_n->connectPort(id_DCITERMDISABLE, xil_iob->getPort(id_DCITERMDISABLE));

        xil_iob->disconnectPort(id_DCITERMDISABLE);
    }
}

void XC7Packer::pack_io()
{
    log_info("Inserting IO buffers..\n");

    get_top_level_pins(ctx, toplevel_ports);
    // Insert PAD cells on top level IO, and IO buffers where one doesn't exist already
    std::vector<CellInfo *> npnr_io;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf") ||
            ci->type == ctx->id("$nextpnr_obuf"))
            npnr_io.push_back(ci);
    }
    std::vector<std::pair<CellInfo *, PortRef>> pad_and_buf;
    for (auto ci : npnr_io) {
        pad_and_buf.push_back(insert_pad_and_buf(ci));
    }
    flush_cells();
    pool<BelId> used_io_bels;
    int unconstr_io_count = 0;
    for (auto &iob : pad_and_buf) {
        CellInfo *pad = iob.first;
        // Process location constraints
        if (pad->attrs.count(id_PACKAGE_PIN)) {
            pad->attrs[id_LOC] = pad->attrs.at(id_PACKAGE_PIN);
        }
        if (pad->attrs.count(id_LOC)) {
            std::string loc = pad->attrs.at(id_LOC).to_string();
            BelId bel = ctx->get_package_pin_bel(ctx->id(loc));
            if (bel == BelId())
                log_error("Unable to constrain IO '%s', device does not have a pin named '%s'\n", pad->name.c_str(ctx),
                          loc.c_str());
            log_info("    Constraining '%s' to pad '%s'\n", pad->name.c_str(ctx), ctx->nameOfBel(bel));
            pad->attrs[id_BEL] = std::string(ctx->nameOfBel(bel));
        }
        if (pad->attrs.count(id_BEL)) {
            used_io_bels.insert(ctx->getBelByNameStr(pad->attrs.at(id_BEL).as_string()));
        } else {
            ++unconstr_io_count;
        }
    }
    // Constrain unconstrained IO
    for (auto &iob : pad_and_buf) {
        CellInfo *pad = iob.first;
        if (!pad->attrs.count(id_BEL)) {
            log_error("FIXME: unconstrained IO not supported (pad %s)\n", ctx->nameOf(pad));
            ;
        }
    }
    // Decompose macro IO primitives to smaller primitives that map logically to the actual IO Bels
    for (auto &iob : pad_and_buf) {
        if (packed_cells.count(iob.second.cell->name))
            continue;
        decompose_iob(iob.second.cell, true, str_or_default(iob.first->attrs, id_IOSTANDARD, ""));
        packed_cells.insert(iob.second.cell->name);
    }
    flush_cells();

    dict<IdString, XFormRule> hriobuf_rules, hpiobuf_rules;
    hriobuf_rules[id_OBUF].new_type = id_IOB33_OUTBUF;
    hriobuf_rules[id_OBUF].port_xform[id_I] = id_IN;
    hriobuf_rules[id_OBUF].port_xform[id_O] = id_OUT;
    hriobuf_rules[id_OBUF].port_xform[id_T] = id_TRI;
    hriobuf_rules[id_OBUFT] = XFormRule(hriobuf_rules[id_OBUF]);

    hriobuf_rules[id_IBUF].new_type = id_IOB33_INBUF_EN;
    hriobuf_rules[id_IBUF].port_xform[id_I] = id_PAD;
    hriobuf_rules[id_IBUF].port_xform[id_O] = id_OUT;
    hriobuf_rules[id_IBUF_INTERMDISABLE] = XFormRule(hriobuf_rules[id_IBUF]);
    hriobuf_rules[id_IBUF_IBUFDISABLE] = XFormRule(hriobuf_rules[id_IBUF]);
    hriobuf_rules[id_IBUFDS_INTERMDISABLE_INT] = XFormRule(hriobuf_rules[id_IBUF]);
    hriobuf_rules[id_IBUFDS_INTERMDISABLE_INT].port_xform[id_IB] = id_DIFFI_IN;
    hriobuf_rules[id_IBUFDS] = XFormRule(hriobuf_rules[id_IBUF]);
    hriobuf_rules[id_IBUFDS].port_xform[id_IB] = id_DIFFI_IN;

    hpiobuf_rules[id_OBUF].new_type = id_IOB18_OUTBUF_DCIEN;
    hpiobuf_rules[id_OBUF].port_xform[id_I] = id_IN;
    hpiobuf_rules[id_OBUF].port_xform[id_O] = id_OUT;
    hpiobuf_rules[id_OBUF].port_xform[id_T] = id_TRI;
    hpiobuf_rules[id_OBUFT] = XFormRule(hpiobuf_rules[id_OBUF]);

    hpiobuf_rules[id_IBUF].new_type = id_IOB18_INBUF_DCIEN;
    hpiobuf_rules[id_IBUF].port_xform[id_I] = id_PAD;
    hpiobuf_rules[id_IBUF].port_xform[id_O] = id_OUT;
    hpiobuf_rules[id_IBUF_INTERMDISABLE] = XFormRule(hpiobuf_rules[id_IBUF]);
    hpiobuf_rules[id_IBUF_IBUFDISABLE] = XFormRule(hpiobuf_rules[id_IBUF]);
    hriobuf_rules[id_IBUFDS_INTERMDISABLE_INT] = XFormRule(hriobuf_rules[id_IBUF]);
    hpiobuf_rules[id_IBUFDS_INTERMDISABLE_INT].port_xform[id_IB] = id_DIFFI_IN;
    hpiobuf_rules[id_IBUFDS] = XFormRule(hpiobuf_rules[id_IBUF]);
    hpiobuf_rules[id_IBUFDS].port_xform[id_IB] = id_DIFFI_IN;

    // Special xform for OBUFx and IBUFx.
    dict<IdString, XFormRule> rules;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (!ci->attrs.count(id_BEL) && ci->bel == BelId())
            continue;
        std::string belname =
                ci->attrs.count(id_BEL) ? ci->attrs[id_BEL].as_string() : std::string(ctx->nameOfBel(ci->bel));
        size_t pos = belname.find(".");
        if (belname.substr(pos + 1, 5) == "IOB18")
            rules = hpiobuf_rules;
        else if (belname.substr(pos + 1, 5) == "IOB33")
            rules = hriobuf_rules;
        else
            log_error("Unexpected IOBUF BEL %s\n", belname.c_str());
        if (rules.count(ci->type)) {
            xform_cell(rules, ci);
        }
    }

    dict<IdString, XFormRule> hrio_rules;
    hrio_rules[id_PAD].new_type = id_PAD;

    hrio_rules[id_INV].new_type = id_INVERTER;
    hrio_rules[id_INV].port_xform[id_I] = id_IN;
    hrio_rules[id_INV].port_xform[id_O] = id_OUT;

    hrio_rules[id_PS7].new_type = id_PS7_PS7;

    generic_xform(hrio_rules, true);

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        std::string type = ci->type.str(ctx);
        if (!boost::starts_with(type, "IOB33") && !boost::starts_with(type, "IOB18"))
            continue;
        if (!ci->attrs.count(id_X_IOB_SITE_TYPE))
            continue;
        type.replace(0, 5, ci->attrs.at(id_X_IOB_SITE_TYPE).as_string());
        ci->type = ctx->id(type);
    }

    // check all PAD cells for IOSTANDARD/DRIVE
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        std::string type = ci->type.str(ctx);
        if (type != "PAD")
            continue;
        check_valid_pad(ci, type);
    }
}

void XC7Packer::check_valid_pad(CellInfo *ci, std::string type)
{
    auto iostandard_id = id_IOSTANDARD;
    auto iostandard_attr = ci->attrs.find(iostandard_id);
    if (iostandard_attr == ci->attrs.end())
        log_error("port %s has no IOSTANDARD property", ci->name.c_str(ctx));

    auto iostandard = iostandard_attr->second.as_string();
    if (!boost::starts_with(iostandard, "LVTTL") && !boost::starts_with(iostandard, "LVCMOS"))
        return;

    auto drive_attr = ci->attrs.find(id_DRIVE);
    // no drive strength attribute: use default
    if (drive_attr == ci->attrs.end())
        return;
    auto drive = drive_attr->second.as_int64();

    bool is_iob33 = boost::starts_with(type, "IOB33");
    if (is_iob33) {
        if (drive == 4 || drive == 8 || drive == 12)
            return;
        if (iostandard != "LVCMOS12" && drive == 16)
            return;
        if ((iostandard == "LVCMOS18" || iostandard == "LVTTL") && drive == 24)
            return;
    } else { // IOB18
        if (drive == 2 || drive == 4 || drive == 6 || drive == 8)
            return;
        if (iostandard != "LVCMOS12" && (drive == 12 || drive == 16))
            return;
    }

    log_error("unsupported DRIVE strength property %s for port %s", drive_attr->second.c_str(), ci->name.c_str(ctx));
}

NEXTPNR_NAMESPACE_END

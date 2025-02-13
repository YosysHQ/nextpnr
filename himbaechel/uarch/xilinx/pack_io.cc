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

SiteIndex XC7Packer::get_ologic_site(BelId io_bel)
{
    BelId ibc_bel;
    if (boost::contains(uarch->bel_name_in_site(io_bel).str(ctx), "IOB18"))
        ibc_bel = uarch->get_site_bel(uarch->get_bel_site(io_bel), ctx->id("IOB18.OUTBUF_DCIEN"));
    else
        ibc_bel = uarch->get_site_bel(uarch->get_bel_site(io_bel), ctx->id("IOB33.OUTBUF"));
    NPNR_ASSERT(ibc_bel != BelId());

    std::queue<WireId> visit;
    visit.push(ctx->getBelPinWire(ibc_bel, id_IN));

    while (!visit.empty()) {
        WireId cursor = visit.front();
        visit.pop();
        for (auto bp : ctx->getWireBelPins(cursor)) {
            auto site = uarch->get_bel_site(bp.bel);
            if (boost::starts_with(uarch->get_site_name(site).str(ctx), "OLOGIC"))
                return site;
        }
        for (auto pip : ctx->getPipsUphill(cursor))
            visit.push(ctx->getPipSrcWire(pip));
    }
    NPNR_ASSERT_FALSE("failed to find OLOGIC");
}

SiteIndex XC7Packer::get_ilogic_site(BelId io_bel)
{
    BelId ibc_bel;
    if (boost::contains(uarch->bel_name_in_site(io_bel).str(ctx), "IOB18"))
        ibc_bel = uarch->get_site_bel(uarch->get_bel_site(io_bel), ctx->id("IOB18.INBUF_DCIEN"));
    else
        ibc_bel = uarch->get_site_bel(uarch->get_bel_site(io_bel), ctx->id("IOB33.INBUF_EN"));

    NPNR_ASSERT(ibc_bel != BelId());

    std::queue<WireId> visit;
    visit.push(ctx->getBelPinWire(ibc_bel, id_OUT));

    while (!visit.empty()) {
        WireId cursor = visit.front();
        visit.pop();
        for (auto bp : ctx->getWireBelPins(cursor)) {
            auto site = uarch->get_bel_site(bp.bel);
            if (boost::starts_with(uarch->get_site_name(site).str(ctx), "ILOGIC"))
                return site;
        }
        for (auto pip : ctx->getPipsDownhill(cursor))
            visit.push(ctx->getPipDstWire(pip));
    }
    NPNR_ASSERT_FALSE("failed to find ILOGIC");
}

SiteIndex XC7Packer::get_idelay_site(BelId io_bel)
{
    BelId ibc_bel;
    if (boost::contains(uarch->bel_name_in_site(io_bel).str(ctx), "IOB18"))
        ibc_bel = uarch->get_site_bel(uarch->get_bel_site(io_bel), ctx->id("IOB18.INBUF_DCIEN"));
    else
        ibc_bel = uarch->get_site_bel(uarch->get_bel_site(io_bel), ctx->id("IOB33.INBUF_EN"));

    NPNR_ASSERT(ibc_bel != BelId());

    std::queue<WireId> visit;
    visit.push(ctx->getBelPinWire(ibc_bel, id_OUT));

    while (!visit.empty()) {
        WireId cursor = visit.front();
        visit.pop();
        for (auto bp : ctx->getWireBelPins(cursor)) {
            auto site = uarch->get_bel_site(bp.bel);
            if (boost::starts_with(uarch->get_site_name(site).str(ctx), "IDELAY"))
                return site;
        }
        for (auto pip : ctx->getPipsDownhill(cursor))
            visit.push(ctx->getPipDstWire(pip));
    }
    NPNR_ASSERT_FALSE("failed to find IDELAY");
}

SiteIndex XC7Packer::get_odelay_site(BelId io_bel)
{
    BelId obc_bel;
    if (boost::contains(uarch->bel_name_in_site(io_bel).str(ctx), "IOB18"))
        obc_bel = uarch->get_site_bel(uarch->get_bel_site(io_bel), ctx->id("IOB18.OUTBUF_DCIEN"));
    else
        log_error("BEL %s is located on a high range bank. High range banks do not have ODELAY\n",
                  ctx->nameOfBel(io_bel));

    std::queue<WireId> visit;
    visit.push(ctx->getBelPinWire(obc_bel, id_IN));

    while (!visit.empty()) {
        WireId cursor = visit.front();
        visit.pop();
        for (auto bp : ctx->getWireBelPins(cursor)) {
            auto site = uarch->get_bel_site(bp.bel);
            if (boost::starts_with(uarch->get_site_name(site).str(ctx), "ODELAY"))
                return site;
        }
        for (auto pip : ctx->getPipsUphill(cursor))
            visit.push(ctx->getPipSrcWire(pip));
    }
    NPNR_ASSERT_FALSE("failed to find ODELAY");
}

SiteIndex XC7Packer::get_ioctrl_site(BelId io_bel)
{
    int hclk_tile = uarch->hclk_for_iob(io_bel);
    const auto &extra_data = uarch->tile_extra_data(hclk_tile);

    for (int site = 0; site < extra_data->sites.ssize(); site++) {
        auto &site_data = extra_data->sites[site];
        if (boost::starts_with(IdString(site_data.name_prefix).str(ctx), "IDELAYCTRL"))
            return SiteIndex(hclk_tile, site);
    }

    NPNR_ASSERT_FALSE("failed to find IOCTRL");
}

void XC7Packer::fold_inverter(CellInfo *cell, std::string port)
{
    IdString p = ctx->id(port);
    NetInfo *net = cell->getPort(p);
    if (!net)
        return;
    CellInfo *drv = net->driver.cell;
    if (!drv)
        return;
    if (drv->type == id_LUT1 && int_or_default(drv->params, id_INIT, 0) == 1) {
        cell->disconnectPort(p);
        NetInfo *preinv = drv->getPort(id_I0);
        cell->connectPort(p, preinv);
        cell->params[ctx->idf("IS_%s_INVERTED", port.c_str())] = 1;
        if (net->users.empty())
            packed_cells.insert(drv->name);
    } else if (drv->type == id_INV) {
        cell->disconnectPort(p);
        NetInfo *preinv = drv->getPort(id_I);
        cell->connectPort(p, preinv);
        cell->params[ctx->idf("IS_%s_INVERTED", port.c_str())] = 1;
        if (net->users.empty())
            packed_cells.insert(drv->name);
    }
}

void XC7Packer::pack_iologic()
{
    log_info("Packing IOLOGIC...\n");
    dict<IdString, BelId> iodelay_to_io;
    dict<IdString, XFormRule> iologic_rules;

    // IDDR
    iologic_rules[id_IDDR].new_type = id_ILOGICE3_IFF;
    iologic_rules[id_IDDR].port_multixform[id_C] = {id_CK, id_CKB};
    iologic_rules[id_IDDR].port_xform[id_S] = id_SR;
    iologic_rules[id_IDDR].port_xform[id_R] = id_SR;

    // SERDES
    iologic_rules[id_ISERDESE2].new_type = id_ISERDESE2_ISERDESE2;
    iologic_rules[id_OSERDESE2].new_type = id_OSERDESE2_OSERDESE2;

    // DELAY
    iologic_rules[id_IDELAYE2].new_type = id_IDELAYE2_IDELAYE2;
    iologic_rules[id_ODELAYE2].new_type = id_ODELAYE2_ODELAYE2;

    // Handles pseudo-diff output buffers without finding multiple sinks
    auto find_p_outbuf = [&](NetInfo *net) {
        CellInfo *outbuf = nullptr;
        for (auto &usr : net->users) {
            IdString type = usr.cell->type;
            if (type.in(id_IOB33_OUTBUF, id_IOB33M_OUTBUF, id_IOB18_OUTBUF_DCIEN, id_IOB18M_OUTBUF_DCIEN)) {
                if (outbuf != nullptr)
                    return (CellInfo *)nullptr; // drives multiple outputs
                outbuf = usr.cell;
            } else if (type == id_ODELAYE2) {
                auto dataout = usr.cell->ports.find(id_DATAOUT);
                if (dataout != usr.cell->ports.end()) {
                    for (auto &user : dataout->second.net->users) {
                        IdString dataout_type = user.cell->type;
                        if (dataout_type.in(id_IOB18_OUTBUF_DCIEN, id_IOB18M_OUTBUF_DCIEN)) {
                            if (outbuf != nullptr)
                                return (CellInfo *)nullptr; // drives multiple outputs
                            outbuf = user.cell;
                        }
                    }
                } else {
                    if (outbuf != nullptr)
                        return (CellInfo *)nullptr; // drives multiple outputs
                }
            }
        }
        return outbuf;
    };

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_IDELAYE2) {
            NetInfo *d = ci->getPort(id_IDATAIN);
            if (!d || !d->driver.cell)
                log_error("%s '%s' has disconnected IDATAIN input\n", ci->type.c_str(ctx), ctx->nameOf(ci));
            CellInfo *drv = d->driver.cell;
            BelId io_bel;
            if (boost::contains(drv->type.str(ctx), "INBUF_EN") || boost::contains(drv->type.str(ctx), "INBUF_DCIEN"))
                io_bel = drv->bel;
            else
                log_error("%s '%s' has IDATAIN input connected to illegal cell type %s\n", ci->type.c_str(ctx),
                          ctx->nameOf(ci), drv->type.c_str(ctx));
            SiteIndex iol_site = get_idelay_site(io_bel);

            BelId idelay_bel = uarch->get_site_bel(iol_site, id_IDELAYE2);
            NPNR_ASSERT(idelay_bel != BelId());
            log_info("   binding input delay cell '%s' to bel '%s'\n", ctx->nameOf(ci), ctx->nameOfBel(idelay_bel));
            ctx->bindBel(idelay_bel, ci, STRENGTH_LOCKED);

            ci->attrs[id_X_IO_BEL] = ctx->getBelName(io_bel).str(ctx);
            iodelay_to_io[ci->name] = io_bel;
        } else if (ci->type == id_ODELAYE2) {
            NetInfo *dataout = ci->getPort(id_DATAOUT);
            if (!dataout || dataout->users.empty())
                log_error("%s '%s' has disconnected DATAOUT input\n", ci->type.c_str(ctx), ctx->nameOf(ci));
            BelId io_bel;
            auto no_users = dataout->users.entries();
            for (auto userport : dataout->users) {
                CellInfo *user = userport.cell;
                auto user_type = user->type.str(ctx);
                // OBUFDS has the negative pin connected to an inverter
                if (no_users == 2 && user_type == "INVERTER")
                    continue;
                if (boost::contains(user_type, "OUTBUF_EN") || boost::contains(user_type, "OUTBUF_DCIEN"))
                    io_bel = user->bel;
                else
                    // TODO: support SIGNAL_PATTERN = CLOCK
                    log_error("%s '%s' has DATAOUT connected to unsupported cell type %s\n", ci->type.c_str(ctx),
                              ctx->nameOf(ci), user_type.c_str());
            }
            SiteIndex iol_site = get_odelay_site(io_bel);

            BelId odelay_bel = uarch->get_site_bel(iol_site, id_ODELAYE2);
            NPNR_ASSERT(odelay_bel != BelId());
            log_info("   binding output delay cell '%s' to bel '%s'\n", ctx->nameOf(ci), ctx->nameOfBel(odelay_bel));
            ctx->bindBel(odelay_bel, ci, STRENGTH_LOCKED);

            ci->attrs[id_X_IO_BEL] = ctx->getBelName(io_bel).str(ctx);
            iodelay_to_io[ci->name] = io_bel;
        }
    }

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_ODDR) {
            NetInfo *q = ci->getPort(id_Q);
            if (q == nullptr || q->users.empty())
                log_error("%s '%s' has disconnected Q output\n", ci->type.c_str(ctx), ctx->nameOf(ci));
            BelId io_bel;
            CellInfo *ob = find_p_outbuf(q);
            if (ob != nullptr)
                io_bel = ob->bel;
            else
                log_error("%s '%s' has illegal fanout on Q output\n", ci->type.c_str(ctx), ctx->nameOf(ci));
            SiteIndex ol_site = get_ologic_site(io_bel);

            PortRef dest_port = *q->users.begin();
            auto is_tristate = dest_port.port == id_TRI;

            dict<IdString, XFormRule> oddr_rules;
            if (boost::contains(uarch->get_site_name(ol_site).str(ctx), "IOB18"))
                oddr_rules[id_ODDR].new_type = is_tristate ? id_OLOGICE2_TFF : id_OLOGICE2_OUTFF;
            else
                oddr_rules[id_ODDR].new_type = is_tristate ? id_OLOGICE3_TFF : id_OLOGICE3_OUTFF;
            oddr_rules[id_ODDR].port_xform[id_C] = id_CK;
            oddr_rules[id_ODDR].port_xform[id_S] = id_SR;
            oddr_rules[id_ODDR].port_xform[id_R] = id_SR;
            xform_cell(oddr_rules, ci);

            BelId oddr_bel = uarch->get_site_bel(ol_site, is_tristate ? ctx->id("TFF") : ctx->id("OUTFF"));
            NPNR_ASSERT(oddr_bel != BelId());
            log_info("   binding output DDR cell '%s' to bel '%s'\n", ctx->nameOf(ci), ctx->nameOfBel(oddr_bel));
            ctx->bindBel(oddr_bel, ci, STRENGTH_LOCKED);
        } else if (ci->type == id_OSERDESE2) {
            NetInfo *q = ci->getPort(id_OQ);
            NetInfo *ofb = ci->getPort(id_OFB);
            bool q_disconnected = !q || q->users.empty();
            bool ofb_disconnected = !ofb || ofb->users.empty();
            if (q_disconnected && ofb_disconnected) {
                log_error("%s '%s' has disconnected OQ/OFB output ports\n", ci->type.c_str(ctx), ctx->nameOf(ci));
            }
            BelId io_bel;
            CellInfo *ob = !q_disconnected ? find_p_outbuf(q) : find_p_outbuf(ofb);
            if (ob != nullptr)
                io_bel = ob->bel;
            else
                log_error("%s '%s' has illegal fanout on OQ or OFB output\n", ci->type.c_str(ctx), ctx->nameOf(ci));

            SiteIndex ol_site = get_ologic_site(io_bel);

            BelId oserdes_bel = uarch->get_site_bel(ol_site, id_OSERDESE2);
            NPNR_ASSERT(oserdes_bel != BelId());
            log_info("   binding output SERDES cell '%s' to bel '%s'\n", ctx->nameOf(ci), ctx->nameOfBel(oserdes_bel));
            ctx->bindBel(oserdes_bel, ci, STRENGTH_LOCKED);

        } else if (ci->type == id_IDDR) {
            fold_inverter(ci, "C");

            BelId io_bel;
            NetInfo *d = ci->getPort(id_D);
            if (!d || !d->driver.cell)
                log_error("%s '%s' has disconnected D input\n", ci->type.c_str(ctx), ctx->nameOf(ci));
            CellInfo *drv = d->driver.cell;
            if (boost::contains(drv->type.str(ctx), "INBUF_EN") || boost::contains(drv->type.str(ctx), "INBUF_DCIEN"))
                io_bel = drv->bel;
            else if (boost::contains(drv->type.str(ctx), "IDELAYE2") && d->driver.port == id_DATAOUT)
                io_bel = iodelay_to_io.at(drv->name);
            else
                log_error("%s '%s' has D input connected to illegal cell type %s\n", ci->type.c_str(ctx),
                          ctx->nameOf(ci), drv->type.c_str(ctx));

            SiteIndex il_site = get_ilogic_site(io_bel);

            BelId iddr_bel = uarch->get_site_bel(il_site, ctx->id("IFF"));
            NPNR_ASSERT(iddr_bel != BelId());
            log_info("   binding input DDR cell '%s' to bel '%s'\n", ctx->nameOf(ci), ctx->nameOfBel(iddr_bel));
            ctx->bindBel(iddr_bel, ci, STRENGTH_LOCKED);
        } else if (ci->type == id_ISERDESE2) {
            fold_inverter(ci, "CLKB");
            fold_inverter(ci, "OCLKB");

            std::string iobdelay = str_or_default(ci->params, id_IOBDELAY, "NONE");
            BelId io_bel;

            if (iobdelay == "IFD") {
                NetInfo *d = ci->getPort(id_DDLY);
                if (!d || !d->driver.cell)
                    log_error("%s '%s' has disconnected DDLY input\n", ci->type.c_str(ctx), ctx->nameOf(ci));
                CellInfo *drv = d->driver.cell;
                if (boost::contains(drv->type.str(ctx), "IDELAYE2") && d->driver.port == id_DATAOUT)
                    io_bel = iodelay_to_io.at(drv->name);
                else
                    log_error("%s '%s' has DDLY input connected to illegal cell type %s\n", ci->type.c_str(ctx),
                              ctx->nameOf(ci), drv->type.c_str(ctx));
            } else if (iobdelay == "NONE") {
                NetInfo *d = ci->getPort(id_D);
                if (!d || !d->driver.cell)
                    log_error("%s '%s' has disconnected D input\n", ci->type.c_str(ctx), ctx->nameOf(ci));
                CellInfo *drv = d->driver.cell;
                if (boost::contains(drv->type.str(ctx), "INBUF_EN") ||
                    boost::contains(drv->type.str(ctx), "INBUF_DCIEN"))
                    io_bel = drv->bel;
                else
                    log_error("%s '%s' has D input connected to illegal cell type %s\n", ci->type.c_str(ctx),
                              ctx->nameOf(ci), drv->type.c_str(ctx));
            } else {
                log_error("%s '%s' has unsupported IOBDELAY value '%s'\n", ci->type.c_str(ctx), ctx->nameOf(ci),
                          iobdelay.c_str());
            }

            SiteIndex il_site = get_ilogic_site(io_bel);

            BelId iserdes_bel = uarch->get_site_bel(il_site, id_ISERDESE2);
            NPNR_ASSERT(iserdes_bel != BelId());
            log_info("   binding input SERDES cell '%s' to bel '%s'\n", ctx->nameOf(ci), ctx->nameOfBel(iserdes_bel));
            ctx->bindBel(iserdes_bel, ci, STRENGTH_LOCKED);
        }
    }

    flush_cells();
    generic_xform(iologic_rules, false);
    flush_cells();
}

void XC7Packer::pack_idelayctrl()
{
    CellInfo *idelayctrl = nullptr;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_IDELAYCTRL) {
            if (idelayctrl)
                log_error("Found more than one IDELAYCTRL cell!\n");
            idelayctrl = ci;
        }
    }
    if (!idelayctrl)
        return;
    std::set<SiteIndex> ioctrl_sites;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type.in(id_IDELAYE2_IDELAYE2, id_ODELAYE2_ODELAYE2)) {
            if (ci->bel == BelId())
                continue;
            ioctrl_sites.insert(get_ioctrl_site(ctx->getBelByNameStr(ci->attrs.at(id_X_IO_BEL).as_string())));
        }
    }
    if (ioctrl_sites.empty())
        log_error("Found IDELAYCTRL but no I/ODELAYs\n");
    NetInfo *rdy = idelayctrl->getPort(id_RDY);
    idelayctrl->disconnectPort(id_RDY);
    std::vector<NetInfo *> dup_rdys;
    int i = 0;
    for (auto site : ioctrl_sites) {
        CellInfo *dup_idc =
                create_cell(id_IDELAYCTRL, int_name(idelayctrl->name, "CTRL_DUP_" + std::to_string(i), false));
        dup_idc->connectPort(id_REFCLK, idelayctrl->getPort(id_REFCLK));
        dup_idc->connectPort(id_RST, idelayctrl->getPort(id_RST));
        if (rdy) {
            NetInfo *dup_rdy =
                    (ioctrl_sites.size() == 1)
                            ? rdy
                            : create_internal_net(idelayctrl->name, "CTRL_DUP_" + std::to_string(i) + "_RDY", false);
            dup_idc->connectPort(id_RDY, dup_rdy);
            dup_rdys.push_back(dup_rdy);
        }
        BelId idc_bel = uarch->get_site_bel(site, id_IDELAYCTRL);
        NPNR_ASSERT(idc_bel != BelId());
        ctx->bindBel(idc_bel, dup_idc, STRENGTH_LOCKED);
        ++i;
    }
    idelayctrl->disconnectPort(id_REFCLK);
    idelayctrl->disconnectPort(id_RST);

    if (rdy != nullptr) {
        // AND together all the RDY signals
        std::vector<NetInfo *> int_anded_rdy;
        int_anded_rdy.push_back(dup_rdys.front());
        for (size_t j = 1; j < dup_rdys.size(); j++) {
            NetInfo *anded_net =
                    (j == (dup_rdys.size() - 1))
                            ? rdy
                            : create_internal_net(idelayctrl->name, "ANDED_RDY_" + std::to_string(j), false);
            create_lut(idelayctrl->name.str(ctx) + "/RDY_AND_LUT_" + std::to_string(j),
                       {int_anded_rdy.at(j - 1), dup_rdys.at(j)}, anded_net, Property(8));
            int_anded_rdy.push_back(anded_net);
        }
    }

    packed_cells.insert(idelayctrl->name);
    flush_cells();

    ioctrl_rules[id_IDELAYCTRL].new_type = id_IDELAYCTRL_IDELAYCTRL;

    generic_xform(ioctrl_rules);
}

NEXTPNR_NAMESPACE_END

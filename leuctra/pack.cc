/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#include "nextpnr.h"
#include "design_utils.h"
#include "cells.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

static bool is_nextpnr_iob(Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("$nextpnr_ibuf") || cell->type == ctx->id("$nextpnr_obuf") ||
           cell->type == ctx->id("$nextpnr_iobuf");
}

static bool is_iob(Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("IOB");
}

class LeuctraPacker
{
  public:
    LeuctraPacker(Context *ctx) : ctx(ctx){};

  private:
    // Process the contents of packed_cells and new_cells
    void flush_cells()
    {
        for (auto pcell : packed_cells) {
            ctx->cells.erase(pcell);
        }
        for (auto &ncell : new_cells) {
            ctx->cells[ncell->name] = std::move(ncell);
        }
        packed_cells.clear();
        new_cells.clear();
    }

    CellInfo *fetch_nxio(CellInfo *cell, IdString port) {
        PortInfo pi = cell->ports.at(port);
        if (pi.net == nullptr)
	    return nullptr;
	CellInfo *res = nullptr;
	IdString res_port;
        if (is_nextpnr_iob(ctx, pi.net->driver.cell)) {
	    res = pi.net->driver.cell;
	    res_port = pi.net->driver.port;
	} else if (pi.net->driver.cell) {
            if (pi.net->driver.cell != cell || pi.net->driver.port != port) {
		log_error("Stray driver on net %s: %s %s\n", pi.net->name.c_str(ctx), pi.net->driver.cell->name.c_str(ctx), pi.net->driver.port.c_str(ctx));
	    }
	}
        for (auto &usr : pi.net->users)
            if (usr.cell == cell && usr.port == port) {
		continue;
	    } else if (is_nextpnr_iob(ctx, usr.cell)) {
		if (res) {
		    log_error("Two nextpnr bufs on net %s: %s %s\n", pi.net->name.c_str(ctx), usr.cell->name.c_str(ctx), res->name.c_str(ctx));
		}
		res = usr.cell;
		res_port = usr.port;
	    } else {
		log_error("Stray load on net %s: %s %s\n", pi.net->name.c_str(ctx), usr.cell->name.c_str(ctx), usr.port.c_str(ctx));
	    }
	if (!res)
	    return nullptr;
	// Kill the connection.
	disconnect_port(ctx, res, res_port);
	disconnect_port(ctx, cell, port);
	return res;
    }

    // Insert IOB cells for ports that don't have one yet.
    void insert_iob()
    {
        log_info("Inserting IOBs...\n");

	/* TODO: get this working maybe? */
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_nextpnr_iob(ctx, ci)) {
		    abort();
		NPNR_ASSERT_FALSE("SURVIVED");

                CellInfo *iob = nullptr;
                std::unique_ptr<CellInfo> io_cell =
                        create_leuctra_cell(ctx, ctx->id("IOB"), ci->name.str(ctx) + "$iob");
                nxio_to_iob(ctx, ci, io_cell.get(), new_cells, packed_cells);
                new_cells.push_back(std::move(io_cell));
                iob = new_cells.back().get();

                packed_cells.insert(ci->name);
                if (iob != nullptr) {
                    for (const auto &attr : ci->attrs)
                        iob->attrs[attr.first] = attr.second;
                    for (const auto &param : ci->params)
                        iob->params[param.first] = param.second;

                    auto loc_attr = iob->attrs.find(ctx->id("LOC"));
                    if (loc_attr != iob->attrs.end()) {
                        std::string pin = loc_attr->second.as_string();
                        BelId pinBel = ctx->getPackagePinBel(pin);
                        if (pinBel == BelId()) {
                            log_error("IO pin '%s' constrained to pin '%s', which does not exist for package '%s'.\n",
                                      iob->name.c_str(ctx), pin.c_str(), ctx->args.package.c_str());
                        } else {
                            log_info("pin '%s' constrained to Bel '%s'.\n", iob->name.c_str(ctx),
                                     ctx->getBelName(pinBel).c_str(ctx));
                        }
                        iob->attrs[ctx->id("BEL")] = Property(ctx->getBelName(pinBel).str(ctx));
                    }
                }
            }
        }

        flush_cells();
    }

    // Convert Xilinx IO buffer primitives into IOB cells.
    void convert_iob()
    {
	enum IoStdKind {
	    // Single ended, settable drive.
	    IOSTD_SINGLE_DRIVE,
	    // Single ended.
	    IOSTD_SINGLE,
	    // Pseudo-differential.
	    IOSTD_PSEUDO_DIFF,
	    // True differential.
	    IOSTD_DIFF,
	};
	std::map<std::string, IoStdKind> iostds;
	iostds["LVTTL"] = IOSTD_SINGLE_DRIVE;
	iostds["LVCMOS33"] = IOSTD_SINGLE_DRIVE;
	iostds["LVCMOS25"] = IOSTD_SINGLE_DRIVE;
	iostds["LVCMOS18"] = IOSTD_SINGLE_DRIVE;
	iostds["LVCMOS15"] = IOSTD_SINGLE_DRIVE;
	iostds["LVCMOS12"] = IOSTD_SINGLE_DRIVE;
	iostds["LVCMOS18_JEDEC"] = IOSTD_SINGLE_DRIVE;
	iostds["LVCMOS15_JEDEC"] = IOSTD_SINGLE_DRIVE;
	iostds["LVCMOS12_JEDEC"] = IOSTD_SINGLE_DRIVE;
	iostds["PCI33_3"] = IOSTD_SINGLE;
	iostds["PCI66_3"] = IOSTD_SINGLE;
	iostds["SDIO"] = IOSTD_SINGLE;
	iostds["MOBILE_DDR"] = IOSTD_SINGLE;
	iostds["DIFF_MOBILE_DDR"] = IOSTD_PSEUDO_DIFF;
	iostds["I2C"] = IOSTD_SINGLE;
	iostds["SMBUS"] = IOSTD_SINGLE;
	iostds["HSTL_I"] = IOSTD_SINGLE;
	iostds["DIFF_HSTL_I"] = IOSTD_PSEUDO_DIFF;
	iostds["HSTL_I_18"] = IOSTD_SINGLE;
	iostds["DIFF_HSTL_I_18"] = IOSTD_PSEUDO_DIFF;
	iostds["HSTL_II"] = IOSTD_SINGLE;
	iostds["DIFF_HSTL_II"] = IOSTD_PSEUDO_DIFF;
	iostds["HSTL_II_18"] = IOSTD_SINGLE;
	iostds["DIFF_HSTL_II_18"] = IOSTD_PSEUDO_DIFF;
	iostds["HSTL_III"] = IOSTD_SINGLE;
	iostds["DIFF_HSTL_III"] = IOSTD_PSEUDO_DIFF;
	iostds["HSTL_III_18"] = IOSTD_SINGLE;
	iostds["DIFF_HSTL_III_18"] = IOSTD_PSEUDO_DIFF;
	iostds["SSTL3_I"] = IOSTD_SINGLE;
	iostds["DIFF_SSTL3_I"] = IOSTD_PSEUDO_DIFF;
	iostds["SSTL2_I"] = IOSTD_SINGLE;
	iostds["DIFF_SSTL2_I"] = IOSTD_PSEUDO_DIFF;
	iostds["SSTL18_I"] = IOSTD_SINGLE;
	iostds["DIFF_SSTL18_I"] = IOSTD_PSEUDO_DIFF;
	iostds["SSTL3_II"] = IOSTD_SINGLE;
	iostds["DIFF_SSTL3_II"] = IOSTD_PSEUDO_DIFF;
	iostds["SSTL2_II"] = IOSTD_SINGLE;
	iostds["DIFF_SSTL2_II"] = IOSTD_PSEUDO_DIFF;
	iostds["SSTL18_II"] = IOSTD_SINGLE;
	iostds["DIFF_SSTL18_II"] = IOSTD_PSEUDO_DIFF;
	iostds["SSTL15_II"] = IOSTD_SINGLE;
	iostds["DIFF_SSTL15_II"] = IOSTD_PSEUDO_DIFF;
	iostds["BLVDS_25"] = IOSTD_PSEUDO_DIFF;
	iostds["LVPECL_25"] = IOSTD_PSEUDO_DIFF;
	iostds["LVPECL_33"] = IOSTD_PSEUDO_DIFF;
	iostds["DISPLAY_PORT"] = IOSTD_PSEUDO_DIFF;
	iostds["LVDS_33"] = IOSTD_DIFF;
	iostds["LVDS_25"] = IOSTD_DIFF;
	iostds["MINI_LVDS_33"] = IOSTD_DIFF;
	iostds["MINI_LVDS_25"] = IOSTD_DIFF;
	iostds["RSDS_33"] = IOSTD_DIFF;
	iostds["RSDS_25"] = IOSTD_DIFF;
	iostds["PPDS_33"] = IOSTD_DIFF;
	iostds["PPDS_25"] = IOSTD_DIFF;
	iostds["TMDS_33"] = IOSTD_DIFF;
	iostds["TML_33"] = IOSTD_DIFF;

        log_info("Converting IOBs...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;

	    IdString port_i, port_o, port_t, port_ib;
	    IdString port_pad_m, port_pad_s;
	    bool diff = false;

	    if (ci->type == ctx->id("IBUF") || ci->type == ctx->id("IBUFG")) {
		port_i = ctx->id("O");
		port_pad_m = ctx->id("I");
	    } else if (ci->type == ctx->id("IBUFDS") || ci->type == ctx->id("IBUFGDS")) {
		port_i = ctx->id("O");
		port_pad_m = ctx->id("I");
		port_pad_s = ctx->id("IB");
		diff = true;
	    } else if (ci->type == ctx->id("IBUFDS_DIFF_OUT") || ci->type == ctx->id("IBUFGDS_DIFF_OUT")) {
		port_i = ctx->id("O");
		port_ib = ctx->id("OB");
		port_pad_m = ctx->id("I");
		port_pad_s = ctx->id("IB");
		diff = true;
	    } else if (ci->type == ctx->id("IOBUF")) {
		port_i = ctx->id("O");
		port_o = ctx->id("I");
		port_t = ctx->id("T");
		port_pad_m = ctx->id("IO");
	    } else if (ci->type == ctx->id("IOBUFDS")) {
		port_i = ctx->id("O");
		port_o = ctx->id("I");
		port_t = ctx->id("T");
		port_pad_m = ctx->id("IO");
		port_pad_s = ctx->id("IOB");
		diff = true;
	    } else if (ci->type == ctx->id("OBUF")) {
		port_o = ctx->id("I");
		port_pad_m = ctx->id("O");
	    } else if (ci->type == ctx->id("OBUFDS")) {
		port_o = ctx->id("I");
		port_pad_m = ctx->id("O");
		port_pad_s = ctx->id("OB");
		diff = true;
	    } else if (ci->type == ctx->id("OBUFT")) {
		port_o = ctx->id("I");
		port_t = ctx->id("T");
		port_pad_m = ctx->id("O");
	    } else if (ci->type == ctx->id("OBUFTDS")) {
		port_o = ctx->id("I");
		port_t = ctx->id("T");
		port_pad_m = ctx->id("O");
		port_pad_s = ctx->id("OB");
		diff = true;
	    } else {
		// Not an IO buffer.
		continue;
	    }

	    // Get the nextpnr-inserted buffers.
            CellInfo *nxb_m = fetch_nxio(ci, port_pad_m);
            CellInfo *nxb_s = nullptr;
	    if (port_pad_s != IdString())
	        nxb_s = fetch_nxio(ci, port_pad_s);

	    if (!nxb_m)
		log_error("Buffer %s not connected to port.\n", ci->name.c_str(ctx));
	    packed_cells.insert(nxb_m->name);
	    if (nxb_s)
	        packed_cells.insert(nxb_s->name);

	    // Merge UCF constraints into the buffer.
            for (const auto &param : nxb_m->params)
                ci->params[param.first] = param.second;
            for (const auto &attr : nxb_m->attrs)
                ci->attrs[attr.first] = attr.second;

	    std::string iostd;
	    auto iostd_attr = ci->params.find(ctx->id("IOSTANDARD"));
	    if (iostd_attr != ci->params.end()) {
		iostd = iostd_attr->second.as_string();
	    } else {
		// Hm.
		if (diff)
		    iostd = "LVDS_33";
		else
		    iostd = "LVCMOS33";
	    }
	    if (iostds.find(iostd) == iostds.end())
		log_error("Unknown IO standard %s for buffer %s", iostd.c_str(), ci->name.c_str(ctx));
	    enum IoStdKind kind = iostds[iostd];
	    if (kind == IOSTD_PSEUDO_DIFF || kind == IOSTD_DIFF) {
		diff = true;
	    } else {
		if (diff) {
		    log_error("Single-ended IO standard %s for differential buffer %s", iostd.c_str(), ci->name.c_str(ctx));
		}
	    }

	    // Create the buffer cells.
            std::unique_ptr<CellInfo> iobm_cell =
                    create_leuctra_cell(ctx, ctx->id("IOB"), ci->name.str(ctx) + "$iob");
            new_cells.push_back(std::move(iobm_cell));
            CellInfo *iobm = new_cells.back().get();
	    CellInfo *iobs = nullptr;
	    if (diff) {
                std::unique_ptr<CellInfo> iobs_cell =
                    create_leuctra_cell(ctx, ctx->id("IOB"), ci->name.str(ctx) + "$iobs");
                new_cells.push_back(std::move(iobs_cell));
                iobs = new_cells.back().get();
		if (kind == IOSTD_DIFF) {
		    iobm->params[ctx->id("__MODE__")] = Property("IOBM");
		    iobs->params[ctx->id("__MODE__")] = Property("IOBS");
		} else {
		    iobm->params[ctx->id("__MODE__")] = Property("IOB");
		    iobs->params[ctx->id("__MODE__")] = Property("IOB");
		}
	    } else {
		iobm->params[ctx->id("__MODE__")] = Property("IOB");
	    }

	    // Deal with input path.
	    if (port_i != IdString()) {
	        replace_port(ci, port_i, iobm, ctx->id("I"));
		if (diff) {
		    iobs->params[ctx->id("PADOUTUSED")] = Property("0");
		    connect_ports(ctx, iobs, ctx->id("PADOUT"), iobm, ctx->id("DIFFI_IN"));
		}
		iobm->params[ctx->id("IMUX")] = Property("I");
		iobm->params[ctx->id("BYPASS_MUX")] = Property("I");
		iobm->params[ctx->id("ISTANDARD")] = Property(iostd);
		if (iobs)
		    iobm->params[ctx->id("ISTANDARD")] = Property(iostd);
	    }
	    if (port_ib != IdString()) {
	        replace_port(ci, port_ib, iobs, ctx->id("I"));
		if (diff) {
		    iobm->params[ctx->id("PADOUTUSED")] = Property("0");
		    connect_ports(ctx, iobm, ctx->id("PADOUT"), iobs, ctx->id("DIFFI_IN"));
		}
		iobs->params[ctx->id("IMUX")] = Property("I");
		iobs->params[ctx->id("BYPASS_MUX")] = Property("I");
	    }

	    // Deal with output path.
	    if (port_o != IdString()) {
	        NetInfo *net_o = ci->ports.at(port_o).net;
	        NetInfo *net_t = nullptr;
	        if (port_t != IdString())
                    net_t = ci->ports.at(port_t).net;
	        connect_port(ctx, net_o, iobm, ctx->id("O"));
		if (kind == IOSTD_PSEUDO_DIFF) {
		    // XXX
		    NPNR_ASSERT_FALSE("PSEUDO DIFF OUTPUT");
	            connect_port(ctx, net_o, iobs, ctx->id("O"));
		}
	        disconnect_port(ctx, ci, port_o);
	        if (net_t) {
	            connect_port(ctx, net_t, iobm, ctx->id("T"));
		    if (kind == IOSTD_PSEUDO_DIFF)
	                connect_port(ctx, net_t, iobs, ctx->id("T"));
	            disconnect_port(ctx, ci, port_t);
	        }
		iobm->params[ctx->id("OSTANDARD")] = Property(iostd);
		if (iobs)
		    iobs->params[ctx->id("OSTANDARD")] = Property(iostd);

		iobm->params[ctx->id("OUSED")] = Property("0");
		if (port_t != IdString())
		    iobm->params[ctx->id("TUSED")] = Property("0");

		if (kind == IOSTD_PSEUDO_DIFF) {
		    iobs->params[ctx->id("OUSED")] = Property("0");
		    if (port_t != IdString())
			iobs->params[ctx->id("TUSED")] = Property("0");
		}

		if (kind == IOSTD_SINGLE_DRIVE) {
		    if (ci->params.count(ctx->id("DRIVE")))
		        iobm->params[ctx->id("DRIVEATTRBOX")] = ci->params[ctx->id("DRIVE")];
		    else
		        iobm->params[ctx->id("DRIVEATTRBOX")] = Property("12");
		    if (ci->params.count(ctx->id("SLEW")))
		        iobm->params[ctx->id("SLEW")] = ci->params[ctx->id("SLEW")];
		    else
		        iobm->params[ctx->id("SLEW")] = Property("SLOW");
		}

	        if (kind == IOSTD_DIFF) {
		    connect_ports(ctx, iobm, ctx->id("DIFFO_OUT"), iobs, ctx->id("DIFFO_IN"));
		    iobs->params[ctx->id("OUTMUX")] = Property("0");
	        }
	    }

	    if (ci->params.count(ctx->id("PULLTYPE")))
		iobm->params[ctx->id("PULLTYPE")] = ci->params[ctx->id("PULLTYPE")];
	    if (ci->params.count(ctx->id("SUSPEND")))
		iobm->params[ctx->id("SUSPEND")] = ci->params[ctx->id("SUSPEND")];
	    if (ci->params.count(ctx->id("PRE_EMPHASIS")))
		iobm->params[ctx->id("PRE_EMPHASIS")] = ci->params[ctx->id("PRE_EMPHASIS")];

	    auto loc_attr = ci->attrs.find(ctx->id("LOC"));
	    if (loc_attr != ci->attrs.end()) {
		std::string pin = loc_attr->second.as_string();
		BelId pinBel = ctx->getPackagePinBel(pin);
		if (pinBel == BelId()) {
		    log_error("IO pin '%s' constrained to pin '%s', which does not exist for package '%s'.\n",
			      ci->name.c_str(ctx), pin.c_str(), ctx->args.package.c_str());
		} else {
		    log_info("pin '%s' constrained to Bel '%s'.\n", ci->name.c_str(ctx),
			     ctx->getBelName(pinBel).c_str(ctx));
		}
		iobm->attrs[ctx->id("BEL")] = Property(ctx->getBelName(pinBel).str(ctx));
		if (iobs) {
		    BelId opinBel = pinBel;
		    if ((pinBel.index & 1) && kind == IOSTD_DIFF) {
		        log_error("True differential IO pin '%s' constrained to pin '%s', which is not a master pin.\n",
			      ci->name.c_str(ctx), pin.c_str());
		        }
		    opinBel.index ^= 1;
		    iobs->attrs[ctx->id("BEL")] = Property(ctx->getBelName(opinBel).str(ctx));
		}
	    } else {
		if (iobs) {
	            // XXX
		    NPNR_ASSERT_FALSE("UNCONSTRAINED DIFF PAIR");
		}
	    }

            packed_cells.insert(ci->name);
	}

        flush_cells();
    }

    // Ensure ilogic/ologic cell for every IOB that needs one.
    void pack_iologic()
    {
        log_info("Packing ILOGICs/OLOGICs...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_iob(ctx, ci)) {
                NetInfo *net_i = ci->ports.at(ctx->id("I")).net;
                if (net_i != nullptr) {
		    // Insert ILOGIC.
                    std::unique_ptr<CellInfo> ilogic =
                            create_leuctra_cell(ctx, ctx->id("ILOGIC2"), ci->name.str(ctx) + "$ilogic");
                    insert_ilogic_pass(ctx, ci, ilogic.get());

                    new_cells.push_back(std::move(ilogic));
		}
                NetInfo *net_o = ci->ports.at(ctx->id("O")).net;
                if (net_o != nullptr) {
		    // Insert OLOGIC.
                    std::unique_ptr<CellInfo> ologic =
                            create_leuctra_cell(ctx, ctx->id("OLOGIC2"), ci->name.str(ctx) + "$ologic");
                    insert_ologic_pass(ctx, ci, ologic.get());

                    new_cells.push_back(std::move(ologic));
		}
                auto bel_attr = ci->attrs.find(ctx->id("BEL"));
                if (bel_attr != ci->attrs.end()) {
		    BelId bel = ctx->getBelByName(ctx->id(ci->attrs[ctx->id("BEL")].as_string()));
		    for (auto &child : ci->constr_children) {
			BelId child_bel = ctx->getRelatedBel(bel, child->constr_spec);
			child->attrs[ctx->id("BEL")] = Property(ctx->getBelName(child_bel).str(ctx));
			child->constr_parent = nullptr;
			child->constr_spec = -1;

		    }
		    ci->constr_children.clear();
		}
            }
        }

        flush_cells();
    }

    // Convert FFs/latches to LEUCTRA_FFs.
    void pack_bram()
    {
        log_info("Packing Block RAMs...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("RAMB16BWER")) {
		fixup_ramb16(ctx, ci, new_cells, packed_cells);
            } else if (ci->type == ctx->id("RAMB8BWER")) {
		fixup_ramb8(ctx, ci, new_cells, packed_cells);
	    }
        }

        flush_cells();
    }

    // Convert FFs/latches to LEUCTRA_FFs.
    void pack_ff()
    {
        log_info("Packing FFs...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("FDRE") ||
		ci->type == ctx->id("FDSE") ||
		ci->type == ctx->id("FDCE") ||
		ci->type == ctx->id("FDPE") ||
		ci->type == ctx->id("FDRE_1") ||
		ci->type == ctx->id("FDSE_1") ||
		ci->type == ctx->id("FDCE_1") ||
		ci->type == ctx->id("FDPE_1") ||
		ci->type == ctx->id("LDCE") ||
		ci->type == ctx->id("LDPE") ||
		ci->type == ctx->id("LDCE_1") ||
		ci->type == ctx->id("LDPE_1")) {
                std::unique_ptr<CellInfo> ff_cell =
                        create_leuctra_cell(ctx, ctx->id("LEUCTRA_FF"), ci->name.str(ctx) + "$ff");
                convert_ff(ctx, ci, ff_cell.get(), new_cells, packed_cells);
                new_cells.push_back(std::move(ff_cell));

                packed_cells.insert(ci->name);
            }
        }

        flush_cells();
    }

    // Convert RAMs to LEUCTRA_LCs.
    void pack_ram()
    {
        log_info("Packing distributed RAM...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
	    if (ci->type == ctx->id("RAM32X1D") || ci->type == ctx->id("RAM64X1D")) {
		int sz = ci->type == ctx->id("RAM32X1D") ? 5 : 6;
		CellInfo *lcs[2];
		for (int i = 0; i < 2; i++) {
			std::unique_ptr<CellInfo> lut_cell =
			    create_leuctra_cell(ctx, ctx->id("LEUCTRA_LC"), ci->name.str(ctx) + "$lc" + std::to_string(i));
			new_cells.push_back(std::move(lut_cell));
			lcs[i] = new_cells.back().get();
			lcs[i]->params[ctx->id("MODE")] = Property("RAM" + std::to_string(sz));
			if (sz == 6)
				lcs[i]->params[ctx->id("DIMUX")] = Property("XI");
			lcs[i]->attrs[ctx->id("NEEDS_M")] = Property(true);
		}
		NetInfo *net;
		bool net_inv;
		if (get_invertible_port(ctx, ci, ctx->id("WCLK"), false, true, net, net_inv)) {
		    set_invertible_port(ctx, lcs[0], ctx->id("CLK"), net, net_inv, true, new_cells);
		    set_invertible_port(ctx, lcs[1], ctx->id("CLK"), net, net_inv, true, new_cells);
		}
		net = ci->ports[ctx->id("WE")].net;
		disconnect_port(ctx, ci, ctx->id("WE"));
		connect_port(ctx,net, lcs[0], ctx->id("WE"));
		connect_port(ctx,net, lcs[1], ctx->id("WE"));
		for (int i = 0; i < sz; i++) {
			IdString pname = ctx->id("A" + std::to_string(i));
			net = ci->ports[pname].net;
			disconnect_port(ctx, ci, pname);
			connect_port(ctx,net, lcs[0], ctx->id("WA" + std::to_string(i+1)));
			connect_port(ctx,net, lcs[1], ctx->id("WA" + std::to_string(i+1)));
			connect_port(ctx,net, lcs[1], ctx->id("RA" + std::to_string(i+1)));
			pname = ctx->id("DPRA" + std::to_string(i));
			net = ci->ports[pname].net;
			disconnect_port(ctx, ci, pname);
			connect_port(ctx,net, lcs[0], ctx->id("RA" + std::to_string(i+1)));
		}
		net = ci->ports[ctx->id("D")].net;
		disconnect_port(ctx, ci, ctx->id("D"));
		if (sz == 5) {
			connect_port(ctx,net, lcs[0], ctx->id("DDI5"));
			connect_port(ctx,net, lcs[1], ctx->id("DDI5"));
			set_const_port(ctx, lcs[0], ctx->id("RA6"), true, new_cells);
			set_const_port(ctx, lcs[1], ctx->id("RA6"), true, new_cells);
		} else {
			connect_port(ctx,net, lcs[0], ctx->id("XI"));
			connect_port(ctx,net, lcs[1], ctx->id("XI"));
		}
		replace_port(ci, ctx->id("SPO"), lcs[1], ctx->id("O6"));
		replace_port(ci, ctx->id("DPO"), lcs[0], ctx->id("O6"));
		Property v = ci->params[ctx->id("INIT")];
		Property nv(0, 64);
		if (sz == 5) {
			for (int i = 0; i < 64; i++)
				nv.str[i] = v.str[i%32];
			nv.update_intval();
		} else {
			nv = v;
		}
		lcs[0]->params[ctx->id("INIT")] = nv;
		lcs[1]->params[ctx->id("INIT")] = nv;
		lcs[1]->attrs[ctx->id("LOCMASK")] = Property(0x8, 4);
		lcs[0]->constr_parent = lcs[1];
		lcs[0]->constr_z = -3;
		lcs[1]->constr_children.push_back(lcs[0]);
		packed_cells.insert(ci->name);
	    } else if (ci->type == ctx->id("RAM128X1D")) {
		CellInfo *lcs[4];
		for (int i = 0; i < 4; i++) {
			std::unique_ptr<CellInfo> lut_cell =
			    create_leuctra_cell(ctx, ctx->id("LEUCTRA_LC"), ci->name.str(ctx) + "$lc" + std::to_string(i));
			new_cells.push_back(std::move(lut_cell));
			lcs[i] = new_cells.back().get();
			lcs[i]->params[ctx->id("MODE")] = Property("RAM7");
			lcs[i]->params[ctx->id("DIMUX")] = Property("DDI7");
			lcs[i]->attrs[ctx->id("NEEDS_M")] = Property(true);
		}
		NetInfo *net;
		bool net_inv;
		if (get_invertible_port(ctx, ci, ctx->id("WCLK"), false, true, net, net_inv)) {
		    for (int i = 0; i < 4; i++)
		        set_invertible_port(ctx, lcs[i], ctx->id("CLK"), net, net_inv, true, new_cells);
		}
		net = ci->ports[ctx->id("WE")].net;
		disconnect_port(ctx, ci, ctx->id("WE"));
		for (int i = 0; i < 4; i++)
		    connect_port(ctx,net, lcs[i], ctx->id("WE"));
		for (int i = 0; i < 7; i++) {
			IdString pname = ctx->id("A[" + std::to_string(i) + "]");
			net = ci->ports[pname].net;
			disconnect_port(ctx, ci, pname);
			for (int j = 0; j < 4; j++)
			    connect_port(ctx,net, lcs[j], ctx->id("WA" + std::to_string(i+1)));
			if (i < 6) {
			    for (int j = 2; j < 4; j++)
			        connect_port(ctx,net, lcs[j], ctx->id("RA" + std::to_string(i+1)));
			} else {
			    connect_port(ctx,net, lcs[2], ctx->id("XI"));
			}

			pname = ctx->id("DPRA[" + std::to_string(i) + "]");
			net = ci->ports[pname].net;
			disconnect_port(ctx, ci, pname);
			if (i < 6) {
			    for (int j = 0; j < 2; j++)
			        connect_port(ctx,net, lcs[j], ctx->id("RA" + std::to_string(i+1)));
			} else {
			    connect_port(ctx,net, lcs[0], ctx->id("XI"));
			}
		}
		net = ci->ports[ctx->id("D")].net;
		disconnect_port(ctx, ci, ctx->id("D"));
		for (int i = 0; i < 4; i++)
			connect_port(ctx,net, lcs[i], ctx->id("DDI7"));
		replace_port(ci, ctx->id("SPO"), lcs[2], ctx->id("MO"));
		replace_port(ci, ctx->id("DPO"), lcs[0], ctx->id("MO"));

		connect_ports(ctx, lcs[3], ctx->id("O6"), lcs[2], ctx->id("DMI0"));
		connect_ports(ctx, lcs[2], ctx->id("O6"), lcs[2], ctx->id("DMI1"));
		connect_ports(ctx, lcs[1], ctx->id("O6"), lcs[0], ctx->id("DMI0"));
		connect_ports(ctx, lcs[0], ctx->id("O6"), lcs[0], ctx->id("DMI1"));
		
		Property v = ci->params[ctx->id("INIT")];
		Property nv(0, 64);
		for (int i = 0; i < 64; i++)
			nv.str[i] = v.str[i];
		nv.update_intval();
		lcs[3]->params[ctx->id("INIT")] = nv;
		lcs[1]->params[ctx->id("INIT")] = nv;
		for (int i = 0; i < 64; i++)
			nv.str[i] = v.str[i + 64];
		nv.update_intval();
		lcs[2]->params[ctx->id("INIT")] = nv;
		lcs[0]->params[ctx->id("INIT")] = nv;

		lcs[3]->attrs[ctx->id("LOCMASK")] = Property(0x8, 4);
		for (int i = 0; i < 3; i++) {
		    lcs[i]->constr_parent = lcs[3];
		    lcs[i]->constr_z = (i - 3) * 3;
		    lcs[3]->constr_children.push_back(lcs[i]);
		}
		packed_cells.insert(ci->name);
	    } else if (ci->type == ctx->id("RAM32M")) {
		CellInfo *lcs[4];
		for (int i = 0; i < 4; i++) {
			std::unique_ptr<CellInfo> lut_cell =
			    create_leuctra_cell(ctx, ctx->id("LEUCTRA_LC"), ci->name.str(ctx) + "$lc" + std::to_string(i));
			new_cells.push_back(std::move(lut_cell));
			lcs[i] = new_cells.back().get();
			lcs[i]->params[ctx->id("MODE")] = Property("RAM5");
			lcs[i]->params[ctx->id("DIMUX")] = Property("XI");
			lcs[i]->attrs[ctx->id("NEEDS_M")] = Property(true);
		}
		NetInfo *net;
		bool net_inv;
		if (get_invertible_port(ctx, ci, ctx->id("WCLK"), false, true, net, net_inv)) {
		    for (int i = 0; i < 4; i++)
		        set_invertible_port(ctx, lcs[i], ctx->id("CLK"), net, net_inv, true, new_cells);
		}
		net = ci->ports[ctx->id("WE")].net;
		disconnect_port(ctx, ci, ctx->id("WE"));
		for (int i = 0; i < 4; i++)
		    connect_port(ctx,net, lcs[i], ctx->id("WE"));
		for (int i = 0; i < 4; i++) {
			std::string l{"ABCD"[i]};
			for (int j = 0; j < 5; j++) {
				IdString pname = ctx->id("ADDR" + l + "[" + std::to_string(j) + "]");
				net = ci->ports[pname].net;
				disconnect_port(ctx, ci, pname);
				if (i == 3) {
				for (int k = 0; k < 4; k++)
				    connect_port(ctx,net, lcs[k], ctx->id("WA" + std::to_string(j+1)));
				}
				connect_port(ctx,net, lcs[i], ctx->id("RA" + std::to_string(j+1)));
			}
			set_const_port(ctx, lcs[i], ctx->id("RA6"), true, new_cells);
			replace_port(ci, ctx->id("DI" + l + "[0]"), lcs[i], ctx->id("XI"));
			replace_port(ci, ctx->id("DI" + l + "[1]"), lcs[i], ctx->id("DDI5"));
			replace_port(ci, ctx->id("DO" + l + "[0]"), lcs[i], ctx->id("O5"));
			replace_port(ci, ctx->id("DO" + l + "[1]"), lcs[i], ctx->id("O6"));
			
			Property v = ci->params[ctx->id("INIT_" + l)];
			Property nv(0, 64);
			for (int j = 0; j < 32; j++)
				for (int k = 0; k < 2; k++)
					nv.str[j | k << 5] = v.str[j << 1 | k];
			nv.update_intval();
			lcs[i]->params[ctx->id("INIT")] = nv;
		}

		lcs[3]->attrs[ctx->id("LOCMASK")] = Property(0x8, 4);
		for (int i = 0; i < 3; i++) {
		    lcs[i]->constr_parent = lcs[3];
		    lcs[i]->constr_z = (i - 3) * 3;
		    lcs[3]->constr_children.push_back(lcs[i]);
		}
		packed_cells.insert(ci->name);
	    }
        }

        flush_cells();
    }

    // Convert CARRY4s to LEUCTRA_LCs.
    void pack_carry()
    {
        log_info("Packing CARRY4s...\n");

	// CARRY4 -> next CARRY4 in the chain
	std::unordered_map<CellInfo *, CellInfo *> chain;
	// CARRY4s that are chain starts.
	std::vector<CellInfo *> init;

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("CARRY4")) {
		NetInfo *net = ci->ports.at(ctx->id("CI")).net;
		CellInfo *prev = nullptr;
		if (net)
		    prev = net->driver.cell;
		bool cval;
		if (!prev || get_const_val(ctx, net, cval)) {
		    init.push_back(ci);
		} else {
		    if (prev->type != ctx->id("CARRY4") || net->driver.port != ctx->id("CO[3]"))
			log_error("CARRY4 %s has weird CI: %s (%s) %s", ci->name.c_str(ctx), prev->name.c_str(ctx), prev->type.c_str(ctx), net->driver.port.c_str(ctx));
		    if (chain.count(prev))
			log_error("Split carry chain: %s %s %s", prev->name.c_str(ctx), ci->name.c_str(ctx), chain[prev]->name.c_str(ctx));
		    chain[prev] = ci;
		}
            }
        }

	for (auto cell : init) {
	    CellInfo *cur = cell;
	    CellInfo *link = nullptr;
	    while (cur) {
	        link = convert_carry4(ctx, cur, link, new_cells, packed_cells);
		cur = chain[cur];
	    }
	}

        flush_cells();
    }

    // Convert MUXF8s to LEUCTRA_LCs.
    void pack_muxf8()
    {
        log_info("Packing MUXF8s...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("MUXF8")) {
		NetInfo *net = ci->ports.at(ctx->id("O")).net;
                convert_muxf8(ctx, net, ci->name.str(ctx) + "$lc", new_cells, packed_cells);
            }
        }

        flush_cells();
    }

    // Convert MUXF7s to LEUCTRA_LCs.
    void pack_muxf7()
    {
        log_info("Packing MUXF7s...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("MUXF7")) {
		NetInfo *net = ci->ports.at(ctx->id("O")).net;
                convert_muxf7(ctx, net, ci->name.str(ctx) + "$lc", new_cells, packed_cells);
            }
        }

        flush_cells();
    }

    // Convert LUTs to LEUCTRA_LCs.
    void pack_lut()
    {
        log_info("Packing LUTs...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_xilinx_lut(ctx, ci)) {
		NetInfo *net = ci->ports.at(ctx->id("O")).net;
                convert_lut(ctx, net, ci->name.str(ctx) + "$lc", new_cells, packed_cells);
            }
        }

        flush_cells();
    }

    // Convert misc cell types.
    void pack_misc()
    {
        log_info("Converting misc cell types...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("BUFG")) {
		ci->type = ctx->id("BUFGMUX");
		rename_port(ctx, ci, ctx->id("I"), ctx->id("I0"));
		set_const_port(ctx, ci, ctx->id("S"), true, new_cells);
		ci->params[ctx->id("SINV")] = Property("S_B");
            } else if (ci->type == ctx->id("PLL_ADV")) {
		for (auto port : {"RST", "REL", "CLKINSEL", "CLKBRST", "ENOUTSYNC", "MANPULF", "MANPDLF", "SKEWSTB", "SKEWRST", "SKEWCLKIN1", "SKEWCLKIN2"})
			handle_invertible_port(ctx, ci, ctx->id(port), false, true, new_cells);
		if (!ci->params.count(ctx->id("BANDWIDTH")))
			ci->params[ctx->id("BANDWIDTH")] = Property("OPTIMIZED");
		if (!ci->params.count(ctx->id("PLL_ADD_LEAKAGE")))
			ci->params[ctx->id("PLL_ADD_LEAKAGE")] = Property(2, 2);
		if (!ci->params.count(ctx->id("PLL_AVDD_COMP_SET")))
			ci->params[ctx->id("PLL_AVDD_COMP_SET")] = Property(2, 2);
		if (!ci->params.count(ctx->id("PLL_CLAMP_BYPASS")))
			ci->params[ctx->id("PLL_CLAMP_BYPASS")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_CLAMP_REF_SEL")))
			ci->params[ctx->id("PLL_CLAMP_REF_SEL")] = Property(1, 3);
		if (!ci->params.count(ctx->id("PLL_CLKCNTRL")))
			ci->params[ctx->id("PLL_CLKCNTRL")] = Property(0, 1);
		if (!ci->params.count(ctx->id("PLL_CLK_LOST_DETECT")))
			ci->params[ctx->id("PLL_CLK_LOST_DETECT")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_CP_BIAS_TRIP_SHIFT")))
			ci->params[ctx->id("PLL_CP_BIAS_TRIP_SHIFT")] = Property("TRUE");
		if (!ci->params.count(ctx->id("PLL_CP_REPL")))
			ci->params[ctx->id("PLL_CP_REPL")] = Property(1, 4);
		if (!ci->params.count(ctx->id("PLL_DVDD_COMP_SET")))
			ci->params[ctx->id("PLL_DVDD_COMP_SET")] = Property(2, 2);
		if (!ci->params.count(ctx->id("PLL_EN_LEAKAGE")))
			ci->params[ctx->id("PLL_EN_LEAKAGE")] = Property(2, 2);
		if (!ci->params.count(ctx->id("PLL_EN_VCO0")))
			ci->params[ctx->id("PLL_EN_VCO0")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_EN_VCO1")))
			ci->params[ctx->id("PLL_EN_VCO1")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_EN_VCO2")))
			ci->params[ctx->id("PLL_EN_VCO2")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_EN_VCO3")))
			ci->params[ctx->id("PLL_EN_VCO3")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_EN_VCO4")))
			ci->params[ctx->id("PLL_EN_VCO4")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_EN_VCO5")))
			ci->params[ctx->id("PLL_EN_VCO5")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_EN_VCO6")))
			ci->params[ctx->id("PLL_EN_VCO6")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_EN_VCO7")))
			ci->params[ctx->id("PLL_EN_VCO7")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_EN_VCO_DIV1")))
			ci->params[ctx->id("PLL_EN_VCO_DIV1")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_EN_VCO_DIV6")))
			ci->params[ctx->id("PLL_EN_VCO_DIV6")] = Property("TRUE");
		if (!ci->params.count(ctx->id("PLL_PFD_CNTRL")))
			ci->params[ctx->id("PLL_PFD_CNTRL")] = Property(8, 4);
		if (!ci->params.count(ctx->id("PLL_PFD_DLY")))
			ci->params[ctx->id("PLL_PFD_DLY")] = Property(1, 2);
		if (!ci->params.count(ctx->id("PLL_PWRD_CFG")))
			ci->params[ctx->id("PLL_PWRD_CFG")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_SEL_SLIPD")))
			ci->params[ctx->id("PLL_SEL_SLIPD")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_TEST_IN_WINDOW")))
			ci->params[ctx->id("PLL_TEST_IN_WINDOW")] = Property("FALSE");
		if (!ci->params.count(ctx->id("PLL_CLKFBOUT2_NOCOUNT")))
			ci->params[ctx->id("PLL_CLKFBOUT2_NOCOUNT")] = Property("TRUE");
		if (!ci->params.count(ctx->id("PLL_EN_CNTRL")))
			ci->params[ctx->id("PLL_EN_CNTRL")] = Property::from_string("0000000000100010011110001110011000011110101000101111110010111110100001000010000000000");
	    }
        }

        flush_cells();
    }

    // Merge a net into a constant net
    void set_net_constant(const Context *ctx, NetInfo *orig, NetInfo *constnet, bool constval)
    {
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

    // Pack constants (simple implementation)
    void pack_constants()
    {
        log_info("Packing constants..\n");

        std::unique_ptr<CellInfo> gnd_cell = create_leuctra_cell(ctx, ctx->id("LEUCTRA_LC"), "$PACKER_GND");
        gnd_cell->params[ctx->id("INIT")] = Property(0, 64);
        std::unique_ptr<NetInfo> gnd_net = std::unique_ptr<NetInfo>(new NetInfo);
        gnd_net->name = ctx->id("$PACKER_GND_NET");
        gnd_net->driver.cell = gnd_cell.get();
        gnd_net->driver.port = ctx->id("O6");
        gnd_cell->ports.at(ctx->id("O6")).net = gnd_net.get();
	gnd_cell->attrs[ctx->id("CONST")] = Property(false);

        std::unique_ptr<CellInfo> vcc_cell = create_leuctra_cell(ctx, ctx->id("LEUCTRA_LC"), "$PACKER_VCC");
        vcc_cell->params[ctx->id("INIT")] = Property(-1, 64);
        std::unique_ptr<NetInfo> vcc_net = std::unique_ptr<NetInfo>(new NetInfo);
        vcc_net->name = ctx->id("$PACKER_VCC_NET");
        vcc_net->driver.cell = vcc_cell.get();
        vcc_net->driver.port = ctx->id("O6");
        vcc_cell->ports.at(ctx->id("O6")).net = vcc_net.get();
	vcc_cell->attrs[ctx->id("CONST")] = Property(true);

        std::vector<IdString> dead_nets;

        bool gnd_used = false, vcc_used = false;

        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            if (ni->driver.cell != nullptr && ni->driver.cell->type == ctx->id("GND")) {
                IdString drv_cell = ni->driver.cell->name;
                set_net_constant(ctx, ni, gnd_net.get(), false);
                gnd_used = true;
                dead_nets.push_back(net.first);
                ctx->cells.erase(drv_cell);
            } else if (ni->driver.cell != nullptr && ni->driver.cell->type == ctx->id("VCC")) {
                IdString drv_cell = ni->driver.cell->name;
                set_net_constant(ctx, ni, vcc_net.get(), true);
                vcc_used = true;
                dead_nets.push_back(net.first);
                ctx->cells.erase(drv_cell);
            }
        }

        if (gnd_used) {
            ctx->cells[gnd_cell->name] = std::move(gnd_cell);
            ctx->nets[gnd_net->name] = std::move(gnd_net);
        }
        if (vcc_used) {
            ctx->cells[vcc_cell->name] = std::move(vcc_cell);
            ctx->nets[vcc_net->name] = std::move(vcc_net);
        }

        for (auto dn : dead_nets) {
            ctx->nets.erase(dn);
        }
    }

  public:
    void pack()
    {
        //insert_iob();
        convert_iob();
        pack_iologic();
	pack_bram();
        pack_ff();
	pack_ram();
	// pack_srl();
        pack_carry();
        pack_muxf8();
        pack_muxf7();
	// clean_inv();
        pack_lut();
	// pack_lc_ff();
        pack_misc();
        pack_constants();
    }

  private:
    Context *ctx;

    std::unordered_set<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;
};

// Main pack function
bool Arch::pack()
{
    Context *ctx = getCtx();
    try {
        log_break();
        LeuctraPacker(ctx).pack();
        ctx->settings[ctx->id("pack")] = 1;
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        // XXX
        //assignArchInfo();
        return true;
    } catch (log_execution_error_exception) {
        // XXX
        //assignArchInfo();
        return false;
    }
}

NEXTPNR_NAMESPACE_END

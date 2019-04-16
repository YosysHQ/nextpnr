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

    // Remove nextpnr iob cells, insert Xilinx primitives instead.
    void pack_iob()
    {
        log_info("Packing IOBs...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_nextpnr_iob(ctx, ci)) {
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

                    auto loc_attr = iob->attrs.find(ctx->id("LOC"));
                    if (loc_attr != iob->attrs.end()) {
                        std::string pin = loc_attr->second;
                        BelId pinBel = ctx->getPackagePinBel(pin);
                        if (pinBel == BelId()) {
                            log_error("IO pin '%s' constrained to pin '%s', which does not exist for package '%s'.\n",
                                      iob->name.c_str(ctx), pin.c_str(), ctx->args.package.c_str());
                        } else {
                            log_info("pin '%s' constrained to Bel '%s'.\n", iob->name.c_str(ctx),
                                     ctx->getBelName(pinBel).c_str(ctx));
                        }
                        iob->attrs[ctx->id("BEL")] = ctx->getBelName(pinBel).str(ctx);
                    }
                }
            }
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
		    BelId bel = ctx->getBelByName(ctx->id(ci->attrs[ctx->id("BEL")]));
		    for (auto &child : ci->constr_children) {
			BelId child_bel = ctx->getRelatedBel(bel, child->constr_spec);
			child->attrs[ctx->id("BEL")] = ctx->getBelName(child_bel).str(ctx);
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
    void pack_ff()
    {
        log_info("Packing FFs...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_xilinx_ff(ctx, ci)) {
                std::unique_ptr<CellInfo> ff_cell =
                        create_leuctra_cell(ctx, ctx->id("LEUCTRA_FF"), ci->name.str(ctx) + "$ff");
                convert_ff(ctx, ci, ff_cell.get(), new_cells, packed_cells);
                new_cells.push_back(std::move(ff_cell));

                packed_cells.insert(ci->name);
            }
        }

        flush_cells();
    }

    // Convert FFs/latches to LEUCTRA_FFs.
    void pack_lut()
    {
        log_info("Packing LUTs...\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_xilinx_lut(ctx, ci)) {
                std::unique_ptr<CellInfo> lut_cell =
                        create_leuctra_cell(ctx, ctx->id("LEUCTRA_LC"), ci->name.str(ctx) + "$lc");
                convert_lut(ctx, ci, lut_cell.get(), new_cells, packed_cells);
                new_cells.push_back(std::move(lut_cell));

                packed_cells.insert(ci->name);
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
        gnd_cell->params[ctx->id("INIT")] = "0000000000000000000000000000000000000000000000000000000000000000";
        std::unique_ptr<NetInfo> gnd_net = std::unique_ptr<NetInfo>(new NetInfo);
        gnd_net->name = ctx->id("$PACKER_GND_NET");
        gnd_net->driver.cell = gnd_cell.get();
        gnd_net->driver.port = ctx->id("O6");
        gnd_cell->ports.at(ctx->id("O6")).net = gnd_net.get();

        std::unique_ptr<CellInfo> vcc_cell = create_leuctra_cell(ctx, ctx->id("LEUCTRA_LC"), "$PACKER_VCC");
        vcc_cell->params[ctx->id("INIT")] = "1111111111111111111111111111111111111111111111111111111111111111";
        std::unique_ptr<NetInfo> vcc_net = std::unique_ptr<NetInfo>(new NetInfo);
        vcc_net->name = ctx->id("$PACKER_VCC_NET");
        vcc_net->driver.cell = vcc_cell.get();
        vcc_net->driver.port = ctx->id("O6");
        vcc_cell->ports.at(ctx->id("O6")).net = vcc_net.get();

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
        pack_iob();
        pack_iologic();
        pack_ff();
        pack_lut();
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

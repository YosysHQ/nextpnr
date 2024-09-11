/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  Myrtle Shah <gatecat@ds0.me>
 *  Copyright (C) 2023  Hans Baier <hansfbaier@gmail.com>
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
#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

void XC7Packer::walk_dsp(CellInfo *root, CellInfo *current_cell, int constr_z)
{
    CellInfo *cascaded_cell = nullptr;

    auto check_illegal_fanout = [&] (NetInfo *ni, std::string port) {
        if (ni->users.entries() > 1)
            log_error("Port %s connected to net %s has more than one user", port.c_str(), ni->name.c_str(ctx));

        PortRef& user = *ni->users.begin();
        if (user.cell->type != id_DSP48E1_DSP48E1)
            log_error("User %s of net %s is not a DSP block, but %s",
                user.cell->name.c_str(ctx), ni->name.c_str(ctx), user.cell->type.c_str(ctx));
    };

    // see if any cascade outputs are connected
    for (auto port : current_cell->ports) {
        if (!boost::contains(port.first.str(ctx), "COUT")) continue;
        NetInfo *cout_net = port.second.net;

        if (cout_net == nullptr) continue;

        check_illegal_fanout(cout_net, port.first.c_str(ctx));
        PortRef& user = *cout_net->users.begin();
        CellInfo *cout_cell = user.cell;
        NPNR_ASSERT(cout_cell != nullptr);

        if (cascaded_cell != nullptr && cout_cell != cascaded_cell)
            log_error("the cascading outputs of DSP block %s are connected to different cells",
                current_cell->name.c_str(ctx));

        cascaded_cell = cout_cell;
    }

    if (cascaded_cell != nullptr) {
        auto is_lower_bel = constr_z == BEL_LOWER_DSP;

        cascaded_cell->cluster = root->name;
        root->constr_children.push_back(cascaded_cell);
        cascaded_cell->constr_x = 0;
        // the connected cell has to be above the current cell,
        // otherwise it cannot be routed, because the cascading ports
        // are only connected to the DSP above
        auto previous_y = (current_cell == root) ? 0 : current_cell->constr_y;
        cascaded_cell->constr_y = previous_y + (is_lower_bel ? -5 : 0);
        cascaded_cell->constr_z = constr_z;
        cascaded_cell->constr_abs_z = true;

        walk_dsp(root, cascaded_cell, is_lower_bel ? BEL_UPPER_DSP : BEL_LOWER_DSP);
    }
}

void XC7Packer::pack_dsps()
{
    log_info("Packing DSPs..\n");

    dict<IdString, XFormRule> dsp_rules;
    dsp_rules[id_DSP48E1].new_type = id_DSP48E1_DSP48E1;
    generic_xform(dsp_rules, true);

    std::vector<CellInfo *> all_dsps;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();

        auto add_const_pin = [&](PortInfo& port, std::string& pins, std::string& pin_name, std::string net) {
            if (port.net && port.net->name == ctx->id(net)) {
                ci->disconnectPort(port.name);
                pins += " " + pin_name;
            }
        };

        if (ci->type == id_DSP48E1_DSP48E1) {
            all_dsps.push_back(ci);
            auto gnd_attr = ctx->id("DSP_GND_PINS");
            auto vcc_attr = ctx->id("DSP_VCC_PINS");

            auto gnd_pins = str_or_default(ci->attrs, gnd_attr, "");
            auto vcc_pins = str_or_default(ci->attrs, vcc_attr, "");

            for (auto &port : ci->ports) {
                std::string n = port.first.str(ctx);

                if (boost::starts_with(n, "ACIN") || boost::starts_with(n, "BCIN") || boost::starts_with(n, "PCIN")) {
                    if (port.second.net == nullptr)
                        continue;
                    if (port.second.net->name == ctx->id("$PACKER_GND_NET"))
                        ci->disconnectPort(port.first);
                }

                // prjxray has extra bits for these ports to hardwire them to VCC/GND
                // as these seem to be interal to the tile,
                // this saves us from having to route those externally
                if (boost::starts_with(n, "D") ||
                    boost::starts_with(n, "RSTD") ||
                    // TODO: these seem to be inverted for unknown reasons
                    // boost::starts_with(n, "INMODE") ||
                    // boost::starts_with(n, "ALUMODE2") ||
                    // boost::starts_with(n, "ALUMODE3") ||
                    boost::starts_with(n, "CARRYINSEL2") ||
                    boost::starts_with(n, "CED") ||
                    boost::starts_with(n, "CEAD") ||
                    boost::starts_with(n, "CEINMODE") ||
                    boost::starts_with(n, "CEALUMODE")) {
                    add_const_pin(port.second, gnd_pins, n, "$PACKER_GND_NET");
                    add_const_pin(port.second, vcc_pins, n, "$PACKER_VCC_NET");
                }
            }

            ci->attrs[gnd_attr] = gnd_pins;
            ci->attrs[vcc_attr] = vcc_pins;
        }
    }

    std::vector<CellInfo *> dsp_roots;
    for (auto ci : all_dsps) {
        bool cascade_input_used = false;
        for (auto port : ci->ports) {
            if (!boost::contains(port.first.str(ctx), "CIN")) continue;
            if (port.second.net != nullptr) {
                cascade_input_used = true;
                break;
            }
        }

        if (!cascade_input_used) {
            dsp_roots.push_back(ci);
        }
    }

    for (auto root : dsp_roots) {
        root->constr_abs_z = true;
        root->constr_z = BEL_LOWER_DSP;
        walk_dsp(root, root, BEL_UPPER_DSP);
    }
}

NEXTPNR_NAMESPACE_END

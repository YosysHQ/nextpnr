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

static bool is_cascade_input(const PortInfo &port, const Context *ctx)
{
    if (port.name == id_CARRYCASCIN || port.name == id_MULTSIGNIN)
        return true;
    const std::string &str = port.name.c_str(ctx);
    return boost::starts_with(str, "ACIN") || boost::starts_with(str, "BCIN") || boost::starts_with(str, "PCIN");
}
static bool is_cascade_output(const PortInfo &port, const Context *ctx)
{
    if (port.name == id_CARRYCASCOUT || port.name == id_MULTSIGNOUT)
        return true;
    const std::string &str = port.name.c_str(ctx);
    return boost::starts_with(str, "ACOUT") || boost::starts_with(str, "BCOUT") || boost::starts_with(str, "PCOUT");
}

// Return : the number of DSP marked as cascaded
unsigned XC7Packer::walk_dsp(CellInfo *root, CellInfo *current_cell, int constr_z)
{
    CellInfo *cascaded_cell = nullptr;
    unsigned num_casc = 0;

    auto check_illegal_fanout = [&](NetInfo *ni, std::string port) {
        if (ni->users.entries() > 1)
            log_error("Port %s connected to net %s has more than one user", port.c_str(), ni->name.c_str(ctx));

        PortRef &user = *ni->users.begin();
        if (user.cell->type != id_DSP48E1_DSP48E1)
            log_error("User %s of net %s is not a DSP block, but %s", user.cell->name.c_str(ctx), ni->name.c_str(ctx),
                      user.cell->type.c_str(ctx));
    };

    // see if any cascade outputs are connected
    for (auto port : current_cell->ports) {
        if (!is_cascade_output(port.second, ctx))
            continue;
        NetInfo *cout_net = port.second.net;

        if (cout_net == nullptr || cout_net->users.empty())
            continue;

        check_illegal_fanout(cout_net, port.first.c_str(ctx));
        PortRef &user = *cout_net->users.begin();
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
        // The connected cell has to be above the current cell,
        // otherwise it cannot be routed, because the cascading ports
        // are only connected to the DSP above
        auto previous_y = (current_cell == root) ? 0 : current_cell->constr_y;
        cascaded_cell->constr_y = previous_y + (is_lower_bel ? -5 : 0);
        cascaded_cell->constr_z = constr_z;
        cascaded_cell->constr_abs_z = true;

        num_casc += 1;
        num_casc += walk_dsp(root, cascaded_cell, is_lower_bel ? BEL_UPPER_DSP : BEL_LOWER_DSP);
    }

    return num_casc;
}

void XC7Packer::pack_dsps()
{
    log_info("Packing DSPs..\n");

    dict<IdString, XFormRule> dsp_rules;
    dsp_rules[id_DSP48E1].new_type = id_DSP48E1_DSP48E1;
    generic_xform(dsp_rules, true);

    std::vector<CellInfo *> all_dsps;

    // Clean connections of DSPs
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();

        auto add_const_pin = [&](PortInfo &port, std::string &pins, std::string &pin_name, std::string net) {
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

                // Cascading inputs do not use routing resources, so disconnect them if constants
                if (is_cascade_input(port.second, ctx)) {
                    if (port.second.net == nullptr)
                        continue;
                    if (port.second.net->name == ctx->id("$PACKER_GND_NET"))
                        ci->disconnectPort(port.first);
                }

                // prjxray has extra bits for these ports to hardwire them to VCC/GND
                // as these seem to be interal to the tile,
                // this saves us from having to route those externally
                if (boost::starts_with(n, "D") || boost::starts_with(n, "RSTD") ||
                    // TODO: these seem to be inverted for unknown reasons
                    // boost::starts_with(n, "INMODE") ||
                    // boost::starts_with(n, "ALUMODE2") ||
                    // boost::starts_with(n, "ALUMODE3") ||
                    boost::starts_with(n, "CARRYINSEL2") || boost::starts_with(n, "CED") ||
                    boost::starts_with(n, "CEAD") || boost::starts_with(n, "CEINMODE") ||
                    boost::starts_with(n, "CEALUMODE")) {
                    add_const_pin(port.second, gnd_pins, n, "$PACKER_GND_NET");
                    add_const_pin(port.second, vcc_pins, n, "$PACKER_VCC_NET");
                }
            }

            ci->attrs[gnd_attr] = gnd_pins;
            ci->attrs[vcc_attr] = vcc_pins;
        }
    }

    // Find the roots of cascaded DSP
    std::vector<CellInfo *> dsp_roots;
    for (auto ci : all_dsps) {
        bool cascade_input_used = false;
        for (auto port : ci->ports) {
            if (!is_cascade_input(port.second, ctx))
                continue;
            if (port.second.net != nullptr) {
                cascade_input_used = true;
                break;
            }
        }

        if (!cascade_input_used) {
            dsp_roots.push_back(ci);
        }
    }

    // Create clusters of cascaded DSPs
    unsigned num_casc = 0;
    for (auto root : dsp_roots) {
        root->constr_abs_z = true;
        root->constr_z = BEL_LOWER_DSP;
        unsigned loc_casc = walk_dsp(root, root, BEL_UPPER_DSP);
        if (loc_casc > 0) {
            root->cluster = root->name;
        }
        num_casc += loc_casc;
    }
    if (num_casc > 0) {
        log_info("Found %u cascaded DSP from %u roots\n", num_casc, (unsigned)dsp_roots.size());
    }
}

NEXTPNR_NAMESPACE_END

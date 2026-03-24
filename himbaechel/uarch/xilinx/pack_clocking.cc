/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019-2023  Myrtle Shah <gatecat@ds0.me>
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
#include <boost/optional.hpp>
#include <iterator>
#include <queue>
#include <unordered_set>
#include "chain_utils.h"
#include "design_utils.h"
#include "extra_data.h"
#include "log.h"
#include "nextpnr.h"
#include "pack.h"
#include "pins.h"

#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

BelId XilinxPacker::find_bel_with_short_route(WireId source, IdString beltype, IdString belpin)
{
    if (source == WireId())
        return BelId();
    const size_t max_visit = 50000; // effort/runtime tradeoff
    pool<WireId> visited;
    std::queue<WireId> visit;
    visit.push(source);
    while (!visit.empty() && visited.size() < max_visit) {
        WireId cursor = visit.front();
        visit.pop();
        for (auto bp : ctx->getWireBelPins(cursor))
            if (bp.pin == belpin && ctx->getBelType(bp.bel) == beltype && ctx->checkBelAvail(bp.bel))
                return bp.bel;
        for (auto pip : ctx->getPipsDownhill(cursor)) {
            WireId dst = ctx->getPipDstWire(pip);
            if (visited.count(dst))
                continue;
            visit.push(dst);
            visited.insert(dst);
        }
    }
    return BelId();
}

void XilinxPacker::try_preplace(CellInfo *cell, IdString port)
{
    if (cell->attrs.count(id_BEL) || cell->bel != BelId())
        return;
    NetInfo *n = cell->getPort(port);
    if (n == nullptr || n->driver.cell == nullptr)
        return;
    CellInfo *drv = n->driver.cell;
    BelId drv_bel = drv->bel;
    if (drv_bel == BelId())
        return;
    WireId drv_wire = ctx->getBelPinWire(drv_bel, n->driver.port);
    if (drv_wire == WireId())
        return;
    BelId tgt = find_bel_with_short_route(drv_wire, cell->type, port);
    if (tgt != BelId()) {
        ctx->bindBel(tgt, cell, STRENGTH_LOCKED);
        log_info("    Constrained %s '%s' to bel '%s' based on dedicated routing\n", cell->type.c_str(ctx),
                 ctx->nameOf(cell), ctx->nameOfBel(tgt));
    }
}

void XilinxPacker::preplace_unique(CellInfo *cell)
{
    if (cell->attrs.count(id_BEL) || cell->bel != BelId())
        return;
    for (auto bel : ctx->getBels()) {
        if (ctx->checkBelAvail(bel) && ctx->getBelType(bel) == cell->type) {
            ctx->bindBel(bel, cell, STRENGTH_LOCKED);
            return;
        }
    }
}

void XC7Packer::prepare_clocking()
{
    log_info("Preparing clocking...\n");
    dict<IdString, IdString> upgrade;
    upgrade[id_MMCME2_BASE] = id_MMCME2_ADV;
    upgrade[id_PLLE2_BASE] = id_PLLE2_ADV;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (upgrade.count(ci->type)) {
            IdString new_type = upgrade.at(ci->type);
            ci->type = new_type;
        } else if (ci->type == id_BUFG) {
            ci->type = id_BUFGCTRL;
            ci->renamePort(id_I, id_I0);
            tie_port(ci, "CE0", true, true);
            tie_port(ci, "S0", true, true);
            tie_port(ci, "S1", false, true);
            tie_port(ci, "IGNORE0", true, true);
        } else if (ci->type == id_BUFGCE) {
            ci->type = id_BUFGCTRL;
            ci->renamePort(id_I, id_I0);
            ci->renamePort(id_CE, id_CE0);
            tie_port(ci, "S0", true, true);
            tie_port(ci, "S1", false, true);
            tie_port(ci, "IGNORE0", true, true);
        }
    }
}

void XC7Packer::pack_plls()
{
    log_info("Packing PLLs...\n");

    auto set_default = [](CellInfo *ci, IdString param, const Property &value) {
        if (!ci->params.count(param))
            ci->params[param] = value;
    };

    dict<IdString, XFormRule> pll_rules;
    pll_rules[id_MMCME2_ADV].new_type = id_MMCME2_ADV_MMCME2_ADV;
    pll_rules[id_PLLE2_ADV].new_type = id_PLLE2_ADV_PLLE2_ADV;
    generic_xform(pll_rules);
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        // Preplace PLLs to make use of dedicated/short routing paths
        if (ci->type.in(id_MMCM_MMCM_TOP, id_PLL_PLL_TOP))
            try_preplace(ci, id_CLKIN1);
        if (ci->type == id_MMCM_MMCM_TOP) {
            // Fixup parameters
            for (int i = 1; i <= 2; i++)
                set_default(ci, ctx->idf("CLKIN%d_PERIOD", i), Property("0.0"));
            for (int i = 0; i <= 6; i++) {
                set_default(ci, ctx->idf("CLKOUT%d_CASCADE", i), Property("FALSE"));
                set_default(ci, ctx->idf("CLKOUT%d_DIVIDE", i), Property(1));
                set_default(ci, ctx->idf("CLKOUT%d_DUTY_CYCLE", i), Property("0.5"));
                set_default(ci, ctx->idf("CLKOUT%d_PHASE", i), Property(0));
                set_default(ci, ctx->idf("CLKOUT%d_USE_FINE_PS", i), Property("FALSE"));
            }
            set_default(ci, id_COMPENSATION, Property("INTERNAL"));

            // Fixup routing
            if (str_or_default(ci->params, id_COMPENSATION, "INTERNAL") == "INTERNAL") {
                ci->disconnectPort(id_CLKFBIN);
                ci->connectPort(id_CLKFBIN, ctx->nets.at(ctx->id("$PACKER_VCC_NET")).get());
            }
        }
    }
}

void XC7Packer::pack_gbs()
{
    log_info("Packing global buffers...\n");
    dict<IdString, XFormRule> gb_rules;
    gb_rules[id_BUFGCTRL].new_type = id_BUFGCTRL;
    gb_rules[id_BUFGCTRL].new_type = id_BUFGCTRL;

    generic_xform(gb_rules);

    // Make sure prerequisites are set up first
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_PS7_PS7)
            preplace_unique(ci);
        if (ci->type.in(id_PSEUDO_GND, id_PSEUDO_VCC))
            preplace_unique(ci);
    }

    // Preplace global buffers to make use of dedicated/short routing
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_BUFGCTRL)
            try_preplace(ci, id_I0);
        if (ci->type == id_BUFG_BUFG)
            try_preplace(ci, id_I);
    }
}

void XC7Packer::pack_clocking()
{
    pack_plls();
    pack_gbs();
}

void XilinxImpl::route_clocks()
{
    log_info("Routing global clocks...\n");
    // Special pass for faster routing of global clock psuedo-net
    for (auto &net : ctx->nets) {
        NetInfo *clk_net = net.second.get();
        if (!clk_net->driver.cell)
            continue;

        // check if we have a global clock net, skip otherwise
        bool is_global = false;
        if ((clk_net->driver.cell->type.in(id_BUFGCTRL, id_BUFCE_BUFG_PS, id_BUFCE_BUFCE, id_BUFGCE_DIV_BUFGCE_DIV)) &&
            clk_net->driver.port == id_O)
            is_global = true;
        else if (clk_net->driver.cell->type == id_PLLE2_ADV_PLLE2_ADV && clk_net->users.entries() == 1 &&
                 ((*clk_net->users.begin()).cell->type.in(id_BUFGCTRL, id_BUFCE_BUFCE, id_BUFGCE_DIV_BUFGCE_DIV)))
            is_global = true;
        else if (clk_net->users.entries() == 1 && (*clk_net->users.begin()).cell->type == id_PLLE2_ADV_PLLE2_ADV &&
                 (*clk_net->users.begin()).port == id_CLKIN1)
            is_global = true;
        if (!is_global)
            continue;

        log_info("    routing clock '%s'\n", clk_net->name.c_str(ctx));
        ctx->bindWire(ctx->getNetinfoSourceWire(clk_net), clk_net, STRENGTH_LOCKED);

        for (auto &usr : clk_net->users) {
            std::queue<WireId> visit;
            dict<WireId, PipId> backtrace;
            WireId dest = WireId();

            auto sink_wire = ctx->getNetinfoSinkWire(clk_net, usr, 0);
            if (ctx->debug) {
                auto sink_wire_name = "(uninitialized)";
                if (sink_wire != WireId())
                    sink_wire_name = ctx->nameOfWire(sink_wire);
                log_info("        routing arc to %s.%s (wire %s):\n", usr.cell->name.c_str(ctx), usr.port.c_str(ctx),
                         sink_wire_name);
            }

            visit.push(sink_wire);
            while (!visit.empty()) {
                WireId curr = visit.front();
                visit.pop();
                if (ctx->getBoundWireNet(curr) == clk_net) {
                    dest = curr;
                    break;
                }
                for (auto uh : ctx->getPipsUphill(curr)) {
                    if (!ctx->checkPipAvail(uh))
                        continue;
                    WireId src = ctx->getPipSrcWire(uh);
                    if (backtrace.count(src))
                        continue;
                    IdString intent = ctx->getWireType(src);
                    if (intent.in(id_NODE_DOUBLE, id_NODE_HLONG, id_NODE_HQUAD, id_NODE_VLONG, id_NODE_VQUAD,
                                  id_NODE_SINGLE, id_NODE_CLE_OUTPUT, id_NODE_OPTDELAY, id_BENTQUAD, id_DOUBLE,
                                  id_HLONG, id_HQUAD, id_OPTDELAY, id_SINGLE, id_VLONG, id_VLONG12, id_VQUAD,
                                  id_PINBOUNCE))
                        continue;
                    if (!ctx->checkWireAvail(src) && ctx->getBoundWireNet(src) != clk_net)
                        continue;
                    backtrace[src] = uh;
                    visit.push(src);
                }
            }
            if (dest == WireId()) {
                log_info("            failed to find a route using dedicated resources.\n");
                if (clk_net->users.entries() == 1 && (*clk_net->users.begin()).cell->type == id_PLLE2_ADV_PLLE2_ADV &&
                    (*clk_net->users.begin()).port == id_CLKIN1) {
                    // Due to some missing pips, currently special case more lenient solution
                    std::queue<WireId> empty;
                    std::swap(visit, empty);
                    backtrace.clear();
                    visit.push(sink_wire);
                    while (!visit.empty()) {
                        WireId curr = visit.front();
                        visit.pop();
                        if (ctx->getBoundWireNet(curr) == clk_net) {
                            dest = curr;
                            break;
                        }
                        for (auto uh : ctx->getPipsUphill(curr)) {
                            if (!ctx->checkPipAvail(uh))
                                continue;
                            WireId src = ctx->getPipSrcWire(uh);
                            if (backtrace.count(src))
                                continue;
                            if (!ctx->checkWireAvail(src) && ctx->getBoundWireNet(src) != clk_net)
                                continue;
                            backtrace[src] = uh;
                            visit.push(src);
                        }
                    }
                    if (dest == WireId())
                        continue;
                } else {
                    continue;
                }
            }
            while (backtrace.count(dest)) {
                auto uh = backtrace[dest];
                dest = ctx->getPipDstWire(uh);
                if (ctx->getBoundWireNet(dest) == clk_net) {
                    NPNR_ASSERT(clk_net->wires.at(dest).pip == uh);
                    break;
                }
                if (ctx->debug)
                    log_info("            bind pip %s --> %s\n", ctx->nameOfPip(uh), ctx->nameOfWire(dest));
                ctx->bindPip(uh, clk_net, STRENGTH_LOCKED);
            }
        }
    }
#if 0
    for (auto& net : nets) {
        NetInfo *ni = net.second.get();
        for (auto &usr : ni->users) {
            if (usr.cell->type != id_BUFGCTRL || usr.port != id_I0)
                continue;
            WireId dst = getCtx()->getNetinfoSinkWire(ni, usr, 0);
            std::queue<WireId> visit;
            visit.push(dst);
            int i = 0;
            while(!visit.empty() && i < 5000) {
                WireId curr = visit.front();
                visit.pop();
                log("  %s\n", nameOfWire(curr));
                for (auto pip : getPipsUphill(curr)) {
                    auto &pd = locInfo(pip).pip_data[pip.index];
                    log_info("    p %s sr %s (t %d s %d sv %d)\n", nameOfPip(pip), nameOfWire(getPipSrcWire(pip)), pd.flags, pd.site, pd.site_variant);
                    if (!checkPipAvail(pip)) {
                        log("      p unavail\n");
                        continue;
                    }
                    WireId src = getPipSrcWire(pip);
                    if (!checkWireAvail(src)) {
                        log("      w unavail (%s)\n", nameOf(getBoundWireNet(src)));
                        continue;
                    }
                    log_info("     p %s s %s\n", nameOfPip(pip), nameOfWire(src));
                    visit.push(src);
                }
                ++i;
            }
        }
    }
#endif
}

namespace {
double float_or_default(CellInfo *ci, IdString p, double def)
{
    if (!ci->params.count(p))
        return def;
    auto &prop = ci->params.at(p);
    if (prop.is_string)
        return std::stod(prop.as_string());
    else
        return prop.as_int64();
}
} // namespace

void XilinxPacker::generate_constraints()
{
    log_info("Generating derived timing constraints...\n");
    auto MHz = [&](delay_t a) { return 1000.0 / ctx->getDelayNS(a); };

    auto equals_epsilon = [](delay_t a, delay_t b) { return (std::abs(a - b) / std::max(double(b), 1.0)) < 1e-3; };
    auto equals_epsilon_pair = [&](DelayPair &a, DelayPair &b) {
        return equals_epsilon(a.min_delay, b.min_delay) && equals_epsilon(a.max_delay, b.max_delay);
    };
    auto equals_epsilon_constr = [&](ClockConstraint &a, ClockConstraint &b) {
        return equals_epsilon_pair(a.high, b.high) && equals_epsilon_pair(a.low, b.low) &&
               equals_epsilon_pair(a.period, b.period);
    };

    pool<IdString> user_constrained, changed_nets;
    for (auto &net : ctx->nets) {
        if (net.second->clkconstr != nullptr)
            user_constrained.insert(net.first);
        changed_nets.insert(net.first);
    }
    auto get_period = [&](CellInfo *ci, IdString port, delay_t &period) {
        if (!ci->ports.count(port))
            return false;
        NetInfo *from = ci->ports.at(port).net;
        if (from == nullptr || from->clkconstr == nullptr)
            return false;
        period = from->clkconstr->period.minDelay();
        return true;
    };

    auto simple_clk_contraint = [&](delay_t period) {
        auto constr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
        constr->low = DelayPair(period / 2);
        constr->high = DelayPair(period / 2);
        constr->period = DelayPair(period);

        return constr;
    };

    auto set_constraint = [&](CellInfo *ci, IdString port, std::unique_ptr<ClockConstraint> constr) {
        if (!ci->ports.count(port))
            return;
        NetInfo *to = ci->ports.at(port).net;
        if (to == nullptr)
            return;
        if (to->clkconstr != nullptr) {
            if (!equals_epsilon_constr(*to->clkconstr, *constr) && user_constrained.count(to->name))
                log_warning("    Overriding derived constraint of %.1f MHz on net %s with user-specified constraint of "
                            "%.1f MHz.\n",
                            MHz(to->clkconstr->period.min_delay), to->name.c_str(ctx), MHz(constr->period.min_delay));
            return;
        }
        to->clkconstr = std::move(constr);
        log_info("    Derived frequency constraint of %.1f MHz for net %s\n", MHz(to->clkconstr->period.minDelay()),
                 to->name.c_str(ctx));
        changed_nets.insert(to->name);
    };

    auto copy_constraint = [&](CellInfo *ci, IdString fromPort, IdString toPort, double ratio = 1.0) {
        if (!ci->ports.count(fromPort) || !ci->ports.count(toPort))
            return;
        NetInfo *from = ci->ports.at(fromPort).net, *to = ci->ports.at(toPort).net;
        if (from == nullptr || from->clkconstr == nullptr || to == nullptr)
            return;
        if (to->clkconstr != nullptr) {
            if (!equals_epsilon(to->clkconstr->period.minDelay(),
                                delay_t(from->clkconstr->period.minDelay() / ratio)) &&
                user_constrained.count(to->name))
                log_warning("    Overriding derived constraint of %.1f MHz on net %s with user-specified constraint of "
                            "%.1f MHz.\n",
                            MHz(to->clkconstr->period.minDelay()), to->name.c_str(ctx),
                            MHz(delay_t(from->clkconstr->period.minDelay() / ratio)));
            return;
        }
        to->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
        to->clkconstr->low = DelayPair(ctx->getDelayFromNS(ctx->getDelayNS(from->clkconstr->low.min_delay) / ratio));
        to->clkconstr->high = DelayPair(ctx->getDelayFromNS(ctx->getDelayNS(from->clkconstr->high.min_delay) / ratio));
        to->clkconstr->period =
                DelayPair(ctx->getDelayFromNS(ctx->getDelayNS(from->clkconstr->period.min_delay) / ratio));
        log_info("    Derived frequency constraint of %.1f MHz for net %s\n", MHz(to->clkconstr->period.minDelay()),
                 to->name.c_str(ctx));
        changed_nets.insert(to->name);
    };

    // Run in a loop while constraints are changing to deal with dependencies
    // Iteration limit avoids hanging in crazy loopback situation (self-fed PLLs or dividers, etc)
    int iter = 0;
    const int itermax = 5000;
    while (!changed_nets.empty() && iter < itermax) {
        ++iter;
        pool<IdString> changed_cells;
        for (auto net : changed_nets) {
            for (auto &user : ctx->nets.at(net)->users)
                if (user.port.in(id_CLKIN1, id_I0, id_PAD))
                    changed_cells.insert(user.cell->name);
        }
        changed_nets.clear();
        for (auto cell : changed_cells) {
            CellInfo *ci = ctx->cells.at(cell).get();
            if (ci->type == id_BUFGCTRL) {
                copy_constraint(ci, id_I0, id_O, 1);
            } else if (ci->type.in(id_IOB33M_INBUF_EN, id_IOB33S_INBUF_EN, id_IOB33_INBUF_EN, id_IOB18_INBUF_DCIEN,
                                   id_IOB18M_INBUF_DCIEN)) {
                copy_constraint(ci, id_PAD, id_OUT, 1);
            } else if (ci->type.in(id_MMCME2_ADV_MMCME2_ADV, id_PLLE2_ADV_PLLE2_ADV)) {
                delay_t period_in;
                if (!get_period(ci, id_CLKIN1, period_in))
                    continue;
                log_info("    Input frequency of PLL '%s' is constrained to %.1f MHz\n", ci->name.c_str(ctx),
                         MHz(period_in));
                double period_in_div = period_in * int_or_default(ci->params, id_DIVCLK_DIVIDE, 1);

                const NetInfo *clkfb = ci->getPort(id_CLKFBIN);
                if (!clkfb || clkfb->driver.cell != ci)
                    continue;
                const std::string &clkfb_port = clkfb->driver.port.str(ctx);
                double feedback_div = 0;
                if (clkfb_port == "CLKFBOUT") {
                    feedback_div = float_or_default(
                            ci, ci->type == id_MMCME2_ADV_MMCME2_ADV ? id_CLKFBOUT_MULT_F : id_CLKFBOUT_MULT, 1);
                } else {
                    if (clkfb_port.substr(0, 6) != "CLKOUT")
                        continue;
                    feedback_div = float_or_default(
                            ci,
                            ctx->idf("CLKOUT%s_DIVIDE%s", clkfb_port.substr(6).c_str(),
                                     (ci->type == id_MMCME2_ADV_MMCME2_ADV && clkfb_port.substr(6) == "0") ? "_F" : ""),
                            1);
                }

                double vco_period = period_in_div / feedback_div;
                double vco_freq = MHz(vco_period);
                log_info("    Derived VCO frequency %.1f MHz for PLL '%s'\n", vco_freq, ci->name.c_str(ctx));

                for (int i = 0; i <= 6; i++) {
                    auto port = ctx->idf("CLKOUT%d", i);
                    if (!ci->getPort(port))
                        continue;
                    set_constraint(
                            ci, port,
                            simple_clk_contraint(
                                    vco_period *
                                    float_or_default(
                                            ci,
                                            ctx->idf("CLKOUT%d_DIVIDE%s", i,
                                                     (ci->type == id_MMCME2_ADV_MMCME2_ADV && i == 0) ? "_F" : ""),
                                            1)));
                }
            }
        }
    }
}
NEXTPNR_NAMESPACE_END

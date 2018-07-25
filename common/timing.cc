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

#include "timing.h"
#include <algorithm>
#include <unordered_map>
#include <utility>
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

typedef std::unordered_map<const PortInfo*, delay_t> UpdateMap;

static delay_t follow_net(Context *ctx, NetInfo *net, int path_length, delay_t slack, UpdateMap &updates, delay_t &min_slack);

// Follow a path, returning budget to annotate
static delay_t follow_user_port(Context *ctx, PortRef &user, int path_length, delay_t slack, UpdateMap &updates, delay_t &min_slack)
{
    delay_t value;
    if (ctx->getPortClock(user.cell, user.port) != IdString()) {
        // At the end of a timing path (arguably, should check setup time
        // here too)
        value = slack / path_length;
        min_slack = std::min(min_slack, value);
    } else {
        // Default to the path ending here, if no further paths found
        value = slack / path_length;
        // Follow outputs of the user
        for (auto port : user.cell->ports) {
            if (port.second.type == PORT_OUT) {
                delay_t comb_delay;
                // Look up delay through this path
                bool is_path = ctx->getCellDelay(user.cell, user.port, port.first, comb_delay);
                if (is_path) {
                    NetInfo *net = port.second.net;
                    if (net) {
                        delay_t path_budget = follow_net(ctx, net, path_length, slack - comb_delay, updates, min_slack);
                        value = std::min(value, path_budget);
                    }
                }
            }
        }
    }

    auto ret = updates.emplace(&user.cell->ports.at(user.port), value);
    if (!ret.second && value < ret.first->second) {
        ret.first->second = value;
    }
    return value;
}

static delay_t follow_net(Context *ctx, NetInfo *net, int path_length, delay_t slack, UpdateMap &updates, delay_t &min_slack)
{
    delay_t net_budget = slack / (path_length + 1);
    for (unsigned i = 0; i < net->users.size(); ++i) {
        auto &usr = net->users[i];
        net_budget = std::min(net_budget, follow_user_port(ctx, usr, path_length + 1, slack - ctx->getNetinfoRouteDelay(net, i), updates, min_slack));
    }
    return net_budget;
}

void assign_budget(Context *ctx)
{
    UpdateMap updates;
    delay_t min_slack = delay_t(1.0e12 / ctx->target_freq);

    log_break();
    log_info("Annotating ports with timing budgets\n");
    // Clear delays to a very high value first
    delay_t default_slack = delay_t(1.0e12 / ctx->target_freq);
    for (auto &net : ctx->nets) {
        for (auto &usr : net.second->users) {
            usr.budget = default_slack;
        }
    }
    min_slack = default_slack;
    // Go through all clocked drivers and set up paths
    for (auto &cell : ctx->cells) {
        for (auto port : cell.second->ports) {
            if (port.second.type == PORT_OUT) {
                IdString clock_domain = ctx->getPortClock(cell.second.get(), port.first);
                if (clock_domain != IdString()) {
                    delay_t slack = delay_t(1.0e12 / ctx->target_freq); // TODO: clock constraints
                    delay_t clkToQ;
                    if (ctx->getCellDelay(cell.second.get(), clock_domain, port.first, clkToQ))
                        slack -= clkToQ;
                    if (port.second.net) {
                        log_break();
                        follow_net(ctx, port.second.net, 0, slack, updates, min_slack);
                    }
                }
            }
        }
    }

    if (!ctx->user_freq) {
        ctx->target_freq = delay_t(1e12 / (default_slack - min_slack));
        if (ctx->verbose)
            log_info("minimum slack for this assign = %d, target Fmax for next update = %f\n", min_slack, ctx->target_freq/1e6);
    }

    // Update the budgets
    for (auto &net : ctx->nets) {
        for (size_t i = 0; i < net.second->users.size(); ++i) {
            auto& user = net.second->users[i];
            auto pi = &user.cell->ports.at(user.port);
            auto it = updates.find(pi);
            if (it == updates.end()) continue;
            auto budget = ctx->getNetinfoRouteDelay(net.second.get(), i) + it->second;
            user.budget = ctx->getBudgetOverride(net.second->driver, budget);

            // Post-update check
            if (user.budget < 0)
                log_warning("port %s.%s, connected to net '%s', has negative "
                            "timing budget of %fns\n",
                            user.cell->name.c_str(ctx), user.port.c_str(ctx), net.first.c_str(ctx),
                            ctx->getDelayNS(user.budget));
            if (ctx->verbose)
                log_info("port %s.%s, connected to net '%s', has "
                         "timing budget of %fns\n",
                         user.cell->name.c_str(ctx), user.port.c_str(ctx), net.first.c_str(ctx),
                         ctx->getDelayNS(user.budget));
        }
    }

    log_info("Checksum: 0x%08x\n", ctx->checksum());
}

void update_budget(Context *ctx)
{
    UpdateMap updates;
    delay_t default_slack = delay_t(1.0e12 / ctx->target_freq);
    delay_t min_slack = delay_t(1.0e12 / ctx->target_freq);

    // Go through all clocked drivers and distribute the available path slack evenly into every budget
    for (auto &cell : ctx->cells) {
        for (auto& port : cell.second->ports) {
            if (port.second.type == PORT_OUT) {
                IdString clock_domain = ctx->getPortClock(cell.second.get(), port.first);
                if (clock_domain != IdString()) {
                    delay_t slack = default_slack; // TODO: clock constraints
                    delay_t clkToQ;
                    if (ctx->getCellDelay(cell.second.get(), clock_domain, port.first, clkToQ))
                        slack -= clkToQ;
                    if (port.second.net)
                        follow_net(ctx, port.second.net, 0, slack, updates, min_slack);
                }
            }
        }
    }

    if (!ctx->user_freq) {
        ctx->target_freq = delay_t(1e12 / (default_slack - min_slack));
        if (ctx->verbose)
            log_info("minimum slack for this update = %d, target Fmax for next update = %f\n", min_slack, ctx->target_freq/1e6);
    }

    // Update the budgets
    for (auto &net : ctx->nets) {
        for (size_t i = 0; i < net.second->users.size(); ++i) {
            auto& user = net.second->users[i];
            auto pi = &user.cell->ports.at(user.port);
            auto it = updates.find(pi);
            if (it == updates.end()) continue;
            auto budget = ctx->getNetinfoRouteDelay(net.second.get(), i) + it->second;
            user.budget = ctx->getBudgetOverride(net.second->driver, budget);

            // Post-update check
            if (ctx->verbose) {
                if (user.budget < 0)
                    log_warning("port %s.%s, connected to net '%s', has negative "
                                "timing budget of %fns\n",
                                user.cell->name.c_str(ctx), user.port.c_str(ctx), net.first.c_str(ctx),
                                ctx->getDelayNS(user.budget));
                else
                    log_info("port %s.%s, connected to net '%s', has "
                             "timing budget of %fns\n",
                             user.cell->name.c_str(ctx), user.port.c_str(ctx), net.first.c_str(ctx),
                             ctx->getDelayNS(user.budget));
            }
        }
    }
}

NEXTPNR_NAMESPACE_END

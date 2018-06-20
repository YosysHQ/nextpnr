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

NEXTPNR_NAMESPACE_BEGIN

static delay_t follow_net(Context *ctx, NetInfo *net, int path_length,
                          delay_t slack);

// Follow a path, returning budget to annotate
static delay_t follow_user_port(Context *ctx, PortRef &user, int path_length,
                                delay_t slack)
{
    delay_t value;
    if (ctx->getPortClock(user.cell, user.port) != IdString()) {
        // At the end of a timing path (arguably, should check setup time
        // here too)
        value = slack / path_length;
    } else {
        // Default to the path ending here, if no further paths found
        value = slack / path_length;
        // Follow outputs of the user
        for (auto port : user.cell->ports) {
            if (port.second.type == PORT_OUT) {
                delay_t comb_delay;
                // Look up delay through this path
                bool is_path = ctx->getCellDelay(user.cell, user.port,
                                                 port.first, comb_delay);
                if (is_path) {
                    NetInfo *net = port.second.net;
                    if (net) {
                        delay_t path_budget = follow_net(ctx, net, path_length,
                                                         slack - comb_delay);
                        value = std::min(value, path_budget);
                    }
                }
            }
        }
    }

    if (value < user.budget) {
        user.budget = value;
    }
    return value;
}

static delay_t follow_net(Context *ctx, NetInfo *net, int path_length,
                          delay_t slack)
{
    delay_t net_budget = slack / (path_length + 1);
    for (auto &usr : net->users) {
        net_budget = std::min(
                net_budget, follow_user_port(ctx, usr, path_length + 1, slack));
    }
    return net_budget;
}

void assign_budget(Context *ctx, float default_clock)
{
    log_info("Annotating ports with timing budgets\n");
    for (auto cell : ctx->cells) {
        for (auto port : cell.second->ports) {
            if (port.second.type == PORT_OUT) {
                IdString clock_domain =
                        ctx->getPortClock(cell.second, port.first);
                if (clock_domain != IdString()) {
                    delay_t slack = delay_t(
                            1.0e12 / default_clock); // TODO: clock constraints
                    if (port.second.net)
                        follow_net(ctx, port.second.net, 0, slack);
                }
            }
        }
    }
}

NEXTPNR_NAMESPACE_END

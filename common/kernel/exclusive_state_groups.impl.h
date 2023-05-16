/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  The SymbiFlow Authors.
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

#pragma once

#include "context.h"
#include "exclusive_state_groups.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

template <size_t StateCount, typename StateType, typename CountType>
void ExclusiveStateGroup<StateCount, StateType, CountType>::print_debug(const Context *ctx, IdString object,
                                                                        const Definition &definition) const
{
    if (state == kNoSelected) {
        NPNR_ASSERT(selected_states.count() == 0);
        log_info("%s.%s is currently unselected\n", object.c_str(ctx), definition.prefix.c_str(ctx));
    } else if (state >= 0) {
        log_info("%s.%s = %s, count = %d\n", object.c_str(ctx), definition.prefix.c_str(ctx),
                 definition.states.at(state).c_str(ctx), count[state]);
    } else {
        NPNR_ASSERT(state == kOverConstrained);
        log_info("%s.%s is currently overconstrained, states selected:\n", object.c_str(ctx),
                 definition.prefix.c_str(ctx));
        for (size_t i = 0; i < definition.states.size(); ++i) {
            if (selected_states[i]) {
                log_info(" - %s, count = %d\n", definition.states.at(i).c_str(ctx), count[i]);
            }
        }
    }
}

template <size_t StateCount, typename StateType, typename CountType>
void ExclusiveStateGroup<StateCount, StateType, CountType>::explain_implies(const Context *ctx, IdString object,
                                                                            IdString cell, const Definition &definition,
                                                                            BelId bel, int32_t next_state) const
{
    if (check_implies(next_state)) {
        log_info("Placing cell %s at bel %s does not violate %s.%s\n", cell.c_str(ctx), ctx->nameOfBel(bel),
                 object.c_str(ctx), definition.prefix.c_str(ctx));
    } else {
        log_info("Placing cell %s at bel %s does violates %s.%s, desired state = %s.\n", cell.c_str(ctx),
                 ctx->nameOfBel(bel), object.c_str(ctx), definition.prefix.c_str(ctx),
                 definition.states.at(next_state).c_str(ctx));
        print_debug(ctx, object, definition);
    }
}

template <size_t StateCount, typename StateType, typename CountType>
template <typename StateRange>
void ExclusiveStateGroup<StateCount, StateType, CountType>::explain_requires(const Context *ctx, IdString object,
                                                                             IdString cell,
                                                                             const Definition &definition, BelId bel,
                                                                             const StateRange state_range) const
{
    if (requires_range(state_range)) {
        log_info("Placing cell %s at bel %s does not violate %s.%s\n", cell.c_str(ctx), ctx->nameOfBel(bel),
                 object.c_str(ctx), definition.prefix.c_str(ctx));
    } else {
        log_info("Placing cell %s at bel %s does violate %s.%s, because current state is %s, constraint requires one "
                 "of:\n",
                 cell.c_str(ctx), ctx->nameOfBel(bel), object.c_str(ctx), definition.prefix.c_str(ctx),
                 state != -1 ? definition.states.at(state).c_str(ctx) : "unset");

        for (const auto required_state : state_range) {
            log_info(" - %s\n", definition.states.at(required_state).c_str(ctx));
        }
        print_debug(ctx, object, definition);
    }
}

NEXTPNR_NAMESPACE_END

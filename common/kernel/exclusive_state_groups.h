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

#ifndef EXCLUSIVE_STATE_GROUPS_H
#define EXCLUSIVE_STATE_GROUPS_H

#include <array>
#include <bitset>
#include <cstdint>
#include <limits>
#include <vector>

#include "archdefs.h"
#include "bits.h"
#include "idstring.h"
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

// Implementation for exclusive state groups, used to implement generic
// constraint system.
template <size_t StateCount, typename StateType = int8_t, typename CountType = uint8_t> struct ExclusiveStateGroup
{
    ExclusiveStateGroup() : state(kNoSelected) { count.fill(0); }
    struct Definition
    {
        IdString prefix;
        IdString default_state;
        std::vector<IdString> states;
    };

    static_assert(StateCount < std::numeric_limits<StateType>::max(), "StateType cannot store max StateType");
    static_assert(std::numeric_limits<StateType>::is_signed, "StateType must be signed");

    std::bitset<StateCount> selected_states;
    StateType state;
    std::array<CountType, StateCount> count;

    static constexpr StateType kNoSelected = -1;
    static constexpr StateType kOverConstrained = -2;

    std::pair<bool, IdString> current_state(const Definition &definition) const
    {
        if (state <= 0) {
            return std::make_pair(state == kNoSelected, definition.default_state);
        } else {
            NPNR_ASSERT(state <= definition.states.size());
            return std::make_pair(true, definition.states[state]);
        }
    }

    bool check_implies(int32_t next_state) const
    {
        // Implies can be satified if either that state is
        // selected, or no state is currently selected.
        return state == next_state || state == kNoSelected;
    }

    bool add_implies(int32_t next_state)
    {
        NPNR_ASSERT(next_state >= 0 && (size_t)next_state < StateCount);

        // Increment and mark the state as selected.
        count[next_state] += 1;
        selected_states[next_state] = true;

        if (state == next_state) {
            // State was already selected, state group is still satified.
            return true;
        } else if (selected_states.count() == 1) {
            // State was not select selected, state is now selected.
            // State group is satified.
            state = next_state;
            return true;
        } else {
            // State group is now overconstrained.
            state = kOverConstrained;
            return false;
        }
    };

    void remove_implies(int32_t next_state)
    {
        NPNR_ASSERT(next_state >= 0 && (size_t)next_state < StateCount);
        NPNR_ASSERT(selected_states[next_state]);

        count[next_state] -= 1;
        NPNR_ASSERT(count[next_state] >= 0);

        // Check if next_state is now unselected.
        if (count[next_state] == 0) {
            // next_state is not longer selected
            selected_states[next_state] = false;

            // Check whether the state group is now unselected or satified.
            auto value = selected_states.to_ulong();
            auto number_selected = Bits::popcount(value);
            if (number_selected == 1) {
                // Group is no longer overconstrained.
                state = Bits::ctz(value);
                NPNR_ASSERT(selected_states[state]);
            } else if (number_selected == 0) {
                // Group is unselected.
                state = kNoSelected;
            } else {
                state = kOverConstrained;
            }
        }
    }

    template <typename StateRange> bool requires_range(const StateRange &state_range) const
    {
        if (state < 0) {
            return false;
        }

        for (const auto required_state : state_range) {
            if (state == required_state) {
                return true;
            }
        }

        return false;
    }

    void print_debug(const Context *ctx, IdString object, const Definition &definition) const;
    void explain_implies(const Context *ctx, IdString object, IdString cell, const Definition &definition, BelId bel,
                         int32_t next_state) const;

    template <typename StateRange>
    void explain_requires(const Context *ctx, IdString object, IdString cell, const Definition &definition, BelId bel,
                          const StateRange state_range) const;
};

NEXTPNR_NAMESPACE_END

#endif

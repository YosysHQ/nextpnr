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

#ifndef CONSTRAINTS_IMPL_H
#define CONSTRAINTS_IMPL_H

#include "exclusive_state_groups.impl.h"

NEXTPNR_NAMESPACE_BEGIN

template <size_t StateCount, typename StateType, typename CountType>
template <typename ConstraintRange>
void Constraints<StateCount, StateType, CountType>::bindBel(TagState *tags, const ConstraintRange constraints)
{
    for (const auto &constraint : constraints) {
        switch (constraint.constraint_type()) {
        case CONSTRAINT_TAG_IMPLIES:
            tags[constraint.tag()].add_implies(constraint.state());
            break;
        case CONSTRAINT_TAG_REQUIRES:
            break;
        default:
            NPNR_ASSERT(false);
        }
    }
}

template <size_t StateCount, typename StateType, typename CountType>
template <typename ConstraintRange>
void Constraints<StateCount, StateType, CountType>::unbindBel(TagState *tags, const ConstraintRange constraints)
{
    for (const auto &constraint : constraints) {
        switch (constraint.constraint_type()) {
        case CONSTRAINT_TAG_IMPLIES:
            tags[constraint.tag()].remove_implies(constraint.state());
            break;
        case CONSTRAINT_TAG_REQUIRES:
            break;
        default:
            NPNR_ASSERT(false);
        }
    }
}

template <size_t StateCount, typename StateType, typename CountType>
template <typename ConstraintRange>
bool Constraints<StateCount, StateType, CountType>::isValidBelForCellType(const Context *ctx, uint32_t prototype,
                                                                          const TagState *tags,
                                                                          const ConstraintRange constraints,
                                                                          IdString object, IdString cell, BelId bel,
                                                                          bool explain_constraints) const
{
    if (explain_constraints) {
        auto &state_definition = definitions.at(prototype);
        for (const auto &constraint : constraints) {
            switch (constraint.constraint_type()) {
            case CONSTRAINT_TAG_IMPLIES:
                tags[constraint.tag()].explain_implies(ctx, object, cell, state_definition.at(constraint.tag()), bel,
                                                       constraint.state());
                break;
            case CONSTRAINT_TAG_REQUIRES:
                tags[constraint.tag()].explain_requires(ctx, object, cell, state_definition.at(constraint.tag()), bel,
                                                        constraint.states());
                break;
            default:
                NPNR_ASSERT(false);
            }
        }
    }

    for (const auto &constraint : constraints) {
        switch (constraint.constraint_type()) {
        case CONSTRAINT_TAG_IMPLIES:
            if (!tags[constraint.tag()].check_implies(constraint.state())) {
                return false;
            }
            break;
        case CONSTRAINT_TAG_REQUIRES:
            if (!tags[constraint.tag()].requires_range(constraint.states())) {
                return false;
            }
            break;
        default:
            NPNR_ASSERT(false);
        }
    }

    return true;
}

NEXTPNR_NAMESPACE_END

#endif

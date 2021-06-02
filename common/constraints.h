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

#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include <cstdint>
#include <vector>

#include "archdefs.h"
#include "exclusive_state_groups.h"
#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context;

template <std::size_t StateCount, typename StateType = int8_t, typename CountType = uint8_t> struct Constraints
{
    using ConstraintStateType = StateType;
    using ConstraintCountType = CountType;

    enum ConstraintType
    {
        CONSTRAINT_TAG_IMPLIES = 0,
        CONSTRAINT_TAG_REQUIRES = 1,
    };

    template <typename StateRange> struct Constraint
    {
        virtual std::size_t tag() const = 0;
        virtual ConstraintType constraint_type() const = 0;
        virtual StateType state() const = 0;
        virtual StateRange states() const = 0;
    };

    typedef ExclusiveStateGroup<StateCount, StateType, CountType> TagState;
    dict<uint32_t, std::vector<typename TagState::Definition>> definitions;

    template <typename ConstraintRange> void bindBel(TagState *tags, const ConstraintRange constraints);

    template <typename ConstraintRange> void unbindBel(TagState *tags, const ConstraintRange constraints);

    template <typename ConstraintRange>
    bool isValidBelForCellType(const Context *ctx, uint32_t prototype, const TagState *tags,
                               const ConstraintRange constraints, IdString object, IdString cell, BelId bel,
                               bool explain_constraints) const;
};

NEXTPNR_NAMESPACE_END

#endif

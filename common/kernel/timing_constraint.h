/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  rowanG077 <goemansrowan@gmail.com>
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

#ifndef TIMING_CONSTRAINT_H
#define TIMING_CONSTRAINT_H

#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

struct FalsePath
{
};

struct MinMaxDelay
{
    enum class Type
    {
        MAXDELAY,
        MINDELAY
    };

    [[maybe_unused]] const std::string type_to_str(Type typ);

    Type type;
    delay_t delay;
    bool datapath_only;
};

struct MultiCycle
{
    size_t cycles;
    enum class Type
    {
        SETUP,
        HOLD
    };
};

using TimingException = std::variant<FalsePath, MinMaxDelay, MultiCycle>;

struct PathConstraint
{
    TimingException exception;

    pool<CellPortKey> from;
    pool<CellPortKey> to;
};

NEXTPNR_NAMESPACE_END

#endif

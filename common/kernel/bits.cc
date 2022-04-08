/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (c) 2013  Mike Pedersen
 *  Copyright (C) 2021  Symbiflow Authors
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

#include "bits.h"

#include <limits>
#include <stdexcept>

#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

int Bits::generic_popcount(unsigned int v)
{
    unsigned int c; // c accumulates the total bits set in v
    for (c = 0; v; c++) {
        v &= v - 1; // clear the least significant bit set
    }

    return c;
}

int Bits::generic_ctz(unsigned int x)
{
    if (x == 0) {
        log_error("Cannot call ctz with arg = 0");
    }

    for (size_t i = 0; i < std::numeric_limits<unsigned int>::digits; ++i) {
        if ((x & (1 << i)) != 0) {
            return i;
        }
    }

    // Unreachable!
    log_error("Unreachable!");
}

NEXTPNR_NAMESPACE_END

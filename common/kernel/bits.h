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

// This is a small library for implementing common bit vector utilities,
// namely:
//
//  - popcount : The number of bits set in an unsigned int
//  - ctz : The number of trailing zero bits in an unsigned int.
//          Must be called with a value that has at least 1 bit set.
//
// These methods will typically use instrinics when available, and have a
// generic fallback in the event that the instrinic is not available.
//
// If clz (count leading zeros) is needed, it can be added when needed.
#ifndef BITS_H
#define BITS_H

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#pragma intrinsic(_BitScanForward, _BitScanReverse, __popcnt)
#endif

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

struct Bits
{
    static int generic_popcount(unsigned int x);
    static int generic_ctz(unsigned int x);

    static int popcount(unsigned int x)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_popcount(x);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        return __popcnt(x);
#else
        return generic_popcount(x);
#endif
    }

    static int ctz(unsigned int x)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_ctz(x);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        unsigned long result;
        _BitScanForward(&result, x);
        return result;
#else
        return generic_ctz(x);
#endif
    }
};

NEXTPNR_NAMESPACE_END

#endif /* BITS_H */

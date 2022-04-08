/*
 *  nextpnr -- Next Generation Place and Route
 *
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

#ifndef DYNAMIC_BITARRAY_H
#define DYNAMIC_BITARRAY_H

#include <cstdint>
#include <limits>
#include <vector>

#include "log.h"
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

// This class implements a simple dynamic bitarray, backed by some resizable
// random access storage.  The default is to use a std::vector<uint8_t>.
template <typename Storage = std::vector<uint8_t>> class DynamicBitarray
{
  public:
    static_assert(!std::numeric_limits<typename Storage::value_type>::is_signed, "Storage must be unsigned!");

    void fill(bool value)
    {
        std::fill(storage.begin(), storage.end(), value ? std::numeric_limits<typename Storage::value_type>::max() : 0);
    }

    constexpr size_t bits_per_value() const { return std::numeric_limits<typename Storage::value_type>::digits; }

    bool get(size_t bit) const
    {
        size_t element_index = bit / bits_per_value();
        size_t bit_offset = bit % bits_per_value();

        auto element = storage.at(element_index);
        return (element & (1 << bit_offset)) != 0;
    }

    void set(size_t bit, bool value)
    {
        size_t element_index = bit / bits_per_value();
        size_t bit_offset = bit % bits_per_value();

        if (value) {
            storage.at(element_index) |= (1 << bit_offset);
        } else {
            storage.at(element_index) &= ~(1 << bit_offset);
        }
    }

    void resize(size_t number_bits)
    {
        size_t required_storage = (number_bits + bits_per_value() - 1) / bits_per_value();
        storage.resize(required_storage);
    }

    size_t size() const { return storage.size() * bits_per_value(); }

    void clear() { return storage.clear(); }

    // Convert IntType to a DynamicBitarray of sufficent width
    template <typename IntType> static DynamicBitarray<Storage> to_bitarray(const IntType &value)
    {
        if (std::numeric_limits<IntType>::is_signed) {
            if (value < 0) {
                log_error("Expected position value, got %s\n", std::to_string(value).c_str());
            }
        }

        DynamicBitarray<Storage> result;
        result.resize(std::numeric_limits<IntType>::digits);
        result.fill(false);

        // Use a 1 of the right type (for shifting)
        IntType one(1);

        for (size_t i = 0; i < std::numeric_limits<IntType>::digits; ++i) {
            if ((value & (one << i)) != 0) {
                result.set(i, true);
            }
        }

        return result;
    }

    // Convert binary bitstring to a DynamicBitarray of sufficent width
    //
    // string must be satisfy the following regex:
    //
    //    [01]+
    //
    // width can either be specified explicitly, or -1 to use a size wide
    // enough to store the given string.
    //
    // If the width is specified and the width is insufficent it will result
    // in an error.
    static DynamicBitarray<Storage> parse_binary_bitstring(int width, const std::string &bits)
    {
        NPNR_ASSERT(width == -1 || width > 0);

        DynamicBitarray<Storage> result;
        // If no width was supplied, use the width from the input data.
        if (width == -1) {
            width = bits.size();
        }

        NPNR_ASSERT(width >= 0);
        if ((size_t)width < bits.size()) {
            log_error("String '%s' is wider than specified width %d\n", bits.c_str(), width);
        }
        result.resize(width);
        result.fill(false);

        for (size_t i = 0; i < bits.size(); ++i) {
            // bits[0] is the MSB!
            size_t index = width - 1 - i;
            if (!(bits[i] == '1' || bits[i] == '0')) {
                log_error("String '%s' is not a valid binary bitstring?\n", bits.c_str());
            }
            result.set(index, bits[i] == '1');
        }

        return result;
    }

    // Convert hex bitstring to a DynamicBitarray of sufficent width
    //
    // string must be satisfy the following regex:
    //
    //    [0-9a-fA-F]+
    //
    // width can either be specified explicitly, or -1 to use a size wide
    // enough to store the given string.
    //
    // If the width is specified and the width is insufficent it will result
    // in an error.
    static DynamicBitarray<Storage> parse_hex_bitstring(int width, const std::string &bits)
    {
        NPNR_ASSERT(width == -1 || width > 0);

        DynamicBitarray<Storage> result;
        // If no width was supplied, use the width from the input data.
        if (width == -1) {
            // Each character is 4 bits!
            width = bits.size() * 4;
        }

        NPNR_ASSERT(width >= 0);
        int rem = width % 4;
        size_t check_width = width;
        if (rem != 0) {
            check_width += (4 - rem);
        }
        if (check_width < bits.size() * 4) {
            log_error("String '%s' is wider than specified width %d (check_width = %zu)\n", bits.c_str(), width,
                      check_width);
        }

        result.resize(width);
        result.fill(false);

        size_t index = 0;
        for (auto nibble_iter = bits.rbegin(); nibble_iter != bits.rend(); ++nibble_iter) {
            char nibble = *nibble_iter;

            int value;
            if (nibble >= '0' && nibble <= '9') {
                value = nibble - '0';
            } else if (nibble >= 'a' && nibble <= 'f') {
                value = 10 + (nibble - 'a');
            } else if (nibble >= 'A' && nibble <= 'F') {
                value = 10 + (nibble - 'A');
            } else {
                log_error("Invalid hex string '%s'?\n", bits.c_str());
            }
            NPNR_ASSERT(value >= 0);
            NPNR_ASSERT(value < 16);

            // Insert nibble into bitarray.
            for (size_t i = 0; i < 4; ++i) {
                result.set(index++, (value & (1 << i)) != 0);
            }
        }

        return result;
    }

  private:
    Storage storage;
};

NEXTPNR_NAMESPACE_END

#endif /* DYNAMIC_BITARRAY_H */

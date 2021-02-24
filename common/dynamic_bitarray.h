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

#include <cstdint>
#include <limits>
#include <vector>

// This class implements a simple dynamic bitarray, backed by some resizable
// random access storage.  The default is to use a std::vector<uint8_t>.

#ifndef DYNAMIC_BITARRAY_H
#define DYNAMIC_BITARRAY_H

namespace nextpnr {

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

  private:
    Storage storage;
};

}; // namespace nextpnr

#endif /* DYNAMIC_BITARRAY_H */

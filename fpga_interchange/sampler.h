/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#ifndef SAMPLER_H_
#define SAMPLER_H_

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

// Given a set of coordinates, generates random samples that are geometric
// distributed.
struct Sampler
{

    void divide_samples(size_t target_sample_count, const std::vector<std::pair<int32_t, int32_t>> &samples);

    size_t number_of_regions() const { return splits.size() - 1; }

    size_t get_sample_from_region(size_t region, std::function<int32_t()> rng) const
    {
        if (region >= (splits.size() - 1)) {
            throw std::runtime_error("region out of range");
        }
        size_t split_begin = splits[region];
        size_t split_end = splits[region + 1];
        if (split_begin == split_end) {
            throw std::runtime_error("Splits should never be empty!");
        }

        // Pick a random element from that region.
        return indicies[split_begin + (rng() % (split_end - split_begin))];
    }

    size_t get_sample(std::function<int32_t()> rng) const
    {
        size_t region = rng() % number_of_regions();
        return get_sample_from_region(region, rng);
    }

    std::vector<size_t> indicies;
    std::vector<size_t> splits;
};

NEXTPNR_NAMESPACE_END

#endif /* SAMPLER_H_ */

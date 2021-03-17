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

#include "sampler.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

NEXTPNR_NAMESPACE_BEGIN

static size_t partition_x(std::vector<size_t>::iterator begin, std::vector<size_t>::iterator end,
                          const std::vector<std::pair<int32_t, int32_t>> &samples)
{
    if (std::distance(begin, end) == 0) {
        return 0;
    }

    // Find the median x value.
    std::vector<int32_t> xs;
    xs.reserve(std::distance(begin, end));

    for (auto iter = begin; iter != end; ++iter) {
        xs.push_back(samples[*iter].first);
    }

    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());

    // Partion on the median x value (e.g. 50% of samples on one side and
    // 50% of samples on the other side).
    int32_t x_div = xs[(xs.size() - 1) / 2];

    auto split = std::partition(begin, end,
                                [x_div, &samples](size_t index) -> bool { return samples[index].first <= x_div; });

    return std::distance(begin, split);
}

/* Don't both splitting when the partition has less than kMinSplit. */
static constexpr ptrdiff_t kMinSplit = 20;

static size_t partition_y(std::vector<size_t>::iterator begin, std::vector<size_t>::iterator end,
                          const std::vector<std::pair<int32_t, int32_t>> &samples)
{
    if (std::distance(begin, end) == 0) {
        return 0;
    }

    std::vector<int32_t> ys;
    ys.reserve(std::distance(begin, end));

    for (auto iter = begin; iter != end; ++iter) {
        ys.push_back(samples[*iter].second);
    }

    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

    int32_t y_div = ys[(ys.size() - 1) / 2];

    auto split = std::partition(begin, end,
                                [y_div, &samples](size_t index) -> bool { return samples[index].second <= y_div; });

    return std::distance(begin, split);
}

static void add_split(std::vector<size_t> *splits, size_t new_split)
{
    if (splits->back() < new_split) {
        splits->push_back(new_split);
    } else if (splits->back() != new_split) {
        throw std::runtime_error("Split is not consectutive!");
    }
}

void Sampler::divide_samples(size_t target_sample_count, const std::vector<std::pair<int32_t, int32_t>> &samples)
{
    // Initialize indicies lookup and make 1 split with entire sample range.
    indicies.resize(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        indicies[i] = i;
    }

    splits.reserve(2);
    splits.push_back(0);
    splits.push_back(samples.size());

    size_t divisions = std::ceil(std::sqrt(target_sample_count) / 2.);
    if (divisions == 0) {
        throw std::runtime_error("Math failure, unreachable!");
    }

    if (divisions > samples.size()) {
        // Handle cases where there are few samples.
        return;
    }

    // Recursively split samples first 50% / 50% in x direction, and then
    // 50% / 50% in y direction.  Repeat until the bucket is smaller than
    // kMinSplit or the samples have been divided `divisions` times.
    std::vector<size_t> new_splits;
    for (size_t division_count = 0; division_count < divisions; ++division_count) {
        new_splits.clear();
        new_splits.push_back(0);
        for (size_t i = 0; i < splits.size() - 1; ++i) {
            size_t split_begin = splits.at(i);
            size_t split_end = splits.at(i + 1);
            if (split_end > indicies.size()) {
                throw std::runtime_error("split_end is not valid!");
            }
            if (split_begin >= split_end) {
                throw std::runtime_error("Invalid split from earlier pass!");
            }

            std::vector<size_t>::iterator begin = indicies.begin() + split_begin;
            std::vector<size_t>::iterator end = indicies.begin() + split_end;

            if (std::distance(begin, end) < kMinSplit) {
                add_split(&new_splits, split_begin);
                continue;
            }

            // Try to split samples 50/50 in x direction.
            size_t split = partition_x(begin, end, samples);
            // Try to split samples 50/50 in y direction after the x split.
            size_t split_y1 = partition_y(begin, begin + split, samples);
            size_t split_y2 = partition_y(begin + split, end, samples);

            // Because the y2 split starts at split, add it here.
            split_y2 += split;

            add_split(&new_splits, split_begin);
            add_split(&new_splits, split_begin + split_y1);
            add_split(&new_splits, split_begin + split);
            add_split(&new_splits, split_begin + split_y2);
        }

        add_split(&new_splits, samples.size());

        if (new_splits.front() != 0) {
            throw std::runtime_error("Split must start at 0");
        }
        if (new_splits.back() != samples.size()) {
            throw std::runtime_error("Split must end at last element");
        }

        for (size_t i = 0; i < new_splits.size() - 1; ++i) {
            if (new_splits[i] >= new_splits[i + 1]) {
                throw std::runtime_error("Split indicies must be increasing");
            }
        }

        std::swap(splits, new_splits);
    }
}

NEXTPNR_NAMESPACE_END

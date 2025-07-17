/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#ifndef GATEMATE_UTIL_H
#define GATEMATE_UTIL_H

#include <map>
#include <set>
#include <string>
#include "nextpnr.h"

#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

template <typename KeyType>
double double_or_default(const dict<KeyType, Property> &ct, const KeyType &key, double def = 0)
{
    auto found = ct.find(key);
    if (found == ct.end())
        return def;
    else {
        if (found->second.is_string) {
            try {
                return std::stod(found->second.as_string());
            } catch (std::invalid_argument &e) {
                log_error("Expecting numeric value but got '%s'.\n", found->second.as_string().c_str());
            }
        } else
            return double(found->second.as_int64());
    }
};

template <typename KeyType>
int extract_bits(const dict<KeyType, Property> &ct, const KeyType &key, int start, int bits, int def = 0)
{
    Property extr = get_or_default(ct, key, Property()).extract(start, bits);

    if (extr.is_string) {
        try {
            return std::stoi(extr.as_string());
        } catch (std::invalid_argument &e) {
            log_error("Expecting numeric value but got '%s'.\n", extr.as_string().c_str());
        }
    } else
        return extr.as_int64();
}

template <typename T>
std::vector<std::vector<T>> splitNestedVector(const std::vector<std::vector<T>> &input, size_t maxSize = 64)
{
    std::vector<std::vector<T>> result;

    for (const auto &inner : input) {
        size_t i = 0;
        while (i < inner.size()) {
            size_t end = std::min(i + maxSize, inner.size());
            result.emplace_back(inner.begin() + i, inner.begin() + end);
            i = end;
            if (i < inner.size())
                log_warning("Carry chain has been split, expect timing penalty.\n");
        }
    }

    return result;
}

NEXTPNR_NAMESPACE_END

#endif

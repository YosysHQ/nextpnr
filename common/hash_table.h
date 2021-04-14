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

#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#if defined(USE_ABSEIL)
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#else
#include <unordered_map>
#include <unordered_set>
#endif

#include <boost/functional/hash.hpp>

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

namespace HashTables {
#if defined(USE_ABSEIL)
template <typename Key, typename Value, typename Hash = std::hash<Key>>
using HashMap = absl::flat_hash_map<Key, Value, Hash>;
template <typename Value, typename Hash = std::hash<Value>> using HashSet = absl::flat_hash_set<Value, Hash>;
#else
template <typename Key, typename Value, typename Hash = std::hash<Key>>
using HashMap = std::unordered_map<Key, Value, Hash>;
template <typename Value, typename Hash = std::hash<Value>> using HashSet = std::unordered_set<Value, Hash>;
#endif

}; // namespace HashTables

struct PairHash
{
    template <typename T1, typename T2> std::size_t operator()(const std::pair<T1, T2> &idp) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<T1>()(idp.first));
        boost::hash_combine(seed, std::hash<T2>()(idp.second));
        return seed;
    }
};

NEXTPNR_NAMESPACE_END

#endif /* HASH_TABLE_H */

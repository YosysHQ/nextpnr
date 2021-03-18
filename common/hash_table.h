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

#if defined(NPNR_DISABLE_THREADS)
#include <unordered_map>
#include <unordered_set>
#else
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#endif

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

namespace HashTables {
#if defined(NPNR_DISABLE_THREADS)
template <typename Key, typename Value> using HashMap = std::unordered_map<Key, Value>;
template <typename Value> using HashSet = std::unordered_set<Value>;
#else
template <typename Key, typename Value> using HashMap = absl::flat_hash_map<Key, Value>;
template <typename Value> using HashSet = absl::flat_hash_set<Value>;
#endif

}; // namespace HashTables

NEXTPNR_NAMESPACE_END

#endif /* HASH_TABLE_H */

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#ifndef CHAIN_UTILS_H
#define CHAIN_UTILS_H

#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

struct CellChain
{
    std::vector<CellInfo *> cells;
};

// Generic chain finder
template <typename F1, typename F2, typename F3>
std::vector<CellChain> find_chains(const Context *ctx, F1 cell_type_predicate, F2 get_previous, F3 get_next,
                                   size_t min_length = 2)
{
    std::set<IdString> chained;
    std::vector<CellChain> chains;
    for (auto cell : sorted(ctx->cells)) {
        if (chained.find(cell.first) != chained.end())
            continue;
        CellInfo *ci = cell.second;
        if (cell_type_predicate(ctx, ci)) {
            CellInfo *start = ci;
            CellInfo *prev_start = ci;
            while (prev_start != nullptr) {
                start = prev_start;
                prev_start = get_previous(ctx, start);
            }
            CellChain chain;
            CellInfo *end = start;
            while (end != nullptr) {
                if (chained.insert(end->name).second)
                    chain.cells.push_back(end);
                end = get_next(ctx, end);
            }
            if (chain.cells.size() >= min_length) {
                chains.push_back(chain);
                for (auto c : chain.cells)
                    chained.insert(c->name);
            }
        }
    }
    return chains;
}

NEXTPNR_NAMESPACE_END
#endif

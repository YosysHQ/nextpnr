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

#ifndef NEXTPNR_H
#error Include "type_wire.h" via "nextpnr.h" only.
#endif

#ifndef COST_MAP_H
#define COST_MAP_H

#include "nextpnr.h"
#include "boost/multi_array.hpp"
#include "type_wire.h"

NEXTPNR_NAMESPACE_BEGIN

class CostMap {
public:
    delay_t get_delay(const Context *ctx, WireId src, WireId dst) const;
    void set_cost_map(const Context *ctx, const TypeWirePair & wire_pair, const absl::flat_hash_map<std::pair<int32_t, int32_t>, delay_t> &delays);
private:
    struct CostMapEntry {
        boost::multi_array<delay_t, 2> data;
        std::pair<int32_t, int32_t> offset;
        delay_t penalty;
    };

    std::mutex cost_map_mutex_;
    absl::flat_hash_map<TypeWirePair, CostMapEntry> cost_map_;

    void fill_holes(const Context *ctx, const TypeWirePair & wire_pair, boost::multi_array<delay_t, 2>& matrix, delay_t delay_penality);

    std::pair<delay_t, int> get_nearby_cost_entry(const boost::multi_array<delay_t, 2>& matrix,
                                                        int cx,
                                                        int cy,
                                                        const ArcBounds& bounds);
    delay_t get_penalty(const boost::multi_array<delay_t, 2>& matrix) const;
};

NEXTPNR_NAMESPACE_END

#endif /* COST_MAP_H */

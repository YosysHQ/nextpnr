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

#ifndef COST_MAP_H
#define COST_MAP_H

#include <boost/multi_array.hpp>
#include <mutex>

#include "lookahead.capnp.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"
#include "type_wire.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context;

class CostMap
{
  public:
    delay_t get_delay(const Context *ctx, WireId src, WireId dst) const;
    void set_cost_map(const Context *ctx, const TypeWirePair &wire_pair,
                      const dict<std::pair<int32_t, int32_t>, delay_t> &delays);

    void from_reader(lookahead_storage::CostMap::Reader reader);
    void to_builder(lookahead_storage::CostMap::Builder builder) const;

  private:
    struct CostMapEntry
    {
        boost::multi_array<delay_t, 2> data;
        std::pair<int32_t, int32_t> offset;
        delay_t penalty;
    };

    std::mutex cost_map_mutex_;
    dict<TypeWirePair, CostMapEntry> cost_map_;

    void fill_holes(const Context *ctx, const TypeWirePair &wire_pair, boost::multi_array<delay_t, 2> &matrix,
                    delay_t delay_penality);

    std::pair<delay_t, int> get_nearby_cost_entry(const boost::multi_array<delay_t, 2> &matrix, int cx, int cy,
                                                  const BoundingBox &bounds);
    delay_t get_penalty(const boost::multi_array<delay_t, 2> &matrix) const;
};

NEXTPNR_NAMESPACE_END

#endif /* COST_MAP_H */

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

#ifndef PSEUDO_PIP_MODEL_H
#define PSEUDO_PIP_MODEL_H

#include <tuple>

#include "dynamic_bitarray.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"
#include "site_router.h"

NEXTPNR_NAMESPACE_BEGIN

struct PseudoPipBel
{
    // Which BEL in the tile does the pseudo pip use?
    int32_t bel_index;

    // What is the index of the input BEL pin that the pseudo pip used?
    //
    // NOTE: This is **not** the name of the pin.
    int32_t input_bel_pin;

    // What is the index of the output BEL pin that the pseudo pip used?
    //
    // NOTE: This is **not** the name of the pin.
    int32_t output_bel_pin;
};

struct LogicBelKey
{
    int32_t tile_type;
    int32_t pip_index;
    int32_t site;

    std::tuple<int32_t, int32_t, int32_t> make_tuple() const { return std::make_tuple(tile_type, pip_index, site); }

    bool operator==(const LogicBelKey &other) const { return make_tuple() == other.make_tuple(); }

    bool operator<(const LogicBelKey &other) const { return make_tuple() < other.make_tuple(); }

    unsigned int hash() const { return mkhash(mkhash(tile_type, pip_index), site); }
};

// Storage for tile type generic pseudo pip data and lookup.
struct PseudoPipData
{
    // Initial data for specified tile type, if not already initialized.
    void init_tile_type(const Context *ctx, int32_t tile_type);

    // Get the highest PipId::index found in a specified tile type.
    size_t get_max_pseudo_pip(int32_t tile_type) const;

    // Get the list of possible sites that a pseudo pip might be used in.
    const std::vector<size_t> &get_possible_sites_for_pip(const Context *ctx, PipId pip) const;

    // Get list of BELs the pseudo pip uses, and how it routes through them.
    //
    // This does **not** include site ports or site pips.
    const std::vector<PseudoPipBel> &get_logic_bels_for_pip(const Context *ctx, int32_t site, PipId pip) const;

    dict<int32_t, size_t> max_pseudo_pip_for_tile_type;
    dict<std::pair<int32_t, int32_t>, std::vector<size_t>> possibles_sites_for_pip;
    dict<LogicBelKey, std::vector<PseudoPipBel>> logic_bels_for_pip;
};

// Tile instance fast pseudo pip lookup.
struct PseudoPipModel
{
    int32_t tile;
    DynamicBitarray<> allowed_pseudo_pips;
    dict<int32_t, size_t> pseudo_pip_sites;
    dict<size_t, std::vector<int32_t>> site_to_pseudo_pips;
    pool<int32_t> active_pseudo_pips;
    std::vector<int32_t> scratch;

    // Call when a tile is initialized.
    void init(Context *ctx, int32_t tile);

    // Call after placement but before routing to update which pseudo pips are
    // legal.  This call is important to ensure that checkPipAvail returns the
    // correct value.
    //
    // If the tile has no placed elements, then prepare_for_routing does not
    // need to be called after init.
    void prepare_for_routing(const Context *ctx, const std::vector<SiteRouter> &sites);

    // Returns true if the pseudo pip is allowed given current site placements
    // and other pseudo pips.
    bool checkPipAvail(const Context *ctx, PipId pip) const;

    // Enables a pseudo pip in the model.  May cause other pseudo pips to
    // become unavailable.
    void bindPip(const Context *ctx, PipId pip);

    // Removes a pseudo pip from the model.  May cause other pseudo pips to
    // become available.
    void unbindPip(const Context *ctx, PipId pip);

    // Internal method to update pseudo pips marked as part of a site.
    void update_site(const Context *ctx, size_t site);
};

NEXTPNR_NAMESPACE_END

#endif /* PSEUDO_PIP_MODEL_H */

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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

#include <list>

#include "log.h"
#include "nextpnr.h"

#if 0
#define dbg(...) log(__VA_ARGS__)
#else
#define dbg(...)
#endif

USING_NEXTPNR_NAMESPACE

#ifndef ARCH_MISTRAL
// The LRU cache to reduce memory usage during the connectivity check relies on getPips() having some spacial locality,
// which the current CycloneV arch impl doesn't have. This may be fixed in the future, though.
#define USING_LRU_CACHE
#endif

namespace {

void archcheck_names(const Context *ctx)
{
    log_info("Checking entity names.\n");

    log_info("Checking bel names..\n");
    for (BelId bel : ctx->getBels()) {
        IdStringList name = ctx->getBelName(bel);
        BelId bel2 = ctx->getBelByName(name);
        if (bel != bel2) {
            log_error("bel != bel2, name = %s\n", ctx->nameOfBel(bel));
        }
    }

    log_info("Checking wire names..\n");
    for (WireId wire : ctx->getWires()) {
        IdStringList name = ctx->getWireName(wire);
        WireId wire2 = ctx->getWireByName(name);
        if (wire != wire2) {
            log_error("wire != wire2, name = %s\n", ctx->nameOfWire(wire));
        }
    }

    log_info("Checking bucket names..\n");
    for (BelBucketId bucket : ctx->getBelBuckets()) {
        IdString name = ctx->getBelBucketName(bucket);
        BelBucketId bucket2 = ctx->getBelBucketByName(name);
        if (bucket != bucket2) {
            log_error("bucket != bucket2, name = %s\n", name.c_str(ctx));
        }
    }

#ifndef ARCH_ECP5
    log_info("Checking pip names..\n");
    for (PipId pip : ctx->getPips()) {
        IdStringList name = ctx->getPipName(pip);
        PipId pip2 = ctx->getPipByName(name);
        if (pip != pip2) {
            log_error("pip != pip2, name = %s\n", ctx->nameOfPip(pip));
        }
    }
#endif
    log_break();
}

void archcheck_locs(const Context *ctx)
{
    log_info("Checking location data.\n");

    log_info("Checking all bels..\n");
    for (BelId bel : ctx->getBels()) {
        log_assert(bel != BelId());
        dbg("> %s\n", ctx->getBelName(bel).c_str(ctx));

        Loc loc = ctx->getBelLocation(bel);
        dbg("   ... %d %d %d\n", loc.x, loc.y, loc.z);

        log_assert(0 <= loc.x);
        log_assert(0 <= loc.y);
        log_assert(0 <= loc.z);
        log_assert(loc.x < ctx->getGridDimX());
        log_assert(loc.y < ctx->getGridDimY());
        log_assert(loc.z < ctx->getTileBelDimZ(loc.x, loc.y));

        BelId bel2 = ctx->getBelByLocation(loc);
        dbg("   ... %s\n", ctx->getBelName(bel2).c_str(ctx));
        log_assert(bel == bel2);
    }

    log_info("Checking all locations..\n");
    for (int x = 0; x < ctx->getGridDimX(); x++)
        for (int y = 0; y < ctx->getGridDimY(); y++) {
            dbg("> %d %d\n", x, y);
            pool<int> usedz;

            for (int z = 0; z < ctx->getTileBelDimZ(x, y); z++) {
                BelId bel = ctx->getBelByLocation(Loc(x, y, z));
                if (bel == BelId())
                    continue;
                Loc loc = ctx->getBelLocation(bel);
                dbg("   + %d %s\n", z, ctx->nameOfBel(bel));
                log_assert(x == loc.x);
                log_assert(y == loc.y);
                log_assert(z == loc.z);
                usedz.insert(z);
            }

            for (BelId bel : ctx->getBelsByTile(x, y)) {
                Loc loc = ctx->getBelLocation(bel);
                dbg("   - %d %s\n", loc.z, ctx->nameOfBel(bel));
                log_assert(x == loc.x);
                log_assert(y == loc.y);
                log_assert(usedz.count(loc.z));
                usedz.erase(loc.z);
            }

            log_assert(usedz.empty());
        }

    log_break();
}

// Implements a LRU cache for pip to wire via getPipsDownhill/getPipsUphill.
//
// This allows a fast way to check getPipsDownhill/getPipsUphill from getPips,
// without balloning memory usage.
struct LruWireCacheMap
{
    LruWireCacheMap(const Context *ctx, size_t cache_size) : ctx(ctx), cache_size(cache_size)
    {
        cache_hits = 0;
        cache_misses = 0;
        cache_evictions = 0;
    }

    const Context *ctx;
    size_t cache_size;

    // Cache stats for checking on cache behavior.
    size_t cache_hits;
    size_t cache_misses;
    size_t cache_evictions;

    // Most recent accessed wires are added to the back of the list, front of
    // list is oldest wire in cache.
    std::list<WireId> last_access_list;
    // Quick wire -> list element lookup.
    dict<WireId, std::list<WireId>::iterator> last_access_map;

    dict<PipId, WireId> pips_downhill;
    dict<PipId, WireId> pips_uphill;

    void removeWireFromCache(WireId wire_to_remove)
    {
        for (PipId pip : ctx->getPipsDownhill(wire_to_remove)) {
            log_assert(pips_downhill.erase(pip) == 1);
        }

        for (PipId pip : ctx->getPipsUphill(wire_to_remove)) {
            log_assert(pips_uphill.erase(pip) == 1);
        }
    }

    void addWireToCache(WireId wire)
    {
        for (PipId pip : ctx->getPipsDownhill(wire)) {
            auto result = pips_downhill.emplace(pip, wire);
            log_assert(result.second);
        }

        for (PipId pip : ctx->getPipsUphill(wire)) {
            auto result = pips_uphill.emplace(pip, wire);
            log_assert(result.second);
        }
    }

    void populateCache(WireId wire)
    {
        // Put this wire at the end of last_access_list.
        auto iter = last_access_list.emplace(last_access_list.end(), wire);
        last_access_map.emplace(wire, iter);

        if (last_access_list.size() > cache_size) {
            // Cache is full, remove front of last_access_list.
            cache_evictions += 1;
            WireId wire_to_remove = last_access_list.front();
            last_access_list.pop_front();
            log_assert(last_access_map.erase(wire_to_remove) == 1);

            removeWireFromCache(wire_to_remove);
        }

        addWireToCache(wire);
    }

    // Determine if wire is in the cache.  If wire is not in the cache,
    // adds the wire to the cache, and potentially evicts the oldest wire if
    // cache is now full.
    void checkCache(WireId wire)
    {
        auto iter = last_access_map.find(wire);
        if (iter == last_access_map.end()) {
            cache_misses += 1;
            populateCache(wire);
        } else {
            // Record that this wire has been accessed.
            cache_hits += 1;
            last_access_list.splice(last_access_list.end(), last_access_list, iter->second);
        }
    }

    // Returns true if pip is uphill of wire (e.g. pip in getPipsUphill(wire)).
    bool isPipUphill(PipId pip, WireId wire)
    {
        checkCache(wire);
        return pips_uphill.at(pip) == wire;
    }

    // Returns true if pip is downhill of wire (e.g. pip in getPipsDownhill(wire)).
    bool isPipDownhill(PipId pip, WireId wire)
    {
        checkCache(wire);
        return pips_downhill.at(pip) == wire;
    }

    void cache_info() const
    {
        log_info("Cache hits: %zu\n", cache_hits);
        log_info("Cache misses: %zu\n", cache_misses);
        log_info("Cache evictions: %zu\n", cache_evictions);
    }
};

void archcheck_conn(const Context *ctx)
{
    log_info("Checking connectivity data.\n");

    log_info("Checking all wires...\n");

#ifndef USING_LRU_CACHE
    dict<PipId, WireId> pips_downhill;
    dict<PipId, WireId> pips_uphill;
#endif

    for (WireId wire : ctx->getWires()) {
        for (BelPin belpin : ctx->getWireBelPins(wire)) {
            WireId wire2 = ctx->getBelPinWire(belpin.bel, belpin.pin);
            log_assert(wire == wire2);
        }

        for (PipId pip : ctx->getPipsDownhill(wire)) {
            WireId wire2 = ctx->getPipSrcWire(pip);
            log_assert(wire == wire2);
#ifndef USING_LRU_CACHE
            auto result = pips_downhill.emplace(pip, wire);
            log_assert(result.second);
#endif
        }

        for (PipId pip : ctx->getPipsUphill(wire)) {
            WireId wire2 = ctx->getPipDstWire(pip);
            log_assert(wire == wire2);
#ifndef USING_LRU_CACHE
            auto result = pips_uphill.emplace(pip, wire);
            log_assert(result.second);
#endif
        }
    }

    log_info("Checking all BELs...\n");
    for (BelId bel : ctx->getBels()) {
        for (IdString pin : ctx->getBelPins(bel)) {
            WireId wire = ctx->getBelPinWire(bel, pin);

            if (wire == WireId()) {
                continue;
            }

            bool found_belpin = false;
            for (BelPin belpin : ctx->getWireBelPins(wire)) {
                if (belpin.bel == bel && belpin.pin == pin) {
                    found_belpin = true;
                    break;
                }
            }

            log_assert(found_belpin);
        }
    }
#ifdef USING_LRU_CACHE
    // This cache is used to meet two goals:
    //  - Avoid linear scan by invoking getPipsDownhill/getPipsUphill directly.
    //  - Avoid having pip -> wire maps for the entire part.
    //
    // The overhead of maintaining the cache is small relatively to the memory
    // gains by avoiding the full pip -> wire map, and still preserves a fast
    // pip -> wire, assuming that pips are returned from getPips with some
    // chip locality.
    LruWireCacheMap pip_cache(ctx, /*cache_size=*/64 * 1024);
#endif
    log_info("Checking all PIPs...\n");
    for (PipId pip : ctx->getPips()) {
        WireId src_wire = ctx->getPipSrcWire(pip);
        if (src_wire != WireId()) {
#ifdef USING_LRU_CACHE
            log_assert(pip_cache.isPipDownhill(pip, src_wire));
#else
            log_assert(pips_downhill.at(pip) == src_wire);
#endif
        }

        WireId dst_wire = ctx->getPipDstWire(pip);
        if (dst_wire != WireId()) {
#ifdef USING_LRU_CACHE
            log_assert(pip_cache.isPipUphill(pip, dst_wire));
#else
            log_assert(pips_uphill.at(pip) == dst_wire);
#endif
        }
    }
}

void archcheck_buckets(const Context *ctx)
{
    log_info("Checking bucket data.\n");

    // BEL buckets should be subsets of BELs that form an exact cover.
    // In particular that means cell types in a bucket should only be
    // placable in that bucket.
    for (BelBucketId bucket : ctx->getBelBuckets()) {

        // Find out which cell types are in this bucket.
        pool<IdString> cell_types_in_bucket;
        for (IdString cell_type : ctx->getCellTypes()) {
            if (ctx->getBelBucketForCellType(cell_type) == bucket) {
                cell_types_in_bucket.insert(cell_type);
            }
        }

        // Make sure that all cell types in this bucket have at least one
        // BelId they can be placed at.
        pool<IdString> cell_types_unused;

        pool<BelId> bels_in_bucket;
        for (BelId bel : ctx->getBelsInBucket(bucket)) {
            BelBucketId bucket2 = ctx->getBelBucketForBel(bel);
            log_assert(bucket == bucket2);

            bels_in_bucket.insert(bel);

            // Check to see if a cell type not in this bucket can be
            // placed at a BEL in this bucket.
            for (IdString cell_type : ctx->getCellTypes()) {
                if (ctx->getBelBucketForCellType(cell_type) == bucket) {
                    if (ctx->isValidBelForCellType(cell_type, bel)) {
                        cell_types_unused.erase(cell_type);
                    }
                } else {
                    log_assert(!ctx->isValidBelForCellType(cell_type, bel));
                }
            }
        }

        // Verify that any BEL not in this bucket reports a different
        // bucket.
        for (BelId bel : ctx->getBels()) {
            if (ctx->getBelBucketForBel(bel) != bucket) {
                log_assert(bels_in_bucket.count(bel) == 0);
            }
        }

        log_assert(cell_types_unused.empty());
    }
}

} // namespace

NEXTPNR_NAMESPACE_BEGIN

void Context::archcheck() const
{
    log_info("Running architecture database integrity check.\n");
    log_break();

    archcheck_names(this);
    archcheck_locs(this);
    archcheck_conn(this);
    archcheck_buckets(this);
}

NEXTPNR_NAMESPACE_END

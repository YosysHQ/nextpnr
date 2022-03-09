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

#ifndef SITE_LUT_MAPPING_CACHE_H
#define SITE_LUT_MAPPING_CACHE_H

#include "idstring.h"
#include "nextpnr_namespaces.h"
#include "site_arch.h"

NEXTPNR_NAMESPACE_BEGIN

// Key structure used in site LUT mapping cache
struct SiteLutMappingKey
{
    // LUT Cell data
    struct Cell
    {
        IdString type;    // Cell type
        int32_t belIndex; // Bound BEL index

        // Port to net assignments. These are local net ids generated during
        // key creation. This is to abstract connections from actual design
        // net names. the Id 0 means unconnected.
        std::vector<int32_t> conns;

        bool operator==(const Cell &other) const
        {
            return (type == other.type) && (belIndex == other.belIndex) && (conns == other.conns);
        }

        bool operator!=(const Cell &other) const
        {
            return (type != other.type) || (belIndex != other.belIndex) || (conns != other.conns);
        }
    };

    int32_t tileType;        // Tile type
    int32_t siteType;        // Site type in that tile type
    size_t numCells;         // LUT cell count
    std::vector<Cell> cells; // LUT cell data

    unsigned int hash_; // Precomputed hash

    // Creates a key from the given site state
    static SiteLutMappingKey create(const SiteInformation &siteInfo);

    // Returns size in bytes of the key
    size_t getSizeInBytes() const { return sizeof(SiteLutMappingKey); }

    // Precomputes hash of the key and stores it within
    void computeHash()
    {
        hash_ = mkhash(0, tileType);
        hash_ = mkhash(hash_, siteType);
        hash_ = mkhash(hash_, numCells);
        for (size_t j = 0; j < numCells; ++j) {
            const auto &cell = cells[j];
            hash_ = mkhash(hash_, cell.type.index);
            hash_ = mkhash(hash_, cell.belIndex);
            for (size_t i = 0; i < cell.conns.size(); ++i) {
                hash_ = mkhash(hash_, cell.conns[i]);
            }
        }
    }

    // Compares cell data of this and other key
    bool compareCells(const SiteLutMappingKey &other) const
    {
        if (numCells != other.numCells) {
            return false;
        }

        for (size_t i = 0; i < numCells; ++i) {
            if (cells[i] != other.cells[i]) {
                return false;
            }
        }
        return true;
    }

    bool operator==(const SiteLutMappingKey &other) const
    {
        return (hash_ == other.hash_) && (tileType == other.tileType) && (siteType == other.siteType) &&
               compareCells(other);
    }

    bool operator!=(const SiteLutMappingKey &other) const
    {
        return (hash_ != other.hash_) || (tileType != other.tileType) || (siteType != other.siteType) ||
               !compareCells(other);
    }

    unsigned int hash() const { return hash_; }
};

// Site LUT mapping result data
struct SiteLutMappingResult
{

    // LUT cell data
    struct Cell
    {
        int32_t belIndex;                 // BEL in tile index
        LutCell lutCell;                  // LUT mapping data
        dict<IdString, IdString> belPins; // Cell to BEL pin mapping
    };

    bool isValid;            // Validity flag
    std::vector<Cell> cells; // Cell data

    pool<std::pair<IdString, IdString>> blockedWires; // Set of blocked wires

    // Applies the mapping result to the site
    bool apply(const SiteInformation &siteInfo);

    // Returns size in bytes
    size_t getSizeInBytes() const;
};

// Site LUT mapping cache object
class SiteLutMappingCache
{
  public:
    // Adds an entry to the cache
    void add(const SiteLutMappingKey &key, const SiteLutMappingResult &result);
    // Retrieves an entry from the cache. Returns false if not found
    bool get(const SiteLutMappingKey &key, SiteLutMappingResult *result);

    // Clears the cache
    void clear();
    // Clears statistics counters of the cache
    void clearStats();

    // Return get() miss ratio
    float getMissRatio() const { return (float)numMisses / (float)(numHits + numMisses); }

    // Returns count of entries in the cache
    size_t getCount() const { return cache_.size(); }

    // Returns size of the cache rounded upwards to full MBs.
    size_t getSizeMB() const
    {
        size_t size = 0;
        for (const auto &it : cache_) {
            size += it.first.getSizeInBytes();
            size += it.second.getSizeInBytes();
        }

        const size_t MB = 1024L * 1024L;
        return (size + MB - 1) / MB; // Round up to megabytes
    }

  private:
    dict<SiteLutMappingKey, SiteLutMappingResult> cache_; // The cache

    size_t numHits = 0;   // Hit count
    size_t numMisses = 0; // Miss count
};

NEXTPNR_NAMESPACE_END

#endif /* SITE_LUT_MAPPING_CACHE_H */

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

NEXTPNR_NAMESPACE_BEGIN

// Key structure used in site LUT mapping cache
struct SiteLutMappingKey {

    // LUT Cell data
    struct Cell {
        IdString    type;       // Cell type
        int32_t     belIndex;   // Bound BEL index
  
        // Port to net assignments. These are local net ids generated during
        // key creation. This is to abstract connections from actual design
        // net names.
        dict<IdString, int32_t> conns;

        bool operator == (const Cell& other) const {
            return  (type == other.type) &&
                    (belIndex == other.belIndex) &&
                    (conns == other.conns);
        }

        bool operator < (const Cell& other) const {            
            return belIndex < other.belIndex;
        }
    };

    int32_t           tileType; // Tile type
    int32_t           siteType; // Site type in that tile type
    std::vector<Cell> cells;    // LUT cell data

    static SiteLutMappingKey create (const SiteInformation& siteInfo);
   
    bool operator == (const SiteLutMappingKey &other) const {
        return (tileType == other.tileType) &&
               (siteType == other.siteType) &&
               (cells == other.cells);
    }

    bool operator != (const SiteLutMappingKey &other) const {
        return (tileType != other.tileType) ||
               (siteType != other.siteType) ||
               (cells != other.cells);
    }

    unsigned int hash () const {
        unsigned int h = 0;
        h = mkhash(h, tileType);
        h = mkhash(h, siteType);
        for (const auto& cell : cells) {
            h = mkhash(h, cell.type.index);
            h = mkhash(h, cell.belIndex);
            for (const auto& conn : cell.conns) {
                h = mkhash(h, conn.first.index);
                h = mkhash(h, conn.second);
            }
        }
        return h;
    }
};

// Site LUT mapping result data
struct SiteLutMappingResult {

    // LUT cell data
    struct Cell {
        int32_t                     belIndex; // BEL in tile index
        LutCell                     lutCell;  // LUT mapping data
        dict<IdString, IdString>    belPins;  // Cell to BEL pin mapping
    };

    bool                isValid; // Validity flag
    std::vector<Cell>   cells;   // Cell data

    pool<std::pair<IdString, IdString>>  blockedWires;

    // Applies the mapping result to the site
    bool apply  (const SiteInformation& siteInfo);
};

// Site LUT mapping cache object
class SiteLutMappingCache {
public:

    void add    (const SiteLutMappingKey& key, const SiteLutMappingResult& result);
    bool get    (const SiteLutMappingKey& key, SiteLutMappingResult* result);

    void clear      ();
    void clearStats ();

    float getMissRatio () const {
        return (float)numMisses / (float)(numHits + numMisses);
    }

private:

    dict<SiteLutMappingKey, SiteLutMappingResult> cache_;

    size_t numHits   = 0;
    size_t numMisses = 0;
};


NEXTPNR_NAMESPACE_END

#endif /* SITE_LUT_MAPPING_CACHE_H */

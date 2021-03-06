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
#error Include "site_router.h" via "nextpnr.h" only.
#endif

#ifndef SITE_ROUTER_H
#define SITE_ROUTER_H

NEXTPNR_NAMESPACE_BEGIN

struct Context;
struct TileStatus;

struct SiteRouter
{
    SiteRouter(int16_t site) : site(site), dirty(false), site_ok(true) {}

    std::unordered_set<CellInfo *> cells_in_site;
    const int16_t site;

    mutable bool dirty;
    mutable bool site_ok;

    void bindBel(CellInfo *cell);
    void unbindBel(CellInfo *cell);
    bool checkSiteRouting(const Context *ctx, const TileStatus &tile_status) const;
};

NEXTPNR_NAMESPACE_END

#endif /* SITE_ROUTER_H */

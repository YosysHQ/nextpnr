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

#ifndef SITE_ROUTER_H
#define SITE_ROUTER_H

#include <cstdint>

#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"
#include "site_arch.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context;
struct TileStatus;

struct RouteNode
{
    void clear()
    {
        parent = std::list<RouteNode>::iterator();
        leafs.clear();
        pip = SitePip();
        wire = SiteWire();
        flags = 0;
        depth = 0;
    }

    enum Flags {
        // Has this path left the site?
        LEFT_SITE = 0,
        // Has this path entered the site?
        ENTERED_SITE = 1,
    };

    bool has_left_site() const {
        return (flags & (1 << LEFT_SITE)) != 0;
    }

    bool can_leave_site() const {
        return !has_left_site();
    }

    void mark_left_site() {
        flags |= (1 << LEFT_SITE);
    }

    bool has_entered_site() const {
        return (flags & (1 << ENTERED_SITE)) != 0;
    }

    bool can_enter_site() const {
        return !has_entered_site();
    }

    void mark_entered_site() {
        flags |= (1 << ENTERED_SITE);
    }

    using Node = std::list<RouteNode>::iterator;

    Node parent;
    std::vector<Node> leafs;

    SitePip pip;   // What pip was taken to reach this node.
    SiteWire wire; // What wire is this routing node located at?
    int32_t flags;
    int32_t depth;
};

struct RouteNodeStorage
{
    // Free list of nodes.
    std::list<RouteNode> nodes;

    // Either allocate a new node if no nodes are on the free list, or return
    // an element from the free list.
    std::list<RouteNode>::iterator alloc_node(std::list<RouteNode> &new_owner)
    {
        if (nodes.empty()) {
            nodes.emplace_front(RouteNode());
        }

        auto ret = nodes.begin();
        new_owner.splice(new_owner.end(), nodes, ret);

        ret->clear();

        return ret;
    }

    // Return 1 node from the current owner to the free list.
    void free_node(std::list<RouteNode> &owner, std::list<RouteNode>::iterator node)
    {
        nodes.splice(nodes.end(), owner, node);
    }

    // Return all node from the current owner to the free list.
    void free_nodes(std::list<RouteNode> &owner)
    {
        nodes.splice(nodes.end(), owner);
        NPNR_ASSERT(owner.empty());
    }
};

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
    void bindSiteRouting(Context *ctx);
};

NEXTPNR_NAMESPACE_END

#endif /* SITE_ROUTER_H */

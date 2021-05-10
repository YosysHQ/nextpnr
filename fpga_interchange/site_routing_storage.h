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

#ifndef SITE_ROUTING_STORAGE
#define SITE_ROUTING_STORAGE

#include <limits>

#include "nextpnr_namespaces.h"
#include "site_arch.h"

NEXTPNR_NAMESPACE_BEGIN

struct RouteNode
{
    void clear()
    {
        parent = std::numeric_limits<size_t>::max();
        pip = SitePip();
        wire = SiteWire();
        flags = 0;
        depth = 0;
    }

    enum Flags
    {
        // Has this path left the site?
        LEFT_SITE = 0,
        // Has this path entered the site?
        ENTERED_SITE = 1,
        // Has this path left the site after entering it?
        // This node should be discarded as being part of an illegal path
        // which allows entering and exiting a site, situation that needs
        // to be handled with a tile PIP.
        LEFT_SITE_AFTER_ENTERING = 2,
    };

    bool has_left_site() const { return (flags & (1 << LEFT_SITE)) != 0; }

    bool has_left_site_after_entering() const { return (flags & (1 << LEFT_SITE_AFTER_ENTERING)) != 0; }

    bool can_leave_site() const { return !has_left_site(); }

    bool is_valid_node() const { return !has_left_site_after_entering(); }

    void mark_left_site() { flags |= (1 << LEFT_SITE); }

    void mark_left_site_after_entering() { flags |= (has_entered_site() << LEFT_SITE_AFTER_ENTERING); }

    bool has_entered_site() const { return (flags & (1 << ENTERED_SITE)) != 0; }

    bool can_enter_site() const { return !has_entered_site(); }

    void mark_entered_site() { flags |= (1 << ENTERED_SITE); }

    size_t parent;

    SitePip pip;   // What pip was taken to reach this node.
    SiteWire wire; // What wire is this routing node located at?
    int32_t flags;
    int32_t depth;
};

struct RouteNodeStorage;

class Node
{
  public:
    Node(RouteNodeStorage *storage, size_t idx) : storage_(storage), idx_(idx) {}

    size_t get_index() const { return idx_; }

    RouteNode &operator*();
    const RouteNode &operator*() const;
    RouteNode *operator->();
    const RouteNode *operator->() const;
    bool has_parent() const;
    Node parent();

  private:
    RouteNodeStorage *storage_;
    size_t idx_;
};

struct RouteNodeStorage
{
    // Free list of nodes.
    std::vector<RouteNode> nodes;
    std::vector<size_t> free_list;

    // Either allocate a new node if no nodes are on the free list, or return
    // an element from the free list.
    Node alloc_node()
    {
        if (free_list.empty()) {
            nodes.emplace_back();
            nodes.back().clear();
            return Node(this, nodes.size() - 1);
        }

        size_t idx = free_list.back();
        free_list.pop_back();
        nodes[idx].clear();

        return Node(this, idx);
    }

    Node get_node(size_t idx)
    {
        NPNR_ASSERT(idx < nodes.size());
        return Node(this, idx);
    }

    const Node get_node(size_t idx) const
    {
        NPNR_ASSERT(idx < nodes.size());
        return Node(const_cast<RouteNodeStorage *>(this), idx);
    }

    // Return all node from the current owner to the free list.
    void free_nodes(std::vector<size_t> &other_free_list)
    {
        free_list.insert(free_list.end(), other_free_list.begin(), other_free_list.end());
    }
};

inline RouteNode &Node::operator*() { return storage_->nodes[idx_]; }
inline const RouteNode &Node::operator*() const { return storage_->nodes[idx_]; }

inline RouteNode *Node::operator->() { return &storage_->nodes[idx_]; }
inline const RouteNode *Node::operator->() const { return &storage_->nodes[idx_]; }

inline bool Node::has_parent() const { return storage_->nodes[idx_].parent < storage_->nodes.size(); }

inline Node Node::parent()
{
    size_t parent_idx = storage_->nodes[idx_].parent;
    NPNR_ASSERT(parent_idx < storage_->nodes.size());
    return Node(storage_, parent_idx);
}

NEXTPNR_NAMESPACE_END

#endif /* SITE_ROUTING_STORAGE */

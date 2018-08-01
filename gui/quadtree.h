/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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

#ifndef QUADTREE_H
#define QUADTREE_H

// This file implements a quad tree used for comitting 2D axis aligned
// bounding boxes and then retrieving them by 2D point.

NEXTPNR_NAMESPACE_BEGIN

// A node of a QuadTree. Internal.
template <typename CoordinateT, typename ElementT> class QuadTreeNode
{
  public:
    class BoundingBox
    {
        friend class QuadTreeNode;

      private:
        CoordinateT x0_, y0_, x1_, y1_;

        static constexpr float pinf = std::numeric_limits<CoordinateT>::infinity();
        static constexpr float ninf = -std::numeric_limits<CoordinateT>::infinity();

      public:
        // Standard constructor for a given (x0,y0), (x1,y1) bounding box
        //
        // @param x0 x coordinate of top-left corner of box
        // @param y0 y coordinate of top-left corner of box
        // @param x1 x coordinate of bottom-right corner of box
        // @param y1 y coordinate of bottom-right corner of box
        BoundingBox(CoordinateT x0, CoordinateT y0, CoordinateT x1, CoordinateT y1) : x0_(x0), y0_(y0), x1_(x1), y1_(y1)
        {
        }

        BoundingBox() : x0_(pinf), y0_(pinf), x1_(ninf), y1_(ninf) {}

        BoundingBox(const BoundingBox &other) : x0_(other.x0_), y0_(other.y0_), x1_(other.x1_), y1_(other.y1_) {}

        // Whether a bounding box contains a given points.
        // A point is defined to be in a bounding box when it's not lesser than
        // the lower coordinate or greater than the higher coordinate, eg:
        // A BoundingBox of x0: 20, y0: 30, x1: 100, y1: 130 fits the following
        // points:
        //   [ (50, 50), (20, 50), (20, 30), (100, 130) ]
        inline bool contains(CoordinateT x, CoordinateT y) const
        {
            if (x < x0_ || x > x1_)
                return false;
            if (y < y0_ || y > y1_)
                return false;
            return true;
        }

        // Sort the bounding box coordinates.
        void fixup()
        {
            if (x1_ < x0_)
                std::swap(x0_, x1_);
            if (y1_ < y0_)
                std::swap(y0_, y1_);
        }

        CoordinateT x0() const { return x0_; }
        CoordinateT y0() const { return y0_; }
        CoordinateT x1() const { return x1_; }
        CoordinateT y1() const { return y1_; }

        void setX0(CoordinateT v) { x0_ = v; }
        void setY0(CoordinateT v) { y0_ = v; }
        void setX1(CoordinateT v) { x1_ = v; }
        void setY1(CoordinateT v) { y1_ = v; }

        void clear()
        {
            x0_ = pinf;
            y0_ = pinf;
            x1_ = ninf;
            y1_ = ninf;
        }

        CoordinateT w() const { return x1_ - x0_; }
        CoordinateT h() const { return y1_ - y0_; }
    };

  private:
    // A pair of Element and BoundingBox that contains it.
    class BoundElement
    {
        friend class QuadTreeNode;

      private:
        BoundingBox bb_;
        ElementT elem_;

      public:
        BoundElement(BoundingBox bb, ElementT elem) : bb_(bb), elem_(elem) {}
    };

    // The bounding box that this node describes.
    BoundingBox bound_;
    // How many elements should be contained in this node until it splits into
    // sub-nodes.
    const size_t max_elems_;
    // Four sub-nodes or nullptr if it hasn't split yet.
    std::unique_ptr<QuadTreeNode<CoordinateT, ElementT>[]> children_ = nullptr;
    // Coordinates of the split.
    // Anything < split_x is west.
    CoordinateT splitx_;
    // Anything < split_y is north.
    CoordinateT splity_;

    // Elements contained directly within this node and not part of children
    // nodes.
    std::vector<BoundElement> elems_;
    // Depth at which this node is - root is at 0, first level at 1, etc.
    int depth_;

    // Checks whether a given bounding box fits within this node - used for
    // sanity checking on insertion.
    // @param b bounding box to check
    // @returns whether b fits in this node entirely
    bool fits(const BoundingBox &b) const
    {
        if (b.x0_ < bound_.x0_ || b.x0_ > bound_.x1_) {
            return false;
        } else if (b.x1_ < bound_.x0_ || b.x1_ > bound_.x1_) {
            return false;
        } else if (b.y0_ < bound_.y0_ || b.y0_ > bound_.y1_) {
            return false;
        } else if (b.y1_ < bound_.y0_ || b.y1_ > bound_.y1_) {
            return false;
        }
        return true;
    }

    // Used to describe one of 5 possible places an element can exist:
    //  - the node itself (THIS)
    //  - any of the 4 children nodes.
    enum Quadrant
    {
        THIS_NODE = -1,
        NW = 0,
        NE = 1,
        SW = 2,
        SE = 3
    };

    // Finds the quadrant to which a bounding box should go (if the node
    // is / were to be split).
    // @param b bounding box to check
    // @returns quadrant in which b belongs to if the node is were to be split
    Quadrant quadrant(const BoundingBox &b) const
    {
        if (children_ == nullptr) {
            return THIS_NODE;
        }

        bool west0 = b.x0_ < splitx_;
        bool west1 = b.x1_ < splitx_;
        bool north0 = b.y0_ < splity_;
        bool north1 = b.y1_ < splity_;

        if (west0 && west1 && north0 && north1)
            return NW;
        if (!west0 && !west1 && north0 && north1)
            return NE;
        if (west0 && west1 && !north0 && !north1)
            return SW;
        if (!west0 && !west1 && !north0 && !north1)
            return SE;
        return THIS_NODE;
    }

    // Checks whether this node should split.
    bool should_split() const
    {
        // The node shouldn't split if it's not large enough to merit it.
        if (elems_.size() < max_elems_)
            return false;

        // The node shouldn't split if its' level is too deep (this is true for
        // 100k+ entries, where the amount of splits causes us to lose
        // significant CPU time on traversing the tree, or worse yet causes a
        // stack overflow).
        if (depth_ > 5)
            return false;

        return true;
    }

  public:
    // Standard constructor for node.
    // @param b BoundingBox this node covers.
    // @param depth depth at which this node is in the tree
    // @max_elems how many elements should this node contain before it splits
    QuadTreeNode(BoundingBox b, int depth, size_t max_elems = 4) : bound_(b), max_elems_(max_elems), depth_(depth) {}
    // Disallow copies.
    QuadTreeNode(const QuadTreeNode &other) = delete;
    QuadTreeNode &operator=(const QuadTreeNode &other) = delete;
    // Allow moves.
    QuadTreeNode(QuadTreeNode &&other)
            : bound_(other.bound_), max_elems_(other.max_elems_), children_(std::move(other.children_)),
              splitx_(other.splitx_), splity_(other.splity_), elems_(std::move(other.elems_)), depth_(other.depth_)
    {
        other.children_ = nullptr;
    }
    QuadTreeNode &operator=(QuadTreeNode &&other)
    {
        if (this == &other)
            return *this;
        bound_ = other.bound_;
        max_elems_ = other.max_elems_;
        children_ = other.max_children_;
        children_ = other.children_;
        splitx_ = other.splitx_;
        splity_ = other.splity_;
        elems_ = std::move(other.elems_);
        depth_ = other.depth_;
        other.children_ = nullptr;
        return *this;
    }

    // Insert an element at a given bounding box.
    bool insert(const BoundingBox &k, ElementT v)
    {
        // Fail early if this BB doesn't fit us at all.
        if (!fits(k)) {
            return false;
        }

        // Do we have children?
        if (children_ != nullptr) {
            // Put the element either recursively into a child if it fits
            // entirely or keep it for ourselves if not.
            auto quad = quadrant(k);
            if (quad == THIS_NODE) {
                elems_.push_back(BoundElement(k, std::move(v)));
            } else {
                return children_[quad].insert(k, std::move(v));
            }
        } else {
            // No children and not about to have any.
            if (!should_split()) {
                elems_.push_back(BoundElement(k, std::move(v)));
                return true;
            }
            // Splitting. Calculate the split point.
            splitx_ = (bound_.x1_ - bound_.x0_) / 2 + bound_.x0_;
            splity_ = (bound_.y1_ - bound_.y0_) / 2 + bound_.y0_;
            // Create the new children.
            children_ = decltype(children_)(new QuadTreeNode<CoordinateT, ElementT>[4] {
                // Note: not using [NW] = QuadTreeNode because that seems to
                //       crash g++ 7.3.0.
                /* NW */ QuadTreeNode<CoordinateT, ElementT>(BoundingBox(bound_.x0_, bound_.y0_, splitx_, splity_),
                                                             depth_ + 1, max_elems_),
                        /* NE */
                        QuadTreeNode<CoordinateT, ElementT>(BoundingBox(splitx_, bound_.y0_, bound_.x1_, splity_),
                                                            depth_ + 1, max_elems_),
                        /* SW */
                        QuadTreeNode<CoordinateT, ElementT>(BoundingBox(bound_.x0_, splity_, splitx_, bound_.y1_),
                                                            depth_ + 1, max_elems_),
                        /* SE */
                        QuadTreeNode<CoordinateT, ElementT>(BoundingBox(splitx_, splity_, bound_.x1_, bound_.y1_),
                                                            depth_ + 1, max_elems_),
            });
            // Move all elements to where they belong.
            auto it = elems_.begin();
            while (it != elems_.end()) {
                auto quad = quadrant(it->bb_);
                if (quad != THIS_NODE) {
                    // Move to one of the children.
                    if (!children_[quad].insert(it->bb_, std::move(it->elem_)))
                        return false;
                    // Delete from ourselves.
                    it = elems_.erase(it);
                } else {
                    // Keep for ourselves.
                    it++;
                }
            }
            // Insert the actual element now that we've split.
            return insert(k, std::move(v));
        }
        return true;
    }

    // Dump a human-readable representation of the tree to stdout.
    void dump(int level) const
    {
        for (int i = 0; i < level; i++)
            printf("  ");
        printf("loc: % 3d % 3d % 3d % 3d\n", bound_.x0_, bound_.y0_, bound_.x1_, bound_.y1_);
        if (elems_.size() != 0) {
            for (int i = 0; i < level; i++)
                printf("  ");
            printf("elems: %zu\n", elems_.size());
        }

        if (children_ != nullptr) {
            for (int i = 0; i < level; i++)
                printf("  ");
            printf("children:\n");
            children_[NW].dump(level + 1);
            children_[NE].dump(level + 1);
            children_[SW].dump(level + 1);
            children_[SE].dump(level + 1);
        }
    }

    // Return count of BoundingBoxes/Elements contained.
    // @returns count of elements contained.
    size_t size() const
    {
        size_t res = elems_.size();
        if (children_ != nullptr) {
            res += children_[NW].size();
            res += children_[NE].size();
            res += children_[SW].size();
            res += children_[SE].size();
        }
        return res;
    }

    // Retrieve elements whose bounding boxes cover the given coordinates.
    //
    // @param x X coordinate of points to query.
    // @param y Y coordinate of points to query.
    // @returns vector of found bounding boxes
    void get(CoordinateT x, CoordinateT y, std::vector<ElementT> &res) const
    {
        if (!bound_.contains(x, y))
            return;

        for (const auto &elem : elems_) {
            const auto &bb = elem.bb_;
            if (bb.contains(x, y)) {
                res.push_back(elem.elem_);
            }
        }
        if (children_ != nullptr) {
            children_[NW].get(x, y, res);
            children_[NE].get(x, y, res);
            children_[SW].get(x, y, res);
            children_[SE].get(x, y, res);
        }
    }
};

// User facing method to manage a quad tree.
//
// @param CoodinateT scalar type of the coordinate system - int, float, ...
// @param ElementT type of the contained element. Must be movable or copiable.
template <typename CoordinateT, typename ElementT> class QuadTree
{
  private:
    // Root of the tree.
    QuadTreeNode<CoordinateT, ElementT> root_;

  public:
    // To let user create bounding boxes of the correct type.
    // Bounding boxes are composed of two 2D points, which designate their
    // top-left and bottom-right corners. All its' edges are axis aligned.
    using BoundingBox = typename QuadTreeNode<CoordinateT, ElementT>::BoundingBox;

    // Standard constructor.
    //
    // @param b Bounding box of the entire tree - all comitted elements must
    //          fit within in.
    QuadTree(BoundingBox b) : root_(b, 0) {}

    // Inserts a new value at a given bounding box.e
    // BoundingBoxes are not deduplicated - if two are pushed with the same
    // coordinates, the first one will take precendence.
    //
    // @param k Bounding box at which to store value.
    // @param v Value at a given bounding box.
    // @returns Whether the insert was succesful.
    bool insert(BoundingBox k, ElementT v)
    {
        k.fixup();
        return root_.insert(k, v);
    }

    // Dump a human-readable representation of the tree to stdout.
    void dump() const { root_.dump(0); }

    // Return count of BoundingBoxes/Elements contained.
    // @returns count of elements contained.
    size_t size() const { return root_.size(); }

    // Retrieve elements whose bounding boxes cover the given coordinates.
    //
    // @param x X coordinate of points to query.
    // @param y Y coordinate of points to query.
    // @returns vector of found bounding boxes
    std::vector<ElementT> get(CoordinateT x, CoordinateT y) const
    {
        std::vector<ElementT> res;
        root_.get(x, y, res);
        return res;
    }
};

NEXTPNR_NAMESPACE_END

#endif

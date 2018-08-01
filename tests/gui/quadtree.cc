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

#include "gtest/gtest.h"
#include "nextpnr.h"

#include "quadtree.h"

USING_NEXTPNR_NAMESPACE

using QT = QuadTree<int, int>;

class QuadTreeTest : public ::testing::Test
{
  protected:
    virtual void SetUp() { qt_ = new QT(QT::BoundingBox(0, 0, width_, height_)); }
    virtual void TearDown() { delete qt_; }

    int width_ = 100;
    int height_ = 100;
    QT *qt_;
};

// Test that we're doing bound checking correctly.
TEST_F(QuadTreeTest, insert_bound_checking)
{
    ASSERT_TRUE(qt_->insert(QT::BoundingBox(10, 10, 20, 20), 10));
    ASSERT_TRUE(qt_->insert(QT::BoundingBox(0, 0, 100, 100), 10));
    ASSERT_FALSE(qt_->insert(QT::BoundingBox(10, 10, 101, 20), 10));
    ASSERT_FALSE(qt_->insert(QT::BoundingBox(-1, 10, 101, 20), 10));
    ASSERT_FALSE(qt_->insert(QT::BoundingBox(-1, -1, 20, 20), 10));
}

// Test whether we are not losing any elements.
TEST_F(QuadTreeTest, insert_count)
{
    auto rng = NEXTPNR_NAMESPACE::DeterministicRNG();

    // Add 10000 random rectangles.
    for (unsigned int i = 0; i < 10000; i++) {
        int x0 = rng.rng(width_);
        int y0 = rng.rng(height_);
        int w = rng.rng(width_ - x0);
        int h = rng.rng(width_ - y0);
        int x1 = x0 + w;
        int y1 = y0 + h;
        ASSERT_TRUE(qt_->insert(QT::BoundingBox(x0, y0, x1, y1), i));
        ASSERT_EQ(qt_->size(), i + 1);
    }
    // Add 100000 random points.
    for (unsigned int i = 0; i < 100000; i++) {
        int x0 = rng.rng(width_);
        int y0 = rng.rng(height_);
        int x1 = x0;
        int y1 = y0;
        ASSERT_TRUE(qt_->insert(QT::BoundingBox(x0, y0, x1, y1), i));
        ASSERT_EQ(qt_->size(), i + 10001);
    }
}

// Test that we can insert and retrieve the same element.
TEST_F(QuadTreeTest, insert_retrieve_same)
{
    auto rng = NEXTPNR_NAMESPACE::DeterministicRNG();

    // Add 10000 small random rectangles.
    rng.rngseed(0);
    for (int i = 0; i < 10000; i++) {
        int x0 = rng.rng(width_);
        int y0 = rng.rng(height_);
        int w = rng.rng(width_ - x0);
        int h = rng.rng(width_ - y0);
        int x1 = x0 + w / 4;
        int y1 = y0 + h / 4;
        ASSERT_TRUE(qt_->insert(QT::BoundingBox(x0, y0, x1, y1), i));
    }

    // Restart RNG, make sure we get the same rectangles back.
    rng.rngseed(0);
    for (int i = 0; i < 10000; i++) {
        int x0 = rng.rng(width_);
        int y0 = rng.rng(height_);
        int w = rng.rng(width_ - x0);
        int h = rng.rng(width_ - y0);
        int x1 = x0 + w / 4;
        int y1 = y0 + h / 4;

        // try to find something in the middle of the square
        int x = (x1 - x0) / 2 + x0;
        int y = (y1 - y0) / 2 + y0;

        auto res = qt_->get(x, y);
        // Somewhat arbirary test to make sure we don't return obscene
        // amounts of data.
        ASSERT_LT(res.size(), 200UL);
        bool found = false;
        for (auto elem : res) {
            // Is this what we're looking for?
            if (elem == i) {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found);
    }
}

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-22  gatecat <gatecat@ds0.me>
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

#ifndef ARRAY2D_H
#define ARRAY2D_H

#include <algorithm>
#include <limits>
#include <type_traits>
#include <vector>

#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

template <typename T> class array2d
{
  public:
    array2d() : m_width(0), m_height(0), m_size(0), data(nullptr){};
    array2d(int width, int height) : m_width(width), m_height(height), m_size(width * height)
    {
        data = new T[m_width * m_height]();
    }
    array2d(int width, int height, const T &init) : m_width(width), m_height(height), m_size(width * height)
    {
        data = new T[m_width * m_height];
        std::fill(data, data + (m_width * m_height), init);
    }
    array2d(const array2d &other) : m_width(other.m_width), m_height(other.m_height), m_size(other.m_size)
    {
        data = new T[m_size]();
        if (m_size > 0)
            std::copy(other.data, other.data + (m_width * m_height), data);
    }
    array2d(array2d &&other) noexcept : m_width(other.m_width), m_height(other.m_height), m_size(other.m_size)
    {
        data = other.data;
        other.data = nullptr;
        other.m_size = 0;
    }
    void reset(int new_width, int new_height, const T &init = {})
    {
        if ((new_width * new_height) > m_size) {
            delete[] data;
            m_size = new_width * new_height;
            data = new T[m_size];
        }
        m_width = new_width;
        m_height = new_height;
        std::fill(data, data + (m_width * m_height), init);
    }

    int width() const { return m_width; }
    int height() const { return m_height; }
    T &at(int x, int y)
    {
        NPNR_ASSERT(x >= 0 && x < m_width);
        NPNR_ASSERT(y >= 0 && y < m_height);
        return data[y * m_width + x];
    }
    T &at(const Loc &l) { return at(l.x, l.y); }
    const T &at(int x, int y) const
    {
        NPNR_ASSERT(x >= 0 && x < m_width);
        NPNR_ASSERT(y >= 0 && y < m_height);
        return data[y * m_width + x];
    }
    const T &at(const Loc &l) const { return at(l.x, l.y); }
    ~array2d() { delete[] data; }
    struct entry
    {
        entry(int x, int y, T &value) : x(x), y(y), value(value){};
        int x, y;
        T &value;
    };
    struct iterator
    {
      public:
        entry operator*() { return {x, y, base->at(x, y)}; }
        inline iterator operator++()
        {
            ++x;
            if (x >= base->width()) {
                x = 0;
                ++y;
            }
            return *this;
        }
        inline iterator operator++(int)
        {
            iterator prior(x, y, base);
            ++x;
            if (x >= base->width()) {
                x = 0;
                ++y;
            }
            return prior;
        }
        inline bool operator!=(const iterator &other) const { return other.x != x || other.y != y; }
        inline bool operator==(const iterator &other) const { return other.x == x && other.y == y; }

      private:
        iterator(int x, int y, array2d<T> &base) : x(x), y(y), base(&base){};
        int x, y;
        array2d<T> *base;
        friend class array2d;
    };
    iterator begin() { return {0, 0, *this}; }
    iterator end() { return {0, m_height, *this}; }

  private:
    int m_width, m_height, m_size;
    T *data;
};
NEXTPNR_NAMESPACE_END

#endif

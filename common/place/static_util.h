/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  gatecat <gatecat@ds0.me>
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

#ifndef STATIC_UTIL_H
#define STATIC_UTIL_H

#include <fstream>
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

namespace StaticUtil {

enum class Axis
{
    X,
    Y
};

struct RealPair
{
    RealPair() : x(0), y(0){};
    RealPair(float x, float y) : x(x), y(y){};
    explicit RealPair(Loc l, float bias = 0.0f) : x(l.x + bias), y(l.y + bias){};
    float x, y;
    RealPair &operator+=(const RealPair &other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }
    RealPair &operator/=(float factor)
    {
        x /= factor;
        y /= factor;
        return *this;
    }
    friend RealPair operator+(RealPair a, RealPair b);
    friend RealPair operator-(RealPair a, RealPair b);
    RealPair operator*(float factor) const { return RealPair(x * factor, y * factor); }
    RealPair operator/(float factor) const { return RealPair(x / factor, y / factor); }
    // to simplify axis-generic code
    inline float &at(Axis axis) { return (axis == Axis::Y) ? y : x; }
    inline const float &at(Axis axis) const { return (axis == Axis::Y) ? y : x; }
};
inline RealPair operator+(RealPair a, RealPair b) { return RealPair(a.x + b.x, a.y + b.y); }
inline RealPair operator-(RealPair a, RealPair b) { return RealPair(a.x - b.x, a.y - b.y); }

// array2d; but as ourafft wants it
struct FFTArray
{
    FFTArray(int width = 0, int height = 0) : m_width(width), m_height(height)
    {
        alloc();
        fill(0);
    }

    void fill(float value)
    {
        for (int x = 0; x < m_width; x++)
            for (int y = 0; y < m_height; y++)
                m_data[x][y] = value;
    }

    void reset(int width, int height, float value = 0)
    {
        if (width != m_height || height != m_height) {
            destroy();
            m_width = width;
            m_height = height;
            alloc();
        }
        fill(value);
    }

    float &at(int x, int y)
    {
        NPNR_ASSERT(x >= 0 && x < m_width && y >= 0 && y < m_height);
        return m_data[x][y];
    }
    float at(int x, int y) const
    {
        NPNR_ASSERT(x >= 0 && x < m_width && y >= 0 && y < m_height);
        return m_data[x][y];
    }
    float **data() { return m_data; }

    void write_csv(const std::string &filename) const
    {
        std::ofstream out(filename);
        NPNR_ASSERT(out);
        for (int y = 0; y < m_height; y++) {
            for (int x = 0; x < m_width; x++) {
                out << at(x, y) << ",";
            }
            out << std::endl;
        }
    }

    ~FFTArray() { destroy(); }

  private:
    int m_width, m_height;
    float **m_data = nullptr;
    void alloc()
    {
        if (m_width == 0)
            return;
        m_data = new float *[m_width];
        for (int x = 0; x < m_width; x++)
            m_data[x] = (m_height > 0) ? (new float[m_height]) : nullptr;
    }
    void destroy()
    {
        for (int x = 0; x < m_width; x++)
            delete[] m_data[x];
        delete[] m_data;
    }
};

}; // namespace StaticUtil

NEXTPNR_NAMESPACE_END

#endif

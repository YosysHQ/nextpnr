/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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

#ifndef PROPERTY_H
#define PROPERTY_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

struct Property
{
    enum State : char
    {
        S0 = '0',
        S1 = '1',
        Sx = 'x',
        Sz = 'z'
    };

    Property();
    Property(int64_t intval, int width = 32);
    Property(const std::string &strval);
    Property(State bit);
    Property &operator=(const Property &other) = default;
    Property(const Property &other) = default;

    bool is_string;

    // The string literal (for string values), or a string of [01xz] (for numeric values)
    std::string str;
    // The lower 64 bits (for numeric values), unused for string values
    int64_t intval;

    void update_intval()
    {
        intval = 0;
        for (int i = 0; i < int(str.size()); i++) {
            NPNR_ASSERT(str[i] == S0 || str[i] == S1 || str[i] == Sx || str[i] == Sz);
            if ((str[i] == S1) && i < 64)
                intval |= (1ULL << i);
        }
    }

    int64_t as_int64() const
    {
        NPNR_ASSERT(!is_string);
        return intval;
    }
    std::vector<bool> as_bits() const
    {
        std::vector<bool> result;
        result.reserve(str.size());
        NPNR_ASSERT(!is_string);
        for (auto c : str)
            result.push_back(c == S1);
        return result;
    }
    const std::string &as_string() const
    {
        NPNR_ASSERT(is_string);
        return str;
    }
    const char *c_str() const
    {
        NPNR_ASSERT(is_string);
        return str.c_str();
    }
    size_t size() const { return is_string ? 8 * str.size() : str.size(); }
    double as_double() const
    {
        NPNR_ASSERT(is_string);
        return std::stod(str);
    }
    bool as_bool() const
    {
        if (int(str.size()) <= 64)
            return intval != 0;
        else
            return std::any_of(str.begin(), str.end(), [](char c) { return c == S1; });
    }
    bool is_fully_def() const
    {
        return !is_string && std::all_of(str.begin(), str.end(), [](char c) { return c == S0 || c == S1; });
    }
    Property extract(int offset, int len, State padding = State::S0) const
    {
        Property ret;
        ret.is_string = false;
        ret.str.reserve(len);
        for (int i = offset; i < offset + len; i++)
            ret.str.push_back(i < int(str.size()) ? str[i] : char(padding));
        ret.update_intval();
        return ret;
    }
    // Convert to a string representation, escaping literal strings matching /^[01xz]* *$/ by adding a space at the end,
    // to disambiguate from binary strings
    std::string to_string() const;
    // Convert a string of four-value binary [01xz], or a literal string escaped according to the above rule
    // to a Property
    static Property from_string(const std::string &s);
};

inline bool operator==(const Property &a, const Property &b) { return a.is_string == b.is_string && a.str == b.str; }
inline bool operator!=(const Property &a, const Property &b) { return a.is_string != b.is_string || a.str != b.str; }

NEXTPNR_NAMESPACE_END

#endif /* PROPERTY_H */

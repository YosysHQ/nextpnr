/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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

#include "property.h"

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

Property::Property() : is_string(false), str(""), intval(0) {}

Property::Property(int64_t intval, int width) : is_string(false), intval(intval)
{
    str.reserve(width);
    for (int i = 0; i < width; i++)
        str.push_back((intval & (1ULL << i)) ? S1 : S0);
}

Property::Property(const std::string &strval) : is_string(true), str(strval), intval(0xDEADBEEF) {}

Property::Property(State bit) : is_string(false), str(std::string("") + char(bit)), intval(bit == S1) {}

std::string Property::to_string() const
{
    if (is_string) {
        std::string result = str;
        int state = 0;
        for (char c : str) {
            if (state == 0) {
                if (c == '0' || c == '1' || c == 'x' || c == 'z')
                    state = 0;
                else if (c == ' ')
                    state = 1;
                else
                    state = 2;
            } else if (state == 1 && c != ' ')
                state = 2;
        }
        if (state < 2)
            result += " ";
        return result;
    } else {
        return std::string(str.rbegin(), str.rend());
    }
}

Property Property::from_string(const std::string &s)
{
    Property p;

    size_t cursor = s.find_first_not_of("01xz");
    if (cursor == std::string::npos) {
        p.str = std::string(s.rbegin(), s.rend());
        p.is_string = false;
        p.update_intval();
    } else if (s.find_first_not_of(' ', cursor) == std::string::npos) {
        p = Property(s.substr(0, s.size() - 1));
    } else {
        p = Property(s);
    }
    return p;
}

NEXTPNR_NAMESPACE_END

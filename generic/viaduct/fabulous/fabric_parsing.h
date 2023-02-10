/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2022  gatecat <gatecat@ds0.me>
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

#ifndef FABULOUS_PARSING_H
#define FABULOUS_PARSING_H

#include <iostream>
#include <limits>
#include <string>
#include "idstring.h"
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

struct BaseCtx;

// Lightweight NIH string_view
struct parser_view
{
    char *m_ptr;
    size_t m_length;
    parser_view() : m_ptr(nullptr), m_length(0){};
    explicit parser_view(std::string &str) : m_ptr(&(str[0])), m_length(str.size()){};
    parser_view(char *ptr, size_t length) : m_ptr(ptr), m_length(length){};

    static constexpr size_t npos = std::numeric_limits<size_t>::max();
    char operator[](size_t idx)
    {
        NPNR_ASSERT(idx < m_length);
        return m_ptr[idx];
    }

    size_t size() const { return m_length; }

    bool empty() const { return m_length == 0; }

    parser_view substr(size_t start, size_t length = npos)
    {
        NPNR_ASSERT(start <= m_length);
        if (length == npos)
            length = m_length - start;
        NPNR_ASSERT(length <= m_length);
        return parser_view(m_ptr + start, length);
    }

    size_t find(char tok) const
    {
        for (size_t i = 0; i < m_length; i++)
            if (m_ptr[i] == tok)
                return i;
        return npos;
    }

    size_t rfind(char tok) const
    {
        for (size_t i = m_length; i > 0; i--)
            if (m_ptr[i - 1] == tok)
                return i - 1;
        return npos;
    }

    IdString to_id(const BaseCtx *ctx)
    {
        // This isn't really ideal, let's hope one day we can move to C++20 and have proper string_views instead :3
        char tmp = m_ptr[m_length];
        m_ptr[m_length] = '\0';
        IdString id = IdString(ctx, m_ptr);
        m_ptr[m_length] = tmp;
        return id;
    }

    long to_int()
    {
        // This isn't really ideal, let's hope one day we can move to C++20 and have proper string_views instead :3
        char tmp = m_ptr[m_length];
        m_ptr[m_length] = '\0';
        long l = strtol(m_ptr, nullptr, 0);
        m_ptr[m_length] = tmp;
        return l;
    }

    parser_view strip(const std::string &ws = " \r\n\t")
    {
        char *ptr = m_ptr;
        size_t length = m_length;
        while (length > 0) {                        // strip front
            if (ws.find(*ptr) == std::string::npos) // not whitespace
                break;
            ptr++;
            length--;
        }
        while (length > 0) {                                   // strip back
            if (ws.find(ptr[length - 1]) == std::string::npos) // not whitespace
                break;
            length--;
        }
        return parser_view(ptr, length);
    }

    char back()
    {
        NPNR_ASSERT(m_length > 0);
        return m_ptr[m_length - 1];
    }

    parser_view back(size_t count)
    {
        NPNR_ASSERT(count <= m_length);
        return parser_view(m_ptr + (m_length - count), count);
    }

    bool starts_with(const std::string &st)
    {
        if (m_length < st.size())
            return false;
        for (size_t i = 0; i < st.length(); i++)
            if (m_ptr[i] != st[i])
                return false;
        return true;
    }
    std::pair<parser_view, parser_view> split(char delim) const
    {
        size_t pos = find(delim);
        NPNR_ASSERT(pos != npos);
        return std::make_pair(parser_view(m_ptr, pos), parser_view(m_ptr + pos + 1, m_length - (pos + 1)));
    }
    std::pair<parser_view, parser_view> rsplit(char delim) const
    {
        size_t pos = rfind(delim);
        NPNR_ASSERT(pos != npos);
        return std::make_pair(parser_view(m_ptr, pos), parser_view(m_ptr + pos + 1, m_length - (pos + 1)));
    }
};

struct CsvParser
{
    explicit CsvParser(std::istream &in) : in(in){};
    std::istream &in;
    std::string buf;
    parser_view view;
    bool fetch_next_line()
    {
        while (!in.eof()) {
            std::getline(in, buf);
            view = parser_view(buf).strip();
            size_t end_pos = view.find('#');
            if (end_pos != parser_view::npos)
                view = view.substr(0, end_pos);
            view = view.strip();
            if (!view.empty())
                return true;
        }
        return false;
    }
    parser_view next_field()
    {
        size_t next_delim = view.find(',');
        if (next_delim == parser_view::npos) {
            parser_view result = view.substr(0, next_delim);
            view = parser_view();
            return result;
        } else {
            parser_view result = view.substr(0, next_delim);
            view = view.substr(next_delim + 1);
            return result;
        }
    }
};

NEXTPNR_NAMESPACE_END

#endif

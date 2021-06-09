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

#include "idstringlist.h"
#include "context.h"

NEXTPNR_NAMESPACE_BEGIN

IdStringList IdStringList::parse(Context *ctx, const std::string &str)
{
    char delim = ctx->getNameDelimiter();
    size_t id_count = std::count(str.begin(), str.end(), delim) + 1;
    IdStringList list(id_count);
    size_t start = 0;
    for (size_t i = 0; i < id_count; i++) {
        size_t end = str.find(delim, start);
        NPNR_ASSERT((i == (id_count - 1)) || (end != std::string::npos));
        list.ids[i] = ctx->id(str.substr(start, end - start));
        start = end + 1;
    }
    return list;
}

void IdStringList::build_str(const Context *ctx, std::string &str) const
{
    char delim = ctx->getNameDelimiter();
    bool first = true;
    str.clear();
    for (auto entry : ids) {
        if (!first)
            str += delim;
        str += entry.str(ctx);
        first = false;
    }
}

std::string IdStringList::str(const Context *ctx) const
{
    std::string s;
    build_str(ctx, s);
    return s;
}

IdStringList IdStringList::concat(IdStringList a, IdStringList b)
{
    IdStringList result(a.size() + b.size());
    for (size_t i = 0; i < a.size(); i++)
        result.ids[i] = a[i];
    for (size_t i = 0; i < b.size(); i++)
        result.ids[a.size() + i] = b[i];
    return result;
}

IdStringList IdStringList::slice(size_t s, size_t e) const
{
    NPNR_ASSERT(e >= s);
    IdStringList result(e - s);
    for (size_t i = 0; i < result.size(); i++)
        result.ids[i] = ids[s + i];
    return result;
}

NEXTPNR_NAMESPACE_END

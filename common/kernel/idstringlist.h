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

#ifndef IDSTRING_LIST_H
#define IDSTRING_LIST_H

#include <boost/functional/hash.hpp>
#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"
#include "sso_array.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context;

struct IdStringList
{
    SSOArray<IdString, 4> ids;

    IdStringList() : ids(1, IdString()) {}
    explicit IdStringList(size_t n) : ids(n, IdString()) {}
    explicit IdStringList(IdString id) : ids(1, id) {}
    template <typename Tlist> explicit IdStringList(const Tlist &list) : ids(list) {}

    static IdStringList parse(Context *ctx, const std::string &str);
    void build_str(const Context *ctx, std::string &str) const;
    std::string str(const Context *ctx) const;

    size_t size() const { return ids.size(); }
    const IdString *begin() const { return ids.begin(); }
    const IdString *end() const { return ids.end(); }
    const IdString &operator[](size_t idx) const { return ids[idx]; }
    bool operator==(const IdStringList &other) const { return ids == other.ids; }
    bool operator!=(const IdStringList &other) const { return ids != other.ids; }
    bool operator<(const IdStringList &other) const
    {
        if (size() > other.size())
            return false;
        if (size() < other.size())
            return true;
        for (size_t i = 0; i < size(); i++) {
            IdString a = ids[i], b = other[i];
            if (a.index < b.index)
                return true;
            if (a.index > b.index)
                return false;
        }
        return false;
    }

    static IdStringList concat(IdStringList a, IdStringList b);
    static IdStringList concat(IdString a, IdString b) { return concat(IdStringList(a), IdStringList(b)); }
    static IdStringList concat(IdStringList a, IdString b) { return concat(a, IdStringList(b)); }
    static IdStringList concat(IdString a, IdStringList b) { return concat(IdStringList(a), b); }

    IdStringList slice(size_t s, size_t e) const;

    unsigned int hash() const
    {
        unsigned int h = mkhash_init;
        for (const auto &val : ids)
            h = mkhash(h, val.hash());
        return h;
    }
};

NEXTPNR_NAMESPACE_END

#endif /* IDSTRING_LIST_H */

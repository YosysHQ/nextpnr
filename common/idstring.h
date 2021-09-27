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

#ifndef IDSTRING_H
#define IDSTRING_H

#include <string>
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

struct BaseCtx;

struct IdString
{
    int index;

    static void initialize_arch(const BaseCtx *ctx);

    static void initialize_add(const BaseCtx *ctx, const char *s, int idx);

    constexpr IdString() : index(0) {}
    explicit constexpr IdString(int index) : index(index) {}

    void set(const BaseCtx *ctx, const std::string &s);

    IdString(const BaseCtx *ctx, const std::string &s) { set(ctx, s); }

    IdString(const BaseCtx *ctx, const char *s) { set(ctx, s); }

    const std::string &str(const BaseCtx *ctx) const;

    const char *c_str(const BaseCtx *ctx) const;

    bool operator<(const IdString &other) const { return index < other.index; }

    bool operator==(const IdString &other) const { return index == other.index; }

    bool operator!=(const IdString &other) const { return index != other.index; }

    bool empty() const { return index == 0; }

    unsigned int hash() const { return index; }

    template <typename... Args> bool in(Args... args) const
    {
        // Credit: https://articles.emptycrate.com/2016/05/14/folds_in_cpp11_ish.html
        bool result = false;
        (void)std::initializer_list<int>{(result = result || in(args), 0)...};
        return result;
    }

    bool in(const IdString &rhs) const { return *this == rhs; }
};

NEXTPNR_NAMESPACE_END

#endif /* IDSTRING_H */

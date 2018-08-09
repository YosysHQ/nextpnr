/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
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
#ifndef SETTINGS_H
#define SETTINGS_H

#include <boost/lexical_cast.hpp>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

class Settings
{
  public:
    explicit Settings(Context *ctx) : ctx(ctx) {}

    template <typename T> T get(const char *name, T defaultValue)
    {
        IdString id = ctx->id(name);
        if (ctx->settings.find(id) != ctx->settings.end())
            return boost::lexical_cast<T>(ctx->settings[id]);
        ctx->settings.emplace(id, std::to_string(defaultValue));
        return defaultValue;
    }

    template <typename T> void set(const char *name, T value)
    {
        IdString id = ctx->id(name);
        if (ctx->settings.find(id) != ctx->settings.end()) {
            ctx->settings[id] = value;
            return;
        }
        ctx->settings.emplace(id, std::to_string(value));
    }

  private:
    Context *ctx;
};

NEXTPNR_NAMESPACE_END

#endif // SETTINGS_H

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#ifndef HIMBAECHEL_GATEMATE_H
#define HIMBAECHEL_GATEMATE_H

#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "himbaechel_helpers.h"

NEXTPNR_NAMESPACE_BEGIN

struct GateMateImpl : HimbaechelAPI
{
    ~GateMateImpl();
    void init_database(Arch *arch) override;

    void init(Context *ctx) override;

    void drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc) override;

  private:
    HimbaechelHelpers h;
};

NEXTPNR_NAMESPACE_END
#endif

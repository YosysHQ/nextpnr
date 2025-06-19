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

#include "gatemate.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#define HIMBAECHEL_GFXIDS "uarch/gatemate/gfxids.inc"
#define HIMBAECHEL_UARCH gatemate

#include "himbaechel_constids.h"
#include "himbaechel_gfxids.h"

NEXTPNR_NAMESPACE_BEGIN

void GateMateImpl::drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc)
{
    GraphicElement el;
    el.type = GraphicElement::TYPE_BOX;
    el.style = style;
    switch (bel_type.index) {
    case id_CPE_LT_L.index:
        el.x1 = loc.x + 0.20;
        el.x2 = el.x1 + 0.20;
        el.y1 = loc.y + 0.25;
        el.y2 = el.y1 + 0.20;
        g.push_back(el);
        break;
    case id_CPE_LT_U.index:
        el.x1 = loc.x + 0.20;
        el.x2 = el.x1 + 0.20;
        el.y1 = loc.y + 0.55;
        el.y2 = el.y1 + 0.20;
        g.push_back(el);
        break;
    case id_RAM.index:
        el.x1 = loc.x + 0.70;
        el.x2 = el.x1 + 0.20;
        el.y1 = loc.y + 0.20;
        el.y2 = el.y1 + 15.60;
        g.push_back(el);
        break;
    case id_GPIO.index:
        el.x1 = loc.x + 0.20;
        el.x2 = el.x1 + 0.60;
        el.y1 = loc.y + 0.20;
        el.y2 = el.y1 + 0.60;
        g.push_back(el);
        break;
    case id_CLKIN.index:
    case id_GLBOUT.index:
        el.x1 = loc.x + 0.15 + loc.z * 0.20;
        el.x2 = el.x1 + 0.15;
        el.y1 = loc.y + 0.10;
        el.y2 = el.y1 + 0.30;
        g.push_back(el);
        break;
    case id_PLL.index:
        el.x1 = loc.x + 0.15 + (loc.z - 2) * 0.20;
        el.x2 = el.x1 + 0.15;
        el.y1 = loc.y + 0.60;
        el.y2 = el.y1 + 0.30;
        g.push_back(el);
        break;
    case id_USR_RSTN.index:
        el.x1 = loc.x + 0.20;
        el.x2 = el.x1 + 0.20;
        el.y1 = loc.y + 0.20;
        el.y2 = el.y1 + 0.20;
        g.push_back(el);
        break;
    }
}

NEXTPNR_NAMESPACE_END

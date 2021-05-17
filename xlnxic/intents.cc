/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
 *
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

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN
bool Arch::is_general_routing(WireId wire) const
{
    switch (getWireType(wire).index) {
    case ID_NODE_HLONG:
    case ID_NODE_SINGLE:
    case ID_NODE_DOUBLE:
    case ID_NODE_HQUAD:
    case ID_NODE_VLONG:
    case ID_NODE_VQUAD:
    case ID_NODE_CLE_OUTPUT:
    case ID_DOUBLE:
    case ID_BENTQUAD:
    case ID_VLONG:
    case ID_HLONG:
    case ID_HQUAD:
    case ID_VLONG12:
    case ID_SVLONG:
    case ID_VQUAD:
    case ID_NODE_HLONG10:
    case ID_NODE_HLONG6:
    case ID_NODE_VLONG12:
    case ID_NODE_VLONG7:
    case ID_NODE_HSINGLE:
    case ID_NODE_HDOUBLE:
    case ID_NODE_VSINGLE:
    case ID_NODE_VDOUBLE:
        return true;
    default:
        return false;
    }
}
NEXTPNR_NAMESPACE_END

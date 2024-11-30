/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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

#ifndef HIMBAECHEL_GFXIDS_H
#define HIMBAECHEL_GFXIDS_H

/*
This enables use of 'gfxids' similar to a 'constids' in a HIMBAECHEL uarch.
To use:
    - create a 'gfxids.inc' file in your uarch folder containing one ID per line; inside X( )
    - set the HIMBAECHEL_UARCH macro to uarch namespace
    - set the HIMBAECHEL_GFXIDS macro to the path to this file relative to the generic arch base
    - include this file
*/

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

namespace HIMBAECHEL_UARCH {
#ifndef Q_MOC_RUN
enum GfxTileWireId
{
    GFX_WIRE_NONE
#define X(t) , GFX_WIRE_##t
#include HIMBAECHEL_GFXIDS
#undef X
    ,
};
#endif
}; // namespace HIMBAECHEL_UARCH

NEXTPNR_NAMESPACE_END

using namespace NEXTPNR_NAMESPACE_PREFIX HIMBAECHEL_UARCH;

#endif

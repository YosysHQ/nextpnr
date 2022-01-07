/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#ifndef VIADUCT_CONSTIDS_H
#define VIADUCT_CONSTIDS_H

/*
This enables use of 'constids' similar to a 'true' nextpnr arch in a viaduct uarch.
To use:
    - create a 'constids.inc' file in your uarch folder containing one ID per line; inside X( )
    - set the VIADUCT_CONSTIDS macro to the path to this file relative to the generic arch base
    - in your main file; also define GEN_INIT_CONSTIDS to create init_uarch_constids(Context*) which you should call in
init
    - include this file
*/

#include "nextpnr_namespaces.h"

#ifdef VIADUCT_MAIN
#include "idstring.h"
#endif

NEXTPNR_NAMESPACE_BEGIN

namespace {
#ifndef Q_MOC_RUN
enum ConstIds
{
    ID_NONE
#define X(t) , ID_##t
#include VIADUCT_CONSTIDS
#undef X
    ,
};

#define X(t) static constexpr auto id_##t = IdString(ID_##t);
#include VIADUCT_CONSTIDS
#undef X
#endif

#ifdef GEN_INIT_CONSTIDS

void init_uarch_constids(Context *ctx)
{
#define X(t) IdString::initialize_add(ctx, #t, ID_##t);

#include VIADUCT_CONSTIDS

#undef X
}

#endif

} // namespace

NEXTPNR_NAMESPACE_END

#endif

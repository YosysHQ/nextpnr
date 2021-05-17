/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2022  gatecat <gatecat@ds0.me>
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

#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

void Arch::pack_bram()
{
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        // Fixup BRAM with missing RAM_MODE so parameter matches work
        if (!ci->type.in(id_RAMB18E1, id_RAMB18E2, id_RAMB36E1, id_RAMB36E2))
            continue;
        if (!ci->params.count(id_RAM_MODE)) {
            int sdp_size = ci->type.in(id_RAMB18E1, id_RAMB18E2) ? 36 : 72;
            if (int_or_default(ci->params, id_READ_WIDTH_A, 0) == sdp_size ||
                int_or_default(ci->params, id_WRITE_WIDTH_B, 0) == sdp_size)
                ci->params[id_RAM_MODE] = std::string("SDP");
            else
                ci->params[id_RAM_MODE] = std::string("TDP");
        }
        // Add default params, also required for correct matching
        for (auto param :
             {id_READ_WIDTH_A, id_READ_WIDTH_B, id_WRITE_WIDTH_A, id_WRITE_WIDTH_B, id_DOA_REG, id_DOB_REG}) {
            if (ci->params.count(param))
                continue;
            ci->params.emplace(param, 0);
        }
    }
}

NEXTPNR_NAMESPACE_END

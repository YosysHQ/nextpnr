/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
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

#ifndef CELL_PARAMETERS_H
#define CELL_PARAMETERS_H

#include <regex>

#include "chipdb.h"
#include "dynamic_bitarray.h"
#include "nextpnr_namespaces.h"
#include "property.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context;

struct CellParameters
{
    CellParameters();
    void init(const Context *ctx);
    DynamicBitarray<> parse_int_like(const Context *ctx, IdString cell_type, IdString parameter,
                                     const Property &property) const;
    bool compare_property(const Context *ctx, IdString cell_type, IdString parameter, const Property &property,
                          IdString value_to_compare) const;

    dict<std::pair<IdString, IdString>, const CellParameterPOD *> parameters;

    std::regex verilog_binary_re;
    std::regex verilog_hex_re;
    std::regex c_binary_re;
    std::regex c_hex_re;
};

NEXTPNR_NAMESPACE_END

#endif /* CELL_PARAMETERS_H */

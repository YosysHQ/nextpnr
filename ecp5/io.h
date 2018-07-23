/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#ifndef IO_H
#define IO_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

enum class IOVoltage
{
    VCC_3V3,
    VCC_2V5,
    VCC_1V8,
    VCC_1V5,
    VCC_1V35,
    VCC_1V2
};

std::string iovoltage_to_str(IOVoltage v);
IOVoltage iovoltage_from_str(const std::string &name);

enum class IOType
{
    TYPE_NONE,
#define X(t) t,
#include "iotypes.inc"
#undef X
    TYPE_UNKNOWN,
};

enum class IOSide
{
    LEFT,
    RIGHT,
    TOP,
    BOTTOM,
};

std::string iotype_to_str(IOType type);
IOType ioType_from_str(const std::string &name);

// IO related functions
IOVoltage get_vccio(IOType type);
bool is_strong_vccio_constraint(IOType type, PortType dir, IOSide side);
bool is_differential(IOType type);
bool is_referenced(IOType type);

bool valid_loc_for_io(IOType type, PortType dir, IOSide side, int z);

NEXTPNR_NAMESPACE_END

#endif

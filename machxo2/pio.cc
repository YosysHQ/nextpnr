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

#include "pio.h"

NEXTPNR_NAMESPACE_BEGIN

std::string iovoltage_to_str(IOVoltage v)
{
    switch (v) {
    case IOVoltage::VCC_3V3:
        return "3V3";
    case IOVoltage::VCC_2V5:
        return "2V5";
    case IOVoltage::VCC_1V8:
        return "1V8";
    case IOVoltage::VCC_1V5:
        return "1V5";
    case IOVoltage::VCC_1V2:
        return "1V2";
    }
    NPNR_ASSERT_FALSE("unknown IO voltage");
}

IOVoltage iovoltage_from_str(const std::string &name)
{
    if (name == "3V3")
        return IOVoltage::VCC_3V3;
    if (name == "2V5")
        return IOVoltage::VCC_2V5;
    if (name == "1V8")
        return IOVoltage::VCC_1V8;
    if (name == "1V5")
        return IOVoltage::VCC_1V5;
    if (name == "1V2")
        return IOVoltage::VCC_1V2;
    NPNR_ASSERT_FALSE("unknown IO voltage");
}

std::string iotype_to_str(IOType type)
{
    if (type == IOType::TYPE_NONE)
        return "NONE";
#define X(t)                                                                                                           \
    if (type == IOType::t)                                                                                             \
        return #t;
#include "iotypes.inc"
#undef X
    if (type == IOType::TYPE_UNKNOWN)
        return "<unknown>";
    NPNR_ASSERT_FALSE("unknown IO type");
}

IOType ioType_from_str(const std::string &name)
{
    if (name == "NONE")
        return IOType::TYPE_NONE;
#define X(t)                                                                                                           \
    if (name == #t)                                                                                                    \
        return IOType::t;
#include "iotypes.inc"
    return IOType::TYPE_UNKNOWN;
}

IOVoltage get_vccio(IOType type)
{
    switch (type) {
    case IOType::LVTTL33:
    case IOType::LVCMOS33:
    case IOType::LVCMOS33D:
    case IOType::LVPECL33:
    case IOType::LVPECL33E:
    case IOType::PCI33:
        return IOVoltage::VCC_3V3;
    case IOType::LVCMOS25:
    case IOType::LVCMOS25D:
    case IOType::LVDS:
    case IOType::RSDS25:
    case IOType::LVDS25E:
    case IOType::MLVDS25:
    case IOType::MLVDS25E:
    case IOType::BLVDS25:
    case IOType::SSTL25_I:
    case IOType::SSTL25_II:
    case IOType::SSTL25D_I:
    case IOType::SSTL25D_II:
        return IOVoltage::VCC_2V5;
    case IOType::LVCMOS18:
    case IOType::LVCMOS18D:
    case IOType::SSTL18_I:
    case IOType::SSTL18_II:
    case IOType::HSTL18_I:
    case IOType::HSTL18_II:
    case IOType::SSTL18D_I:
    case IOType::SSTL18D_II:
    case IOType::HSTL18D_I:
    case IOType::HSTL18D_II:
        return IOVoltage::VCC_1V8;
    case IOType::LVCMOS15:
    case IOType::LVCMOS15D:
        return IOVoltage::VCC_1V5;
    case IOType::LVCMOS12:
    case IOType::LVCMOS12D:
    case IOType::MIPI:
        return IOVoltage::VCC_1V2;
    default:
        NPNR_ASSERT_FALSE("unknown IO type, unable to determine VccIO");
    }
}

bool is_differential(IOType type)
{
    switch (type) {
    case IOType::LVCMOS33D:
    case IOType::LVCMOS25D:
    case IOType::LVCMOS15D:
    case IOType::LVCMOS12D:
    case IOType::LVPECL33:
    case IOType::LVDS:
    case IOType::MLVDS25:
    case IOType::BLVDS25:
    case IOType::LVCMOS18D:
    case IOType::SSTL18D_I:
    case IOType::SSTL18D_II:
    case IOType::SSTL25D_I:
    case IOType::SSTL25D_II:
    case IOType::HSTL18D_I:
    case IOType::HSTL18D_II:
    case IOType::MIPI:
    case IOType::RSDS25:
        return true;
    default:
        return false;
    }
}

bool is_referenced(IOType type)
{
    switch (type) {
    case IOType::SSTL18_I:
    case IOType::SSTL18_II:
    case IOType::SSTL18D_I:
    case IOType::SSTL18D_II:
    case IOType::SSTL25_I:
    case IOType::SSTL25_II:
    case IOType::SSTL25D_I:
    case IOType::SSTL25D_II:
    case IOType::HSTL18_I:
    case IOType::HSTL18_II:
    case IOType::HSTL18D_I:
    case IOType::HSTL18D_II:
        return true;
    default:
        return false;
    }
}

bool is_lvcmos(IOType type)
{
    switch (type) {
    case IOType::LVTTL33:
    case IOType::LVCMOS33:
    case IOType::LVCMOS25:
    case IOType::LVCMOS18:
    case IOType::LVCMOS15:
    case IOType::LVCMOS12:
        return true;
    default:
        return false;
    }
}

bool is_drive_ok(IOType type, std::string d)
{
    switch (type) {
    case IOType::LVTTL33:
    case IOType::LVCMOS33:
        return (d == "4" || d == "8" || d == "12" || d == "16" || d == "24");
    case IOType::LVCMOS25:
        return (d == "4" || d == "8" || d == "12" || d == "16");
    case IOType::LVCMOS18:
        return (d == "4" || d == "8" || d == "12");
    case IOType::LVCMOS15:
        return (d == "4" || d == "8");
    case IOType::LVCMOS12:
        return (d == "2" || d == "6");
    default:
        return false;
    }
}

bool opendrain_capable(IOType type, std::string dir)
{
    if (dir != "OUTPUT" && dir != "BIDIR")
        return false;
        
    return is_lvcmos(type);
}

NEXTPNR_NAMESPACE_END

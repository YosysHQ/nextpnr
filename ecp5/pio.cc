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
    case IOVoltage::VCC_1V35:
        return "1V35";
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
    if (name == "1V35")
        return IOVoltage::VCC_1V35;
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
        return IOVoltage::VCC_3V3;
    case IOType::LVCMOS25:
    case IOType::LVCMOS25D:
    case IOType::LVDS:
    case IOType::SLVS:
    case IOType::SUBLVDS:
    case IOType::LVDS25E:
    case IOType::MLVDS25:
    case IOType::MLVDS25E:
    case IOType::BLVDS25:
        return IOVoltage::VCC_2V5;
    case IOType::LVCMOS18:
    case IOType::LVCMOS18D:
    case IOType::SSTL18_I:
    case IOType::SSTL18_II:
    case IOType::SSTL18D_I:
    case IOType::SSTL18D_II:
        return IOVoltage::VCC_1V8;
    case IOType::LVCMOS15:
    case IOType::SSTL15_I:
    case IOType::SSTL15_II:
    case IOType::SSTL15D_I:
    case IOType::SSTL15D_II:
        return IOVoltage::VCC_1V5;
    case IOType::SSTL135_I:
    case IOType::SSTL135_II:
    case IOType::SSTL135D_I:
    case IOType::SSTL135D_II:
        return IOVoltage::VCC_1V35;
    case IOType::LVCMOS12:
    case IOType::HSUL12:
    case IOType::HSUL12D:
        return IOVoltage::VCC_1V2;
    default:
        NPNR_ASSERT_FALSE("unknown IO type, unable to determine VccIO");
    }
}

bool is_strong_vccio_constraint(IOType type, PortType dir, IOSide side)
{
    if (dir == PORT_OUT || dir == PORT_INOUT)
        return true;
    switch (type) {
    case IOType::TYPE_NONE:
    case IOType::LVCMOS33D:
    case IOType::LVPECL33:
    case IOType::LVDS:
    case IOType::MLVDS25:
    case IOType::BLVDS25:
    case IOType::SLVS:
    case IOType::SUBLVDS:
    case IOType::LVCMOS12:
    case IOType::HSUL12:
    case IOType::HSUL12D:
        return false;
    case IOType::LVCMOS33:
    case IOType::LVTTL33:
    case IOType::LVCMOS25:
        return (side == IOSide::LEFT || side == IOSide::RIGHT);
    default:
        return true;
    }
}

bool is_differential(IOType type)
{
    switch (type) {
    case IOType::LVCMOS33D:
    case IOType::LVCMOS25D:
    case IOType::LVPECL33:
    case IOType::LVDS:
    case IOType::MLVDS25:
    case IOType::BLVDS25:
    case IOType::SLVS:
    case IOType::SUBLVDS:
    case IOType::LVCMOS18D:
    case IOType::SSTL18D_I:
    case IOType::SSTL18D_II:
    case IOType::SSTL15D_I:
    case IOType::SSTL15D_II:
    case IOType::SSTL135D_I:
    case IOType::SSTL135D_II:
    case IOType::HSUL12D:
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
    case IOType::SSTL15_I:
    case IOType::SSTL15_II:
    case IOType::SSTL15D_I:
    case IOType::SSTL15D_II:
    case IOType::SSTL135_I:
    case IOType::SSTL135_II:
    case IOType::SSTL135D_I:
    case IOType::SSTL135D_II:
    case IOType::HSUL12:
    case IOType::HSUL12D:
        return true;
    default:
        return false;
    }
}

bool valid_loc_for_io(IOType type, PortType dir, IOSide side, int z)
{
    bool is_lr = side == IOSide::LEFT || side == IOSide::RIGHT;
    if (is_referenced(type) && !is_lr)
        return false;
    if (is_differential(type) && (!is_lr || ((z % 2) == 1)))
        return false;
    if ((type == IOType::LVCMOS18D || type == IOType::LVDS) && (dir == PORT_OUT || dir == PORT_INOUT) && z != 0)
        return false;
    return true;
}

NEXTPNR_NAMESPACE_END

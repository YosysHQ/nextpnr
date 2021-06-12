/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  gatecat <gatecat@ds0.me>
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

NEXTPNR_NAMESPACE_BEGIN

const dict<std::string, IOTypeData> Arch::io_types = {
        {"LVCMOS33", {IOSTYLE_SE_WR, 330}},      {"LVCMOS25", {IOSTYLE_SE_WR, 250}},
        {"LVCMOS18", {IOSTYLE_SE_WR, 180}},      {"LVCMOS15", {IOSTYLE_SE_WR, 150}},
        {"LVCMOS12", {IOSTYLE_SE_WR, 120}},      {"LVCMOS10", {IOSTYLE_SE_WR, 120}},

        {"LVCMOS33D", {IOSTYLE_PD_WR, 330}},     {"LVCMOS25D", {IOSTYLE_PD_WR, 250}},

        {"LVCMOS18H", {IOSTYLE_SE_HP, 180}},     {"LVCMOS15H", {IOSTYLE_SE_HP, 150}},
        {"LVCMOS12H", {IOSTYLE_SE_HP, 120}},     {"LVCMOS10R", {IOSTYLE_SE_HP, 120}},
        {"LVCMOS10H", {IOSTYLE_SE_HP, 100}},

        {"HSTL15_I", {IOSTYLE_REF_HP, 150}},     {"SSTL15_I", {IOSTYLE_REF_HP, 150}},
        {"SSTL15_II", {IOSTYLE_REF_HP, 150}},    {"SSTL135_I", {IOSTYLE_REF_HP, 135}},
        {"SSTL135_II", {IOSTYLE_REF_HP, 135}},   {"HSUL12", {IOSTYLE_REF_HP, 120}},

        {"LVDS", {IOSTYLE_DIFF_HP, 180}},        {"SLVS", {IOSTYLE_DIFF_HP, 120}},
        {"MIPI_DPHY", {IOSTYLE_DIFF_HP, 120}},   {"HSUL12D", {IOSTYLE_DIFF_HP, 120}},

        {"HSTL15D_I", {IOSTYLE_DIFF_HP, 150}},   {"SSTL15D_I", {IOSTYLE_DIFF_HP, 150}},
        {"SSTL15D_II", {IOSTYLE_DIFF_HP, 150}},  {"SSTL135D_I", {IOSTYLE_DIFF_HP, 135}},
        {"SSTL135D_II", {IOSTYLE_DIFF_HP, 135}}, {"HSUL12D", {IOSTYLE_DIFF_HP, 120}},
};

int Arch::get_io_type_vcc(const std::string &io_type) const
{
    if (!io_types.count(io_type))
        log_error("IO type '%s' not supported.\n", io_type.c_str());
    return io_types.at(io_type).vcco;
}

bool Arch::is_io_type_diff(const std::string &io_type) const
{
    if (!io_types.count(io_type))
        log_error("IO type '%s' not supported.\n", io_type.c_str());
    return io_types.at(io_type).style & IOMODE_DIFF;
}

bool Arch::is_io_type_ref(const std::string &io_type) const
{
    if (!io_types.count(io_type))
        log_error("IO type '%s' not supported.\n", io_type.c_str());
    return io_types.at(io_type).style & IOMODE_REF;
}

NEXTPNR_NAMESPACE_END

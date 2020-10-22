/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
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

namespace {

static const std::unordered_map<IdString, Arch::CellPinsData> base_cell_pin_data = {
        {id_OXIDE_COMB,
         {
                 {id_WCK, PINSTYLE_DEDI},
                 {id_WRE, PINSTYLE_DEDI},

                 {id_FCI, PINSTYLE_DEDI},
                 {id_F1, PINSTYLE_DEDI},
                 {id_WAD0, PINSTYLE_DEDI},
                 {id_WAD1, PINSTYLE_DEDI},
                 {id_WAD2, PINSTYLE_DEDI},
                 {id_WAD3, PINSTYLE_DEDI},
                 {id_WDI, PINSTYLE_DEDI},

                 {{}, PINSTYLE_PU},
         }},
        {id_OXIDE_FF,
         {
                 {id_CLK, PINSTYLE_CLK},
                 {id_LSR, PINSTYLE_LSR},
                 {id_CE, PINSTYLE_CE},
                 {{}, PINSTYLE_DEDI},
         }},
        {id_RAMW,
         {
                 {id_CLK, PINSTYLE_CLK},
                 {{}, PINSTYLE_DEDI},
         }},
        {id_SEIO18_CORE,
         {
                 {id_T, PINSTYLE_T},
                 {id_B, PINSTYLE_DEDI},
                 {{}, PINSTYLE_PU},
         }},
        {id_DIFFIO18_CORE,
         {
                 {id_T, PINSTYLE_T},
                 {id_B, PINSTYLE_DEDI},
                 {{}, PINSTYLE_PU},
         }},
        {id_SEIO33_CORE,
         {
                 {id_T, PINSTYLE_T},
                 {id_B, PINSTYLE_DEDI},
                 {{}, PINSTYLE_PU},
         }},
        {id_OXIDE_EBR,
         {{id_CLKA, PINSTYLE_CLK},    {id_CLKB, PINSTYLE_CLK},   {id_CEA, PINSTYLE_CE},     {id_CEB, PINSTYLE_CE},
          {id_CSA0, PINSTYLE_PU},     {id_CSA1, PINSTYLE_PU},    {id_CSA2, PINSTYLE_PU},    {id_CSB0, PINSTYLE_PU},
          {id_CSB1, PINSTYLE_PU},     {id_CSB2, PINSTYLE_PU},    {id_ADA0, PINSTYLE_ADLSB}, {id_ADA1, PINSTYLE_ADLSB},
          {id_ADA2, PINSTYLE_ADLSB},  {id_ADA2, PINSTYLE_ADLSB}, {id_ADA3, PINSTYLE_ADLSB}, {id_ADB0, PINSTYLE_ADLSB},
          {id_ADB1, PINSTYLE_ADLSB},  {id_WEA, PINSTYLE_INV_PD}, {id_WEB, PINSTYLE_INV_PD}, {id_RSTA, PINSTYLE_INV_PD},
          {id_RSTB, PINSTYLE_INV_PD}, {{}, PINSTYLE_CIB}}},
        {id_OSC_CORE,
         {
                 {id_HFOUTEN, PINSTYLE_PU},
                 {{}, PINSTYLE_CIB},
         }},
};
} // namespace

void Arch::init_cell_pin_data() { cell_pins_db = base_cell_pin_data; }

CellPinStyle Arch::get_cell_pin_style(const CellInfo *cell, IdString port) const
{
    // Look up the pin style in the cell database
    auto fnd_cell = cell_pins_db.find(cell->type);
    if (fnd_cell == cell_pins_db.end())
        return PINSTYLE_NONE;
    auto fnd_port = fnd_cell->second.find(port);
    if (fnd_port != fnd_cell->second.end())
        return fnd_port->second;
    // If there isn't an exact port match, then the empty IdString
    // represents a wildcard default match
    auto fnd_default = fnd_cell->second.find({});
    if (fnd_default != fnd_cell->second.end())
        return fnd_default->second;

    return PINSTYLE_NONE;
}

NEXTPNR_NAMESPACE_END

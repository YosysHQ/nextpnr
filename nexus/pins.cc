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
          {id_RSTB, PINSTYLE_INV_PD}, {{id_DWS0}, PINSTYLE_PU},  {{id_DWS1}, PINSTYLE_PU},  {{id_DWS2}, PINSTYLE_PU},
          {{id_DWS3}, PINSTYLE_PU},   {{id_DWS4}, PINSTYLE_PU},  {{}, PINSTYLE_CIB}}},
        {id_OSC_CORE,
         {
                 {id_HFOUTEN, PINSTYLE_PU},
                 {{}, PINSTYLE_CIB},
         }},
        {id_PREADD9_CORE,
         {
                 {id_CLK, PINSTYLE_CLK}, {id_RSTCL, PINSTYLE_LSR},   {id_RSTB, PINSTYLE_LSR}, {id_CECL, PINSTYLE_CE},
                 {id_CEB, PINSTYLE_CE},

                 {id_B0, PINSTYLE_CIB},  {id_B1, PINSTYLE_CIB},      {id_B2, PINSTYLE_CIB},   {id_B3, PINSTYLE_CIB},
                 {id_B4, PINSTYLE_CIB},  {id_B5, PINSTYLE_CIB},      {id_B6, PINSTYLE_CIB},   {id_B7, PINSTYLE_CIB},
                 {id_B8, PINSTYLE_CIB},  {id_BSIGNED, PINSTYLE_CIB},

                 {id_C0, PINSTYLE_CIB},  {id_C1, PINSTYLE_CIB},      {id_C2, PINSTYLE_CIB},   {id_C3, PINSTYLE_CIB},
                 {id_C4, PINSTYLE_CIB},  {id_C5, PINSTYLE_CIB},      {id_C6, PINSTYLE_CIB},   {id_C7, PINSTYLE_CIB},
                 {id_C8, PINSTYLE_CIB},  {id_C9, PINSTYLE_CIB},

                 {{}, PINSTYLE_DEDI},
         }},
        {id_MULT9_CORE,
         {
                 {id_CLK, PINSTYLE_CLK},
                 {id_RSTA, PINSTYLE_LSR},
                 {id_RSTP, PINSTYLE_LSR},
                 {id_CEA, PINSTYLE_CE},
                 {id_CEP, PINSTYLE_CE},

                 {id_A0, PINSTYLE_CIB},
                 {id_A1, PINSTYLE_CIB},
                 {id_A2, PINSTYLE_CIB},
                 {id_A3, PINSTYLE_CIB},
                 {id_A4, PINSTYLE_CIB},
                 {id_A5, PINSTYLE_CIB},
                 {id_A6, PINSTYLE_CIB},
                 {id_A7, PINSTYLE_CIB},
                 {id_A8, PINSTYLE_CIB},
                 {id_ASIGNED, PINSTYLE_CIB},

                 {{}, PINSTYLE_DEDI},
         }},
        {id_REG18_CORE,
         {
                 {id_CLK, PINSTYLE_CLK},
                 {id_RSTP, PINSTYLE_LSR},
                 {id_CEP, PINSTYLE_CE},
         }}};
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

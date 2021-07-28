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

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

static const dict<IdString, Arch::CellPinsData> base_cell_pin_data = {
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
                 {{}, PINSTYLE_DEDI},
         }},
        {id_MULT18_CORE,
         {
                 {id_SFTCTRL0, PINSTYLE_PU},
                 {id_SFTCTRL1, PINSTYLE_PU},
                 {id_SFTCTRL2, PINSTYLE_PU},
                 {id_SFTCTRL3, PINSTYLE_PU},
                 {id_ROUNDEN, PINSTYLE_CIB},
                 {{}, PINSTYLE_DEDI},
         }},
        {id_MULT18X36_CORE,
         {
                 {id_SFTCTRL0, PINSTYLE_PU},
                 {id_SFTCTRL1, PINSTYLE_PU},
                 {id_SFTCTRL2, PINSTYLE_PU},
                 {id_SFTCTRL3, PINSTYLE_PU},
                 {id_ROUNDEN, PINSTYLE_CIB},
                 {{}, PINSTYLE_DEDI},
         }},
        {id_ACC54_CORE,
         {
                 {id_CLK, PINSTYLE_CLK},      {id_RSTC, PINSTYLE_LSR},    {id_CEC, PINSTYLE_CE},
                 {id_SIGNEDI, PINSTYLE_CIB},  {id_RSTCTRL, PINSTYLE_LSR}, {id_CECTRL, PINSTYLE_CE},
                 {id_RSTCIN, PINSTYLE_LSR},   {id_CECIN, PINSTYLE_CE},    {id_LOAD, PINSTYLE_CIB},
                 {id_ADDSUB0, PINSTYLE_CIB},  {id_ADDSUB1, PINSTYLE_CIB}, {id_M9ADDSUB0, PINSTYLE_PU},
                 {id_M9ADDSUB1, PINSTYLE_PU}, {id_ROUNDEN, PINSTYLE_CIB}, {id_RSTO, PINSTYLE_LSR},
                 {id_CEO, PINSTYLE_CE},       {id_CIN, PINSTYLE_CIB},     {id_SFTCTRL0, PINSTYLE_PU},
                 {id_SFTCTRL1, PINSTYLE_PU},  {id_SFTCTRL2, PINSTYLE_PU}, {id_SFTCTRL3, PINSTYLE_PU},
                 {{}, PINSTYLE_DEDI},
         }},
        {id_PLL_CORE,
         {
                 {id_REFCK, PINSTYLE_DEDI},
                 {id_FBKCK, PINSTYLE_DEDI},
                 {id_SCANCLK, PINSTYLE_DEDI},
                 {id_SCANRST, PINSTYLE_DEDI},
                 {id_LMMICLK, PINSTYLE_CLK},
                 {id_LMMIRESETN, PINSTYLE_CE},
                 {id_OPCGLDCK, PINSTYLE_DEDI},
                 {id_ZRSEL3, PINSTYLE_DEDI},
                 {id_ENEXT, PINSTYLE_DEDI},
                 {{}, PINSTYLE_CIB},
         }},
        {id_LRAM_CORE,
         {
                 {id_CLK, PINSTYLE_CLK},
                 {id_CEA, PINSTYLE_PU_NONCIB},
                 {id_CEB, PINSTYLE_PU_NONCIB},
                 {id_OCEA, PINSTYLE_PU},
                 {id_OCEB, PINSTYLE_PU},
                 {id_CSA, PINSTYLE_PU},
                 {id_CSB, PINSTYLE_PU},
                 {id_RSTA, PINSTYLE_LSR},
                 {id_RSTB, PINSTYLE_LSR},
                 {id_WEA, PINSTYLE_INV_PD_CIB},
                 {id_WEB, PINSTYLE_INV_PD_CIB},
                 {id_IGN, PINSTYLE_PU},
                 {id_INITN, PINSTYLE_PU},
                 {id_STDBYN, PINSTYLE_PU},
                 {id_TBISTN, PINSTYLE_PU},
                 {id_SCANCLK, PINSTYLE_DEDI},
                 {id_SCANRST, PINSTYLE_DEDI},
                 {id_OPCGLDCK, PINSTYLE_DEDI},
                 {{}, PINSTYLE_CIB},
         }},
        {id_DPHY_CORE,
         {
                 {id_CKN, PINSTYLE_DEDI},
                 {id_CKP, PINSTYLE_DEDI},
                 {id_DN0, PINSTYLE_DEDI},
                 {id_DN1, PINSTYLE_DEDI},
                 {id_DN2, PINSTYLE_DEDI},
                 {id_DN3, PINSTYLE_DEDI},
                 {id_DP0, PINSTYLE_DEDI},
                 {id_DP1, PINSTYLE_DEDI},
                 {id_DP2, PINSTYLE_DEDI},
                 {id_DP3, PINSTYLE_DEDI},
                 {id_SCCLKIN, PINSTYLE_DEDI},
                 {id_SCRSTNIN, PINSTYLE_DEDI},
                 {id_SCANCLK, PINSTYLE_DEDI},
                 {id_SCANRST, PINSTYLE_DEDI},
                 {id_LMMIRESETN, PINSTYLE_DEDI},
                 {id_CLKREF, PINSTYLE_DEDI},
                 {id_U2TDE4CK, PINSTYLE_DEDI},
                 {id_OPCGLDCK, PINSTYLE_DEDI},
                 {id_U1ENTHEN, PINSTYLE_PD_NONCIB},
                 {id_U2END2, PINSTYLE_PD_NONCIB},
                 {id_U3END3, PINSTYLE_PD_NONCIB},
                 {id_UED0THEN, PINSTYLE_PD_NONCIB},
                 {{}, PINSTYLE_CIB},
         }},
        {id_SIOLOGIC,
         {
                 {id_SCLKIN, PINSTYLE_IOL_CLK},
                 {id_SCLKOUT, PINSTYLE_IOL_CLK},
                 {id_LSRIN, PINSTYLE_IOL_CELSR},
                 {id_LSROUT, PINSTYLE_IOL_CELSR},
                 {id_CEIN, PINSTYLE_IOL_CELSR},
                 {id_CEOUT, PINSTYLE_IOL_CELSR},
                 {id_TXDATA0, PINSTYLE_CIB},
                 {id_TXDATA1, PINSTYLE_CIB},
                 {id_TSDATA0, PINSTYLE_CIB},

         }},
        {id_IOLOGIC,
         {
                 {id_SCLKIN, PINSTYLE_IOL_CLK},
                 {id_SCLKOUT, PINSTYLE_IOL_CLK},
                 {id_LSRIN, PINSTYLE_IOL_CELSR},
                 {id_LSROUT, PINSTYLE_IOL_CELSR},
                 {id_CEIN, PINSTYLE_IOL_CELSR},
                 {id_CEOUT, PINSTYLE_IOL_CELSR},
                 {id_TXDATA0, PINSTYLE_CIB},
                 {id_TXDATA1, PINSTYLE_CIB},
                 {id_TSDATA0, PINSTYLE_CIB},
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

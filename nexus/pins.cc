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

void Arch::get_invertible_pins(std::unordered_map<IdString, std::unordered_set<IdString>> &pins) const
{
    pins[id_OXIDE_FF] = {id_CLK, id_LSR, id_CE};
    pins[id_RAMW] = {id_WCK};
    pins[id_SEIO18_CORE] = {id_T};
    pins[id_SEIO33_CORE] = {id_T};
}

void Arch::get_pins_floating_value(std::unordered_map<IdString, std::unordered_map<IdString, bool>> &pins) const
{
    pins[id_OXIDE_COMB] = {{id_A, true}, {id_B, true}, {id_C, true}, {id_D, true}, {id_SEL, true}};
    pins[id_OXIDE_FF] = {{id_CLK, false}, {id_LSR, true}, {id_CE, true}};
    pins[id_SEIO18_CORE] = {{id_T, true}};
    pins[id_SEIO33_CORE] = {{id_T, true}};
}

void Arch::get_pins_default_value(
        std::unordered_map<IdString, std::unordered_map<IdString, Property::State>> &pins) const
{
    pins[id_OXIDE_COMB] = {{id_A, Property::S1},    {id_B, Property::S1},    {id_C, Property::S1},
                           {id_D, Property::S1},    {id_SEL, Property::S1},  {id_WAD0, Property::Sx},
                           {id_WAD1, Property::Sx}, {id_WAD2, Property::Sx}, {id_WAD3, Property::Sx},
                           {id_WCK, Property::Sx},  {id_WRE, Property::Sx},  {id_WD, Property::Sx}};
    pins[id_OXIDE_FF] = {{id_CE, Property::S1}, {id_DI, Property::Sx}};
    pins[id_SEIO18_CORE] = {{id_T, Property::S1}};
    pins[id_SEIO33_CORE] = {{id_T, Property::S1}};
}

NEXTPNR_NAMESPACE_END
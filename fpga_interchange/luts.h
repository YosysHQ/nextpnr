/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
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

#ifndef LUTS_H
#define LUTS_H

#include "idstring.h"
#include "nextpnr_namespaces.h"

#include "dynamic_bitarray.h"
#include "hashlib.h"

NEXTPNR_NAMESPACE_BEGIN

struct CellInfo;
struct Context;

struct SiteLutMappingResult;

enum LogicLevel
{
    LL_Zero,
    LL_One,
    LL_DontCare
};

struct LutCell
{
    enum class PinConnection
    {
        Unconnected,
        Gnd,
        Vcc,
        Const,
        Signal
    };

    // LUT cell pins for equation, LSB first.
    std::vector<IdString> pins;
    pool<IdString> lut_pins;
    dict<IdString, PinConnection> pin_connections;
    DynamicBitarray<> equation;

    static const std::string nameOfPinConnection(PinConnection conn);
};

struct LutBel
{
    IdString name;

    // LUT BEL pins to LUT array index.
    std::vector<IdString> pins;
    dict<IdString, size_t> pin_to_index;

    IdString output_pin;

    // What part of the LUT equation does this LUT output use?
    // This assumes contiguous LUT bits.
    uint32_t low_bit;
    uint32_t high_bit;

    int32_t min_pin;
    int32_t max_pin;
};

struct SiteLutMapping
{
    struct LutCellMapping
    {
        LutCell lut_cell;
    };
};

// Work forward from cell definition and cell -> bel pin map and check that
// equation is valid.
void check_equation(const LutCell &lut_cell, const dict<IdString, IdString> &cell_to_bel_map, const LutBel &lut_bel,
                    const std::vector<LogicLevel> &equation, uint32_t used_pins);

struct LutElement
{
    size_t width;
    dict<IdString, LutBel> lut_bels;

    void compute_pin_order();

    std::vector<IdString> pins;
    dict<IdString, size_t> pin_to_index;
};

struct LutMapper
{
    LutMapper(const LutElement &element) : element(element) {}
    const LutElement &element;

    std::vector<CellInfo *> cells;

    bool remap_luts(const Context *ctx, SiteLutMappingResult *lut_mapping,
                    pool<const LutBel *, hash_ptr_ops> *blocked_luts);

    // Determine which wires given the current mapping must be tied to the
    // default constant.
    //
    // Returns a bit mask, 1 meaning it must be tied.   Otherwise means that
    // the pin is free to be a signal.
    uint32_t check_wires(const std::vector<std::vector<int32_t>> &bel_to_cell_pin_remaps,
                         const std::vector<const LutBel *> &lut_bels, uint32_t used_pins,
                         pool<const LutBel *, hash_ptr_ops> *blocked_luts) const;

    // Version of check_wires that uses current state of cells based on pin
    // mapping in cells variable.
    uint32_t check_wires(const Context *ctx) const;
};

// Rotate and merge a LUT equation into an array of levels.
//
// If a conflict arises, return false and result is in an indeterminate state.
bool rotate_and_merge_lut_equation(std::vector<LogicLevel> *result, const LutBel &lut_bel,
                                   const DynamicBitarray<> &old_equation, const std::vector<size_t> &pin_map,
                                   uint32_t used_pins);

NEXTPNR_NAMESPACE_END

#endif /* LUTS_H */

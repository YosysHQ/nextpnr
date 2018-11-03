/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#include "nextpnr.h"

#ifndef ICE40_CELLS_H
#define ICE40_CELLS_H

NEXTPNR_NAMESPACE_BEGIN

// Create a standard xc7 cell and return it
// Name will be automatically assigned if not specified
std::unique_ptr<CellInfo> create_xc7_cell(Context *ctx, IdString type, std::string name = "");

// Return true if a cell is a LUT
inline bool is_lut(const BaseCtx *ctx, const CellInfo *cell)
{
    return cell->type == id_LUT1 || cell->type == id_LUT2 || cell->type == id_LUT3 || cell->type == id_LUT4 ||
           cell->type == id_LUT5 || cell->type == id_LUT6;
}

// Return true if a cell is a flipflop
inline bool is_ff(const BaseCtx *ctx, const CellInfo *cell)
{
    return cell->type == id_FDRE || cell->type == id_FDSE || cell->type == id_FDCE || cell->type == id_FDPE;
}

inline bool is_carry(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == ctx->id("SB_CARRY"); }

inline bool is_lc(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == ctx->id("XC7_LC"); }

// Return true if a cell is a SB_IO
inline bool is_sb_io(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == ctx->id("SB_IO"); }

// Return true if a cell is a global buffer
inline bool is_gbuf(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_BUFGCTRL; }

// Return true if a cell is a RAM
inline bool is_ram(const BaseCtx *ctx, const CellInfo *cell)
{
    return cell->type == ctx->id("SB_RAM40_4K") || cell->type == ctx->id("SB_RAM40_4KNR") ||
           cell->type == ctx->id("SB_RAM40_4KNW") || cell->type == ctx->id("SB_RAM40_4KNRNW");
}

inline bool is_sb_lfosc(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == ctx->id("SB_LFOSC"); }

inline bool is_sb_hfosc(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == ctx->id("SB_HFOSC"); }

inline bool is_sb_spram(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == ctx->id("SB_SPRAM256KA"); }

inline bool is_sb_mac16(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == ctx->id("SB_MAC16"); }

inline bool is_sb_pll40(const BaseCtx *ctx, const CellInfo *cell)
{
    return cell->type == ctx->id("SB_PLL40_PAD") || cell->type == ctx->id("SB_PLL40_2_PAD") ||
           cell->type == ctx->id("SB_PLL40_2F_PAD") || cell->type == ctx->id("SB_PLL40_CORE") ||
           cell->type == ctx->id("SB_PLL40_2F_CORE");
}

inline bool is_sb_pll40_pad(const BaseCtx *ctx, const CellInfo *cell)
{
    return cell->type == ctx->id("SB_PLL40_PAD") || cell->type == ctx->id("SB_PLL40_2_PAD") ||
           cell->type == ctx->id("SB_PLL40_2F_PAD");
}

uint8_t sb_pll40_type(const BaseCtx *ctx, const CellInfo *cell);

// Convert a SB_LUT primitive to (part of) an ICESTORM_LC, swapping ports
// as needed. Set no_dff if a DFF is not being used, so that the output
// can be reconnected
void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff = true);

// Convert a SB_DFFx primitive to (part of) an ICESTORM_LC, setting parameters
// and reconnecting signals as necessary. If pass_thru_lut is True, the LUT will
// be configured as pass through and D connected to I0, otherwise D will be
// ignored
void dff_to_lc(const Context *ctx, CellInfo *dff, CellInfo *lc, bool pass_thru_lut = false);

// Convert a nextpnr IO buffer to a SB_IO
void nxio_to_sb(Context *ctx, CellInfo *nxio, CellInfo *sbio);

// Return true if a port is a clock port
bool is_clock_port(const BaseCtx *ctx, const PortRef &port);

// Return true if a port is a reset port
bool is_reset_port(const BaseCtx *ctx, const PortRef &port);

// Return true if a port is a clock enable port
bool is_enable_port(const BaseCtx *ctx, const PortRef &port);

NEXTPNR_NAMESPACE_END

#endif

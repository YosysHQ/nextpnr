/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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

#include "nextpnr.h"

#ifndef ICE40_CELLS_H
#define ICE40_CELLS_H

NEXTPNR_NAMESPACE_BEGIN

// Create a standard iCE40 cell and return it
// Name will be automatically assigned if not specified
std::unique_ptr<CellInfo> create_ice_cell(Context *ctx, IdString type, std::string name = "");

// Return true if a cell is a LUT
inline bool is_lut(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_LUT4; }

// Return true if a cell is a flipflop
inline bool is_ff(const BaseCtx * /*ctx*/, const CellInfo *cell)
{
    return cell->type.in(id_SB_DFF, id_SB_DFFE, id_SB_DFFSR, id_SB_DFFR, id_SB_DFFSS, id_SB_DFFS, id_SB_DFFESR,
                         id_SB_DFFER, id_SB_DFFESS, id_SB_DFFES, id_SB_DFFN, id_SB_DFFNE, id_SB_DFFNSR, id_SB_DFFNR,
                         id_SB_DFFNSS, id_SB_DFFNS, id_SB_DFFNESR, id_SB_DFFNER, id_SB_DFFNESS, id_SB_DFFNES);
}

inline bool is_carry(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_CARRY; }

inline bool is_lc(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_ICESTORM_LC; }

// Return true if a cell is a SB_IO
inline bool is_sb_io(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_IO; }

// Return true if a cell is a SB_GB_IO
inline bool is_sb_gb_io(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_GB_IO; }

// Return true if a cell is a global buffer
inline bool is_gbuf(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_GB; }

// Return true if a cell is a RAM
inline bool is_ram(const BaseCtx * /*ctx*/, const CellInfo *cell)
{
    return cell->type.in(id_SB_RAM40_4K, id_SB_RAM40_4KNR, id_SB_RAM40_4KNW, id_SB_RAM40_4KNRNW);
}

inline bool is_sb_lfosc(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_LFOSC; }

inline bool is_sb_hfosc(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_HFOSC; }

inline bool is_sb_spram(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_SPRAM256KA; }

inline bool is_sb_mac16(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_MAC16; }

inline bool is_sb_rgba_drv(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_RGBA_DRV; }

inline bool is_sb_rgb_drv(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_RGB_DRV; }

inline bool is_sb_led_drv_cur(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_LED_DRV_CUR; }

inline bool is_sb_ledda_ip(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_LEDDA_IP; }

inline bool is_sb_i2c(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_I2C; }

inline bool is_sb_spi(const BaseCtx * /*ctx*/, const CellInfo *cell) { return cell->type == id_SB_SPI; }

inline bool is_sb_pll40(const BaseCtx * /*ctx*/, const CellInfo *cell)
{
    return cell->type.in(id_SB_PLL40_PAD, id_SB_PLL40_2_PAD, id_SB_PLL40_2F_PAD, id_SB_PLL40_CORE, id_SB_PLL40_2F_CORE);
}

inline bool is_sb_pll40_pad(const BaseCtx * /*ctx*/, const CellInfo *cell)
{
    return cell->type.in(id_SB_PLL40_PAD, id_SB_PLL40_2_PAD, id_SB_PLL40_2F_PAD) ||
           (cell->type == id_ICESTORM_PLL && (cell->attrs.at(id_TYPE).as_string() == "SB_PLL40_PAD" ||
                                              cell->attrs.at(id_TYPE).as_string() == "SB_PLL40_2_PAD" ||
                                              cell->attrs.at(id_TYPE).as_string() == "SB_PLL40_2F_PAD"));
}

inline bool is_sb_pll40_dual(const BaseCtx * /*ctx*/, const CellInfo *cell)
{
    return cell->type.in(id_SB_PLL40_2_PAD, id_SB_PLL40_2F_PAD, id_SB_PLL40_2F_CORE) ||
           (cell->type == id_ICESTORM_PLL && (cell->attrs.at(id_TYPE).as_string() == "SB_PLL40_2_PAD" ||
                                              cell->attrs.at(id_TYPE).as_string() == "SB_PLL40_2F_PAD" ||
                                              cell->attrs.at(id_TYPE).as_string() == "SB_PLL40_2F_CORE"));
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
void nxio_to_sb(Context *ctx, CellInfo *nxio, CellInfo *sbio, pool<IdString> &todelete_cells);

// Return true if a port is a clock port
bool is_clock_port(const BaseCtx *ctx, const PortRef &port);

// Return true if a port is a reset port
bool is_reset_port(const BaseCtx *ctx, const PortRef &port);

// Return true if a port is a clock enable port
bool is_enable_port(const BaseCtx *ctx, const PortRef &port);

NEXTPNR_NAMESPACE_END

#endif

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

// Create a standard iCE40 cell and return it
// Name will be automatically assigned if not specified
CellInfo *create_ice_cell(Context *ctx, IdString type,
                          IdString name = IdString());

// Return true if a cell is a LUT
inline bool is_lut(const CellInfo *cell) { return cell->type == "SB_LUT4"; }

// Return true if a cell is a flipflop
inline bool is_ff(const CellInfo *cell)
{
    return cell->type == "SB_DFF" || cell->type == "SB_DFFE" ||
           cell->type == "SB_DFFSR" || cell->type == "SB_DFFR" ||
           cell->type == "SB_DFFSS" || cell->type == "SB_DFFS" ||
           cell->type == "SB_DFFESR" || cell->type == "SB_DFFER" ||
           cell->type == "SB_DFFESS" || cell->type == "SB_DFFES" ||
           cell->type == "SB_DFFN" || cell->type == "SB_DFFNE" ||
           cell->type == "SB_DFFNSR" || cell->type == "SB_DFFNR" ||
           cell->type == "SB_DFFNSS" || cell->type == "SB_DFFNS" ||
           cell->type == "SB_DFFNESR" || cell->type == "SB_DFFNER" ||
           cell->type == "SB_DFFNESS" || cell->type == "SB_DFFNES";
}

// Return true if a cell is a SB_IO
inline bool is_sb_io(const CellInfo *cell) { return cell->type == "SB_IO"; }

// Return true if a cell is a global buffer
inline bool is_gbuf(const CellInfo *cell) { return cell->type == "SB_GB"; }

// Return true if a cell is a RAM
inline bool is_ram(const CellInfo *cell)
{
    return cell->type == "SB_RAM40_4K" || cell->type == "SB_RAM40_4KNR" ||
           cell->type == "SB_RAM40_4KNW" || cell->type == "SB_RAM40_4KNRNW";
}

// Convert a SB_LUT primitive to (part of) an ICESTORM_LC, swapping ports
// as needed. Set no_dff if a DFF is not being used, so that the output
// can be reconnected
void lut_to_lc(CellInfo *lut, CellInfo *lc, bool no_dff = true);

// Convert a SB_DFFx primitive to (part of) an ICESTORM_LC, setting parameters
// and reconnecting signals as necessary. If pass_thru_lut is True, the LUT will
// be configured as pass through and D connected to I0, otherwise D will be
// ignored
void dff_to_lc(CellInfo *dff, CellInfo *lc, bool pass_thru_lut = false);

// Convert a nextpnr IO buffer to a SB_IO
void nxio_to_sb(CellInfo *nxio, CellInfo *sbio);

// Return true if a net is a global net
bool is_global_net(const NetInfo *net);

// Return true if a port is a clock port
bool is_clock_port(const PortRef &port);

// Return true if a port is a reset port
bool is_reset_port(const PortRef &port);

// Return true if a port is a clock enable port
bool is_enable_port(const PortRef &port);

NEXTPNR_NAMESPACE_END

#endif

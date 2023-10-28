/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#ifndef HIMBAECHEL_HELPERS_H
#define HIMBAECHEL_HELPERS_H

#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

/*
Himbächel -- a series of small arches

See himbaechel_api.h for more background.

himbaechel_helpers provides some features for building up arches using the himbächel API
*/

// Used to configure various generic pack functions
struct CellTypePort
{
    CellTypePort() : cell_type(), port(){};
    CellTypePort(IdString cell_type, IdString port) : cell_type(cell_type), port(port){};
    explicit CellTypePort(const PortRef &net_port)
            : cell_type(net_port.cell ? net_port.cell->type : IdString()), port(net_port.port){};
    inline bool operator==(const CellTypePort &other) const
    {
        return cell_type == other.cell_type && port == other.port;
    }
    inline bool operator!=(const CellTypePort &other) const
    {
        return cell_type != other.cell_type || port != other.port;
    }
    inline unsigned hash() const { return mkhash(cell_type.hash(), port.hash()); }
    IdString cell_type, port;
};

struct HimbaechelHelpers
{
    HimbaechelHelpers(){};
    Context *ctx;
    void init(Context *ctx) { this->ctx = ctx; }
    // Common packing functions
    // Remove nextpnr-inserted IO buffers; where IO buffer insertion is done in synthesis
    // expects a set of top-level port types
    void remove_nextpnr_iobs(const pool<CellTypePort> &top_ports);
    // Constrain cells with certain port connection patterns together with a fixed z-offset
    int constrain_cell_pairs(const pool<CellTypePort> &src_ports, const pool<CellTypePort> &sink_ports, int delta_z,
                             bool allow_fanout = true);
    // Replace constants with given driving cells
    void replace_constants(CellTypePort vcc_driver, CellTypePort gnd_driver,
                           const dict<IdString, Property> &vcc_params = {},
                           const dict<IdString, Property> &gnd_params = {}, IdString vcc_const_val = IdString(),
                           IdString gnd_const_val = IdString());
};

NEXTPNR_NAMESPACE_END

#endif

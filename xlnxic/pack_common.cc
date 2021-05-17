/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

std::vector<CellInfo *> Arch::get_cells_by_type(IdString type)
{
    std::vector<CellInfo *> result;
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == type)
            result.push_back(ci);
    }
    return result;
}

IdString Arch::derive_name(IdString base, IdString postfix, bool is_net)
{
    IdString concat_name = id(stringf("%s/%s", base.c_str(this), postfix.c_str(this)));
    IdString result = concat_name;
    int autoidx = 0;
    while (is_net ? nets.count(result) : cells.count(result)) {
        // Add an extra postfix to disambiguate
        result = id(stringf("%s$%d", concat_name.c_str(this), autoidx++));
    }
    return result;
}

CellInfo *Arch::create_lib_cell(IdString name, IdString type)
{
    const CellTypePOD *cell_type = nullptr;
    for (auto &type_data : chip_info->cell_types) {
        if (IdString(type_data.cell_type) == type) {
            cell_type = &type_data;
            break;
        }
    }
    NPNR_ASSERT(cell_type != nullptr);
    CellInfo *cell = createCell(name, type);
    for (auto &log_port : cell_type->logical_ports) {
        IdString port_name(log_port.name);
        if (log_port.bus_start != -1) {
            for (int idx = log_port.bus_start; idx <= log_port.bus_end; idx++) {
                IdString bit_name = id(stringf("%s[%d]", port_name.c_str(this), idx));
                cell->ports[bit_name].name = bit_name;
                cell->ports[bit_name].type = PortType(log_port.dir);
            }
        } else {
            cell->ports[port_name].name = port_name;
            cell->ports[port_name].type = PortType(log_port.dir);
        }
    }
    return cell;
}

NEXTPNR_NAMESPACE_END

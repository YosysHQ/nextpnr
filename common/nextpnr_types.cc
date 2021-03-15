/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#include "nextpnr_types.h"

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

void CellInfo::addInput(IdString name)
{
    ports[name].name = name;
    ports[name].type = PORT_IN;
}
void CellInfo::addOutput(IdString name)
{
    ports[name].name = name;
    ports[name].type = PORT_OUT;
}
void CellInfo::addInout(IdString name)
{
    ports[name].name = name;
    ports[name].type = PORT_INOUT;
}

void CellInfo::setParam(IdString name, Property value) { params[name] = value; }
void CellInfo::unsetParam(IdString name) { params.erase(name); }
void CellInfo::setAttr(IdString name, Property value) { attrs[name] = value; }
void CellInfo::unsetAttr(IdString name) { attrs.erase(name); }

bool CellInfo::isConstrained(bool include_abs_z_constr) const
{
    return constr_parent != nullptr || !constr_children.empty() || (include_abs_z_constr && constr_abs_z);
}

bool CellInfo::testRegion(BelId bel) const
{
    return region == nullptr || !region->constr_bels || region->bels.count(bel);
}
Loc CellInfo::getConstrainedLoc(Loc parent_loc) const
{
    NPNR_ASSERT(constr_parent != nullptr);
    Loc cloc = parent_loc;
    if (constr_x != UNCONSTR)
        cloc.x += constr_x;
    if (constr_y != UNCONSTR)
        cloc.y += constr_y;
    if (constr_z != UNCONSTR)
        cloc.z = constr_abs_z ? constr_z : (parent_loc.z + constr_z);
    return cloc;
}

NEXTPNR_NAMESPACE_END

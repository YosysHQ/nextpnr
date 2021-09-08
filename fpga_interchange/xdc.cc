/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  gatecat <gatecat@ds0.me>
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

#include "xdc.h"

#include <string>

#include "context.h"
#include "log.h"

// Include tcl.h late because it messed with #define's and lets them leave the
// scope of the header.
#include <tcl.h>

NEXTPNR_NAMESPACE_BEGIN

static int obj_set_from_any(Tcl_Interp *interp, Tcl_Obj *objPtr) { return TCL_ERROR; }

static void set_tcl_obj_string(Tcl_Obj *objPtr, const std::string &s)
{
    NPNR_ASSERT(objPtr->bytes == nullptr);
    // Need to have space for the end null byte.
    objPtr->bytes = Tcl_Alloc(s.size() + 1);

    // Length is length of string, not including the end null byte.
    objPtr->length = s.size();

    std::copy(s.begin(), s.end(), objPtr->bytes);
    objPtr->bytes[objPtr->length] = '\0';
}

static void port_update_string(Tcl_Obj *objPtr)
{
    const Context *ctx = static_cast<const Context *>(objPtr->internalRep.twoPtrValue.ptr1);
    PortInfo *port_info = static_cast<PortInfo *>(objPtr->internalRep.twoPtrValue.ptr2);

    std::string port_name = port_info->name.str(ctx);
    set_tcl_obj_string(objPtr, port_name);
}

static void cell_update_string(Tcl_Obj *objPtr)
{
    const Context *ctx = static_cast<const Context *>(objPtr->internalRep.twoPtrValue.ptr1);
    CellInfo *cell_info = static_cast<CellInfo *>(objPtr->internalRep.twoPtrValue.ptr2);

    std::string cell_name = cell_info->name.str(ctx);
    set_tcl_obj_string(objPtr, cell_name);
}

static void obj_dup(Tcl_Obj *srcPtr, Tcl_Obj *dupPtr)
{
    dupPtr->internalRep.twoPtrValue = srcPtr->internalRep.twoPtrValue;
}

static void obj_free(Tcl_Obj *objPtr) {}

static void Tcl_SetStringResult(Tcl_Interp *interp, const std::string &s)
{
    char *copy = Tcl_Alloc(s.size() + 1);
    std::copy(s.begin(), s.end(), copy);
    copy[s.size()] = '\0';
    Tcl_SetResult(interp, copy, TCL_DYNAMIC);
}

static Tcl_ObjType port_object = {
        "port", obj_free, obj_dup, port_update_string, obj_set_from_any,
};

static Tcl_ObjType cell_object = {
        "cell", obj_free, obj_dup, cell_update_string, obj_set_from_any,
};

static int get_ports(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    const Context *ctx = static_cast<const Context *>(data);
    if (objc == 1) {
        // Return list of all ports.
        Tcl_SetStringResult(interp, "Unimplemented");
        return TCL_ERROR;
    } else if (objc == 2) {
        const char *arg0 = Tcl_GetString(objv[1]);
        IdString port_name = ctx->id(arg0);

        auto iter = ctx->ports.find(port_name);
        if (iter == ctx->ports.end()) {
            Tcl_SetStringResult(interp, "Could not find port " + port_name.str(ctx));
            return TCL_ERROR;
        }

        Tcl_Obj *result = Tcl_NewObj();
        result->typePtr = &port_object;
        result->internalRep.twoPtrValue.ptr1 = (void *)(ctx);
        result->internalRep.twoPtrValue.ptr2 = (void *)(&iter->second);

        result->bytes = nullptr;
        port_update_string(result);

        Tcl_SetObjResult(interp, result);
        return TCL_OK;
    } else if (objc > 2) {
        log_warning("get_ports options not implemented!\n");
        return TCL_OK;
    } else {
        return TCL_ERROR;
    }
}

static int get_cells(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    const Context *ctx = static_cast<const Context *>(data);
    if (objc == 1) {
        // Return list of all ports.
        Tcl_SetStringResult(interp, "Unimplemented");
        return TCL_ERROR;
    } else if (objc == 2) {
        const char *arg0 = Tcl_GetString(objv[1]);
        IdString cell_name = ctx->id(arg0);

        auto iter = ctx->cells.find(cell_name);
        if (iter == ctx->cells.end()) {
            Tcl_SetStringResult(interp, "Could not find cell " + cell_name.str(ctx));
            return TCL_ERROR;
        }

        Tcl_Obj *result = Tcl_NewObj();
        result->typePtr = &cell_object;
        result->internalRep.twoPtrValue.ptr1 = (void *)(ctx);
        result->internalRep.twoPtrValue.ptr2 = (void *)(iter->second.get());

        result->bytes = nullptr;
        cell_update_string(result);

        Tcl_SetObjResult(interp, result);
        return TCL_OK;
    } else if (objc > 2) {
        log_warning("get_cells options not implemented!\n");
        return TCL_OK;
    } else {
        return TCL_ERROR;
    }
}

static int set_property(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    // set_property <property> <value> <object>
    if (objc != 4) {
        Tcl_SetStringResult(interp, "Only simple 'set_property <property> <value> <object>' is supported");
        return TCL_ERROR;
    }

    const char *property = Tcl_GetString(objv[1]);
    const char *value = Tcl_GetString(objv[2]);
    const Tcl_Obj *object = objv[3];

    if (object->typePtr == &port_object) {
        const Context *ctx = static_cast<const Context *>(object->internalRep.twoPtrValue.ptr1);
        PortInfo *port_info = static_cast<PortInfo *>(object->internalRep.twoPtrValue.ptr2);
        NPNR_ASSERT(port_info->net != nullptr);
        CellInfo *cell = ctx->port_cells.at(port_info->name);

        cell->attrs[ctx->id(property)] = Property(value);
    } else if (object->typePtr == &cell_object) {
        const Context *ctx = static_cast<const Context *>(object->internalRep.twoPtrValue.ptr1);
        CellInfo *cell = static_cast<CellInfo *>(object->internalRep.twoPtrValue.ptr2);

        cell->attrs[ctx->id(property)] = Property(value);
    }

    return TCL_OK;
}

static int not_implemented(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    // TCL command that is not yet implemented
    log_warning("%s command is not implemented!\n", Tcl_GetString(objv[0]));
    return TCL_OK;
}

TclInterp::TclInterp(Context *ctx)
{
    interp = Tcl_CreateInterp();
    NPNR_ASSERT(Tcl_Init(interp) == TCL_OK);

    Tcl_RegisterObjType(&port_object);
    Tcl_RegisterObjType(&cell_object);

    NPNR_ASSERT(Tcl_Eval(interp, "rename unknown _original_unknown") == TCL_OK);
    NPNR_ASSERT(Tcl_Eval(interp, "proc unknown args {\n"
                                 "  set result [scan [lindex $args 0] \"%d\" value]\n"
                                 "  if { $result == 1 && [llength $args] == 1 } {\n"
                                 "    return \\[$value\\]\n"
                                 "  } else {\n"
                                 "    uplevel 1 [list _original_unknown {*}$args]\n"
                                 "  }\n"
                                 "}") == TCL_OK);
    Tcl_CreateObjCommand(interp, "get_ports", get_ports, ctx, nullptr);
    Tcl_CreateObjCommand(interp, "get_cells", get_cells, ctx, nullptr);
    Tcl_CreateObjCommand(interp, "set_property", set_property, ctx, nullptr);

    // Not implemented TCL commands
    Tcl_CreateObjCommand(interp, "create_clock", not_implemented, ctx, nullptr);
    Tcl_CreateObjCommand(interp, "get_clocks", not_implemented, ctx, nullptr);
    Tcl_CreateObjCommand(interp, "get_iobanks", not_implemented, ctx, nullptr);
    Tcl_CreateObjCommand(interp, "get_nets", not_implemented, ctx, nullptr);
    Tcl_CreateObjCommand(interp, "get_pins", not_implemented, ctx, nullptr);
    Tcl_CreateObjCommand(interp, "set_clock_groups", not_implemented, ctx, nullptr);
    Tcl_CreateObjCommand(interp, "set_false_path", not_implemented, ctx, nullptr);
    Tcl_CreateObjCommand(interp, "set_max_delay", not_implemented, ctx, nullptr);
}

TclInterp::~TclInterp() { Tcl_DeleteInterp(interp); }

NEXTPNR_NAMESPACE_END

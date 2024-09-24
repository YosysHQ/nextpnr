/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  NG-Ultra Architecture Implementation
 *
 *  Copyright (C) 2024  YosysHQ GmbH
 *
 */

#include "nextpnr.h"
#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/ng-ultra/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

std::unique_ptr<CellInfo> NgUltraPacker::create_cell(IdString type, IdString name)
{
    auto cell = std::make_unique<CellInfo>(ctx, name, type);

    auto add_port = [&](const IdString id, PortType dir) {
        cell->ports[id].name = id;
        cell->ports[id].type = dir;
    };
    if (type == id_BEYOND_FE) {
        add_port(id_I1, PORT_IN);
        add_port(id_I2, PORT_IN);
        add_port(id_I3, PORT_IN);
        add_port(id_I4, PORT_IN);
        add_port(id_LO, PORT_OUT);
        add_port(id_DI, PORT_IN);
        add_port(id_L, PORT_IN);
        add_port(id_CK, PORT_IN);
        add_port(id_R, PORT_IN);
        add_port(id_DO, PORT_OUT);
    } else {
        log_error("Trying to create unknown cell type %s\n", type.c_str(ctx));
    }
    return cell;
}

CellInfo *NgUltraPacker::create_cell_ptr(IdString type, IdString name)
{
    CellInfo *cell = ctx->createCell(name, type);

    auto add_port = [&](const IdString id, PortType dir) {
        cell->ports[id].name = id;
        cell->ports[id].type = dir;
    };
    if (type == id_BEYOND_FE) {
        add_port(id_I1, PORT_IN);
        add_port(id_I2, PORT_IN);
        add_port(id_I3, PORT_IN);
        add_port(id_I4, PORT_IN);
        add_port(id_LO, PORT_OUT);
        add_port(id_DI, PORT_IN);
        add_port(id_L, PORT_IN);
        add_port(id_CK, PORT_IN);
        add_port(id_R, PORT_IN);
        add_port(id_DO, PORT_OUT);
    } else if (type == id_BFR) {
        add_port(id_I, PORT_IN);
        add_port(id_O, PORT_OUT);
    } else if (type == id_DFR) {
        add_port(id_I, PORT_IN);
        add_port(id_O, PORT_OUT);
        add_port(id_L, PORT_IN);
        add_port(id_CK, PORT_IN);
        add_port(id_R, PORT_IN);
    } else if (type == id_DDFR) {
        add_port(id_I, PORT_IN);
        add_port(id_O, PORT_OUT);
        add_port(id_L, PORT_IN);
        add_port(id_CK, PORT_IN);
        add_port(id_R, PORT_IN);
        add_port(id_I2, PORT_IN);
        add_port(id_O2, PORT_OUT);
        add_port(id_CKF, PORT_IN);
    } else if (type == id_IOM) {
        add_port(id_P17RI, PORT_IN);
        add_port(id_CKO1, PORT_OUT);
        add_port(id_P19RI, PORT_IN);
        add_port(id_CKO2, PORT_OUT);
    } else if (type == id_WFB) {
        add_port(id_ZI, PORT_IN);
        add_port(id_ZO, PORT_OUT);
    } else if (type == id_GCK) {
        add_port(id_SI1, PORT_IN);
        add_port(id_SI2, PORT_IN);
        add_port(id_CMD, PORT_IN);
        add_port(id_SO, PORT_OUT);
    } else {
        log_error("Trying to create unknown cell type %s\n", type.c_str(ctx));
    }
    return cell;
}

NEXTPNR_NAMESPACE_END

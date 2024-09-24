/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  NG-Ultra Architecture Implementation
 *
 *  Copyright (C) 2024  YosysHQ GmbH
 *
 */

#include "nextpnr.h"
#include "ng_ultra.h"

#ifndef NG_ULTRA_LOCATION_MAP_H
#define NG_ULTRA_LOCATION_MAP_H

NEXTPNR_NAMESPACE_BEGIN

namespace ng_ultra {

Loc getNextLocInDSPChain(const NgUltraImpl *impl, Loc loc);
Loc getNextLocInCYChain(Loc loc);
Loc getNextLocInLUTChain(Loc loc);
Loc getNextLocInDFFChain(Loc loc);
Loc getCYFE(Loc root, int pos);
Loc getXLUTFE(Loc root, int pos);
Loc getXRFFE(Loc root, int pos);
Loc getCDCFE(Loc root, int pos);
Loc getFIFOFE(Loc root, int pos);

};

NEXTPNR_NAMESPACE_END
#endif

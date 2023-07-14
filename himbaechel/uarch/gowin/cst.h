#ifndef GOWIN_CST_H
#define GOWIN_CST_H

#include <fstream>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

bool gowin_apply_constraints(Context *ctx, std::istream &in);

NEXTPNR_NAMESPACE_END

#endif

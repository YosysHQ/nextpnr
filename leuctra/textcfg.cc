/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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
#include "textcfg.h"

NEXTPNR_NAMESPACE_BEGIN

void write_textcfg(const Context *ctx, std::ostream &out)
{
    out << "DEVICE " << ctx->args.device << " " << ctx->args.package << " " << ctx->args.speed << std::endl;

    for (auto &cell : ctx->cells) {
	auto &belid = cell.second->bel;
	auto &bel = ctx->getTileTypeBel(belid);
	auto name = IdString(bel.name_id);
	out << "PRIM " << belid.location.x << " " << belid.location.y << " " << name.str(ctx) << " " << cell.second->name.str(ctx) << std::endl;
	for (auto &attr : cell.second->attrs) {
	    out << "OPT " << attr.first.str(ctx) << " " << attr.second << std::endl;
	}
	for (auto &param : cell.second->params) {
	    out << "OPT " << param.first.str(ctx) << " " << param.second << std::endl;
	}
    }
    for (auto &net : ctx->nets) {
	out << "NET " << net.second->name.str(ctx) << std::endl;
	out << "FROM " << net.second->driver.cell->name.str(ctx) << " " << net.second->driver.port.str(ctx) << std::endl;
	for (auto &user : net.second->users) {
	    out << "TO " << user.cell->name.str(ctx) << " " << user.port.str(ctx) << std::endl;
	}
	for (auto &wire : net.second->wires) {
		auto &pip = wire.second.pip;
		if (pip != PipId() && pip.kind == PIP_KIND_PIP) {
		    WireId dst = ctx->getPipDstWire(pip);
		    WireId src = ctx->getPipSrcWire(pip);
		    out << "PIP " << pip.location.x << " " << pip.location.y << " " << ctx->getWireBasename(dst).str(ctx) << " " << ctx->getWireBasename(src).str(ctx) << std::endl;
		}
	}
    }
}

NEXTPNR_NAMESPACE_END

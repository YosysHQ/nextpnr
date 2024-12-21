/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include "gatemate.h"
#include "design_utils.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"

#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

GateMateImpl::~GateMateImpl() {};

void GateMateImpl::init_database(Arch *arch)
{
    const ArchArgs &args = arch->args;
    init_uarch_constids(arch);
    arch->load_chipdb(stringf("gatemate/chipdb-%s.bin", args.device.c_str()));
    arch->set_package("FBGA324");
    arch->set_speed_grade("DEFAULT");
}

void GateMateImpl::init(Context *ctx) { HimbaechelAPI::init(ctx); }

delay_t GateMateImpl::estimateDelay(WireId src, WireId dst) const
{
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);

    return 100 * (std::abs(dx - sx) / 4 + std::abs(dy - sy) / 4 + 2);
}

void GateMateImpl::postRoute()
{
    ctx->assignArchInfo();
    log_break();
    log_info("Resources spent on routing:\n");
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        for (auto &w : ni->wires) {
            if (w.second.pip != PipId()) {
                const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(
                        chip_pip_info(ctx->chip_info, w.second.pip).extra_data.get());
                if (!extra_data.name)
                    continue;
                if (extra_data.type == PipExtra::PIP_EXTRA_CPE) {
                    IdStringList id = ctx->getPipName(w.second.pip);
                    BelId bel = ctx->getBelByName(IdStringList::concat(id[0], id_CPE));
                    if (!ctx->getBoundBelCell(bel)) {
                        CellInfo *cell = ctx->createCell(ctx->id(ctx->nameOfBel(bel)), id_CPE);
                        ctx->bindBel(bel, cell, PlaceStrength::STRENGTH_FIXED);
                    }
                    CellInfo *cell = ctx->getBoundBelCell(bel);
                    if (IdString(extra_data.name) == id_RAM_O2) {
                        cell->params[id_INIT_L00] = Property(5, 4);  //"0101");
                        cell->params[id_INIT_L01] = Property(15, 4); // Property("1111");
                        cell->params[id_INIT_L02] = Property(15, 4); // Property("1111");
                        cell->params[id_INIT_L03] = Property(15, 4); // Property("1111");
                        cell->params[id_INIT_L10] = Property(8, 4);  // Property("1000");
                        cell->params[id_INIT_L20] = Property(12, 4); // Property("1100");
                        cell->params[ctx->id("O2")] = Property(3, 2);
                        cell->params[id_RAM_O2] = Property(1, 1);
                    }
                }
            }
        }
    }

    print_utilisation(ctx);

    const ArchArgs &args = ctx->args;
    if (args.options.count("out")) {
        write_bitstream(args.device, args.options.at("out"));
    }
}

void GateMateImpl::setupArchContext()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("read")) {
        if (!read_bitstream(args.device, args.options.at("read")))
            log_error("Loading bitstream failed.\n");
    }
}

// Bel bucket functions
IdString GateMateImpl::getBelBucketForCellType(IdString cell_type) const
{
    if (cell_type.in(id_CC_IBUF, id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF, id_CC_LVDS_IBUF, id_CC_LVDS_TOBUF,
                     id_CC_LVDS_OBUF, id_CC_LVDS_IOBUF))
        return id_GPIO;
    else
        return cell_type;
}

bool GateMateImpl::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type == id_GPIO)
        return cell_type.in(id_CC_IBUF, id_CC_OBUF, id_CC_TOBUF, id_CC_IOBUF, id_CC_LVDS_IBUF, id_CC_LVDS_TOBUF,
                            id_CC_LVDS_OBUF, id_CC_LVDS_IOBUF);
    else
        return (bel_type == cell_type);
}

struct GateMateArch : HimbaechelArch
{
    GateMateArch() : HimbaechelArch("gatemate") {};
    bool match_device(const std::string &device) override
    {
        return device.size() > 6 && device.substr(0, 6) == "CCGM1A";
    }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device, const dict<std::string, std::string> &args)
    {
        return std::make_unique<GateMateImpl>();
    }
} gateMateArch;

NEXTPNR_NAMESPACE_END

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

bool GateMateImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    CellInfo *cell = ctx->getBoundBelCell(bel);
    if (cell == nullptr) {
        return true;
    }
    if (ctx->getBelType(bel) == id_CPE) {
        Loc loc = ctx->getBelLocation(bel);
        int x = loc.x - 2;
        int y = loc.y - 2;
        if (x < 2 || x > 167)
            return false;
        if (y < 2 || y > 127)
            return false;
        return true;
    }
    return true;
}

void updateLUT(Context *ctx, CellInfo *cell, IdString port, IdString init)
{
    if (cell->params.count(init) == 0) return;
    unsigned init_val = int_or_default(cell->params, init);
    WireId pin_wire = ctx->getBelPinWire(cell->bel, port);
    for (PipId pip : ctx->getPipsUphill(pin_wire)) {
        if (!ctx->getBoundPipNet(pip))
            continue;
        const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(
                chip_pip_info(ctx->chip_info, pip).extra_data.get());
        if (!extra_data.name)
            continue;
        if (extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_CPE_INV)) {
            if (port.in(id_IN1,id_IN3,id_IN5,id_IN7))
                init_val = (init_val & 0b1010) >> 1 | (init_val & 0b0101) << 1;
            else
                init_val = (init_val & 0b0011) << 2 | (init_val & 0b1100) >> 2;
            cell->params[init] = Property(init_val, 4);
        }
    }
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
                        if (IdString(extra_data.name) == id_RAM_O2) {
                            cell->params[id_INIT_L00] = Property(0b1010, 4);
                            cell->params[id_INIT_L01] = Property(0b1111, 4);
                            cell->params[id_INIT_L02] = Property(0b1111, 4);
                            cell->params[id_INIT_L03] = Property(0b1111, 4);
                            cell->params[id_INIT_L10] = Property(0b1000, 4);
                            cell->params[id_INIT_L20] = Property(0b1100, 4);
                            cell->params[id_O2] = Property(0b11, 2);
                            cell->params[id_RAM_O2] = Property(1, 1);
                        }
                    } else
                        log_error("Issue adding pass trough signal.\n");
                }
            }
        }
    }


    for (auto &cell : ctx->cells) {
        if (cell.second->type == id_CPE) {
            // if LUT part used
            updateLUT(ctx, cell.second.get(), id_IN1, id_INIT_L00);
            updateLUT(ctx, cell.second.get(), id_IN2, id_INIT_L00);
            updateLUT(ctx, cell.second.get(), id_IN3, id_INIT_L01);
            updateLUT(ctx, cell.second.get(), id_IN4, id_INIT_L01);
            updateLUT(ctx, cell.second.get(), id_IN5, id_INIT_L02);
            updateLUT(ctx, cell.second.get(), id_IN6, id_INIT_L02);
            updateLUT(ctx, cell.second.get(), id_IN7, id_INIT_L03);
            updateLUT(ctx, cell.second.get(), id_IN8, id_INIT_L03);
        }
    }

    print_utilisation(ctx);

    const ArchArgs &args = ctx->args;
    if (args.options.count("out")) {
        write_bitstream(args.device, args.options.at("out"));
    }
}

void GateMateImpl::postPlace()
{
    log_break();
    log_info("Limiting routing...\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (ci.type == id_CPE) {
            if (!ci.params.count(id_FUNCTION)) continue;
            uint8_t func = int_or_default(ci.params, id_FUNCTION, 0);
            if (func!=4) continue; // Skip all that are not MUX
            for (int i = 1; i <= 4; i++) {
                IdString port = ctx->idf("IN%d", i);
                NetInfo *net = ci.getPort(port);
                if (!net)
                    continue;
                WireId dwire = ctx->getBelPinWire(ci.bel, port);
                for (PipId pip : ctx->getPipsUphill(dwire)) {
                    const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(
                            chip_pip_info(ctx->chip_info, pip).extra_data.get());
                    if (!extra_data.name)
                        continue;
                    if (extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_CPE_INV)) {
                        blocked_pips.emplace(pip);
                    }
                }
            }
        }
    }
    ctx->assignArchInfo();
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

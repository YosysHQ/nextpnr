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

#include <fstream>
#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "himbaechel_helpers.h"

#include "config.h"
#include "gatemate.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#define HIMBAECHEL_GFXIDS "uarch/gatemate/gfxids.inc"
#define HIMBAECHEL_UARCH gatemate

#include "himbaechel_constids.h"
#include "himbaechel_gfxids.h"

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

void GateMateImpl::init(Context *ctx)
{
    h.init(ctx);
    HimbaechelAPI::init(ctx);
}

void GateMateImpl::drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc)
{
    GraphicElement el;
    el.type = GraphicElement::TYPE_BOX;
    el.style = style;
    switch (bel_type.index) {
    case id_CPE.index:
        el.x1 = loc.x + 0.70;
        el.x2 = el.x1 + 0.20;
        el.y1 = loc.y + 0.55;
        el.y2 = el.y1 + 0.40;
        g.push_back(el);
        break;
    case id_GPIO.index:
        el.x1 = loc.x + 0.20;
        el.x2 = el.x1 + 0.60;
        el.y1 = loc.y + 0.20;
        el.y2 = el.y1 + 0.60;
        g.push_back(el);
        break;
    }
}

void GateMateImpl::pack()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("ccf")) {
        parse_ccf(args.options.at("ccf"));
    }
    // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
    for (auto &port : ctx->ports) {
        if (!ctx->cells.count(port.first))
            log_error("Port '%s' doesn't seem to have a corresponding top level IO\n", ctx->nameOf(port.first));
        CellInfo *ci = ctx->cells.at(port.first).get();

        PortRef top_port;
        top_port.cell = nullptr;
        bool is_npnr_iob = false;

        if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
            // Might have an input buffer connected to it
            is_npnr_iob = true;
            NetInfo *o = ci->getPort(id_O);
            if (o == nullptr)
                ;
            else if (o->users.entries() > 1)
                log_error("Top level pin '%s' has multiple input buffers\n", ctx->nameOf(port.first));
            else if (o->users.entries() == 1)
                top_port = *o->users.begin();
        }
        if (ci->type == ctx->id("$nextpnr_obuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
            // Might have an output buffer connected to it
            is_npnr_iob = true;
            NetInfo *i = ci->getPort(id_I);
            if (i != nullptr && i->driver.cell != nullptr) {
                if (top_port.cell != nullptr)
                    log_error("Top level pin '%s' has multiple input/output buffers\n", ctx->nameOf(port.first));
                top_port = i->driver;
            }
            // Edge case of a bidirectional buffer driving an output pin
            if (i->users.entries() > 2) {
                log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
            } else if (i->users.entries() == 2) {
                if (top_port.cell != nullptr)
                    log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
                for (auto &usr : i->users) {
                    if (usr.cell->type == ctx->id("$nextpnr_obuf") || usr.cell->type == ctx->id("$nextpnr_iobuf"))
                        continue;
                    top_port = usr;
                    break;
                }
            }
        }
        if (!is_npnr_iob)
            log_error("Port '%s' doesn't seem to have a corresponding top level IO (internal cell type mismatch)\n",
                      ctx->nameOf(port.first));

        if (top_port.cell == nullptr) {
            log_info("Trimming port '%s' as it is unused.\n", ctx->nameOf(port.first));
        } else {
            // Copy attributes to real IO buffer
            for (auto &attrs : ci->attrs)
                top_port.cell->attrs[attrs.first] = attrs.second;
            for (auto &params : ci->params)
                top_port.cell->params[params.first] = params.second;

            // Make sure that top level net is set correctly
            port.second.net = top_port.cell->ports.at(top_port.port).net;
        }
        // Now remove the nextpnr-inserted buffer
        ci->disconnectPort(id_I);
        ci->disconnectPort(id_O);
        ctx->cells.erase(port.first);
    }

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_IBUF, id_CC_OBUF))
            continue;
        if (ci.type == id_CC_IBUF) {
            ci.renamePort(id_I, id_DI);
            ci.renamePort(id_Y, id_IN1);
            std::string loc = ci.params.at(ctx->id("LOC")).to_string();
            BelId bel = ctx->get_package_pin_bel(ctx->id(loc));
            ci.params[ctx->id("INIT")] =
                    Property("000000000000000000000001000000000000000000000000000000000000000001010000");
            ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_FIXED);
        }
        if (ci.type == id_CC_OBUF) {
            ci.renamePort(id_O, id_DO);
            ci.renamePort(id_A, id_OUT2);
            ci.params[ctx->id("INIT")] =
                    Property("000000000000000000000000000000000000000100000000000000010000100100000000");
            std::string loc = ci.params.at(ctx->id("LOC")).to_string();
            BelId bel = ctx->get_package_pin_bel(ctx->id(loc));
            ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_FIXED);
        }
        ci.type = id_GPIO;
    }
}

delay_t GateMateImpl::estimateDelay(WireId src, WireId dst) const
{
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);

    return 100 * (std::abs(dx - sx) / 4 + std::abs(dy - sy) / 4 + 2);
}

void get_bitstream_tile(int x, int y, int &b_x, int &b_y)
{
    // Edge blocks are bit bigger
    if (x == -2)
        x++;
    if (x == 163)
        x--;
    if (y == -2)
        y++;
    if (y == 131)
        y--;
    b_x = (x + 1) / 2;
    b_y = (y + 1) / 2;
}

std::vector<bool> int_to_bitvector(int val, int size)
{
    std::vector<bool> bv;
    for (int i = 0; i < size; i++) {
        bv.push_back((val & (1 << i)) != 0);
    }
    return bv;
}

std::vector<bool> str_to_bitvector(std::string str, int size)
{
    std::vector<bool> bv;
    bv.resize(size, 0);
    for (int i = 0; i < int(str.size()); i++) {
        char c = str.at((str.size() - i) - 1);
        NPNR_ASSERT(c == '0' || c == '1');
        bv.at(i) = (c == '1');
    }
    return bv;
}

CfgLoc getConfigLoc(Context *ctx, int tile)
{
    int x0, y0;
    int bx, by;
    tile_xy(ctx->chip_info, tile, x0, y0);
    get_bitstream_tile(x0 - 2, y0 - 2, bx, by);
    CfgLoc loc;
    loc.die = 0;
    loc.x = bx;
    loc.y = by;
    return loc;
}

void GateMateImpl::postRoute()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("out")) {
        ChipConfig cc;
        cc.chip_name = args.device;
        cc.configs[0].add_word("GPIO.BANK_E1", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_E2", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_N1", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_N2", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_S1", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_S2", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_W1", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_W2", int_to_bitvector(1, 1));
        for (auto &cell : ctx->cells) {
            switch (cell.second->type.index) {
            case id_GPIO.index: {
                CfgLoc loc = getConfigLoc(ctx, cell.second.get()->bel.tile);
                cc.tiles[loc].add_word(
                        "GPIO.INIT",
                        str_to_bitvector(str_or_default(cell.second.get()->params, ctx->id("INIT"), ""), 72));
                break;
            }
            default:
                break;
            }
        }

        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (ni->wires.empty())
                continue;
            std::set<std::string> nets;
            for (auto &w : ni->wires) {
                if (w.second.pip != PipId()) {
                    PipId pip = w.second.pip;
                    const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(
                            chip_pip_info(ctx->chip_info, pip).extra_data.get());
                    if (extra_data.name != 0) {
                        IdString name = IdString(extra_data.name);
                        CfgLoc loc = getConfigLoc(ctx, pip.tile);
                        cc.tiles[loc].add_word(name.c_str(ctx), int_to_bitvector(extra_data.value, extra_data.bits));
                    }
                }
            }
        }
        std::ofstream out_config(args.options.at("out"));
        out_config << cc;
    }
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

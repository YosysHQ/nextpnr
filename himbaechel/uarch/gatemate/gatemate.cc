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

#include "gatemate.h"
#include "config.h"

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
    const pool<CellTypePort> top_ports{
            CellTypePort(id_CC_IBUF, id_I),
            CellTypePort(id_CC_OBUF, id_O),
    };
    h.remove_nextpnr_iobs(top_ports);

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_IBUF, id_CC_OBUF))
            continue;
        if (ci.type == id_CC_IBUF) {
            ci.renamePort(id_I, id_DI);
            ci.renamePort(id_Y, id_IN1);
            ci.params[ctx->id("INIT")] = Property("000000000000000000000001000000000000000000000000000000000000000001010000");
            // x=-2 y=99
            BelId bel = ctx->getBelByName(IdStringList::concat(ctx->idf("X%dY%d",-2+2,99+2), id_GPIO));
            ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_FIXED);
        }
        if (ci.type == id_CC_OBUF) {
            ci.renamePort(id_O, id_DO);
            ci.renamePort(id_A, id_OUT2);
            ci.params[ctx->id("INIT")] = Property("000000000000000000000000000000000000000100000000000000010000100100000000");
            // x=-2 y=95
            BelId bel = ctx->getBelByName(IdStringList::concat(ctx->idf("X%dY%d",-2+2,95+2), id_GPIO));
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

    return 100 * (std::abs(dx - sx)/4 + std::abs(dy - sy)/4 + 2);
}

void get_bitstream_tile(int x,int y,int &b_x,int &b_y)
{
    // Edge blocks are bit bigger
    if (x==-2) x++;
    if (x==163) x--;
    if (y==-2) y++;
    if (y==131) y--;
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
    get_bitstream_tile(x0 - 2,y0 - 2, bx, by);
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
        cc.configs[0].add_word("GPIO.BANK_E1", int_to_bitvector(1,1));
        cc.configs[0].add_word("GPIO.BANK_E2", int_to_bitvector(1,1));
        cc.configs[0].add_word("GPIO.BANK_N1", int_to_bitvector(1,1));
        cc.configs[0].add_word("GPIO.BANK_N2", int_to_bitvector(1,1));
        cc.configs[0].add_word("GPIO.BANK_S1", int_to_bitvector(1,1));
        cc.configs[0].add_word("GPIO.BANK_S2", int_to_bitvector(1,1));
        cc.configs[0].add_word("GPIO.BANK_W1", int_to_bitvector(1,1));
        cc.configs[0].add_word("GPIO.BANK_W2", int_to_bitvector(1,1));
        for (auto &cell : ctx->cells) {
            switch (cell.second->type.index) {
                case id_GPIO.index: {
                    CfgLoc loc = getConfigLoc(ctx, cell.second.get()->bel.tile);
                    cc.tiles[loc].add_word("GPIO.INIT", str_to_bitvector(str_or_default(cell.second.get()->params,ctx->id("INIT"),""), 72));
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
                    const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(chip_pip_info(ctx->chip_info, pip).extra_data.get());
                    if (extra_data.name!=0) {
                        IdString name = IdString(extra_data.name);
                        CfgLoc loc = getConfigLoc(ctx, pip.tile);
                        cc.tiles[loc].add_word(name.c_str(ctx),int_to_bitvector(extra_data.value, extra_data.bits));
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

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  gatecat <gatecat@ds0.me>
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

#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "himbaechel_helpers.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/example/constids.inc"
#define HIMBAECHEL_GFXIDS "uarch/example/gfxids.inc"
#define HIMBAECHEL_UARCH example
#include "himbaechel_constids.h"
#include "himbaechel_gfxids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct ExampleImpl : HimbaechelAPI
{

    static constexpr int K = 4;

    ~ExampleImpl() {};

    po::options_description getUarchOptions()
    {
        po::options_description specific("Example specific options");
        return specific;
    }

    void init_database(Arch *arch) override
    {
        init_uarch_constids(arch);
        arch->load_chipdb("example/chipdb-example.bin");
        arch->set_speed_grade("DEFAULT");
    }

    void init(Context *ctx) override
    {
        h.init(ctx);
        HimbaechelAPI::init(ctx);
    }

    void prePlace() override { assign_cell_info(); }

    void pack() override
    {
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
                CellTypePort(id_INBUF, id_PAD),
                CellTypePort(id_OUTBUF, id_PAD),
        };
        h.remove_nextpnr_iobs(top_ports);
        // Replace constants with LUTs
        const dict<IdString, Property> vcc_params = {{id_INIT, Property(0xFFFF, 16)}};
        const dict<IdString, Property> gnd_params = {{id_INIT, Property(0x0000, 16)}};
        h.replace_constants(CellTypePort(id_VCC_DRV, id_VCC), CellTypePort(id_GND_DRV, id_GND), {}, {}, id_VCC, id_GND);
        // Constrain directly connected LUTs and FFs together to use dedicated resources
        int lutffs = h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT4, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1);
        log_info("Constrained %d LUTFF pairs.\n", lutffs);
    }

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override
    {
        Loc l = ctx->getBelLocation(bel);
        if (ctx->getBelType(bel).in(id_LUT4, id_DFF)) {
            return slice_valid(l.x, l.y, l.z / 2);
        } else {
            return true;
        }
    }

    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override
    {
        if (cell_type.in(id_INBUF, id_OUTBUF))
            return id_IOB;
        return cell_type;
    }
    bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        IdString bel_type = ctx->getBelType(bel);
        if (bel_type == id_IOB)
            return cell_type.in(id_INBUF, id_OUTBUF);
        else
            return (bel_type == cell_type);
    }

  private:
    HimbaechelHelpers h;

    // Validity checking
    struct ExampleCellInfo
    {
        const NetInfo *lut_f = nullptr, *ff_d = nullptr;
        bool lut_i3_used = false;
    };
    std::vector<ExampleCellInfo> fast_cell_info;
    void assign_cell_info()
    {
        fast_cell_info.resize(ctx->cells.size());
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            auto &fc = fast_cell_info.at(ci->flat_index);
            if (ci->type == id_LUT4) {
                fc.lut_f = ci->getPort(id_F);
                fc.lut_i3_used = (ci->getPort(ctx->idf("I[%d]", K - 1)) != nullptr);
            } else if (ci->type == id_DFF) {
                fc.ff_d = ci->getPort(id_D);
            }
        }
    }
    bool slice_valid(int x, int y, int z) const
    {
        const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2)));
        const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2 + 1)));
        if (!lut || !ff)
            return true; // always valid if only LUT or FF used
        const auto &lut_data = fast_cell_info.at(lut->flat_index);
        const auto &ff_data = fast_cell_info.at(ff->flat_index);
        // In our example arch; the FF D can either be driven from LUT F or LUT I3
        // so either; FF D must equal LUT F or LUT I3 must be unused
        if (ff_data.ff_d == lut_data.lut_f)
            return true;
        if (lut_data.lut_i3_used)
            return false;
        return true;
    }

    void drawGroup(std::vector<GraphicElement> &g, GroupId group, Loc loc) override
    {
        IdString group_type = ctx->getGroupType(group);
        if (group_type == id_SWITCHBOX) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = GraphicElement::STYLE_FRAME;

            el.x1 = loc.x + 0.1;
            el.x2 = el.x1 + 0.4;
            el.y1 = loc.y + 0.1;
            el.y2 = el.y1 + 0.8;
            g.push_back(el);
        }
    }
    void drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc) override
    {
        GraphicElement el;
        el.type = GraphicElement::TYPE_BOX;
        el.style = style;
        switch (bel_type.index) {
        case id_LUT4.index:
            el.x1 = loc.x + 0.55;
            el.x2 = el.x1 + 0.15;
            el.y1 = loc.y + 0.90 - (loc.z / 2) * 0.1;
            el.y2 = el.y1 - 0.05;
            g.push_back(el);
            break;
        case id_DFF.index:
            el.x1 = loc.x + 0.75;
            el.x2 = el.x1 + 0.15;
            el.y1 = loc.y + 0.90 - (loc.z / 2) * 0.1;
            el.y2 = el.y1 - 0.05;
            g.push_back(el);
            break;
        case id_GND_DRV.index:
        case id_VCC_DRV.index:
        case id_IOB.index:
            el.x1 = loc.x + 0.55;
            el.x2 = el.x1 + 0.35;
            el.y1 = loc.y + 0.90 - loc.z * 0.40;
            el.y2 = el.y1 - 0.25;
            g.push_back(el);
            break;
        case id_BRAM_512X16.index:
            el.x1 = loc.x + 0.55;
            el.x2 = el.x1 + 0.35;
            el.y1 = loc.y + 0.90;
            el.y2 = el.y1 - 0.60;
            g.push_back(el);
            break;
        }
    }

    void drawWire(std::vector<GraphicElement> &g, GraphicElement::style_t style, Loc loc, IdString wire_type,
                  int32_t tilewire, IdString tile_type) override
    {
        GraphicElement el;
        el.type = GraphicElement::TYPE_LINE;
        el.style = style;
        int z;
        switch (tile_type.index) {
        case id_LOGIC.index:
            switch (wire_type.index) {
            case id_LUT_INPUT.index:
                z = (tilewire - GFX_WIRE_L0_I0) / 4;
                el.x1 = loc.x + 0.54;
                el.x2 = el.x1 + 0.01;
                el.y1 = loc.y + 0.90 - z * 0.1 - ((tilewire - GFX_WIRE_L0_I0) % 4 + 1) * 0.01;
                el.y2 = el.y1;
                g.push_back(el);
                break;
            case id_LUT_OUT.index:
                z = tilewire - GFX_WIRE_L0_O;
                el.x1 = loc.x + 0.70;
                el.x2 = el.x1 + 0.01;
                el.y1 = loc.y + 0.90 - z * 0.1 - 0.025;
                el.y2 = el.y1;
                g.push_back(el);
                break;
            case id_FF_DATA.index:
                z = tilewire - GFX_WIRE_L0_D;
                el.x1 = loc.x + 0.74;
                el.x2 = el.x1 + 0.01;
                el.y1 = loc.y + 0.90 - z * 0.1 - 0.025;
                el.y2 = el.y1;
                g.push_back(el);
                break;
            case id_FF_OUT.index:
                z = tilewire - GFX_WIRE_L0_Q;
                el.x1 = loc.x + 0.90;
                el.x2 = el.x1 + 0.01;
                el.y1 = loc.y + 0.90 - z * 0.1 - 0.025;
                el.y2 = el.y1;
                g.push_back(el);
                break;
            case id_TILE_CLK.index:
                for (int i = 0; i < 8; i++) {
                    GraphicElement el;
                    el.type = GraphicElement::TYPE_LINE;
                    el.style = style;
                    el.x1 = loc.x + 0.6;
                    el.x2 = el.x1;
                    el.y1 = loc.y + 0.90 - i * 0.1 - 0.05;
                    el.y2 = el.y1 - 0.05;
                    g.push_back(el);
                }
                break;
            }
            break;
        case id_BRAM.index:
            switch (wire_type.index) {
            case id_RAM_IN.index:
                z = tilewire - GFX_WIRE_RAM_WA0;
                el.x1 = loc.x + 0.54;
                el.x2 = el.x1 + 0.01;
                el.y1 = loc.y + 0.90 - z * 0.015 - 0.025;
                el.y2 = el.y1;
                g.push_back(el);
                break;
            case id_RAM_OUT.index:
                z = tilewire - GFX_WIRE_RAM_DO0;
                el.x1 = loc.x + 0.90;
                el.x2 = el.x1 + 0.01;
                el.y1 = loc.y + 0.90 - z * 0.015 - 0.025;
                el.y2 = el.y1;
                g.push_back(el);
                break;
            case id_TILE_CLK.index:
                el.x1 = loc.x + 0.60;
                el.x2 = el.x1;
                el.y1 = loc.y + 0.30;
                el.y2 = el.y1 - 0.025;
                g.push_back(el);
                break;
            }
            break;
        case id_IO.index:
            switch (wire_type.index) {
            case id_IO_I.index:
                break;
            case id_IO_O.index:
                break;
            case id_IO_T.index:
                break;
            case id_IO_PAD.index:
                break;
            case id_TILE_CLK.index:
                break;
            case id_GCLK.index:
                break;
            }
            break;
        case id_NULL.index:
            switch (wire_type.index) {
            case id_CLK_ROUTE.index:
                break;
            case id_GND.index:
                break;
            case id_VCC.index:
                break;
            case id_TILE_CLK.index:
                break;
            }
            break;
        }
    }

    void drawPip(std::vector<GraphicElement> &g, GraphicElement::style_t style, Loc loc, WireId src, IdString src_type,
                 int32_t src_id, WireId dst, IdString dst_type, int32_t dst_id) override
    {
        GraphicElement el;
        el.type = GraphicElement::TYPE_ARROW;
        el.style = style;
        int z;
        if (src_type == id_LUT_OUT && dst_type == id_FF_DATA) {
            z = src_id - GFX_WIRE_L0_O;
            el.x1 = loc.x + 0.45;
            el.y1 = loc.y + 0.85 - z * 0.1 - 0.025;
            el.x2 = loc.x + 0.50;
            el.y2 = el.y1;
            g.push_back(el);
        }
    }
};

struct ExampleArch : HimbaechelArch
{
    ExampleArch() : HimbaechelArch("example") {};
    bool match_device(const std::string &device) override { return device == "EXAMPLE"; }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device) override
    {
        return std::make_unique<ExampleImpl>();
    }
} exampleArch;
} // namespace

NEXTPNR_NAMESPACE_END

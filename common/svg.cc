/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  gatecat <gatecat@ds0.me>
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

#include <boost/algorithm/string.hpp>
#include <fstream>
#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN
namespace {
struct SVGWriter
{
    const Context *ctx;
    std::ostream &out;
    float scale = 500.0;
    bool hide_inactive = false;
    SVGWriter(const Context *ctx, std::ostream &out) : ctx(ctx), out(out){};
    const char *get_stroke_colour(GraphicElement::style_t style)
    {
        switch (style) {
        case GraphicElement::STYLE_GRID:
            return "#CCC";
        case GraphicElement::STYLE_FRAME:
            return "#808080";
        case GraphicElement::STYLE_INACTIVE:
            return "#C0C0C0";
        case GraphicElement::STYLE_ACTIVE:
            return "#FF3030";
        default:
            return "#000";
        }
    }

    void write_decal(const DecalXY &dxy)
    {
        for (const auto &el : ctx->getDecalGraphics(dxy.decal)) {
            if (el.style == GraphicElement::STYLE_HIDDEN ||
                (hide_inactive && el.style == GraphicElement::STYLE_INACTIVE))
                continue;
            switch (el.type) {
            case GraphicElement::TYPE_LINE:
            case GraphicElement::TYPE_ARROW:
            case GraphicElement::TYPE_LOCAL_LINE:
            case GraphicElement::TYPE_LOCAL_ARROW:
                out << stringf("<line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\" stroke=\"%s\"/>", (el.x1 + dxy.x) * scale,
                               (el.y1 + dxy.y) * scale, (el.x2 + dxy.x) * scale, (el.y2 + dxy.y) * scale,
                               get_stroke_colour(el.style))
                    << std::endl;
                break;
            case GraphicElement::TYPE_BOX:
                out << stringf("<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\" stroke=\"%s\" fill=\"%s\"/>",
                               (el.x1 + dxy.x) * scale, (el.y1 + dxy.y) * scale, (el.x2 - el.x1) * scale,
                               (el.y2 - el.y1) * scale, get_stroke_colour(el.style),
                               el.style == GraphicElement::STYLE_ACTIVE ? "#FF8080" : "none")
                    << std::endl;
                break;
            default:
                break;
            }
        }
    }

    void operator()(const std::string &flags)
    {
        std::vector<std::string> options;
        boost::algorithm::split(options, flags, boost::algorithm::is_space());
        bool noroute = false;
        for (const auto &opt : options) {
            if (boost::algorithm::starts_with(opt, "scale=")) {
                scale = float(std::stod(opt.substr(6)));
                continue;
            } else if (opt == "hide_routing") {
                noroute = true;
            } else if (opt == "hide_inactive") {
                hide_inactive = true;
            } else {
                log_error("Unknown SVG option '%s'\n", opt.c_str());
            }
        }
        float max_x = 0, max_y = 0;
        for (auto group : ctx->getGroups()) {
            auto decal = ctx->getGroupDecal(group);
            for (auto el : ctx->getDecalGraphics(decal.decal)) {
                max_x = std::max(max_x, decal.x + el.x1 + 1);
                max_y = std::max(max_y, decal.y + el.y1 + 1);
            }
        }
        for (auto bel : ctx->getBels()) {
            auto decal = ctx->getBelDecal(bel);
            for (auto el : ctx->getDecalGraphics(decal.decal)) {
                max_x = std::max(max_x, decal.x + el.x1 + 1);
                max_y = std::max(max_y, decal.y + el.y1 + 1);
            }
        }
        for (auto wire : ctx->getWires()) {
            auto decal = ctx->getWireDecal(wire);
            for (auto el : ctx->getDecalGraphics(decal.decal)) {
                max_x = std::max(max_x, decal.x + el.x1 + 1);
                max_y = std::max(max_y, decal.y + el.y1 + 1);
            }
        }
        for (auto pip : ctx->getPips()) {
            auto decal = ctx->getPipDecal(pip);
            for (auto el : ctx->getDecalGraphics(decal.decal)) {
                max_x = std::max(max_x, decal.x + el.x1 + 1);
                max_y = std::max(max_y, decal.y + el.y1 + 1);
            }
        }
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>" << std::endl;
        out << stringf("<svg viewBox=\"0 0 %f %f\" width=\"%f\" height=\"%f\" xmlns=\"http://www.w3.org/2000/svg\">",
                       max_x * scale, max_y * scale, max_x * scale, max_y * scale)
            << std::endl;
        out << "<rect x=\"0\" y=\"0\" width=\"100%\" height=\"100%\" stroke=\"#fff\" fill=\"#fff\"/>" << std::endl;
        for (auto group : ctx->getGroups())
            write_decal(ctx->getGroupDecal(group));
        for (auto bel : ctx->getBels())
            write_decal(ctx->getBelDecal(bel));
        if (!noroute) {
            for (auto wire : ctx->getWires())
                write_decal(ctx->getWireDecal(wire));
            for (auto pip : ctx->getPips())
                write_decal(ctx->getPipDecal(pip));
        }
        out << "</svg>" << std::endl;
    }
};
} // namespace

void Context::writeSVG(const std::string &filename, const std::string &flags) const
{
    std::ofstream out(filename);
    SVGWriter(this, out)(flags);
}

NEXTPNR_NAMESPACE_END

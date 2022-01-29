/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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
#include <iostream>

#include "gfx.h"

NEXTPNR_NAMESPACE_BEGIN

const int PIP_SRC_DST_LEN = 20;

static void get_pip_xy(CruSide side, float &off, float &x, float &y)
{
    switch (side) {
    case Top:
        x = off;
        y = cru_y + cru_h;
        break;
    case Bottom:
        x = off;
        y = cru_y;
        break;
    case Left:
        x = cru_x;
        y = off;
        break;
    case Right:
        x = cru_x + cru_w;
        y = off;
        break;
    case Center:
        x = cru_x + cru_w / 2.f;
        y = off;
        break;
    }
}

void gfxSetPipDefaultDecal(Arch *arch, PipInfo &pip)
{
    DecalXY active, inactive;
    std::vector<std::string> split_res;
    IdString src_loc_id, dst_loc_id;
    char buf[PIP_SRC_DST_LEN];

    active.x = inactive.x = pip.loc.x;
    active.y = inactive.y = arch->gridDimY - 1. - pip.loc.y;
    boost::split(split_res, pip.name.str(arch), [](char c) { return c == '_'; });
    src_loc_id = arch->id(split_res.at(1));
    dst_loc_id = arch->id(split_res.at(2));
    snprintf(buf, PIP_SRC_DST_LEN, "%s_%s_active", src_loc_id.c_str(arch), dst_loc_id.c_str(arch));
    IdString active_id = arch->id(buf);
    active.decal = active_id;
    snprintf(buf, PIP_SRC_DST_LEN, "%s_%s_inactive", src_loc_id.c_str(arch), dst_loc_id.c_str(arch));
    IdString inactive_id = arch->id(buf);
    inactive.decal = inactive_id;
    // create if absent
    if (arch->decal_graphics.count(active_id) == 0) {
        // clock?
        if (dst_loc_id == id_GT00 || dst_loc_id == id_GT10) {
            WireInfo &wi = arch->wire_info(pip.srcWire);
            if (wi.type.str(arch).substr(0, 3) != "UNK") {
                // create pip
                GraphicElement el;
                el.type = GraphicElement::TYPE_LOCAL_LINE;
                el.style = GraphicElement::STYLE_ACTIVE;
                if (dst_loc_id == id_GT00) {
                    el.x1 = WIRE_X(CLK_GT00_X);
                } else {
                    el.x1 = WIRE_X(CLK_GT10_X);
                }
                el.x2 = el.x1 + spine_pip_off;
                el.y2 = spineY.at(arch->wire_info(pip.srcWire).type);
                el.y1 = el.y2 - spine_pip_off;
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
            }
        } else {
            // XXX
            if (pipPoint.count(src_loc_id) == 0 || pipPoint.count(dst_loc_id) == 0) {
                // std::cout << "*R" << pip.loc.y + 1 << "C" << pip.loc.x + 1 << " no " << pip.name.str(arch) << " " <<
                // buf << std::endl;
            } else {
                GraphicElement el;
                el.type = GraphicElement::TYPE_LOCAL_ARROW;
                el.style = GraphicElement::STYLE_ACTIVE;
                CruSide srcSide = pipPoint.at(src_loc_id).first;
                float srcOff = pipPoint.at(src_loc_id).second;
                CruSide dstSide = pipPoint.at(dst_loc_id).first;
                float dstOff = pipPoint.at(dst_loc_id).second;
                if (srcSide != dstSide) {
                    get_pip_xy(srcSide, srcOff, el.x1, el.y1);
                    get_pip_xy(dstSide, dstOff, el.x2, el.y2);
                    arch->addDecalGraphic(active_id, el);
                    el.style = GraphicElement::STYLE_HIDDEN;
                    arch->addDecalGraphic(inactive_id, el);
                } else {
                    get_pip_xy(srcSide, srcOff, el.x1, el.y1);
                    float dst_x = 0, dst_y = 0, m_x = 0, m_y = 0;
                    get_pip_xy(dstSide, dstOff, dst_x, dst_y);
                    switch (dstSide) {
                    case Top:
                        m_x = el.x1 + (dst_x - el.x1) / 2.f;
                        m_y = dst_y - std::max(cru_h * 0.1f, std::min(cru_h * 0.4f, std::abs(el.x1 - dst_x)));
                        break;
                    case Bottom:
                        m_x = el.x1 + (dst_x - el.x1) / 2.f;
                        m_y = dst_y + std::max(cru_h * 0.1f, std::min(cru_h * 0.4f, std::abs(el.x1 - dst_x)));
                        break;
                    case Right:
                        m_x = dst_x - std::max(cru_w * 0.1f, std::min(cru_w * 0.4f, std::abs(el.y1 - dst_y)));
                        m_y = el.y1 + (dst_y - el.y1) / 2.f;
                        break;
                    case Left:
                        m_x = dst_x + std::max(cru_w * 0.1f, std::min(cru_w * 0.4f, std::abs(el.y1 - dst_y)));
                        m_y = el.y1 + (dst_y - el.y1) / 2.f;
                        break;
                    default: // unreachable
                        break;
                    }
                    el.x2 = m_x;
                    el.y2 = m_y;
                    arch->addDecalGraphic(active_id, el);
                    el.style = GraphicElement::STYLE_HIDDEN;
                    arch->addDecalGraphic(inactive_id, el);
                    el.style = GraphicElement::STYLE_ACTIVE;
                    el.x1 = m_x;
                    el.y1 = m_y;
                    el.x2 = dst_x;
                    el.y2 = dst_y;
                    arch->addDecalGraphic(active_id, el);
                    el.style = GraphicElement::STYLE_HIDDEN;
                    arch->addDecalGraphic(inactive_id, el);
                }
            }
        }
    }
    arch->setPipDecal(pip.name, active, inactive);
}

const int WIRE_ID_LEN = 30;

void gfxSetWireDefaultDecal(Arch *arch, WireInfo &wire)
{
    DecalXY active, inactive;
    IdString active_id, inactive_id;
    GraphicElement el;
    std::vector<std::string> split_res;
    char buf[WIRE_ID_LEN];

    if (std::find(decalless_wires.begin(), decalless_wires.end(), wire.name) != decalless_wires.end()) {
        arch->setWireDecal(wire.type, DecalXY(), DecalXY());
        return;
    }
    // local to cell
    if (arch->haveBelType(wire.x, wire.y, id_SLICE) && sliceLocalWires.count(wire.type) != 0) {
        snprintf(buf, sizeof(buf), "%s_active", wire.type.c_str(arch));
        active_id = arch->id(buf);
        active.decal = active_id;
        snprintf(buf, sizeof(buf), "%s_inactive", wire.type.c_str(arch));
        inactive_id = arch->id(buf);
        inactive.decal = inactive_id;
        active.x = inactive.x = wire.x;
        active.y = inactive.y = arch->gridDimY - 1. - wire.y;

        // create if absent
        if (arch->decal_graphics.count(active_id) == 0) {
            el.type = GraphicElement::TYPE_LOCAL_LINE;
            for (auto seg : sliceLocalWires.at(wire.type)) {
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = std::get<0>(seg);
                el.y1 = std::get<1>(seg);
                el.x2 = std::get<2>(seg);
                el.y2 = std::get<3>(seg);
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
            }
        }
        arch->setWireDecal(wire.name, active, inactive);
        return;
    }
    // spines
    if (spineY.count(wire.type) != 0) {
        snprintf(buf, sizeof(buf), "%s_active", wire.type.c_str(arch));
        active_id = arch->id(buf);
        active.decal = active_id;
        snprintf(buf, sizeof(buf), "%s_inactive", wire.type.c_str(arch));
        inactive_id = arch->id(buf);
        inactive.decal = inactive_id;
        active.x = inactive.x = 0.;
        active.y = inactive.y = 0.;

        // update clock spines cache
        arch->updateClockSpinesCache(wire.type, wire.name);

        if (arch->decal_graphics.count(active_id) == 0) {
            el.type = GraphicElement::TYPE_LINE;
            el.style = GraphicElement::STYLE_ACTIVE;
            el.x1 = 0.2;                                        // cell's x will be added later in fixClockSpineDecals
            el.x2 = 0.7;                                        // cell's x will be added later in fixClockSpineDecals
            el.y1 = spineY.at(wire.type) + arch->gridDimY - 1.; // cell's y will be added later in fixClockSpineDecals
            el.y2 = el.y1;
            arch->addDecalGraphic(active_id, el);
            el.style = GraphicElement::STYLE_HIDDEN;
            arch->addDecalGraphic(inactive_id, el);
        }
        arch->setWireDecal(wire.name, active, inactive);
        return;
    }

    // global simple wires like IMUX
    if (globalSimpleWires.count(wire.type) != 0) {
        snprintf(buf, sizeof(buf), "%s_active", wire.name.c_str(arch));
        active_id = arch->id(buf);
        active.decal = active_id;
        snprintf(buf, sizeof(buf), "%s_inactive", wire.name.c_str(arch));
        inactive_id = arch->id(buf);
        inactive.decal = inactive_id;
        active.x = inactive.x = 0;
        active.y = inactive.y = 0;

        // create if absent
        if (arch->decal_graphics.count(active_id) == 0) {
            el.type = GraphicElement::TYPE_LINE;
            for (auto seg : globalSimpleWires.at(wire.type)) {
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = std::get<0>(seg) + wire.x;
                el.y1 = std::get<1>(seg) + arch->gridDimY - 1. - wire.y;
                el.x2 = std::get<2>(seg) + wire.x;
                el.y2 = std::get<3>(seg) + arch->gridDimY - 1. - wire.y;
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
            }
        }
        arch->setWireDecal(wire.name, active, inactive);
        return;
    }

    // global
    boost::split(split_res, wire.name.str(arch), [](char c) { return c == '_'; });
    if (split_res.size() >= 2) {
        IdString wire_id = arch->id(split_res.at(1));
        // wrap
        if ((wire.y == (arch->gridDimY - 1) && split_res.at(1).at(0) == 'S') ||
            (wire.y == 0 && split_res.at(1).at(0) == 'N')) {
            wire_id = arch->id(split_res.at(1) + "_loop0");
        }
        if ((wire.x == (arch->gridDimX - 1) && split_res.at(1).at(0) == 'E') ||
            (wire.x == 0 && split_res.at(1).at(0) == 'W')) {
            wire_id = arch->id(split_res.at(1) + "_loop0");
        }
        // SN wires
        if (split_res.at(1).substr(0, 2) == "SN") {
            if (wire.y == 0) {
                wire_id = arch->id(split_res.at(1) + "_loop_n");
            } else {
                if (wire.y == (arch->gridDimY - 1)) {
                    wire_id = arch->id(split_res.at(1) + "_loop_s");
                }
            }
        } else {
            // wrap 2 hop
            if ((wire.y == (arch->gridDimY - 2) && split_res.at(1).substr(0, 2) == "S2") ||
                (wire.y == 1 && split_res.at(1).substr(0, 2) == "N2")) {
                wire_id = arch->id(split_res.at(1) + "_loop1");
            }
            // wrap 4 hop
            if (split_res.at(1).substr(0, 2) == "S8" || split_res.at(1).substr(0, 2) == "N8") {
                char loop_buf[5 + 2];
                if (split_res.at(1).substr(0, 2) == "N8") {
                    if (wire.y < 8) {
                        snprintf(loop_buf, sizeof(loop_buf), "_loop%1u", wire.y);
                        wire_id = arch->id(split_res.at(1) + loop_buf);
                    }
                } else {
                    if (arch->gridDimY - 1 - wire.y < 8) {
                        snprintf(loop_buf, sizeof(loop_buf), "_loop%1u", arch->gridDimY - 1 - wire.y);
                        wire_id = arch->id(split_res.at(1) + loop_buf);
                    }
                }
            }
        }
        // EW wires
        if (split_res.at(1).substr(0, 2) == "EW") {
            if (wire.x == 0) {
                wire_id = arch->id(split_res.at(1) + "_loop_w");
            } else {
                if (wire.x == (arch->gridDimX - 1)) {
                    wire_id = arch->id(split_res.at(1) + "_loop_e");
                }
            }
        } else {
            // wrap 2 hop
            if ((wire.x == (arch->gridDimX - 2) && split_res.at(1).substr(0, 2) == "E2") ||
                (wire.x == 1 && split_res.at(1).substr(0, 2) == "W2")) {
                wire_id = arch->id(split_res.at(1) + "_loop1");
            }
            // wrap 4 hop
            if (split_res.at(1).substr(0, 2) == "E8" || split_res.at(1).substr(0, 2) == "W8") {
                char loop_buf[5 + 2];
                if (split_res.at(1).substr(0, 2) == "W8") {
                    if (wire.x < 8) {
                        snprintf(loop_buf, sizeof(loop_buf), "_loop%1u", wire.x);
                        wire_id = arch->id(split_res.at(1) + loop_buf);
                    }
                } else {
                    if (arch->gridDimX - 1 - wire.x < 8) {
                        snprintf(loop_buf, sizeof(loop_buf), "_loop%1u", arch->gridDimX - 1 - wire.x);
                        wire_id = arch->id(split_res.at(1) + loop_buf);
                    }
                }
            }
        }
        // really create decal
        if (globalWires.count(wire_id) != 0) {
            snprintf(buf, sizeof(buf), "%s_active", wire.name.c_str(arch));
            active_id = arch->id(buf);
            active.decal = active_id;
            snprintf(buf, sizeof(buf), "%s_inactive", wire.name.c_str(arch));
            inactive_id = arch->id(buf);
            inactive.decal = inactive_id;
            active.x = inactive.x = 0;
            active.y = inactive.y = 0;

            // create if absent
            if (arch->decal_graphics.count(active_id) == 0) {
                el.type = GraphicElement::TYPE_LINE;
                for (auto seg : globalWires.at(wire_id)) {
                    el.style = GraphicElement::STYLE_ACTIVE;
                    el.x1 = std::get<0>(seg) + wire.x;
                    el.y1 = std::get<1>(seg) + arch->gridDimY - 1. - wire.y;
                    el.x2 = std::get<2>(seg) + wire.x;
                    el.y2 = std::get<3>(seg) + arch->gridDimY - 1. - wire.y;
                    arch->addDecalGraphic(active_id, el);
                    el.style = GraphicElement::STYLE_INACTIVE;
                    arch->addDecalGraphic(inactive_id, el);
                }
            }
            arch->setWireDecal(wire.name, active, inactive);
            return;
        }
        // clock branches
        // # of rows is unknown so generate wire ids at runtime
        if (split_res.at(1).substr(0, 3) == "GBO") {
            snprintf(buf, sizeof(buf), "%s_active", wire.name.c_str(arch));
            active_id = arch->id(buf);
            active.decal = active_id;
            inactive_id = IdString();
            inactive.decal = inactive_id;
            active.x = inactive.x = 0;
            active.y = inactive.y = 0;

            float pip_x = PIP_X(id_GBO0);
            float line_y = WIRE_Y(CLK_GBO0_Y) + arch->gridDimY - 1. - wire.y;
            float line_0 = WIRE_Y(0) + arch->gridDimY - 1. - wire.y;
            if (split_res.at(1).at(3) == '1') {
                pip_x = PIP_X(id_GBO1);
                line_y = WIRE_Y(CLK_GBO1_Y) + arch->gridDimY - 1. - wire.y;
            }

            // create if absent
            if (arch->decal_graphics.count(active_id) == 0) {
                el.type = GraphicElement::TYPE_LINE;
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = wire.x + pip_x;
                el.y1 = line_y;
                el.x2 = el.x1;
                el.y2 = line_0;
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_HIDDEN;
                arch->addDecalGraphic(inactive_id, el);

                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = pip_x;
                el.y1 = line_y;
                el.x2 = pip_x + arch->gridDimX - 1.;
                el.y2 = el.y1;
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_HIDDEN;
                arch->addDecalGraphic(inactive_id, el);
            }
            arch->setWireDecal(wire.name, active, inactive);
            return;
        } else {
            if (split_res.at(1).substr(0, 2) == "GT") {
                snprintf(buf, sizeof(buf), "%s_active", wire.name.c_str(arch));
                active_id = arch->id(buf);
                active.decal = active_id;
                // snprintf(buf, sizeof(buf), "%s_inactive", wire.name.c_str(arch));
                // inactive_id = arch->id(buf);
                inactive_id = IdString();
                inactive.decal = inactive_id;
                active.x = inactive.x = 0;
                active.y = inactive.y = 0;

                float pip_y = PIP_Y(id_GT00);
                float line_x = WIRE_X(CLK_GT00_X) + wire.x;
                float line_0 = WIRE_X(0) + wire.x;
                if (split_res.at(1).at(2) == '1') {
                    pip_y = PIP_Y(id_GT10);
                    line_x = WIRE_X(CLK_GT10_X) + wire.x;
                }

                // create if absent
                if (arch->decal_graphics.count(active_id) == 0) {
                    el.type = GraphicElement::TYPE_LINE;
                    el.style = GraphicElement::STYLE_ACTIVE;
                    el.x1 = line_x;
                    el.y1 = pip_y + arch->gridDimY - 1.;
                    el.x2 = el.x1;
                    el.y2 = pip_y;
                    arch->addDecalGraphic(active_id, el);
                    el.style = GraphicElement::STYLE_HIDDEN;
                    arch->addDecalGraphic(inactive_id, el);

                    for (int i = 0; i <= arch->gridDimY - 1; ++i) {
                        el.style = GraphicElement::STYLE_ACTIVE;
                        el.x1 = line_x;
                        el.y1 = pip_y + arch->gridDimY - 1. - i;
                        el.x2 = line_0;
                        el.y2 = el.y1;
                        arch->addDecalGraphic(active_id, el);
                        el.style = GraphicElement::STYLE_HIDDEN;
                        arch->addDecalGraphic(inactive_id, el);
                    }
                }
                arch->setWireDecal(wire.name, active, inactive);
                return;
            } else {
                if (split_res.at(1).substr(0, 2) == "GB") {
                    snprintf(buf, sizeof(buf), "%s_active", wire.name.c_str(arch));
                    active_id = arch->id(buf);
                    active.decal = active_id;
                    snprintf(buf, sizeof(buf), "%s_inactive", wire.name.c_str(arch));
                    inactive_id = arch->id(buf);
                    inactive.decal = inactive_id;
                    active.x = inactive.x = 0;
                    active.y = inactive.y = 0;

                    float line_y = WIRE_Y(CLK_GBO0_Y) + arch->gridDimY - 1. - wire.y;
                    float line_0 = WIRE_Y(0) + arch->gridDimY - 1. - wire.y;
                    float pip_x = PIP_X(arch->id(split_res.at(1)));
                    if (split_res.at(1).at(2) >= '4') {
                        line_y = WIRE_Y(CLK_GBO1_Y) + arch->gridDimY - 1. - wire.y;
                    }

                    // create if absent
                    if (arch->decal_graphics.count(active_id) == 0) {
                        el.type = GraphicElement::TYPE_LINE;
                        el.style = GraphicElement::STYLE_ACTIVE;
                        el.x1 = wire.x + pip_x;
                        el.y1 = line_y;
                        el.x2 = el.x1;
                        el.y2 = line_0;
                        arch->addDecalGraphic(active_id, el);
                        el.style = GraphicElement::STYLE_INACTIVE;
                        arch->addDecalGraphic(inactive_id, el);
                    }
                    arch->setWireDecal(wire.name, active, inactive);
                    return;
                }
            }
        }
    }
    // std::cout << wire.name.str(arch) << ":" << wire.type.str(arch) << " R" << wire.y + 1 << "C" << wire.x + 1 <<
    // std::endl;
}

void gfxCreateBelDecals(Arch *arch)
{
    GraphicElement el;
    // LUTs
    el.type = GraphicElement::TYPE_BOX;
    el.style = GraphicElement::STYLE_ACTIVE;
    el.x1 = lut_x;
    el.x2 = el.x1 + lut_w;
    el.y1 = 0.;
    el.y2 = el.y1 + lut_h;
    arch->addDecalGraphic(id_DECAL_LUT_ACTIVE, el);
    arch->addDecalGraphic(id_DECAL_LUTDFF_ACTIVE, el);
    arch->addDecalGraphic(id_DECAL_LUT_UNUSED_DFF_ACTIVE, el);
    arch->addDecalGraphic(id_DECAL_ALU_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    arch->addDecalGraphic(id_DECAL_LUT_INACTIVE, el);
    arch->addDecalGraphic(id_DECAL_LUTDFF_INACTIVE, el);
    el.x1 = dff_x;
    el.x2 = el.x1 + dff_w;
    el.y1 = 0.;
    el.y2 = el.y1 + lut_h;
    arch->addDecalGraphic(id_DECAL_LUTDFF_INACTIVE, el);
    arch->addDecalGraphic(id_DECAL_LUT_UNUSED_DFF_ACTIVE, el);
    arch->addDecalGraphic(id_DECAL_ALU_ACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_LUTDFF_ACTIVE, el);
    el.type = GraphicElement::TYPE_LOCAL_LINE;
    el.x1 = lut_x + 0.33f * lut_w;
    el.x2 = el.x1 + 0.33f * lut_w;
    el.y1 = 0.66f * lut_h;
    el.y2 = el.y1;
    arch->addDecalGraphic(id_DECAL_ALU_ACTIVE, el);
    el.y1 = 0.3f * lut_h;
    el.y2 = el.y1;
    arch->addDecalGraphic(id_DECAL_ALU_ACTIVE, el);
    el.x1 = lut_x + 0.5f * lut_w;
    el.x2 = el.x1;
    el.y1 = 0.5f * lut_h;
    el.y2 = el.y1 + 0.33f * lut_h;
    arch->addDecalGraphic(id_DECAL_ALU_ACTIVE, el);

    // LUT group
    el.type = GraphicElement::TYPE_BOX;
    el.style = GraphicElement::STYLE_FRAME;
    el.x1 = grp_lut_x;
    el.x2 = el.x1 + grp_lut_w;
    el.y1 = 0.;
    el.y2 = el.y1 + grp_lut_h;
    arch->addDecalGraphic(id_DECAL_GRP_LUT, el);

    // CRU group
    el.type = GraphicElement::TYPE_BOX;
    el.style = GraphicElement::STYLE_FRAME;
    el.x1 = cru_x;
    el.x2 = el.x1 + cru_w;
    el.y1 = cru_y;
    el.y2 = el.y1 + cru_h;
    arch->addDecalGraphic(id_DECAL_CRU, el);

    // Mux with upper 1 input
    el.type = GraphicElement::TYPE_LINE;
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = 0.;
    el.x2 = mux_w;
    el.y1 = 0.;
    el.y2 = mux_f;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = el.x2;
    el.y1 = el.y2;
    el.y2 = mux_h - mux_f;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x2 = 0.;
    el.y1 = el.y2;
    el.y2 = mux_h;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = el.x2;
    el.y1 = mux_h;
    el.y2 = 0.;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_ACTIVE, el);
    // 1
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = 0.0038;
    el.x2 = 0.0118;
    el.y1 = el.y2 = 0.0598;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = (el.x1 + el.x2) / 2.;
    el.x2 = el.x1;
    el.y2 = 0.0808;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x2 = 0.0038;
    el.y1 = el.y2;
    el.y2 = 0.0797;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXUPPER_ACTIVE, el);

    // Mux with lower 1 input
    el.type = GraphicElement::TYPE_LINE;
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = 0.;
    el.x2 = mux_w;
    el.y1 = 0.;
    el.y2 = mux_f;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = el.x2;
    el.y1 = el.y2;
    el.y2 = mux_h - mux_f;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x2 = 0.;
    el.y1 = el.y2;
    el.y2 = mux_h;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = el.x2;
    el.y1 = mux_h;
    el.y2 = 0.;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_ACTIVE, el);
    // 1
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = 0.0038;
    el.x2 = 0.0118;
    el.y1 = el.y2 = 0.0140;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = (el.x1 + el.x2) / 2.;
    el.x2 = el.x1;
    el.y2 = 0.0352;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x2 = 0.0038;
    el.y1 = el.y2;
    el.y2 = 0.0341;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_MUXLOWER_ACTIVE, el);

    // IOB
    el.type = GraphicElement::TYPE_LINE;
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = 0.;
    el.x2 = io_w;
    el.y1 = 0.;
    el.y2 = el.y1;
    arch->addDecalGraphic(id_DECAL_IOB_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOB_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = el.x2;
    el.y2 = io_h;
    arch->addDecalGraphic(id_DECAL_IOB_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOB_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = 0.;
    el.y1 = el.y2;
    arch->addDecalGraphic(id_DECAL_IOB_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOB_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x2 = el.x1;
    el.y2 = 0.;
    arch->addDecalGraphic(id_DECAL_IOB_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOB_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = io_w;
    el.x2 = io_w * 1.3f;
    el.y2 = el.y1 = io_h / 2.f;
    arch->addDecalGraphic(id_DECAL_IOB_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOB_ACTIVE, el);

    // IOBS
    el.type = GraphicElement::TYPE_LINE;
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = 0.;
    el.x2 = ios_w;
    el.y1 = 0.;
    el.y2 = el.y1;
    arch->addDecalGraphic(id_DECAL_IOBS_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOBS_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = el.x2;
    el.y2 = ios_h;
    arch->addDecalGraphic(id_DECAL_IOBS_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOBS_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = 0.;
    el.y1 = el.y2;
    arch->addDecalGraphic(id_DECAL_IOBS_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOBS_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x2 = el.x1;
    el.y2 = 0.;
    arch->addDecalGraphic(id_DECAL_IOBS_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOBS_ACTIVE, el);
    el.style = GraphicElement::STYLE_INACTIVE;
    el.x1 = ios_w;
    el.x2 = ios_w * 1.3f;
    el.y2 = el.y1 = ios_h / 2.f;
    arch->addDecalGraphic(id_DECAL_IOBS_INACTIVE, el);
    el.style = GraphicElement::STYLE_ACTIVE;
    arch->addDecalGraphic(id_DECAL_IOBS_ACTIVE, el);
}

void gfxSetBelDefaultDecal(Arch *arch, BelInfo &bel)
{
    DecalXY active, inactive;
    switch (bel.type.hash()) {
    case ID_SLICE:
        active.x = inactive.x = bel.x;
        active.y = inactive.y = arch->gridDimY - 1. - bel.y + lut_y[bel.z];
        if (bel.z < 6) {
            active.decal = id_DECAL_LUTDFF_ACTIVE;
            inactive.decal = id_DECAL_LUTDFF_INACTIVE;
        } else {
            active.decal = id_DECAL_LUT_ACTIVE;
            inactive.decal = id_DECAL_LUT_INACTIVE;
        }
        arch->setBelDecal(bel.name, active, inactive);
        break;
    case ID_GW_MUX2_LUT5:
        active.x = inactive.x = bel.x + mux2lut5_x;
        active.y = inactive.y = arch->gridDimY - 1. - bel.y + mux2lut5_y[(bel.z - arch->mux_0_z) >> 1];
        active.decal = id_DECAL_MUXUPPER_ACTIVE;
        inactive.decal = id_DECAL_MUXUPPER_INACTIVE;
        arch->setBelDecal(bel.name, active, inactive);
        break;
    case ID_GW_MUX2_LUT6:
        active.x = inactive.x = bel.x + mux2lut6_x;
        active.y = inactive.y = arch->gridDimY - 1. - bel.y + mux2lut6_y[(bel.z - arch->mux_0_z) / 5];
        active.decal = id_DECAL_MUXLOWER_ACTIVE;
        inactive.decal = id_DECAL_MUXLOWER_INACTIVE;
        arch->setBelDecal(bel.name, active, inactive);
        break;
    case ID_GW_MUX2_LUT7:
        active.x = inactive.x = bel.x + mux2lut7_x;
        active.y = inactive.y = arch->gridDimY - 1. - bel.y + mux2lut7_y;
        active.decal = id_DECAL_MUXLOWER_ACTIVE;
        inactive.decal = id_DECAL_MUXLOWER_INACTIVE;
        arch->setBelDecal(bel.name, active, inactive);
        break;
    case ID_GW_MUX2_LUT8:
        active.x = inactive.x = bel.x + mux2lut8_x;
        active.y = inactive.y = arch->gridDimY - 1. - bel.y + mux2lut8_y;
        active.decal = id_DECAL_MUXUPPER_ACTIVE;
        inactive.decal = id_DECAL_MUXUPPER_INACTIVE;
        arch->setBelDecal(bel.name, active, inactive);
        break;
    case ID_IOB:
        active.x = inactive.x = bel.x + io_x;
        active.y = inactive.y = arch->gridDimY - 1. - bel.y + io_y + bel.z * (2 * io_gap + io_h);
        active.decal = id_DECAL_IOB_ACTIVE;
        inactive.decal = id_DECAL_IOB_INACTIVE;
        arch->setBelDecal(bel.name, active, inactive);
        gfxSetIOBWireDecals(arch, bel);
        break;
    case ID_IOBS:
        active.x = inactive.x = bel.x + ios_x + (ios_w + ios_gap_x) * (bel.z % 3);
        active.y = inactive.y = arch->gridDimY - 1. - bel.y + ios_y + (ios_h + ios_gap_y) * (bel.z / 3);
        active.decal = id_DECAL_IOBS_ACTIVE;
        inactive.decal = id_DECAL_IOBS_INACTIVE;
        arch->setBelDecal(bel.name, active, inactive);
        gfxSetIOBSWireDecals(arch, bel);
        break;
    default:
        break;
    }
}

void gfxSetIOBWireDecals(Arch *arch, BelInfo &bel)
{
    DecalXY active, inactive;
    GraphicElement el;
    char buf[20];

    // set decals for I, O and OE input wires
    for (auto pi : bel.pins) {
        WireInfo &wi = arch->wire_info(pi.second.wire);
        // decal name: wire_port_z_active|inactive
        snprintf(buf, sizeof(buf), "%s_%s_%u_active", wi.type.c_str(arch), pi.first.c_str(arch), bel.z);
        IdString active_id = arch->id(buf);
        active.decal = active_id;
        snprintf(buf, sizeof(buf), "%s_%s_%u_inactive", wi.type.c_str(arch), pi.first.c_str(arch), bel.z);
        IdString inactive_id = arch->id(buf);
        inactive.decal = inactive_id;
        active.x = inactive.x = bel.x;
        active.y = inactive.y = arch->gridDimY - 1. - bel.y;
        if (arch->decal_graphics.count(active_id) == 0) {
            el.type = GraphicElement::TYPE_LOCAL_LINE;
            el.style = GraphicElement::STYLE_ACTIVE;
            el.x1 = cru_x + cru_w;
            el.y1 = pipPoint.at(wi.type).second;
            el.x2 = io_x;
            el.y2 = portPoint.at(pi.first) + io_y + bel.z * (2 * io_gap + io_h);
            arch->addDecalGraphic(active_id, el);
            el.style = GraphicElement::STYLE_INACTIVE;
            arch->addDecalGraphic(inactive_id, el);
            for (auto seg : portSign.at(pi.first)) {
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = std::get<0>(seg) + io_x;
                el.y1 = std::get<1>(seg) + io_y + bel.z * (2 * io_gap + io_h);
                el.x2 = std::get<2>(seg) + io_x;
                el.y2 = std::get<3>(seg) + io_y + bel.z * (2 * io_gap + io_h);
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
            }
        }
        arch->setWireDecal(wi.name, active, inactive);
    }
}

void gfxSetIOBSWireDecals(Arch *arch, BelInfo &bel)
{
    DecalXY active, inactive;
    GraphicElement el;
    char buf[20];

    // set decals for I, O and OE input wires
    for (auto pi : bel.pins) {
        WireInfo &wi = arch->wire_info(pi.second.wire);
        // decal name: ios_wire_port_z_active|inactive
        snprintf(buf, sizeof(buf), "ios_%s_%s_%u_active", wi.type.c_str(arch), pi.first.c_str(arch), bel.z);
        IdString active_id = arch->id(buf);
        active.decal = active_id;
        snprintf(buf, sizeof(buf), "ios_%s_%s_%u_inactive", wi.type.c_str(arch), pi.first.c_str(arch), bel.z);
        IdString inactive_id = arch->id(buf);
        inactive.decal = inactive_id;
        active.x = inactive.x = bel.x;
        active.y = inactive.y = arch->gridDimY - 1. - bel.y;
        if (arch->decal_graphics.count(active_id) == 0) {
            // leftmost wires
            el.type = GraphicElement::TYPE_LOCAL_LINE;
            if (bel.z % 3 == 0) {
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = cru_x + cru_w;
                el.y1 = pipPoint.at(wi.type).second;
                el.x2 = ios_x;
                el.y2 = ios_scl * portPoint.at(pi.first) + ios_y + (ios_h + ios_gap_y) * (bel.z / 3);
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
            } else {
                float col = (bel.z % 3) - 1;
                float rel_port = portPoint.at(pi.first) / io_h;
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = cru_x + cru_w;
                el.y1 = pipPoint.at(wi.type).second;
                el.x2 = ios_x * (0.97 - 0.02 * col);
                el.y2 = (rel_port + col) * 0.5 * ios_gap_y + ios_y + ios_h + (ios_h + ios_gap_y) * (bel.z / 3);
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = ios_x + (ios_w + ios_gap_x) * (col + 1) - ios_gap_x + ios_w * 0.3 +
                        rel_port * (ios_gap_x - 0.3 * ios_w);
                el.y1 = el.y2;
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x2 = el.x1;
                el.y2 = ios_scl * portPoint.at(pi.first) + ios_y + (ios_h + ios_gap_y) * (bel.z / 3);
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = ios_x + (ios_w + ios_gap_x) * (col + 1);
                el.y1 = el.y2;
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
                el.style = GraphicElement::STYLE_ACTIVE;
            }
            // signs
            for (auto seg : portSign.at(pi.first)) {
                el.style = GraphicElement::STYLE_ACTIVE;
                el.x1 = ios_scl * std::get<0>(seg) + ios_x + (ios_w + ios_gap_x) * (bel.z % 3);
                el.y1 = ios_scl * std::get<1>(seg) + ios_y + (ios_h + ios_gap_y) * (bel.z / 3);
                el.x2 = ios_scl * std::get<2>(seg) + ios_x + (ios_w + ios_gap_x) * (bel.z % 3);
                el.y2 = ios_scl * std::get<3>(seg) + ios_y + (ios_h + ios_gap_y) * (bel.z / 3);
                arch->addDecalGraphic(active_id, el);
                el.style = GraphicElement::STYLE_INACTIVE;
                arch->addDecalGraphic(inactive_id, el);
            }
        }
        arch->setWireDecal(wi.name, active, inactive);
    }
}

DecalXY gfxGetLutGroupDecalXY(int x, int y, int z)
{
    DecalXY decalxy;
    decalxy.decal = id_DECAL_GRP_LUT;
    decalxy.x = x;
    decalxy.y = y + grp_lut_y[z];
    return decalxy;
}

DecalXY gfxGetCruGroupDecalXY(int x, int y)
{
    DecalXY decalxy;
    decalxy.decal = id_DECAL_CRU;
    decalxy.x = x;
    decalxy.y = y;
    return decalxy;
}

NEXTPNR_NAMESPACE_END

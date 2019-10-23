/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2019  Miodrag Milanovic <miodrag@symbioticeda.com>
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

#include "gfx.h"

NEXTPNR_NAMESPACE_BEGIN

void gfxTileWire(std::vector<GraphicElement> &g, int x, int y, IdString wire_type, GfxTileWireId tilewire, GraphicElement::style_t style)
{
    if (wire_type == id_WIRE_TYPE_SLICE && tilewire != GfxTileWireId::TILE_WIRE_NONE) {
        GraphicElement el;
        el.type = GraphicElement::TYPE_LINE;
        el.style = style;
        if (tilewire >= TILE_WIRE_FCO_SLICE && tilewire <=TILE_WIRE_FCI_SLICE)
        {
            int gap = (tilewire - TILE_WIRE_FCO_SLICE) / 24;
            int item = (tilewire - TILE_WIRE_FCO_SLICE) % 24;
            el.x1 = x + slice_x1 - 0.005f;
            el.x2 = x + slice_x1;
            el.y1 = y + slice_y2 - 0.0017f * (tilewire - TILE_WIRE_FCO_SLICE + 1 + gap*2) + 3*slice_pitch;
            el.y2 = y + slice_y2 - 0.0017f * (tilewire - TILE_WIRE_FCO_SLICE + 1 + gap*2) + 3*slice_pitch;
            g.push_back(el);
            // FX to F connection - top
            if (item == (TILE_WIRE_FXD_SLICE-TILE_WIRE_FCO_SLICE))
            {
                el.x2 = el.x1;
                el.y2 = el.y1 - 0.0017f;
                g.push_back(el);
            }
            // F5 to F connection - bottom
            if (item == (TILE_WIRE_F5D_SLICE-TILE_WIRE_FCO_SLICE))
            {
                el.x2 = el.x1;
                el.y2 = el.y1 + 0.0017f;
                g.push_back(el);
            }
            // connection between slices
            if (item == (TILE_WIRE_FCID_SLICE-TILE_WIRE_FCO_SLICE) && tilewire!=TILE_WIRE_FCI_SLICE)
            {
                el.x2 = el.x1;
                el.y2 = el.y1 - 0.0017f * 3;
                g.push_back(el);
            }
        }
        if (tilewire >= TILE_WIRE_DUMMY_D2 && tilewire <=TILE_WIRE_WAD0A_SLICE)
        {
            int gap = (tilewire - TILE_WIRE_DUMMY_D2) / 12;
            el.x1 = x + slice_x2 + 0.005f;
            el.x2 = x + slice_x2;
            el.y1 = y + slice_y2 - 0.0017f * (tilewire - TILE_WIRE_DUMMY_D2 + 1 + gap*14) + 3*slice_pitch;
            el.y2 = y + slice_y2 - 0.0017f * (tilewire - TILE_WIRE_DUMMY_D2 + 1 + gap*14) + 3*slice_pitch;
            g.push_back(el);
        }
    }
    if (wire_type == id_WIRE_TYPE_V01) {
        if (tilewire >= TILE_WIRE_V01N0001 && tilewire <=TILE_WIRE_V01S0100)
        {
            GraphicElement el;
            el.type = GraphicElement::TYPE_LINE;
            el.style = style;
            el.x1 = x + switchbox_x2 - 0.0017f*16 + 0.0017f * (tilewire - TILE_WIRE_V01N0001);
            el.x2 = el.x1;
            el.y1 = y + switchbox_y1;
            el.y2 = y + switchbox_y2 - 1;
            g.push_back(el);
        } 
    }  
    if (wire_type == id_WIRE_TYPE_H01) {
        if (tilewire >= TILE_WIRE_H01E0001 && tilewire <=TILE_WIRE_HL7W0001)
        {
            GraphicElement el;
            el.type = GraphicElement::TYPE_LINE;
            el.style = style;
            el.x1 = x + switchbox_x1;
            el.x2 = x + switchbox_x2 - 1;
            el.y1 = y + switchbox_y1 + 0.0017f*16 - 0.0017f * (tilewire - TILE_WIRE_H01E0001);
            el.y2 = el.y1;
            g.push_back(el);
        } 
    }  
    if (wire_type == id_WIRE_TYPE_V00) {
        int group = (tilewire - TILE_WIRE_V00T0000) / 2;
        GraphicElement el;
        el.type = GraphicElement::TYPE_LINE;
        el.style = style;
        el.x1 = x + switchbox_x2 - 0.0017f*(8 - ((tilewire - TILE_WIRE_V00T0000) % 2)*4);
        el.x2 = el.x1;
        if (group) {
            el.y1 = y + switchbox_y1;
            el.y2 = y + switchbox_y1 - 0.0017f*4;
        } else {
            el.y1 = y + switchbox_y2;
            el.y2 = y + switchbox_y2 + 0.0017f*4;
        }
        g.push_back(el);
    }
    if (wire_type == id_WIRE_TYPE_H00) {
        int group = (tilewire - TILE_WIRE_H00L0000) / 2;
        GraphicElement el;
        el.type = GraphicElement::TYPE_LINE;
        el.style = style;
        el.y1 = y + switchbox_y1 + 0.0017f*(8 - ((tilewire - TILE_WIRE_H00L0000) % 2)*4);
        el.y2 = el.y1;

        if (group) {
            el.x1 = x + switchbox_x2 + 0.0017f*4;
            el.x2 = x + switchbox_x2;
        } else {
            el.x1 = x + switchbox_x1 - 0.0017f*4;
            el.x2 = x + switchbox_x1;
        }
        g.push_back(el);
    }
    if (wire_type == id_WIRE_TYPE_H02) {
        GraphicElement el;
        el.type = GraphicElement::TYPE_LINE;
        el.style = style;
        el.x1 = x + switchbox_x1 + 0.0017f*(16 + (tilewire - TILE_WIRE_H02W0701)+ 20 *(x%3));
        el.x2 = el.x1;
        el.y1 = y + switchbox_y1;
        el.y2 = y + switchbox_y1 - 0.0017f*(20 + (tilewire - TILE_WIRE_H02W0701)+ 20 *(x%3));
        g.push_back(el);

        el.x2 = (x+2) + switchbox_x1 + 0.0017f*(16 + (tilewire - TILE_WIRE_H02W0701)+ 20 *(x%3));
        el.y1 = el.y2;
        g.push_back(el);

        el.x2 = (x+1) + switchbox_x1 + 0.0017f*(16 + (tilewire - TILE_WIRE_H02W0701)+ 20 *(x%3));
        el.x1 = el.x2;
        el.y1 = y + switchbox_y1;
        g.push_back(el);

        el.x2 = (x+2) + switchbox_x1 + 0.0017f*(16 + (tilewire - TILE_WIRE_H02W0701)+ 20 *(x%3));
        el.x1 = el.x2;
        el.y1 = y + switchbox_y1;
        g.push_back(el);
    }
    if (wire_type == id_WIRE_TYPE_H06) {
        GraphicElement el;
        el.type = GraphicElement::TYPE_LINE;
        el.style = style;
        el.x1 = x + switchbox_x1 + 0.0017f*(96 + (tilewire - TILE_WIRE_H06W0303)+ 20 *(x%3));
        el.x2 = el.x1;
        el.y1 = y + switchbox_y1;
        el.y2 = y + switchbox_y1 - 0.0017f*(96 + (tilewire - TILE_WIRE_H06W0303)+ 20 *(x%3));
        g.push_back(el);

        el.x2 = (x+6) + switchbox_x1 + 0.0017f*(96 + (tilewire - TILE_WIRE_H06W0303)+ 20 *(x%3));
        el.y1 = el.y2;
        g.push_back(el);

        el.x2 = (x+3) + switchbox_x1 + 0.0017f*(96 + (tilewire - TILE_WIRE_H06W0303)+ 20 *(x%3));
        el.x1 = el.x2;
        el.y1 = y + switchbox_y1;
        g.push_back(el);

        el.x2 = (x+6) + switchbox_x1 + 0.0017f*(96 + (tilewire - TILE_WIRE_H06W0303)+ 20 *(x%3));
        el.x1 = el.x2;
        el.y1 = y + switchbox_y1;
        g.push_back(el);
    }

    if (wire_type == id_WIRE_TYPE_NONE) {
        if (tilewire >= TILE_WIRE_NBOUNCE && tilewire <=TILE_WIRE_SBOUNCE)
        {
            GraphicElement el;
            el.type = GraphicElement::TYPE_LINE;
            el.style = style;
            el.x1 = x + switchbox_x2 - 0.0017f*4;
            el.x2 = x + switchbox_x2 - 0.0017f*8;
            if (tilewire == TILE_WIRE_NBOUNCE) {
                el.y1 = y + switchbox_y2 + 0.0017f*4;
                el.y2 = el.y1;
            } else {
                el.y1 = y + switchbox_y1 - 0.0017f*4;
                el.y2 = el.y1;
            }
            g.push_back(el);
        }
        if (tilewire >= TILE_WIRE_WBOUNCE && tilewire <=TILE_WIRE_EBOUNCE)
        {
            GraphicElement el;
            el.type = GraphicElement::TYPE_LINE;
            el.style = style;
            el.y1 = y + switchbox_y1 + 0.0017f*4;
            el.y2 = y + switchbox_y1 + 0.0017f*8;
            if (tilewire == TILE_WIRE_WBOUNCE) {
                el.x1 = x + switchbox_x1 - 0.0017f*4;
                el.x2 = el.x1;
            } else {
                el.x1 = x + switchbox_x2 + 0.0017f*4;
                el.x2 = el.x1;
            }
            g.push_back(el);
        }            
        if (tilewire >= TILE_WIRE_CLK0 && tilewire <=TILE_WIRE_LSR1)
        {
            GraphicElement el;                
            el.type = GraphicElement::TYPE_LINE;
            el.style = style;
            el.x1 = x + switchbox_x2;
            el.x2 = x + slice_x2 + 0.0255f +  (8 - (tilewire - TILE_WIRE_CLK0)) * 0.0017f;
            el.y1 = y + slice_y2 - 0.0017f * (tilewire - TILE_WIRE_CLK0 - 5) + 3*slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);
            el.x1 = el.x2;
            el.y2 = y + slice_y2 - 0.0017f * (3 + (tilewire - TILE_WIRE_CLK0));
            g.push_back(el);
            for (int i=0;i<4;i++)
            {
                el.x1 = x + slice_x2 + 0.0255f + 0.0017f;
                el.x2 = x + slice_x2 + 0.0255f +  (8 - (tilewire - TILE_WIRE_CLK0)) * 0.0017f;
                el.y1 = y + slice_y2 - 0.0017f * (TILE_WIRE_CLK3_SLICE - TILE_WIRE_DUMMY_D2 + 1 + tilewire - TILE_WIRE_CLK0)+ i*slice_pitch;
                el.y2 = el.y1;
                g.push_back(el);
            }
            if (tilewire==TILE_WIRE_CLK1 || tilewire==TILE_WIRE_LSR1) {
                for (int i=0;i<2;i++)
                {
                    el.x1 = x + slice_x2 + 0.0051f;
                    el.x2 = x + slice_x2 + 0.0255f +  (8 - (tilewire - TILE_WIRE_CLK0)) * 0.0017f;
                    el.y1 = y + slice_y2 - 0.0017f * (TILE_WIRE_CLK3_SLICE - TILE_WIRE_DUMMY_D2 - 1 + (tilewire - TILE_WIRE_CLK0)/2)+ i*slice_pitch;
                    el.y2 = el.y1;
                    g.push_back(el);
                }
            }
        }       

        if (tilewire >= TILE_WIRE_FCO && tilewire <=TILE_WIRE_FCI)
        {
            int gap = (tilewire - TILE_WIRE_FCO) / 24;
            GraphicElement el;                
            el.type = GraphicElement::TYPE_LINE;
            el.style = style;
            el.x1 = x + switchbox_x2;
            el.x2 = x + slice_x1 - 0.005f;
            el.y1 = y + slice_y2 - 0.0017f * (tilewire - TILE_WIRE_FCO + 1 + gap*2) + 3*slice_pitch;
            el.y2 = y + slice_y2 - 0.0017f * (tilewire - TILE_WIRE_FCO + 1 + gap*2) + 3*slice_pitch;
            g.push_back(el);
        }       

        if (tilewire >= TILE_WIRE_MUXCLK3 && tilewire <=TILE_WIRE_MUXLSR0)
        {
            int gap = (tilewire - TILE_WIRE_MUXCLK3) / 2;
            int part = (tilewire - TILE_WIRE_MUXCLK3) % 2;
            GraphicElement el;
            el.type = GraphicElement::TYPE_LINE;
            el.style = style;
            el.x1 = x + slice_x2 + 0.0051f;
            el.x2 = x + slice_x2 + 0.0255f;
            el.y1 = y + slice_y2 - 0.0017f * (TILE_WIRE_CLK3_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part + gap*26) + 3*slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);
        }

        if (tilewire >= TILE_WIRE_WD3 && tilewire <=TILE_WIRE_WD0)
        {
            GraphicElement el;
            int part =  (tilewire - TILE_WIRE_WD3) % 4;
            int group =  (tilewire - TILE_WIRE_WD3) / 2;
            el.type = GraphicElement::TYPE_LINE;
            el.style = style;
            el.x1 = x + slice_x2 + 0.005f;
            el.x2 = x + slice_x2 + 0.005f + 0.0017f *(4 - part);
            el.y1 = y + slice_y2 - 0.0017f * (TILE_WIRE_WDO3C_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part + 14) + 3*slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);

            el.x1 = el.x2;
            el.y2 = y + slice_y2 - 0.0017f * (TILE_WIRE_WD1B_SLICE - TILE_WIRE_DUMMY_D2 + 1 + (part & 1) + 14*2) + (3-group)*slice_pitch;
            g.push_back(el);

            el.x1 = x + slice_x2 + 0.005f;
            el.y1 = el.y2;
            g.push_back(el);
        }
        if (tilewire >= TILE_WIRE_WAD3 && tilewire <=TILE_WIRE_WAD0)
        {
            GraphicElement el;
            int part =  (tilewire - TILE_WIRE_WAD3) % 4;
            el.type = GraphicElement::TYPE_LINE;
            el.style = style;
            el.x1 = x + slice_x2 + 0.005f;
            el.x2 = x + slice_x2 + 0.005f + 0.0017f *(8 - part);
            el.y1 = y + slice_y2 - 0.0017f * (TILE_WIRE_WADO3C_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part+ 14) + 3*slice_pitch;
            el.y2 = el.y1;
            g.push_back(el);

            el.x1 = el.x2;
            el.y2 = y + slice_y2 - 0.0017f * (TILE_WIRE_WAD3B_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part+ 14*2) + 2*slice_pitch;
            g.push_back(el);

            el.x1 = x + slice_x2 + 0.005f;
            el.y1 = el.y2;
            g.push_back(el);

            // middle line
            el.x1 = x + slice_x2 + 0.005f;
            el.x2 = x + slice_x2 + 0.005f + 0.0017f *(8 - part);
            el.y2 = y + slice_y2 - 0.0017f * (TILE_WIRE_WAD3B_SLICE - TILE_WIRE_DUMMY_D2 + 1 + part+ 14*2) + 3*slice_pitch;
            el.y1 = el.y2;
            g.push_back(el);
        }
    }

}

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

#include "chip.h"

// -----------------------------------------------------------------------

IdString belTypeToId(BelType type)
{
	if (type == TYPE_ICESTORM_LC)
		return "ICESTORM_LC";
	if (type == TYPE_ICESTORM_RAM)
		return "ICESTORM_RAM";
	if (type == TYPE_SB_IO)
		return "SB_IO";
	return IdString();
}

BelType belTypeFromId(IdString id)
{
	if (id == "ICESTORM_LC")
		return TYPE_ICESTORM_LC;
	if (id == "ICESTORM_RAM")
		return TYPE_ICESTORM_RAM;
	if (id == "SB_IO")
		return TYPE_SB_IO;
	return TYPE_NIL;
}

// -----------------------------------------------------------------------

IdString PortPinToId(PortPin type)
{
#define X(t) if (type == PIN_##t) return #t;

	X(IN_0)
	X(IN_1)
	X(IN_2)
	X(IN_3)
	X(O)
	X(LO)
	X(CIN)
	X(COUT)
	X(CEN)
	X(CLK)
	X(SR)

	X(MASK_0)
	X(MASK_1)
	X(MASK_2)
	X(MASK_3)
	X(MASK_4)
	X(MASK_5)
	X(MASK_6)
	X(MASK_7)
	X(MASK_8)
	X(MASK_9)
	X(MASK_10)
	X(MASK_11)
	X(MASK_12)
	X(MASK_13)
	X(MASK_14)
	X(MASK_15)

	X(RDATA_0)
	X(RDATA_1)
	X(RDATA_2)
	X(RDATA_3)
	X(RDATA_4)
	X(RDATA_5)
	X(RDATA_6)
	X(RDATA_7)
	X(RDATA_8)
	X(RDATA_9)
	X(RDATA_10)
	X(RDATA_11)
	X(RDATA_12)
	X(RDATA_13)
	X(RDATA_14)
	X(RDATA_15)

	X(WDATA_0)
	X(WDATA_1)
	X(WDATA_2)
	X(WDATA_3)
	X(WDATA_4)
	X(WDATA_5)
	X(WDATA_6)
	X(WDATA_7)
	X(WDATA_8)
	X(WDATA_9)
	X(WDATA_10)
	X(WDATA_11)
	X(WDATA_12)
	X(WDATA_13)
	X(WDATA_14)
	X(WDATA_15)

	X(WADDR_0)
	X(WADDR_1)
	X(WADDR_2)
	X(WADDR_3)
	X(WADDR_4)
	X(WADDR_5)
	X(WADDR_6)
	X(WADDR_7)
	X(WADDR_8)
	X(WADDR_9)
	X(WADDR_10)

	X(RADDR_0)
	X(RADDR_1)
	X(RADDR_2)
	X(RADDR_3)
	X(RADDR_4)
	X(RADDR_5)
	X(RADDR_6)
	X(RADDR_7)
	X(RADDR_8)
	X(RADDR_9)
	X(RADDR_10)

	X(WCLK)
	X(WCLKE)
	X(WE)

	X(RCLK)
	X(RCLKE)
	X(RE)

	X(PACKAGE_PIN)
	X(LATCH_INPUT_VALUE)
	X(CLOCK_ENABLE)
	X(INPUT_CLK)
	X(OUTPUT_CLK)
	X(OUTPUT_ENABLE)
	X(D_OUT_0)
	X(D_OUT_1)
	X(D_IN_0)
	X(D_IN_1)

#undef X
	return IdString();
}

PortPin PortPinFromId(IdString id)
{
#define X(t) if (id == #t) return PIN_##t;

	X(IN_0)
	X(IN_1)
	X(IN_2)
	X(IN_3)
	X(O)
	X(LO)
	X(CIN)
	X(COUT)
	X(CEN)
	X(CLK)
	X(SR)

	X(MASK_0)
	X(MASK_1)
	X(MASK_2)
	X(MASK_3)
	X(MASK_4)
	X(MASK_5)
	X(MASK_6)
	X(MASK_7)
	X(MASK_8)
	X(MASK_9)
	X(MASK_10)
	X(MASK_11)
	X(MASK_12)
	X(MASK_13)
	X(MASK_14)
	X(MASK_15)

	X(RDATA_0)
	X(RDATA_1)
	X(RDATA_2)
	X(RDATA_3)
	X(RDATA_4)
	X(RDATA_5)
	X(RDATA_6)
	X(RDATA_7)
	X(RDATA_8)
	X(RDATA_9)
	X(RDATA_10)
	X(RDATA_11)
	X(RDATA_12)
	X(RDATA_13)
	X(RDATA_14)
	X(RDATA_15)

	X(WDATA_0)
	X(WDATA_1)
	X(WDATA_2)
	X(WDATA_3)
	X(WDATA_4)
	X(WDATA_5)
	X(WDATA_6)
	X(WDATA_7)
	X(WDATA_8)
	X(WDATA_9)
	X(WDATA_10)
	X(WDATA_11)
	X(WDATA_12)
	X(WDATA_13)
	X(WDATA_14)
	X(WDATA_15)

	X(WADDR_0)
	X(WADDR_1)
	X(WADDR_2)
	X(WADDR_3)
	X(WADDR_4)
	X(WADDR_5)
	X(WADDR_6)
	X(WADDR_7)
	X(WADDR_8)
	X(WADDR_9)
	X(WADDR_10)

	X(RADDR_0)
	X(RADDR_1)
	X(RADDR_2)
	X(RADDR_3)
	X(RADDR_4)
	X(RADDR_5)
	X(RADDR_6)
	X(RADDR_7)
	X(RADDR_8)
	X(RADDR_9)
	X(RADDR_10)

	X(WCLK)
	X(WCLKE)
	X(WE)

	X(RCLK)
	X(RCLKE)
	X(RE)

	X(PACKAGE_PIN)
	X(LATCH_INPUT_VALUE)
	X(CLOCK_ENABLE)
	X(INPUT_CLK)
	X(OUTPUT_CLK)
	X(OUTPUT_ENABLE)
	X(D_OUT_0)
	X(D_OUT_1)
	X(D_IN_0)
	X(D_IN_1)

#undef X
	return PIN_NIL;
}

// -----------------------------------------------------------------------

Chip::Chip(ChipArgs args)
{
#ifdef ICE40_HX1K_ONLY
	if (args.type == ChipArgs::HX1K) {
		chip_info = chip_info_1k;
		return;
	}
#else
	if (args.type == ChipArgs::LP384) {
		chip_info = chip_info_384;
		return;
	} else if (args.type == ChipArgs::LP1K || args.type == ChipArgs::HX1K) {
		chip_info = chip_info_1k;
		return;
	} else if (args.type == ChipArgs::UP5K) {
		chip_info = chip_info_5k;
		return;
	} else if (args.type == ChipArgs::LP8K || args.type == ChipArgs::HX8K) {
		chip_info = chip_info_8k;
		return;
	} else {
		fprintf(stderr, "Unsupported chip type\n");
		exit(EXIT_FAILURE);
	}
#endif

	abort();
}

// -----------------------------------------------------------------------

BelId Chip::getBelByName(IdString name) const
{
	BelId ret;

	if (bel_by_name.empty()) {
		for (int i = 0; i < chip_info.num_bels; i++)
			bel_by_name[chip_info.bel_data[i].name] = i;
	}

	auto it = bel_by_name.find(name);
	if (it != bel_by_name.end())
		ret.index = it->second;

	return ret;
}

WireId Chip::getWireBelPin(BelId bel, PortPin pin) const
{
	// FIXME
	return WireId();
}

// -----------------------------------------------------------------------

WireId Chip::getWireByName(IdString name) const
{
	WireId ret;

	if (wire_by_name.empty()) {
		for (int i = 0; i < chip_info.num_wires; i++)
			wire_by_name[chip_info.wire_data[i].name] = i;
	}

	auto it = wire_by_name.find(name);
	if (it != wire_by_name.end())
		ret.index = it->second;

	return ret;
}

// -----------------------------------------------------------------------

PipId Chip::getPipByName(IdString name) const
{
	PipId ret;

	if (pip_by_name.empty()) {
		for (int i = 0; i < chip_info.num_pips; i++) {
			PipId pip;
			pip.index = i;
			pip_by_name[getPipName(pip)] = i;
		}
	}

	auto it = pip_by_name.find(name);
	if (it != pip_by_name.end())
		ret.index = it->second;

	return ret;
}

// -----------------------------------------------------------------------

void Chip::getBelPosition(BelId bel, float &x, float &y) const
{
	assert(!bel.nil());
	x = chip_info.bel_data[bel.index].x;
	y = chip_info.bel_data[bel.index].y;
}

void Chip::getWirePosition(WireId wire, float &x, float &y) const
{
	assert(!wire.nil());
	x = chip_info.wire_data[wire.index].x;
	y = chip_info.wire_data[wire.index].y;
}

void Chip::getPipPosition(PipId pip, float &x, float &y) const
{
	assert(!pip.nil());
	x = chip_info.pip_data[pip.index].x;
	y = chip_info.pip_data[pip.index].y;
}

vector<GraphicElement> Chip::getBelGraphics(BelId bel) const
{
	vector<GraphicElement> ret;

	auto bel_type = getBelType(bel);

	if (bel_type == TYPE_ICESTORM_LC) {
		GraphicElement el;
		el.type = GraphicElement::G_BOX;
		el.x1 = chip_info.bel_data[bel.index].x + 0.1;
		el.x2 = chip_info.bel_data[bel.index].x + 0.9;
		el.y1 = chip_info.bel_data[bel.index].y + 0.10 + (chip_info.bel_data[bel.index].z) * (0.8/8);
		el.y2 = chip_info.bel_data[bel.index].y + 0.18 + (chip_info.bel_data[bel.index].z) * (0.8/8);
		el.z = 0;
		ret.push_back(el);
	}

	if (bel_type == TYPE_SB_IO) {
		if (chip_info.bel_data[bel.index].x == 0 || chip_info.bel_data[bel.index].x == chip_info.width-1)
		{
			GraphicElement el;
			el.type = GraphicElement::G_BOX;
			el.x1 = chip_info.bel_data[bel.index].x + 0.1;
			el.x2 = chip_info.bel_data[bel.index].x + 0.9;
			if (chip_info.bel_data[bel.index].z == 0) {
				el.y1 = chip_info.bel_data[bel.index].y + 0.10;
				el.y2 = chip_info.bel_data[bel.index].y + 0.45;
			} else {
				el.y1 = chip_info.bel_data[bel.index].y + 0.55;
				el.y2 = chip_info.bel_data[bel.index].y + 0.90;
			}
			el.z = 0;
			ret.push_back(el);
		}
		else
		{
			GraphicElement el;
			el.type = GraphicElement::G_BOX;
			if (chip_info.bel_data[bel.index].z == 0) {
				el.x1 = chip_info.bel_data[bel.index].x + 0.10;
				el.x2 = chip_info.bel_data[bel.index].x + 0.45;
			} else {
				el.x1 = chip_info.bel_data[bel.index].x + 0.55;
				el.x2 = chip_info.bel_data[bel.index].x + 0.90;
			}
			el.y1 = chip_info.bel_data[bel.index].y + 0.1;
			el.y2 = chip_info.bel_data[bel.index].y + 0.9;
			el.z = 0;
			ret.push_back(el);
		}
	}

	if (bel_type == TYPE_ICESTORM_RAM) {
		GraphicElement el;
		el.type = GraphicElement::G_BOX;
		el.x1 = chip_info.bel_data[bel.index].x + 0.1;
		el.x2 = chip_info.bel_data[bel.index].x + 0.9;
		el.y1 = chip_info.bel_data[bel.index].y + 0.1;
		el.y2 = chip_info.bel_data[bel.index].y + 1.9;
		el.z = 0;
		ret.push_back(el);
	}

	return ret;
}

vector<GraphicElement> Chip::getWireGraphics(WireId wire) const
{
	vector<GraphicElement> ret;
	// FIXME
	return ret;
}

vector<GraphicElement> Chip::getPipGraphics(PipId pip) const
{
	vector<GraphicElement> ret;
	// FIXME
	return ret;
}

vector<GraphicElement> Chip::getFrameGraphics() const
{
	vector<GraphicElement> ret;

	for (int x = 0; x <= chip_info.width; x++)
	for (int y = 0; y <= chip_info.height; y++)
	{
		GraphicElement el;
		el.type = GraphicElement::G_LINE;
		el.x1 = x - 0.05, el.x2 = x + 0.05, el.y1 = y, el.y2 = y, el.z = 0;
		ret.push_back(el);
		el.x1 = x, el.x2 = x, el.y1 = y - 0.05, el.y2 = y + 0.05, el.z = 0;
		ret.push_back(el);
	}

	return ret;
}

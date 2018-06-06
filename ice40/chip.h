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

#include "design.h"

#ifndef CHIP_H
#define CHIP_H

struct DelayInfo
{
	float delay = 0;

	float raiseDelay() { return delay; }
	float fallDelay() { return delay; }
};

// -----------------------------------------------------------------------

enum BelType
{
	TYPE_NIL,
	TYPE_ICESTORM_LC,
	TYPE_ICESTORM_RAM,
	TYPE_SB_IO
};

IdString belTypeToId(BelType type);
BelType belTypeFromId(IdString id);

enum PortPin
{
	PIN_NIL,

	PIN_IN_0,
	PIN_IN_1,
	PIN_IN_2,
	PIN_IN_3,
	PIN_O,
	PIN_LO,
	PIN_CIN,
	PIN_COUT,
	PIN_CEN,
	PIN_CLK,
	PIN_SR,

	PIN_MASK_0,
	PIN_MASK_1,
	PIN_MASK_2,
	PIN_MASK_3,
	PIN_MASK_4,
	PIN_MASK_5,
	PIN_MASK_6,
	PIN_MASK_7,
	PIN_MASK_8,
	PIN_MASK_9,
	PIN_MASK_10,
	PIN_MASK_11,
	PIN_MASK_12,
	PIN_MASK_13,
	PIN_MASK_14,
	PIN_MASK_15,

	PIN_RDATA_0,
	PIN_RDATA_1,
	PIN_RDATA_2,
	PIN_RDATA_3,
	PIN_RDATA_4,
	PIN_RDATA_5,
	PIN_RDATA_6,
	PIN_RDATA_7,
	PIN_RDATA_8,
	PIN_RDATA_9,
	PIN_RDATA_10,
	PIN_RDATA_11,
	PIN_RDATA_12,
	PIN_RDATA_13,
	PIN_RDATA_14,
	PIN_RDATA_15,

	PIN_WDATA_0,
	PIN_WDATA_1,
	PIN_WDATA_2,
	PIN_WDATA_3,
	PIN_WDATA_4,
	PIN_WDATA_5,
	PIN_WDATA_6,
	PIN_WDATA_7,
	PIN_WDATA_8,
	PIN_WDATA_9,
	PIN_WDATA_10,
	PIN_WDATA_11,
	PIN_WDATA_12,
	PIN_WDATA_13,
	PIN_WDATA_14,
	PIN_WDATA_15,

	PIN_WADDR_0,
	PIN_WADDR_1,
	PIN_WADDR_2,
	PIN_WADDR_3,
	PIN_WADDR_4,
	PIN_WADDR_5,
	PIN_WADDR_6,
	PIN_WADDR_7,
	PIN_WADDR_8,
	PIN_WADDR_9,
	PIN_WADDR_10,

	PIN_RADDR_0,
	PIN_RADDR_1,
	PIN_RADDR_2,
	PIN_RADDR_3,
	PIN_RADDR_4,
	PIN_RADDR_5,
	PIN_RADDR_6,
	PIN_RADDR_7,
	PIN_RADDR_8,
	PIN_RADDR_9,
	PIN_RADDR_10,

	PIN_WCLK,
	PIN_WCLKE,
	PIN_WE,

	PIN_RCLK,
	PIN_RCLKE,
	PIN_RE,

	PIN_PACKAGE_PIN,
	PIN_LATCH_INPUT_VALUE,
	PIN_CLOCK_ENABLE,
	PIN_INPUT_CLK,
	PIN_OUTPUT_CLK,
	PIN_OUTPUT_ENABLE,
	PIN_D_OUT_0,
	PIN_D_OUT_1,
	PIN_D_IN_0,
	PIN_D_IN_1
};

IdString PortPinToId(PortPin type);
PortPin PortPinFromId(IdString id);

// -----------------------------------------------------------------------


struct BelInfoPOD
{
	const char *name;
	BelType type;
};

struct BelPortPOD
{
	int32_t bel_index;
	PortPin port;
};

struct PipInfoPOD
{
	int32_t src, dst;
	float delay;
};

struct WireInfoPOD
{
	const char *name;
	int num_uphill, num_downhill;
	int *pips_uphill, *pips_downhill;

	int num_bels_downhill;
	BelPortPOD bel_uphill;
	BelPortPOD *bels_downhill;
};

struct ChipInfoPOD
{
	int num_bels, num_wires, num_pips;
	BelInfoPOD *bel_data;
	WireInfoPOD *wire_data;
	PipInfoPOD *pip_data;
};

extern ChipInfoPOD chip_info_384;
extern ChipInfoPOD chip_info_1k;
extern ChipInfoPOD chip_info_5k;
extern ChipInfoPOD chip_info_8k;

// -----------------------------------------------------------------------

struct BelId
{
	int32_t index = -1;

	bool nil() const {
		return index < 0;
	}

	bool operator==(const BelId &other) const { return index == other.index; }
	bool operator!=(const BelId &other) const { return index != other.index; }
};

struct WireId
{
	int32_t index = -1;

	bool nil() const {
		return index < 0;
	}

	bool operator==(const WireId &other) const { return index == other.index; }
	bool operator!=(const WireId &other) const { return index != other.index; }
};

struct PipId
{
	int32_t index = -1;

	bool nil() const {
		return index < 0;
	}

	bool operator==(const PipId &other) const { return index == other.index; }
	bool operator!=(const PipId &other) const { return index != other.index; }
};

struct BelPin
{
	BelId bel;
	PortPin pin;
};

namespace std
{
	template<> struct hash<BelId>
        {
		std::size_t operator()(const BelId &bel) const noexcept
		{
			return bel.index;
		}
	};

	template<> struct hash<WireId>
        {
		std::size_t operator()(const WireId &wire) const noexcept
		{
			return wire.index;
		}
	};

	template<> struct hash<PipId>
        {
		std::size_t operator()(const PipId &wire) const noexcept
		{
			return wire.index;
		}
	};
}

// -----------------------------------------------------------------------

struct BelIterator
{
	int cursor;

	void operator++() { cursor++; }
	bool operator!=(const BelIterator &other) const { return cursor != other.cursor; }

	BelId operator*() const {
		BelId ret;
		ret.index = cursor;
		return ret;
	}
};

struct BelRange
{
	BelIterator b, e;
	BelIterator begin() const { return b; }
	BelIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct BelPinIterator
{
	BelPortPOD *ptr = nullptr;

	void operator++() { ptr++; }
	bool operator!=(const BelPinIterator &other) const { return ptr != other.ptr; }

	BelPin operator*() const {
		BelPin ret;
		ret.bel.index = ptr->bel_index;
		ret.pin = ptr->port;
		return ret;
	}
};

struct BelPinRange
{
	BelPinIterator b, e;
	BelPinIterator begin() const { return b; }
	BelPinIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct WireIterator
{
	int cursor = -1;

	void operator++() { cursor++; }
	bool operator!=(const WireIterator &other) const { return cursor != other.cursor; }

	WireId operator*() const {
		WireId ret;
		ret.index = cursor;
		return ret;
	}
};

struct WireRange
{
	WireIterator b, e;
	WireIterator begin() const { return b; }
	WireIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct AllPipIterator
{
	int cursor = -1;

	void operator++() { cursor++; }
	bool operator!=(const AllPipIterator &other) const { return cursor != other.cursor; }

	PipId operator*() const {
		PipId ret;
		ret.index = cursor;
		return ret;
	}
};

struct AllPipRange
{
	AllPipIterator b, e;
	AllPipIterator begin() const { return b; }
	AllPipIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct PipIterator
{
	int *cursor = nullptr;

	void operator++() { cursor++; }
	bool operator!=(const PipIterator &other) const { return cursor != other.cursor; }

	PipId operator*() const {
		PipId ret;
		ret.index = *cursor;
		return ret;
	}
};

struct PipRange
{
	PipIterator b, e;
	PipIterator begin() const { return b; }
	PipIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct ChipArgs
{
	enum {
		NONE,
		LP384,
		LP1K,
		LP8K,
		HX1K,
		HX8K,
		UP5K
	} type = NONE;
};

struct Chip
{
	ChipInfoPOD chip_info;

	mutable dict<IdString, int> bel_by_name;
	mutable dict<IdString, int> wire_by_name;
	mutable dict<IdString, int> pip_by_name;

	Chip(ChipArgs args);

	// -------------------------------------------------

	BelId getBelByName(IdString name) const;

	IdString getBelName(BelId bel) const
	{
		assert(!bel.nil());
		return chip_info.bel_data[bel.index].name;
	}

	void bindBel(BelId bel, IdString cell)
	{
	}

	void unbindBel(BelId bel)
	{
	}

	bool checkBelAvail(BelId bel) const
	{
	}

	BelRange getBels() const
	{
		BelRange range;
		range.b.cursor = 0;
		range.e.cursor = chip_info.num_bels;
		return range;
	}

	BelRange getBelsByType(BelType type) const
	{
		BelRange range;
		// FIXME
#if 0
		if (type == "TYPE_A") {
			range.b.cursor = bels_type_a_begin;
			range.e.cursor = bels_type_a_end;
		}
		...
#endif
		return range;
	}

	BelType getBelType(BelId bel) const
	{
		assert(!bel.nil());
		return chip_info.bel_data[bel.index].type;
	}

	WireId getWireBelPin(BelId bel, PortPin pin) const;

	BelPin getBelPinUphill(WireId wire) const
	{
		BelPin ret;
		assert(!wire.nil());

		if (chip_info.wire_data[wire.index].bel_uphill.bel_index >= 0) {
			ret.bel.index = chip_info.wire_data[wire.index].bel_uphill.bel_index;
			ret.pin = chip_info.wire_data[wire.index].bel_uphill.port;
		}

		return ret;
	}

	BelPinRange getBelPinsDownhill(WireId wire) const
	{
		BelPinRange range;
		assert(!wire.nil());
		range.b.ptr = chip_info.wire_data[wire.index].bels_downhill;
		range.e.ptr = range.b.ptr + chip_info.wire_data[wire.index].num_bels_downhill;
		return range;
	}

	// -------------------------------------------------

	WireId getWireByName(IdString name) const;

	IdString getWireName(WireId wire) const
	{
		assert(!wire.nil());
		return chip_info.wire_data[wire.index].name;
	}

	void bindWire(WireId bel, IdString net)
	{
	}

	void unbindWire(WireId bel)
	{
	}

	bool checkWireAvail(WireId bel) const
	{
	}

	WireRange getWires() const
	{
		WireRange range;
		range.b.cursor = 0;
		range.e.cursor = chip_info.num_wires;
		return range;
	}

	// -------------------------------------------------

	PipId getPipByName(IdString name) const;

	IdString getPipName(PipId pip) const
	{
		assert(!pip.nil());
		std::string src_name = chip_info.wire_data[chip_info.pip_data[pip.index].src].name;
		std::string dst_name = chip_info.wire_data[chip_info.pip_data[pip.index].dst].name;
		return src_name + "->" + dst_name;
	}

	void bindPip(PipId bel, IdString net)
	{
	}

	void unbindPip(PipId bel)
	{
	}

	bool checkPipAvail(PipId bel) const
	{
	}

	AllPipRange getPips() const
	{
		AllPipRange range;
		range.b.cursor = 0;
		range.e.cursor = chip_info.num_pips;
		return range;
	}

	WireId getPipSrcWire(PipId pip) const
	{
		WireId wire;
		assert(!pip.nil());
		wire.index = chip_info.pip_data[pip.index].src;
		return wire;
	}

	WireId getPipDstWire(PipId pip) const
	{
		WireId wire;
		assert(!pip.nil());
		wire.index = chip_info.pip_data[pip.index].dst;
		return wire;
	}

	DelayInfo getPipDelay(PipId pip) const
	{
		DelayInfo delay;
		assert(!pip.nil());
		delay.delay = chip_info.pip_data[pip.index].delay;
		return delay;
	}

	PipRange getPipsDownhill(WireId wire) const
	{
		PipRange range;
		assert(!wire.nil());
		range.b.cursor = chip_info.wire_data[wire.index].pips_downhill;
		range.e.cursor = range.b.cursor + chip_info.wire_data[wire.index].num_downhill;
		return range;
	}

	PipRange getPipsUphill(WireId wire) const
	{
		PipRange range;
		assert(!wire.nil());
		range.b.cursor = chip_info.wire_data[wire.index].pips_uphill;
		range.e.cursor = range.b.cursor + chip_info.wire_data[wire.index].num_uphill;
		return range;
	}

	PipRange getWireAliases(WireId wire) const
	{
		PipRange range;
		assert(!wire.nil());
		range.b.cursor = nullptr;
		range.e.cursor = nullptr;
		return range;
	}

	// -------------------------------------------------

	void getBelPosition(BelId bel, float &x, float &y) const;
	void getWirePosition(WireId wire, float &x, float &y) const;
	void getPipPosition(WireId wire, float &x, float &y) const;
	vector<GraphicElement> getBelGraphics(BelId bel) const;
	vector<GraphicElement> getWireGraphics(WireId wire) const;
	vector<GraphicElement> getPipGraphics(PipId pip) const;
	vector<GraphicElement> getFrameGraphics() const;
};

#endif

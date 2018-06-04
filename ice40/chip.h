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

struct WireDelayPOD
{
	int32_t wire_index;
	float delay;
};

struct BelPortPOD
{
	int32_t bel_index;
	PortPin port;
};

struct WireInfoPOD
{
	const char *name;
	int num_uphill, num_downhill, num_bidir;
	WireDelayPOD *wires_uphill, *wires_downhill, *wires_bidir;

	int num_bels_downhill;
	BelPortPOD bel_uphill;
	BelPortPOD *bels_downhill;
};

extern int num_bels_384;
extern int num_bels_1k;
extern int num_bels_5k;
extern int num_bels_8k;

extern BelInfoPOD bel_data_384[];
extern BelInfoPOD bel_data_1k[];
extern BelInfoPOD bel_data_5k[];
extern BelInfoPOD bel_data_8k[];

extern int num_wires_384;
extern int num_wires_1k;
extern int num_wires_5k;
extern int num_wires_8k;

extern WireInfoPOD wire_data_384[];
extern WireInfoPOD wire_data_1k[];
extern WireInfoPOD wire_data_5k[];
extern WireInfoPOD wire_data_8k[];

// -----------------------------------------------------------------------

struct BelId
{
	int32_t index = -1;

	bool nil() const {
		return index < 0;
	}
};

struct WireId
{
	int32_t index = -1;

	bool nil() const {
		return index < 0;
	}
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

struct AllWireIterator
{
	int cursor;

	void operator++() { cursor++; }
	bool operator!=(const AllWireIterator &other) const { return cursor != other.cursor; }

	WireId operator*() const {
		WireId ret;
		ret.index = cursor;
		return ret;
	}
};

struct AllWireRange
{
	AllWireIterator b, e;
	AllWireIterator begin() const { return b; }
	AllWireIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct WireDelay
{
	WireId wire;
	DelayInfo delay;
};

struct WireDelayIterator
{
	WireDelayPOD *ptr = nullptr;

	void operator++() { ptr++; }
	bool operator!=(const WireDelayIterator &other) const { return ptr != other.ptr; }

	WireDelay operator*() const {
		WireDelay ret;
		ret.wire.index = ptr->wire_index;
		ret.delay.delay = ptr->delay;
		return ret;
	}
};

struct WireDelayRange
{
	WireDelayIterator b, e;
	WireDelayIterator begin() const { return b; }
	WireDelayIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct BelPin
{
	BelId bel;
	PortPin pin;
};

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
	int num_bels, num_wires;
	BelInfoPOD *bel_data;
	WireInfoPOD *wire_data;

	mutable dict<IdString, int> wire_by_name;
	mutable dict<IdString, int> bel_by_name;

	Chip(ChipArgs args);

	void setBelActive(BelId, bool) { }
	bool getBelActive(BelId) { return true; }

	BelId getBelByName(IdString name) const;
	WireId getWireByName(IdString name) const;

	IdString getBelName(BelId bel) const
	{
		return bel_data[bel.index].name;
	}

	IdString getWireName(WireId wire) const
	{
		return wire_data[wire.index].name;
	}

	BelRange getBels() const
	{
		BelRange range;
		range.b.cursor = 0;
		range.e.cursor = num_bels;
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
		return bel_data[bel.index].type;
	}

	// FIXME: void getBelPosition(BelId bel, float &x, float &y) const;
	// FIXME: void getWirePosition(WireId wire, float &x, float &y) const;
	// FIXME: vector<GraphicElement> getBelGraphics(BelId bel) const;
	// FIXME: vector<GraphicElement> getWireGraphics(WireId wire) const;
	// FIXME: vector<GraphicElement> getPipGraphics(WireId src, WireId dst) const;
	// FIXME: vector<GraphicElement> getFrameGraphics() const;

	AllWireRange getWires() const
	{
		AllWireRange range;
		range.b.cursor = 0;
		range.e.cursor = num_wires;
		return range;
	}

	WireDelayRange getWiresUphill(WireId wire) const
	{
		WireDelayRange range;
		range.b.ptr = wire_data[wire.index].wires_uphill;
		range.e.ptr = wire_data[wire.index].wires_uphill + wire_data[wire.index].num_uphill;
		return range;
	}

	WireDelayRange getWiresDownhill(WireId wire) const
	{
		WireDelayRange range;
		range.b.ptr = wire_data[wire.index].wires_downhill;
		range.e.ptr = wire_data[wire.index].wires_downhill + wire_data[wire.index].num_downhill;
		return range;
	}

	WireDelayRange getWiresBidir(WireId wire) const
	{
		WireDelayRange range;
		range.b.ptr = wire_data[wire.index].wires_bidir;
		range.e.ptr = wire_data[wire.index].wires_bidir + wire_data[wire.index].num_bidir;
		return range;
	}

	WireDelayRange getWireAliases(WireId wire) const
	{
		WireDelayRange range;
		return range;
	}

	WireId getWireBelPin(BelId bel, PortPin pin) const;

	BelPin getBelPinUphill(WireId wire) const
	{
		BelPin ret;

		if (wire_data[wire.index].bel_uphill.bel_index >= 0) {
			ret.bel.index = wire_data[wire.index].bel_uphill.bel_index;
			ret.pin = wire_data[wire.index].bel_uphill.port;
		}

		return ret;
	}

	BelPinRange getBelPinsDownhill(WireId wire) const
	{
		BelPinRange range;
		range.b.ptr = wire_data[wire.index].bels_downhill;
		range.e.ptr = wire_data[wire.index].bels_downhill + wire_data[wire.index].num_bels_downhill;
		return range;
	}
};

#endif

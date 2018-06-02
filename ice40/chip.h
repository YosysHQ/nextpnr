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
	TYPE_A
};

IdString belTypeToId(BelType type);
BelType belTypeFromId(IdString id);

enum PortPin
{
	PIN_NIL,
	PIN_FOO = 1,
	PIN_BAR = 2
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

struct GuiLine
{
	float x1, y1, x2, y2;
};

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
	// FIXME: vector<GuiLine> getBelGuiLines(BelId bel) const;
	// FIXME: vector<GuiLine> getWireGuiLines(WireId wire) const;

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

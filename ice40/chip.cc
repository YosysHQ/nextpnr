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
	if (type == TYPE_SB_IO)
		return "SB_IO";
	return IdString();
}

BelType belTypeFromId(IdString id)
{
	if (id == "ICESTORM_LC")
		return TYPE_ICESTORM_LC;
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
	if (args.type == ChipArgs::LP384) {
		num_bels = 0;
		bel_data = nullptr;
		num_wires = num_wires_384;
		wire_data = wire_data_384;
		return;
	}

	abort();
}

BelId Chip::getBelByName(IdString name) const
{
	BelId ret;

	if (bel_by_name.empty()) {
		for (int i = 0; i < num_bels; i++)
			bel_by_name[bel_data[i].name] = i;
	}

	auto it = bel_by_name.find(name);
	if (it != bel_by_name.end())
		ret.index = it->second;

	return ret;
}

WireId Chip::getWireByName(IdString name) const
{
	WireId ret;

	if (wire_by_name.empty()) {
		for (int i = 0; i < num_wires; i++)
			wire_by_name[wire_data[i].name] = i;
	}

	auto it = wire_by_name.find(name);
	if (it != wire_by_name.end())
		ret.index = it->second;

	return ret;
}

WireId Chip::getWireBelPin(BelId bel, PortPin pin) const
{
	// FIXME
	return WireId();
}

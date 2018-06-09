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

Chip::Chip(ChipArgs) {}

// ---------------------------------------------------------------

BelId Chip::getBelByName(IdString name) const { return BelId(); }

IdString Chip::getBelName(BelId bel) const { return IdString(); }

void Chip::bindBel(BelId bel, IdString cell) {}

void Chip::unbindBel(BelId bel) {}

bool Chip::checkBelAvail(BelId bel) const { return false; }

IdString Chip::getBelCell(BelId bel) const { return IdString(); }

const vector<BelId> &Chip::getBels() const
{
    static vector<BelId> ret;
    return ret;
}

const vector<BelId> &Chip::getBelsByType(BelType type) const
{
    static vector<BelId> ret;
    return ret;
}

BelType Chip::getBelType(BelId bel) const { return BelType(); }

WireId Chip::getWireBelPin(BelId bel, PortPin pin) const { return WireId(); }

BelPin Chip::getBelPinUphill(WireId wire) const { return BelPin(); }

const vector<BelPin> &Chip::getBelPinsDownhill(WireId wire) const
{
    static vector<BelPin> ret;
    return ret;
}

// ---------------------------------------------------------------

WireId Chip::getWireByName(IdString name) const { return WireId(); }

IdString Chip::getWireName(WireId wire) const { return IdString(); }

void Chip::bindWire(WireId wire, IdString net) {}

void Chip::unbindWire(WireId wire) {}

bool Chip::checkWireAvail(WireId wire) const { return false; }

IdString Chip::getWireNet(WireId wire) const { return IdString(); }

const vector<WireId> &Chip::getWires() const
{
    static vector<WireId> ret;
    return ret;
}

// ---------------------------------------------------------------

PipId Chip::getPipByName(IdString name) const { return PipId(); }

IdString Chip::getPipName(PipId pip) const { return IdString(); }

void Chip::bindPip(PipId pip, IdString net) {}

void Chip::unbindPip(PipId pip) {}

bool Chip::checkPipAvail(PipId pip) const { return false; }

IdString Chip::getPipNet(PipId pip) const { return IdString(); }

const vector<PipId> &Chip::getPips() const
{
    static vector<PipId> ret;
    return ret;
}

WireId Chip::getPipSrcWire(PipId pip) const { return WireId(); }

WireId Chip::getPipDstWire(PipId pip) const { return WireId(); }

DelayInfo Chip::getPipDelay(PipId pip) const { return DelayInfo(); }

const vector<PipId> &Chip::getPipsDownhill(WireId wire) const
{
    static vector<PipId> ret;
    return ret;
}

const vector<PipId> &Chip::getPipsUphill(WireId wire) const
{
    static vector<PipId> ret;
    return ret;
}

const vector<PipId> &Chip::getWireAliases(WireId wire) const
{
    static vector<PipId> ret;
    return ret;
}

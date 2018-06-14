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

#ifndef CHIP_H
#define CHIP_H

#ifndef NEXTPNR_H
#error Include "chip.h" via "nextpnr.h" only.
#endif

NEXTPNR_NAMESPACE_BEGIN

struct DelayInfo
{
    float delay = 0;

    float raiseDelay() const { return delay; }
    float fallDelay() const { return delay; }
    float avgDelay() const { return delay; }

    DelayInfo operator+(const DelayInfo &other) const
    {
        DelayInfo ret;
        ret.delay = this->delay + other.delay;
        return ret;
    }
};

typedef IdString BelType;
typedef IdString PortPin;

static inline IdString belTypeToId(BelType type) { return type; }
static inline IdString portPinToId(PortPin type) { return type; }

static inline BelType belTypeFromId(IdString id) { return id; }
static inline PortPin portPinFromId(IdString id) { return id; }

typedef IdString BelId;
typedef IdString WireId;
typedef IdString PipId;

struct BelPin
{
    BelId bel;
    PortPin pin;
};

struct ChipArgs
{
};

std::string getChipName(ChipArgs id);

struct Chip
{
    Chip(ChipArgs args);

    std::string getChipName();

    BelId getBelByName(IdString name) const;
    IdString getBelName(BelId bel) const;
    void bindBel(BelId bel, IdString cell);
    void unbindBel(BelId bel);
    bool checkBelAvail(BelId bel) const;
    IdString getBelCell(BelId bel, bool conflicting = false) const;
    const std::vector<BelId> &getBels() const;
    const std::vector<BelId> &getBelsByType(BelType type) const;
    BelType getBelType(BelId bel) const;
    WireId getWireBelPin(BelId bel, PortPin pin) const;
    BelPin getBelPinUphill(WireId wire) const;
    const std::vector<BelPin> &getBelPinsDownhill(WireId wire) const;

    WireId getWireByName(IdString name) const;
    IdString getWireName(WireId wire) const;
    void bindWire(WireId wire, IdString net);
    void unbindWire(WireId wire);
    bool checkWireAvail(WireId wire) const;
    IdString getWireNet(WireId wire, bool conflicting = false) const;
    const std::vector<WireId> &getWires() const;

    PipId getPipByName(IdString name) const;
    IdString getPipName(PipId pip) const;
    void bindPip(PipId pip, IdString net);
    void unbindPip(PipId pip);
    bool checkPipAvail(PipId pip) const;
    IdString getPipNet(PipId pip, bool conflicting = false) const;
    const std::vector<PipId> &getPips() const;
    WireId getPipSrcWire(PipId pip) const;
    WireId getPipDstWire(PipId pip) const;
    DelayInfo getPipDelay(PipId pip) const;
    const std::vector<PipId> &getPipsDownhill(WireId wire) const;
    const std::vector<PipId> &getPipsUphill(WireId wire) const;
    const std::vector<PipId> &getWireAliases(WireId wire) const;

    bool estimatePosition(BelId bel, float &x, float &y) const;
    float estimateDelay(WireId src, WireId dst) const;

    std::vector<GraphicElement> getFrameGraphics() const;
    std::vector<GraphicElement> getBelGraphics(BelId bel) const;
    std::vector<GraphicElement> getWireGraphics(WireId wire) const;
    std::vector<GraphicElement> getPipGraphics(PipId pip) const;

    bool allGraphicsReload = false;
    bool frameGraphicsReload = false;
    std::unordered_set<BelId> belGraphicsReload;
    std::unordered_set<WireId> wireGraphicsReload;
    std::unordered_set<PipId> pipGraphicsReload;
};

NEXTPNR_NAMESPACE_END

#endif

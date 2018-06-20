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

#ifndef NEXTPNR_H
#error Include "arch.h" via "nextpnr.h" only.
#endif

#ifdef NEXTPNR_ARCH_TOP

NEXTPNR_NAMESPACE_BEGIN

typedef float delay_t;

struct DelayInfo
{
    delay_t delay = 0;

    delay_t raiseDelay() const { return delay; }
    delay_t fallDelay() const { return delay; }
    delay_t avgDelay() const { return delay; }

    DelayInfo operator+(const DelayInfo &other) const
    {
        DelayInfo ret;
        ret.delay = this->delay + other.delay;
        return ret;
    }
};

typedef IdString BelType;
typedef IdString PortPin;

typedef IdString BelId;
typedef IdString WireId;
typedef IdString PipId;

struct BelPin
{
    BelId bel;
    PortPin pin;
};

NEXTPNR_NAMESPACE_END

#endif // NEXTPNR_ARCH_TOP

#ifdef NEXTPNR_ARCH_BOTTOM

NEXTPNR_NAMESPACE_BEGIN

struct ArchArgs
{
};

struct Arch : BaseCtx
{
    Arch(ArchArgs args);

    std::string getChipName();

    virtual IdString id(const std::string &s) const { abort(); }
    virtual IdString id(const char *s) const { abort(); }

    IdString belTypeToId(BelType type) const { return type; }
    IdString portPinToId(PortPin type) const { return type; }

    BelType belTypeFromId(IdString id) const { return id; }
    PortPin portPinFromId(IdString id) const { return id; }

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

    void estimatePosition(BelId bel, int &x, int &y, bool &gb) const;
    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t getDelayEpsilon() const { return 0.01; }
    float getDelayNS(delay_t v) const { return v; }

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

#endif // NEXTPNR_ARCH_BOTTOM

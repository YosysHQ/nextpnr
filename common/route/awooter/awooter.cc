/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2022  Lofty <lofty@yosyshq.com>
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
 */

#include "log.h"
#include "nextpnr.h"

namespace {
    USING_NEXTPNR_NAMESPACE;

    template<typename T>
    uint64_t wrap(T thing) {
        static_assert(sizeof(T) <= 8, "T is too big for FFI");
        uint64_t b = 0;
        memcpy(&b, &thing, sizeof(T));
        return b;
    }

    BelId unwrap_bel(uint64_t bel) {
        static_assert(sizeof(BelId) <= 8, "T is too big for FFI");
        auto b = BelId();
        memcpy(&b, &bel, sizeof(BelId));
        return b;
    }

    PipId unwrap_pip(uint64_t pip) {
        static_assert(sizeof(PipId) <= 8, "T is too big for FFI");
        auto p = PipId();
        memcpy(&p, &pip, sizeof(PipId));
        return p;
    }

    WireId unwrap_wire(uint64_t wire) {
        static_assert(sizeof(WireId) <= 8, "T is too big for FFI");
        auto w = WireId();
        memcpy(&w, &wire, sizeof(WireId));
        return w;
    }
}

using DownhillIter = decltype(Context(ArchArgs()).getPipsDownhill(WireId()).begin());

struct DownhillIterWrapper {
    DownhillIter current;
    DownhillIter end;

    DownhillIterWrapper(DownhillIter begin, DownhillIter end) : current(begin), end(end) {}
};
using UphillIter = decltype(Context(ArchArgs()).getPipsUphill(WireId()).begin());

struct UphillIterWrapper {
    UphillIter current;
    UphillIter end;

    UphillIterWrapper(UphillIter begin, UphillIter end) : current(begin), end(end) {}
};

extern "C" {
    USING_NEXTPNR_NAMESPACE;

    /*
        DONE:
        ctx->bindPip
        ctx->bindWire
        ctx->check
        ctx->debug
        ctx->estimateDelay
        ctx->getDelayEpsilon
        ctx->getPipDstWire
        ctx->getPipSrcWire
        ctx->getGridDimX
        ctx->getGridDimY
        ctx->id
        ctx->nameOf
        ctx->unbindWire
        ctx->verbose

        UNNECESSARY:
        ctx->getDelayNS - all FFI calls go through it anyway.

        TODO:
        ctx->checkPipAvail
        ctx->checkPipAvailForNet
        ctx->checkWireAvail
        ctx->getBelPinType
        ctx->getBoundPipNet
        ctx->getBoundWireNet
        ctx->getNetinfoSinkWire
        ctx->getNetinfoSinkWires
        ctx->getNetinfoSourceWire
        ctx->getPipDelay
        ctx->getPipLocation
        ctx->getPipsDownhill
        ctx->getPipsUphill
        ctx->getRouteBoundingBox
        ctx->getWireBelPins
        ctx->getWireDelay
        ctx->getWires
        ctx->getWireType
        ctx->nameOfPip
        ctx->nameOfWire
        ctx->nets
        ctx->nets.at
        ctx->nets.size
        ctx->rng64
        ctx->setting<bool>
        ctx->setting<float>
        ctx->setting<int>
        ctx->sorted_shuffle
    */

    void npnr_log_info(const char *const format) { log_info("%s", format); }
    void npnr_log_error(const char *const format) { log_error("%s", format); }

    uint64_t npnr_belid_null() { return wrap(BelId()); }
    uint64_t npnr_wireid_null() { return wrap(WireId()); }
    uint64_t npnr_pipid_null() { return wrap(PipId()); }

    int npnr_context_get_grid_dim_x(const Context *const ctx) { return ctx->getGridDimX(); }
    int npnr_context_get_grid_dim_y(const Context *const ctx) { return ctx->getGridDimY(); }
    void npnr_context_bind_bel(Context *ctx, uint64_t bel, CellInfo* cell, PlaceStrength strength) { return ctx->bindBel(unwrap_bel(bel), cell, strength); }
    void npnr_context_unbind_bel(Context *ctx, uint64_t bel) { return ctx->unbindBel(unwrap_bel(bel)); }
    bool npnr_context_check_bel_avail(Context *const ctx, uint64_t bel) { return ctx->checkBelAvail(unwrap_bel(bel)); }
    void npnr_context_bind_wire(Context *ctx, uint64_t wire, NetInfo* net, PlaceStrength strength) { ctx->bindWire(unwrap_wire(wire), net, strength); }
    void npnr_context_unbind_wire(Context *ctx, uint64_t wire) { ctx->unbindWire(unwrap_wire(wire)); }
    void npnr_context_bind_pip(Context *ctx, uint64_t pip, NetInfo* net, PlaceStrength strength) { ctx->bindPip(unwrap_pip(pip), net, strength); }
    void npnr_context_unbind_pip(Context *ctx, uint64_t pip) { ctx->unbindPip(unwrap_pip(pip)); }
    uint64_t npnr_context_get_pip_src_wire(const Context *const ctx, uint64_t pip) { return wrap(ctx->getPipSrcWire(unwrap_pip(pip))); }
    uint64_t npnr_context_get_pip_dst_wire(const Context *const ctx, uint64_t pip) { return wrap(ctx->getPipDstWire(unwrap_pip(pip))); }
    float npnr_context_estimate_delay(const Context *const ctx, uint64_t src, uint64_t dst) { return ctx->getDelayNS(ctx->estimateDelay(unwrap_wire(src), unwrap_wire(dst))); }
    float npnr_context_get_pip_delay(const Context *const ctx, uint64_t pip) { return ctx->getDelayNS(ctx->getPipDelay(unwrap_pip(pip)).maxDelay()); }
    float npnr_context_get_wire_delay(const Context *const ctx, uint64_t wire) { return ctx->getDelayNS(ctx->getWireDelay(unwrap_wire(wire)).maxDelay()); }
    float npnr_context_delay_epsilon(const Context *const ctx) { return ctx->getDelayNS(ctx->getDelayEpsilon()); }
    Loc npnr_context_get_pip_location(const Context *const ctx, uint64_t pip) { return ctx->getPipLocation(unwrap_pip(pip)); }
    bool npnr_context_check_pip_avail_for_net(const Context *const ctx, uint64_t pip, NetInfo *net) { return ctx->checkPipAvailForNet(unwrap_pip(pip), net); }

    // This method's in C++ temporarily, while I figure out some better way of getting a pip iterator.
    Loc npnr_context_get_pip_direction(const Context *const ctx, uint64_t _pip) {
        auto pip = unwrap_pip(_pip);
        auto src_loc = Loc{};
        auto dst_loc = Loc{};

        auto uh_pips = 0;
        for (auto uh : ctx->getPipsUphill(ctx->getPipSrcWire(pip))) {
            auto loc = ctx->getPipLocation(uh);
            src_loc.x += loc.x;
            src_loc.y += loc.y;
            uh_pips++;
        }
        if (uh_pips > 1) {
            src_loc.x /= uh_pips;
            src_loc.y /= uh_pips;
        }

        auto dh_pips = 0;
        for (auto dh : ctx->getPipsDownhill(ctx->getPipDstWire(pip))) {
            auto loc = ctx->getPipLocation(dh);
            dst_loc.x += loc.x;
            dst_loc.y += loc.y;
            dh_pips++;
        }
        if (dh_pips > 1) {
            dst_loc.x /= dh_pips;
            dst_loc.y /= dh_pips;
        }

        dst_loc.x -= src_loc.x;
        dst_loc.y -= src_loc.y;
        return dst_loc;
    }

    uint64_t npnr_context_get_pips_leak(const Context *const ctx, PipId **pips) {
        auto pip_vec = std::vector<PipId>{};
        for (auto pip : ctx->getPips()) {
            pip_vec.push_back(pip);
        }
        pip_vec.shrink_to_fit();
        auto size = pip_vec.size();
        *pips = pip_vec.data();
        auto dummy = std::vector<PipId>{};
        // Yes, by memcpying over `pip_vec` we leak memory.
        std::memcpy(&pip_vec, &dummy, sizeof(dummy));
        return size;
    }

    uint64_t npnr_context_get_wires_leak(const Context *const ctx, WireId **wires) {
        auto wire_vec = std::vector<WireId>{};
        for (auto wire : ctx->getWires()) {
            wire_vec.push_back(wire);
        }
        wire_vec.shrink_to_fit();
        auto size = wire_vec.size();
        *wires = wire_vec.data();
        auto dummy = std::vector<WireId>{};
        // Yes, by memcpying over `wire_vec` we leak memory.
        std::memcpy(&wire_vec, &dummy, sizeof(dummy));
        return size;
    }

    void npnr_context_check(const Context *const ctx) { ctx->check(); }
    bool npnr_context_debug(const Context *const ctx) { return ctx->debug; }
    int npnr_context_id(const Context *const ctx, const char *const str) { return ctx->id(str).hash(); }
    const char *npnr_context_name_of(const Context *const ctx, IdString str) { return ctx->nameOf(str); }
    const char *npnr_context_name_of_pip(const Context *const ctx, uint64_t pip) { return ctx->nameOfPip(unwrap_pip(pip)); }
    const char *npnr_context_name_of_wire(const Context *const ctx, uint64_t wire) { return ctx->nameOfWire(unwrap_wire(wire)); }
    bool npnr_context_verbose(const Context *const ctx) { return ctx->verbose; }

    uint64_t npnr_context_get_netinfo_source_wire(const Context *const ctx, const NetInfo *const net) { return wrap(ctx->getNetinfoSourceWire(net)); }
    uint64_t npnr_context_get_netinfo_sink_wire(const Context *const ctx, const NetInfo *const net, const PortRef *const sink, uint32_t n) { return wrap(ctx->getNetinfoSinkWire(net, *sink, n)); }

    uint32_t npnr_context_nets_leak(const Context *const ctx, int **names, NetInfo ***nets) {
        auto name_vec = std::vector<int>{};
        auto nets_vec = std::vector<NetInfo*>{};
        for (auto& item : ctx->nets) {
            name_vec.push_back(item.first.hash());
            nets_vec.push_back(item.second.get());
        }
        name_vec.shrink_to_fit();
        nets_vec.shrink_to_fit();
        auto size = name_vec.size();
        *names = name_vec.data();
        *nets = nets_vec.data();
        // Yes, by memcpying over `name_vec` and `nets_vec` we leak memory.
        auto dummy1 = std::vector<int>{};
        auto dummy2 = std::vector<NetInfo*>{};
        std::memcpy(&name_vec, &dummy1, sizeof(dummy1));
        std::memcpy(&nets_vec, &dummy2, sizeof(dummy2));
        return size;
    }

    DownhillIterWrapper *npnr_context_get_pips_downhill(Context *ctx, uint64_t wire_id) {
        auto wire = unwrap_wire(wire_id);
        auto range = ctx->getPipsDownhill(wire);
        return new DownhillIterWrapper(range.begin(), range.end());
    }
    void npnr_delete_downhill_iter(DownhillIterWrapper *iter) {
        delete iter;
    }
    UphillIterWrapper *npnr_context_get_pips_uphill(Context *ctx, uint64_t wire_id) {
        auto wire = unwrap_wire(wire_id);
        auto range = ctx->getPipsUphill(wire);
        return new UphillIterWrapper(range.begin(), range.end());
    }
    void npnr_delete_uphill_iter(UphillIterWrapper *iter) {
        delete iter;
    }

    PortRef* npnr_netinfo_driver(NetInfo *const net) {
        if (net == nullptr) {
            return nullptr;
        }
        return &net->driver;
    }

    uint32_t npnr_netinfo_users_leak(NetInfo *const net, PortRef ***users) {
        auto x = std::vector<PortRef*>{};
        for (auto& item : net->users) {
            x.push_back(&item);
        }
        x.shrink_to_fit();
        *users = x.data();
        auto size = x.size();
        // Yes, by memcpying over `x` we leak memory.
        auto dummy = std::vector<PortRef*>{};
        std::memcpy(&x, &dummy, sizeof(dummy));
        return size;
    }

#ifdef ARCH_ECP5
    bool npnr_netinfo_is_global(NetInfo *const net) { return net->is_global; }
#else
    bool npnr_netinfo_is_global(NetInfo *const net) { return false; }
#endif

    int32_t npnr_netinfo_udata(NetInfo *const net) { return net->udata; }
    void npnr_netinfo_udata_set(NetInfo *const net, int32_t value) { net->udata = value; }

    CellInfo* npnr_portref_cell(const PortRef *const port) { return port->cell; }
    Loc npnr_cellinfo_get_location(const CellInfo *const info) { return info->getLocation(); }

    void npnr_inc_downhill_iter(DownhillIterWrapper *iter) {
        ++iter->current;
    }
    uint64_t npnr_deref_downhill_iter(DownhillIterWrapper *iter) {
        return wrap(*iter->current);
    }
    bool npnr_is_downhill_iter_done(DownhillIterWrapper *iter) {
        return !(iter->current != iter->end);
    }
    void npnr_inc_uphill_iter(UphillIterWrapper *iter) {
        ++iter->current;
    }
    uint64_t npnr_deref_uphill_iter(UphillIterWrapper *iter) {
        return wrap(*iter->current);
    }
    bool npnr_is_uphill_iter_done(UphillIterWrapper *iter) {
        return !(iter->current != iter->end);
    }

    extern bool npnr_router_awooter(Context *ctx, float pressure, float history);
}

NEXTPNR_NAMESPACE_BEGIN

bool router_awooter(Context *ctx) {
    auto pressure = ctx->setting<float>("awooter-pressure-factor", 0.05);
    auto history = ctx->setting<float>("awooter-history-factor", 0.04);
    log_info("Running Awooter...\n");
    auto result = npnr_router_awooter(ctx, pressure, history);
    log_info("Router returned: %d\n", result);
    NPNR_ASSERT_FALSE_STR("I haven't implemented anything beyond this yet.");
    return result;
}

NEXTPNR_NAMESPACE_END

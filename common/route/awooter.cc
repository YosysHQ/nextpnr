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
    float npnr_context_delay_epsilon(const Context *const ctx) { return ctx->getDelayNS(ctx->getDelayEpsilon()); }

    void npnr_context_check(const Context *const ctx) { ctx->check(); }
    bool npnr_context_debug(const Context *const ctx) { return ctx->debug; }
    int npnr_context_id(const Context *const ctx, const char *const str) { return ctx->id(str).hash(); }
    const char *npnr_context_name_of(const Context *const ctx, IdString str) { return ctx->nameOf(str); }
    bool npnr_context_verbose(const Context *const ctx) { return ctx->verbose; }

    uint64_t npnr_context_get_netinfo_source_wire(const Context *const ctx, const NetInfo *const net) { return wrap(ctx->getNetinfoSourceWire(net)); }
    uint64_t npnr_context_get_netinfo_sink_wire(const Context *const ctx, const NetInfo *const net, const PortRef *const sink, uint32_t n) {
        if (ctx == nullptr) {
            return 0;
        }
        return wrap(ctx->getNetinfoSinkWire(net, *sink, n));
    }

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
        // Yes, by placement-newing over `name_vec` and `nets_vec` we leak memory.
        new (&name_vec) std::vector<int>;
        new (&nets_vec) std::vector<NetInfo*>;
        return size;
    }

    // Yes, this is quadratic. It gets imported once and then never worried about again.
    // There are bigger fish to fry.
    int npnr_context_nets_key(const Context *const ctx, uint32_t n) {
        if (ctx == nullptr) {
            return 0;
        }
        for (auto& item : ctx->nets) {
            if (n == 0) {
                return item.first.hash();
            }
            n--;
        }
        return 0;
    }
    NetInfo* npnr_context_nets_value(const Context *const ctx, uint32_t n) {
        if (ctx == nullptr) {
            return nullptr;
        }
        for (auto& item : ctx->nets) {
            if (n == 0) {
                return item.second.get();
            }
            n--;
        }
        return nullptr;
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
        // Yes, by placement-newing over `x` we leak memory.
        new (&x) std::vector<PortRef*>{};
        return size;
    }

#ifdef ARCH_ECP5
    bool npnr_netinfo_is_global(NetInfo *const net) { return net->is_global; }
#else
    bool npnr_netinfo_is_global(NetInfo *const net) { return false; }
#endif

    CellInfo* npnr_portref_cell(const PortRef *const port) { return port->cell; }
    int npnr_cellinfo_get_location_x(const CellInfo *const info) { return info->getLocation().x; }
    int npnr_cellinfo_get_location_y(const CellInfo *const info) { return info->getLocation().y; }

    extern bool npnr_router_awooter(Context *ctx);
}

NEXTPNR_NAMESPACE_BEGIN

bool router_awooter(Context *ctx) {
    static_assert(std::is_standard_layout<IdString>::value == true, "IdString is not FFI-safe");

    log_info("Running Awooter...\n");
    auto result = npnr_router_awooter(ctx);
    log_info("Router returned: %d\n", result);
    NPNR_ASSERT_FALSE_STR("I haven't implemented anything beyond this yet.");
    return result;
}

NEXTPNR_NAMESPACE_END

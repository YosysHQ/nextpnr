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

#include <array>
#include <numeric>
#include "log.h"
#include "nextpnr.h"

namespace {
USING_NEXTPNR_NAMESPACE;

template <typename T> static inline uint64_t wrap(const T &thing) noexcept
{
    static_assert(sizeof(T) <= 8, "T is too big for FFI");
    uint64_t b = 0;
    memcpy(&b, &thing, sizeof(T));
    return b;
}

template <typename T> static inline T unwrap(const std::array<uint8_t, 8> &value) noexcept
{
    static_assert(sizeof(T) <= 8, "T is too big for FFI");
    T result{};
    memcpy(&result, value.data(), sizeof(result));
    return result;
}

template <typename T> static inline T unwrap(const uint64_t value) noexcept
{
    std::array<uint8_t, 8> data{};
    static_assert(sizeof(value) >= data.size(), "uint64_t is not an appropriate size");
    memcpy(data.data(), &value, data.size());
    return unwrap<T>(data);
}

static inline BelId unwrap_bel(const uint64_t bel) noexcept { return unwrap<BelId>(bel); }

static inline PipId unwrap_pip(const uint64_t pip) noexcept { return unwrap<PipId>(pip); }

static inline WireId unwrap_wire(const uint64_t wire) noexcept { return unwrap<WireId>(wire); }
} // namespace

using DownhillIter = decltype(Context(ArchArgs()).getPipsDownhill(WireId()).begin());

struct DownhillIterWrapper
{
    DownhillIter current;
    DownhillIter end;

    DownhillIterWrapper(DownhillIter begin, DownhillIter end) : current(begin), end(end) {}
};
using UphillIter = decltype(Context(ArchArgs()).getPipsUphill(WireId()).begin());

struct UphillIterWrapper
{
    UphillIter current;
    UphillIter end;

    UphillIterWrapper(UphillIter begin, UphillIter end) : current(begin), end(end) {}
};

extern "C" {
USING_NEXTPNR_NAMESPACE;

void npnr_log_info(const char *string) { log_info("%s", string); }
void npnr_log_error(const char *string) { log_error("%s", string); }

uint64_t npnr_belid_null() { return wrap(BelId()); }
uint64_t npnr_wireid_null() { return wrap(WireId()); }
uint64_t npnr_pipid_null() { return wrap(PipId()); }

int npnr_context_get_grid_dim_x(const Context *ctx) { return ctx->getGridDimX(); }
int npnr_context_get_grid_dim_y(const Context *ctx) { return ctx->getGridDimY(); }
void npnr_context_bind_bel(Context *ctx, uint64_t bel, CellInfo *cell, PlaceStrength strength)
{
    return ctx->bindBel(unwrap_bel(bel), cell, strength);
}
void npnr_context_unbind_bel(Context *ctx, uint64_t bel) { return ctx->unbindBel(unwrap_bel(bel)); }
bool npnr_context_check_bel_avail(Context *ctx, uint64_t bel) { return ctx->checkBelAvail(unwrap_bel(bel)); }
void npnr_context_bind_wire(Context *ctx, uint64_t wire, NetInfo *net, PlaceStrength strength)
{
    ctx->bindWire(unwrap_wire(wire), net, strength);
}
void npnr_context_unbind_wire(Context *ctx, uint64_t wire) { ctx->unbindWire(unwrap_wire(wire)); }
void npnr_context_bind_pip(Context *ctx, uint64_t pip, NetInfo *net, PlaceStrength strength)
{
    ctx->bindPip(unwrap_pip(pip), net, strength);
}
void npnr_context_unbind_pip(Context *ctx, uint64_t pip) { ctx->unbindPip(unwrap_pip(pip)); }
uint64_t npnr_context_get_pip_src_wire(const Context *ctx, uint64_t pip)
{
    return wrap(ctx->getPipSrcWire(unwrap_pip(pip)));
}
uint64_t npnr_context_get_pip_dst_wire(const Context *ctx, uint64_t pip)
{
    return wrap(ctx->getPipDstWire(unwrap_pip(pip)));
}
float npnr_context_estimate_delay(const Context *ctx, uint64_t src, uint64_t dst)
{
    return ctx->getDelayNS(ctx->estimateDelay(unwrap_wire(src), unwrap_wire(dst)));
}
float npnr_context_get_pip_delay(const Context *ctx, uint64_t pip)
{
    return ctx->getDelayNS(ctx->getPipDelay(unwrap_pip(pip)).maxDelay());
}
float npnr_context_get_wire_delay(const Context *ctx, uint64_t wire)
{
    return ctx->getDelayNS(ctx->getWireDelay(unwrap_wire(wire)).maxDelay());
}
float npnr_context_delay_epsilon(const Context *ctx) { return ctx->getDelayNS(ctx->getDelayEpsilon()); }
Loc npnr_context_get_pip_location(const Context *ctx, uint64_t pip) { return ctx->getPipLocation(unwrap_pip(pip)); }
bool npnr_context_check_pip_avail_for_net(const Context *ctx, uint64_t pip, NetInfo *net)
{
    return ctx->checkPipAvailForNet(unwrap_pip(pip), net);
}

uint64_t npnr_context_get_pips_leak(const Context *ctx, uint64_t **const pips)
{
    const auto ctx_pips{ctx->getPips()};
    const auto size{std::accumulate(ctx_pips.begin(), ctx_pips.end(), /*initial value*/ size_t{},
                                    [](size_t value, const auto & /*pip*/) { return value + 1U; })};
    *pips = new uint64_t[size];
    auto idx = 0;
    for (const auto &pip : ctx_pips) {
        (*pips)[idx] = wrap(pip);
        idx++;
    }
    // Yes, by never deleting pip_vec, we leak memory.
    return size;
}

uint64_t npnr_context_get_wires_leak(const Context *ctx, uint64_t **const wires)
{
    const auto ctx_wires{ctx->getWires()};
    const auto size{std::accumulate(ctx_wires.begin(), ctx_wires.end(), /*initial value*/ size_t{},
                                    [](size_t value, const auto & /*wire*/) { return value + 1U; })};
    *wires = new uint64_t[size];
    auto idx = 0;
    for (const auto &wire : ctx_wires) {
        (*wires)[idx] = wrap(wire);
        idx++;
    }
    // Yes, by never deleting wires, we leak memory.
    return size;
}

void npnr_context_check(const Context *ctx) { ctx->check(); }
bool npnr_context_debug(const Context *ctx) { return ctx->debug; }
int npnr_context_id(const Context *ctx, const char *str) { return ctx->id(str).hash(); }
const char *npnr_context_name_of(const Context *ctx, IdString str) { return ctx->nameOf(str); }
const char *npnr_context_name_of_pip(const Context *ctx, uint64_t pip) { return ctx->nameOfPip(unwrap_pip(pip)); }
const char *npnr_context_name_of_wire(const Context *ctx, uint64_t wire) { return ctx->nameOfWire(unwrap_wire(wire)); }
bool npnr_context_verbose(const Context *ctx) { return ctx->verbose; }

uint64_t npnr_context_get_netinfo_source_wire(const Context *ctx, const NetInfo *net)
{
    return wrap(ctx->getNetinfoSourceWire(net));
}
uint64_t npnr_context_get_netinfo_sink_wire(const Context *ctx, const NetInfo *net, const PortRef *sink, uint32_t n)
{
    return wrap(ctx->getNetinfoSinkWire(net, *sink, n));
}

uint32_t npnr_context_nets_leak(const Context *ctx, int **names, NetInfo ***nets)
{
    auto size = ctx->nets.size();
    *names = new int[size];
    *nets = new NetInfo *[size];
    auto idx = 0;
    for (auto &item : ctx->nets) {
        (*names)[idx] = item.first.index;
        (*nets)[idx] = item.second.get();
        idx++;
    }
    // Yes, by never deleting `names` and `nets` we leak memory.
    return size;
}

DownhillIterWrapper *npnr_context_get_pips_downhill(Context *ctx, uint64_t wire_id)
{
    auto wire = unwrap_wire(wire_id);
    auto range = ctx->getPipsDownhill(wire);
    return new DownhillIterWrapper(range.begin(), range.end());
}
void npnr_delete_downhill_iter(DownhillIterWrapper *iter) { delete iter; }
UphillIterWrapper *npnr_context_get_pips_uphill(Context *ctx, uint64_t wire_id)
{
    auto wire = unwrap_wire(wire_id);
    auto range = ctx->getPipsUphill(wire);
    return new UphillIterWrapper(range.begin(), range.end());
}
void npnr_delete_uphill_iter(UphillIterWrapper *iter) { delete iter; }

PortRef *npnr_netinfo_driver(NetInfo *net)
{
    if (net == nullptr) {
        return nullptr;
    }
    return &net->driver;
}

uint32_t npnr_netinfo_users_leak(const NetInfo *net, const PortRef ***users)
{
    auto size = net->users.entries();
    *users = new const PortRef *[size];
    auto idx = 0;
    for (auto &item : net->users) {
        (*users)[idx] = &item;
        idx++;
    }
    // Yes, by not freeing `users`, we leak memory.
    return size;
}

#ifdef ARCH_ECP5
bool npnr_netinfo_is_global(NetInfo *net) { return net->is_global; }
#else
bool npnr_netinfo_is_global(NetInfo * /*net*/) { return false; }
#endif

int32_t npnr_netinfo_udata(NetInfo *net) { return net->udata; }
void npnr_netinfo_udata_set(NetInfo *net, int32_t value) { net->udata = value; }

CellInfo *npnr_portref_cell(const PortRef *port) { return port->cell; }
Loc npnr_cellinfo_get_location(const CellInfo *info) { return info->getLocation(); }

void npnr_inc_downhill_iter(DownhillIterWrapper *iter) { ++iter->current; }
uint64_t npnr_deref_downhill_iter(DownhillIterWrapper *iter) { return wrap(*iter->current); }
bool npnr_is_downhill_iter_done(DownhillIterWrapper *iter) { return !(iter->current != iter->end); }
void npnr_inc_uphill_iter(UphillIterWrapper *iter) { ++iter->current; }
uint64_t npnr_deref_uphill_iter(UphillIterWrapper *iter) { return wrap(*iter->current); }
bool npnr_is_uphill_iter_done(UphillIterWrapper *iter) { return !(iter->current != iter->end); }

void rust_example_printnets(Context *ctx);
}

NEXTPNR_NAMESPACE_BEGIN

void example_printnets(Context *ctx) { rust_example_printnets(ctx); }

NEXTPNR_NAMESPACE_END

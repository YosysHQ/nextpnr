/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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

#include "context.h"

#include "log.h"
#include "nextpnr_namespaces.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

WireId Context::getNetinfoSourceWire(const NetInfo *net_info) const
{
    if (net_info->driver.cell == nullptr)
        return WireId();

    if (net_info->driver.cell->isPseudo())
        return net_info->driver.cell->pseudo_cell->getPortWire(net_info->driver.port);

    auto src_bel = net_info->driver.cell->bel;

    if (src_bel == BelId())
        return WireId();

    auto bel_pins = getBelPinsForCellPin(net_info->driver.cell, net_info->driver.port);
    auto iter = bel_pins.begin();
    if (iter == bel_pins.end())
        return WireId();
    WireId driver = getBelPinWire(src_bel, *iter);
    ++iter;
    NPNR_ASSERT(iter == bel_pins.end()); // assert there is only one driver bel pin;
    return driver;
}

SSOArray<WireId, 2> Context::getNetinfoSinkWires(const NetInfo *net_info, const PortRef &user_info) const
{
    if (user_info.cell->isPseudo())
        return SSOArray<WireId, 2>(1, user_info.cell->pseudo_cell->getPortWire(user_info.port));
    auto dst_bel = user_info.cell->bel;
    if (dst_bel == BelId())
        return SSOArray<WireId, 2>(0, WireId());
    size_t bel_pin_count = 0;
    // We use an SSOArray here because it avoids any heap allocation for the 99.9% case of 1 or 2 sink wires
    // but as SSOArray doesn't (currently) support resizing to keep things simple it does mean we have to do
    // two loops
    for (auto s : getBelPinsForCellPin(user_info.cell, user_info.port)) {
        (void)s; // unused
        ++bel_pin_count;
    }
    SSOArray<WireId, 2> result(bel_pin_count, WireId());
    bel_pin_count = 0;
    for (auto pin : getBelPinsForCellPin(user_info.cell, user_info.port)) {
        result[bel_pin_count++] = getBelPinWire(dst_bel, pin);
    }
    return result;
}

size_t Context::getNetinfoSinkWireCount(const NetInfo *net_info, const PortRef &sink) const
{
    size_t count = 0;
    for (auto s : getNetinfoSinkWires(net_info, sink)) {
        (void)s; // unused
        ++count;
    }
    return count;
}

WireId Context::getNetinfoSinkWire(const NetInfo *net_info, const PortRef &sink, size_t phys_idx) const
{
    size_t count = 0;
    for (auto s : getNetinfoSinkWires(net_info, sink)) {
        if (count == phys_idx)
            return s;
        ++count;
    }
    /* TODO: This should be an assertion failure, but for the zero-wire case of unplaced sinks; legacy code currently
    assumes WireId Remove once the refactoring process is complete.
    */
    return WireId();
}

delay_t Context::predictArcDelay(const NetInfo *net_info, const PortRef &sink) const
{
    if (net_info->driver.cell == nullptr || net_info->driver.cell->bel == BelId() || sink.cell->bel == BelId())
        return 0;
    IdString driver_pin, sink_pin;
    // Pick the first pin for a prediction; assume all will be similar enouhg
    for (auto pin : getBelPinsForCellPin(net_info->driver.cell, net_info->driver.port)) {
        driver_pin = pin;
        break;
    }
    for (auto pin : getBelPinsForCellPin(sink.cell, sink.port)) {
        sink_pin = pin;
        break;
    }
    if (driver_pin == IdString() || sink_pin == IdString())
        return 0;
    return predictDelay(net_info->driver.cell->bel, driver_pin, sink.cell->bel, sink_pin);
}

delay_t Context::getNetinfoRouteDelay(const NetInfo *net_info, const PortRef &user_info) const
{
#ifdef ARCH_ECP5
    if (net_info->is_global)
        return 0;
#endif

    if (net_info->wires.empty())
        return predictArcDelay(net_info, user_info);

    WireId src_wire = getNetinfoSourceWire(net_info);
    if (src_wire == WireId())
        return 0;

    DelayQuad quad_result;
    if (getArcDelayOverride(net_info, user_info, quad_result)) {
        // Arch overrides delay
        return quad_result.maxDelay();
    }

    delay_t max_delay = 0;

    for (auto dst_wire : getNetinfoSinkWires(net_info, user_info)) {
        WireId cursor = dst_wire;
        delay_t delay = 0;

        while (cursor != WireId() && cursor != src_wire) {
            auto it = net_info->wires.find(cursor);

            if (it == net_info->wires.end())
                break;

            PipId pip = it->second.pip;
            if (pip == PipId())
                break;

            delay += getPipDelay(pip).maxDelay();
            delay += getWireDelay(cursor).maxDelay();
            cursor = getPipSrcWire(pip);
        }

        if (cursor == src_wire)
            max_delay = std::max(max_delay, delay + getWireDelay(src_wire).maxDelay()); // routed
        else
            max_delay = std::max(max_delay, predictArcDelay(net_info, user_info)); // unrouted
    }
    return max_delay;
}

DelayQuad Context::getNetinfoRouteDelayQuad(const NetInfo *net_info, const PortRef &user_info) const
{
#ifdef ARCH_ECP5
    if (net_info->is_global)
        return DelayQuad(0);
#endif

    if (net_info->wires.empty())
        return DelayQuad(predictArcDelay(net_info, user_info));

    WireId src_wire = getNetinfoSourceWire(net_info);
    if (src_wire == WireId())
        return DelayQuad(0);

    DelayQuad result(std::numeric_limits<delay_t>::max(), std::numeric_limits<delay_t>::lowest());

    if (getArcDelayOverride(net_info, user_info, result)) {
        // Arch overrides delay
        return result;
    }

    for (auto dst_wire : getNetinfoSinkWires(net_info, user_info)) {
        WireId cursor = dst_wire;
        DelayQuad delay{0};

        while (cursor != WireId() && cursor != src_wire) {
            auto it = net_info->wires.find(cursor);

            if (it == net_info->wires.end())
                break;

            PipId pip = it->second.pip;
            if (pip == PipId())
                break;

            delay = delay + getPipDelay(pip);
            delay = delay + getWireDelay(cursor);
            cursor = getPipSrcWire(pip);
        }

        if (cursor == src_wire)
            delay = delay + getWireDelay(src_wire);
        else
            delay = DelayQuad(predictArcDelay(net_info, user_info)); // unrouted
        result.rise.min_delay = std::min(result.rise.min_delay, delay.rise.min_delay);
        result.rise.max_delay = std::max(result.rise.max_delay, delay.rise.max_delay);
        result.fall.min_delay = std::min(result.fall.min_delay, delay.fall.min_delay);
        result.fall.max_delay = std::max(result.fall.max_delay, delay.fall.max_delay);
    }
    return result;
}

static uint32_t xorshift32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

uint32_t Context::checksum() const
{
    uint32_t cksum = xorshift32(123456789);

    uint32_t cksum_nets_sum = 0;
    for (auto &it : nets) {
        auto &ni = *it.second;
        uint32_t x = 123456789;
        x = xorshift32(x + xorshift32(it.first.index));
        x = xorshift32(x + xorshift32(ni.name.index));
        if (ni.driver.cell)
            x = xorshift32(x + xorshift32(ni.driver.cell->name.index));
        x = xorshift32(x + xorshift32(ni.driver.port.index));

        for (auto &u : ni.users) {
            if (u.cell)
                x = xorshift32(x + xorshift32(u.cell->name.index));
            x = xorshift32(x + xorshift32(u.port.index));
        }

        uint32_t attr_x_sum = 0;
        for (auto &a : ni.attrs) {
            uint32_t attr_x = 123456789;
            attr_x = xorshift32(attr_x + xorshift32(a.first.index));
            for (char ch : a.second.str)
                attr_x = xorshift32(attr_x + xorshift32((int)ch));
            attr_x_sum += attr_x;
        }
        x = xorshift32(x + xorshift32(attr_x_sum));

        uint32_t wire_x_sum = 0;
        for (auto &w : ni.wires) {
            uint32_t wire_x = 123456789;
            wire_x = xorshift32(wire_x + xorshift32(getWireChecksum(w.first)));
            wire_x = xorshift32(wire_x + xorshift32(getPipChecksum(w.second.pip)));
            wire_x = xorshift32(wire_x + xorshift32(int(w.second.strength)));
            wire_x_sum += wire_x;
        }
        x = xorshift32(x + xorshift32(wire_x_sum));

        cksum_nets_sum += x;
    }
    cksum = xorshift32(cksum + xorshift32(cksum_nets_sum));

    uint32_t cksum_cells_sum = 0;
    for (auto &it : cells) {
        auto &ci = *it.second;
        uint32_t x = 123456789;
        x = xorshift32(x + xorshift32(it.first.index));
        x = xorshift32(x + xorshift32(ci.name.index));
        x = xorshift32(x + xorshift32(ci.type.index));

        uint32_t port_x_sum = 0;
        for (auto &p : ci.ports) {
            uint32_t port_x = 123456789;
            port_x = xorshift32(port_x + xorshift32(p.first.index));
            port_x = xorshift32(port_x + xorshift32(p.second.name.index));
            if (p.second.net)
                port_x = xorshift32(port_x + xorshift32(p.second.net->name.index));
            port_x = xorshift32(port_x + xorshift32(p.second.type));
            port_x_sum += port_x;
        }
        x = xorshift32(x + xorshift32(port_x_sum));

        uint32_t attr_x_sum = 0;
        for (auto &a : ci.attrs) {
            uint32_t attr_x = 123456789;
            attr_x = xorshift32(attr_x + xorshift32(a.first.index));
            for (char ch : a.second.str)
                attr_x = xorshift32(attr_x + xorshift32((int)ch));
            attr_x_sum += attr_x;
        }
        x = xorshift32(x + xorshift32(attr_x_sum));

        uint32_t param_x_sum = 0;
        for (auto &p : ci.params) {
            uint32_t param_x = 123456789;
            param_x = xorshift32(param_x + xorshift32(p.first.index));
            for (char ch : p.second.str)
                param_x = xorshift32(param_x + xorshift32((int)ch));
            param_x_sum += param_x;
        }
        x = xorshift32(x + xorshift32(param_x_sum));

        x = xorshift32(x + xorshift32(getBelChecksum(ci.bel)));
        x = xorshift32(x + xorshift32(ci.belStrength));

        cksum_cells_sum += x;
    }
    cksum = xorshift32(cksum + xorshift32(cksum_cells_sum));

    return cksum;
}

void Context::check() const
{
    bool check_failed = false;

#define CHECK_FAIL(...)                                                                                                \
    do {                                                                                                               \
        log_nonfatal_error(__VA_ARGS__);                                                                               \
        check_failed = true;                                                                                           \
    } while (false)

    for (auto &n : nets) {
        auto ni = n.second.get();
        if (n.first != ni->name)
            CHECK_FAIL("net key '%s' not equal to name '%s'\n", nameOf(n.first), nameOf(ni->name));
        for (auto &w : ni->wires) {
            if (ni != getBoundWireNet(w.first))
                CHECK_FAIL("net '%s' not bound to wire '%s' in wires map\n", nameOf(n.first), nameOfWire(w.first));
            if (w.second.pip != PipId()) {
                if (w.first != getPipDstWire(w.second.pip))
                    CHECK_FAIL("net '%s' has dest mismatch '%s' vs '%s' in for pip '%s'\n", nameOf(n.first),
                               nameOfWire(w.first), nameOfWire(getPipDstWire(w.second.pip)), nameOfPip(w.second.pip));
                if (ni != getBoundPipNet(w.second.pip))
                    CHECK_FAIL("net '%s' not bound to pip '%s' in wires map\n", nameOf(n.first),
                               nameOfPip(w.second.pip));
            }
        }
        if (ni->driver.cell != nullptr) {
            if (!ni->driver.cell->ports.count(ni->driver.port)) {
                CHECK_FAIL("net '%s' driver port '%s' missing on cell '%s'\n", nameOf(n.first), nameOf(ni->driver.port),
                           nameOf(ni->driver.cell));
            } else {
                const NetInfo *p_net = ni->driver.cell->ports.at(ni->driver.port).net;
                if (p_net != ni)
                    CHECK_FAIL("net '%s' driver port '%s.%s' connected to incorrect net '%s'\n", nameOf(n.first),
                               nameOf(ni->driver.cell), nameOf(ni->driver.port), p_net ? nameOf(p_net) : "<nullptr>");
            }
        }
        for (auto user : ni->users) {
            if (!user.cell->ports.count(user.port)) {
                CHECK_FAIL("net '%s' user port '%s' missing on cell '%s'\n", nameOf(n.first), nameOf(user.port),
                           nameOf(user.cell));
            } else {
                const NetInfo *p_net = user.cell->ports.at(user.port).net;
                if (p_net != ni)
                    CHECK_FAIL("net '%s' user port '%s.%s' connected to incorrect net '%s'\n", nameOf(n.first),
                               nameOf(user.cell), nameOf(user.port), p_net ? nameOf(p_net) : "<nullptr>");
            }
        }
    }
#ifdef CHECK_WIRES
    for (auto w : getWires()) {
        auto ni = getBoundWireNet(w);
        if (ni != nullptr) {
            if (!ni->wires.count(w))
                CHECK_FAIL("wire '%s' missing in wires map of bound net '%s'\n", nameOfWire(w), nameOf(ni));
        }
    }
#endif
    for (auto &c : cells) {
        auto ci = c.second.get();
        if (c.first != ci->name)
            CHECK_FAIL("cell key '%s' not equal to name '%s'\n", nameOf(c.first), nameOf(ci->name));
        if (ci->bel != BelId()) {
            if (getBoundBelCell(c.second->bel) != ci)
                CHECK_FAIL("cell '%s' not bound to bel '%s' in bel field\n", nameOf(c.first), nameOfBel(ci->bel));
        }
        for (auto &port : c.second->ports) {
            NetInfo *net = port.second.net;
            if (net != nullptr) {
                if (nets.find(net->name) == nets.end()) {
                    CHECK_FAIL("cell port '%s.%s' connected to non-existent net '%s'\n", nameOf(c.first),
                               nameOf(port.first), nameOf(net->name));
                } else if (port.second.type == PORT_OUT) {
                    if (net->driver.cell != c.second.get() || net->driver.port != port.first) {
                        CHECK_FAIL("output cell port '%s.%s' not in driver field of net '%s'\n", nameOf(c.first),
                                   nameOf(port.first), nameOf(net));
                    }
                } else if (port.second.type == PORT_IN) {
                    if (!port.second.user_idx)
                        CHECK_FAIL("input cell port '%s.%s' on net '%s' has no user index\n", nameOf(c.first),
                                   nameOf(port.first), nameOf(net));
                    auto net_user = net->users.at(port.second.user_idx);
                    if (net_user.cell != c.second.get() || net_user.port != port.first)
                        CHECK_FAIL("input cell port '%s.%s' not in associated user entry of net '%s'\n",
                                   nameOf(c.first), nameOf(port.first), nameOf(net));
                }
            }
        }
    }

#undef CHECK_FAIL

    if (check_failed)
        log_error("INTERNAL CHECK FAILED: please report this error with the design and full log output. Failure "
                  "details are above this message.\n");
}

namespace {
struct FixupHierarchyWorker
{
    FixupHierarchyWorker(Context *ctx) : ctx(ctx){};
    Context *ctx;
    void run()
    {
        trim_hierarchy(ctx->top_module);
        rebuild_hierarchy();
    };
    // Remove cells and nets that no longer exist in the netlist
    std::vector<IdString> todelete_cells, todelete_nets;
    void trim_hierarchy(IdString path)
    {
        auto &h = ctx->hierarchy.at(path);
        todelete_cells.clear();
        todelete_nets.clear();
        for (auto &lc : h.leaf_cells) {
            if (!ctx->cells.count(lc.second))
                todelete_cells.push_back(lc.first);
        }
        for (auto &n : h.nets)
            if (!ctx->nets.count(n.second))
                todelete_nets.push_back(n.first);
        for (auto tdc : todelete_cells) {
            h.leaf_cells_by_gname.erase(h.leaf_cells.at(tdc));
            h.leaf_cells.erase(tdc);
        }
        for (auto tdn : todelete_nets) {
            h.nets_by_gname.erase(h.nets.at(tdn));
            h.nets.erase(tdn);
        }
        for (auto &sc : h.hier_cells)
            trim_hierarchy(sc.second);
    }

    IdString construct_local_name(HierarchicalCell &hc, IdString global_name, bool is_cell)
    {
        std::string gn = global_name.str(ctx);
        auto dp = gn.find_last_of('.');
        if (dp != std::string::npos)
            gn = gn.substr(dp + 1);
        IdString name = ctx->id(gn);
        // Make sure name is unique
        int adder = 0;
        while (is_cell ? hc.leaf_cells.count(name) : hc.nets.count(name)) {
            ++adder;
            name = ctx->id(gn + "$" + std::to_string(adder));
        }
        return name;
    }

    // Update hierarchy structure for nets and cells that have hiercell set
    void rebuild_hierarchy()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->hierpath == IdString())
                ci->hierpath = ctx->top_module;
            auto &hc = ctx->hierarchy.at(ci->hierpath);
            if (hc.leaf_cells_by_gname.count(ci->name))
                continue; // already known
            IdString local_name = construct_local_name(hc, ci->name, true);
            hc.leaf_cells_by_gname[ci->name] = local_name;
            hc.leaf_cells[local_name] = ci->name;
        }
    }
};
} // namespace

void Context::fixupHierarchy() { FixupHierarchyWorker(this).run(); }

NEXTPNR_NAMESPACE_END

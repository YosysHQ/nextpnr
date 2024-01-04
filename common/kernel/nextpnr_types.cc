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

#include "nextpnr_types.h"
#include "context.h"
#include "log.h"

#include "nextpnr_namespaces.h"

#include <type_traits>

NEXTPNR_NAMESPACE_BEGIN

// Invariant: architecture ID types must all be trivially copyable
static_assert(std::is_trivially_copyable<BelId>::value == true);
static_assert(std::is_trivially_copyable<WireId>::value == true);
static_assert(std::is_trivially_copyable<PipId>::value == true);

void CellInfo::addInput(IdString name)
{
    ports[name].name = name;
    ports[name].type = PORT_IN;
}
void CellInfo::addOutput(IdString name)
{
    ports[name].name = name;
    ports[name].type = PORT_OUT;
}
void CellInfo::addInout(IdString name)
{
    ports[name].name = name;
    ports[name].type = PORT_INOUT;
}

void CellInfo::setParam(IdString name, Property value) { params[name] = value; }
void CellInfo::unsetParam(IdString name) { params.erase(name); }
void CellInfo::setAttr(IdString name, Property value) { attrs[name] = value; }
void CellInfo::unsetAttr(IdString name) { attrs.erase(name); }

bool CellInfo::testRegion(BelId bel) const
{
    return region == nullptr || !region->constr_bels || region->bels.count(bel);
}

void CellInfo::connectPort(IdString port_name, NetInfo *net)
{
    if (net == nullptr)
        return;
    PortInfo &port = ports.at(port_name);
    NPNR_ASSERT(port.net == nullptr);
    port.net = net;
    if (port.type == PORT_OUT) {
        NPNR_ASSERT(net->driver.cell == nullptr);
        net->driver.cell = this;
        net->driver.port = port_name;
    } else if (port.type == PORT_IN || port.type == PORT_INOUT) {
        PortRef user;
        user.cell = this;
        user.port = port_name;
        port.user_idx = net->users.add(user);
    } else {
        NPNR_ASSERT_FALSE("invalid port type for connectPort");
    }
}

void CellInfo::disconnectPort(IdString port_name)
{
    if (!ports.count(port_name))
        return;
    PortInfo &port = ports.at(port_name);
    if (port.net != nullptr) {
        if (port.user_idx)
            port.net->users.remove(port.user_idx);
        if (port.net->driver.cell == this && port.net->driver.port == port_name)
            port.net->driver.cell = nullptr;
        port.net = nullptr;
    }
}

void CellInfo::connectPorts(IdString port, CellInfo *other, IdString other_port)
{
    PortInfo &port1 = ports.at(port);
    if (port1.net == nullptr) {
        // No net on port1; need to create one
        NetInfo *p1net = ctx->createNet(ctx->id(name.str(ctx) + "$conn$" + port.str(ctx)));
        connectPort(port, p1net);
    }
    other->connectPort(other_port, port1.net);
}

void CellInfo::movePortTo(IdString port, CellInfo *other, IdString other_port)
{
    if (!ports.count(port))
        return;
    PortInfo &old = ports.at(port);

    // Create port on the replacement cell if it doesn't already exist
    if (!other->ports.count(other_port)) {
        other->ports[other_port].name = other_port;
        other->ports[other_port].type = old.type;
    }

    PortInfo &rep = other->ports.at(other_port);
    NPNR_ASSERT(old.type == rep.type);

    rep.net = old.net;
    rep.user_idx = old.user_idx;
    old.net = nullptr;
    old.user_idx = store_index<PortRef>{};
    if (rep.type == PORT_OUT) {
        if (rep.net != nullptr) {
            rep.net->driver.cell = other;
            rep.net->driver.port = other_port;
        }
    } else if (rep.type == PORT_IN) {
        if (rep.net != nullptr) {
            auto &load = rep.net->users.at(rep.user_idx);
            load.cell = other;
            load.port = other_port;
        }
    } else {
        NPNR_ASSERT(false);
    }
}

void CellInfo::renamePort(IdString old_name, IdString new_name)
{
    if (!ports.count(old_name))
        return;
    PortInfo pi = ports.at(old_name);
    if (pi.net != nullptr) {
        if (pi.net->driver.cell == this && pi.net->driver.port == old_name)
            pi.net->driver.port = new_name;
        if (pi.user_idx)
            pi.net->users.at(pi.user_idx).port = new_name;
    }
    ports.erase(old_name);
    pi.name = new_name;
    ports[new_name] = pi;
}

void CellInfo::movePortBusTo(IdString old_name, int old_offset, bool old_brackets, CellInfo *new_cell,
                             IdString new_name, int new_offset, bool new_brackets, int width)
{
    for (int i = 0; i < width; i++) {
        IdString old_port = ctx->idf(old_brackets ? "%s[%d]" : "%s%d", old_name.c_str(ctx), i + old_offset);
        IdString new_port = ctx->idf(new_brackets ? "%s[%d]" : "%s%d", new_name.c_str(ctx), i + new_offset);
        movePortTo(old_port, new_cell, new_port);
    }
}

void CellInfo::copyPortTo(IdString port, CellInfo *other, IdString other_port)
{
    if (!ports.count(port))
        return;
    other->ports[other_port].name = other_port;
    other->ports[other_port].type = ports.at(port).type;
    other->connectPort(other_port, ports.at(port).net);
}

void CellInfo::copyPortBusTo(IdString old_name, int old_offset, bool old_brackets, CellInfo *new_cell,
                             IdString new_name, int new_offset, bool new_brackets, int width)
{
    for (int i = 0; i < width; i++) {
        IdString old_port = ctx->idf(old_brackets ? "%s[%d]" : "%s%d", old_name.c_str(ctx), i + old_offset);
        IdString new_port = ctx->idf(new_brackets ? "%s[%d]" : "%s%d", new_name.c_str(ctx), i + new_offset);
        copyPortTo(old_port, new_cell, new_port);
    }
}

Loc CellInfo::getLocation() const
{
    if (pseudo_cell) {
        return pseudo_cell->getLocation();
    } else {
        NPNR_ASSERT(bel != BelId());
        return ctx->getBelLocation(bel);
    }
}

NEXTPNR_NAMESPACE_END

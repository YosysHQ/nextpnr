/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#include "nextpnr.h"
#include <boost/algorithm/string.hpp>
#include "design_utils.h"
#include "log.h"
#include "util.h"

#if defined(__wasm)
extern "C" {
    // FIXME: WASI does not currently support exceptions.
    void* __cxa_allocate_exception(size_t thrown_size) throw() {
        return malloc(thrown_size);
    }
    bool __cxa_uncaught_exception() throw();
    void __cxa_throw(void* thrown_exception, struct std::type_info * tinfo, void (*dest)(void*)) {
        std::terminate();
    }
}

namespace boost {
    void throw_exception( std::exception const & e ) {
        NEXTPNR_NAMESPACE::log_error("boost::exception(): %s\n", e.what());
    }
}
#endif

NEXTPNR_NAMESPACE_BEGIN

assertion_failure::assertion_failure(std::string msg, std::string expr_str, std::string filename, int line)
        : runtime_error("Assertion failure: " + msg + " (" + filename + ":" + std::to_string(line) + ")"), msg(msg),
          expr_str(expr_str), filename(filename), line(line)
{
    log_flush();
}

void IdString::set(const BaseCtx *ctx, const std::string &s)
{
    auto it = ctx->idstring_str_to_idx->find(s);
    if (it == ctx->idstring_str_to_idx->end()) {
        index = ctx->idstring_idx_to_str->size();
        auto insert_rc = ctx->idstring_str_to_idx->insert({s, index});
        ctx->idstring_idx_to_str->push_back(&insert_rc.first->first);
    } else {
        index = it->second;
    }
}

const std::string &IdString::str(const BaseCtx *ctx) const { return *ctx->idstring_idx_to_str->at(index); }

const char *IdString::c_str(const BaseCtx *ctx) const { return str(ctx).c_str(); }

void IdString::initialize_add(const BaseCtx *ctx, const char *s, int idx)
{
    NPNR_ASSERT(ctx->idstring_str_to_idx->count(s) == 0);
    NPNR_ASSERT(int(ctx->idstring_idx_to_str->size()) == idx);
    auto insert_rc = ctx->idstring_str_to_idx->insert({s, idx});
    ctx->idstring_idx_to_str->push_back(&insert_rc.first->first);
}

TimingConstrObjectId BaseCtx::timingWildcardObject()
{
    TimingConstrObjectId id;
    id.index = 0;
    return id;
}

TimingConstrObjectId BaseCtx::timingClockDomainObject(NetInfo *clockDomain)
{
    NPNR_ASSERT(clockDomain->clkconstr != nullptr);
    if (clockDomain->clkconstr->domain_tmg_id != TimingConstrObjectId()) {
        return clockDomain->clkconstr->domain_tmg_id;
    } else {
        TimingConstraintObject obj;
        TimingConstrObjectId id;
        id.index = int(constraintObjects.size());
        obj.id = id;
        obj.type = TimingConstraintObject::CLOCK_DOMAIN;
        obj.entity = clockDomain->name;
        clockDomain->clkconstr->domain_tmg_id = id;
        constraintObjects.push_back(obj);
        return id;
    }
}

TimingConstrObjectId BaseCtx::timingNetObject(NetInfo *net)
{
    if (net->tmg_id != TimingConstrObjectId()) {
        return net->tmg_id;
    } else {
        TimingConstraintObject obj;
        TimingConstrObjectId id;
        id.index = int(constraintObjects.size());
        obj.id = id;
        obj.type = TimingConstraintObject::NET;
        obj.entity = net->name;
        constraintObjects.push_back(obj);
        net->tmg_id = id;
        return id;
    }
}

TimingConstrObjectId BaseCtx::timingCellObject(CellInfo *cell)
{
    if (cell->tmg_id != TimingConstrObjectId()) {
        return cell->tmg_id;
    } else {
        TimingConstraintObject obj;
        TimingConstrObjectId id;
        id.index = int(constraintObjects.size());
        obj.id = id;
        obj.type = TimingConstraintObject::CELL;
        obj.entity = cell->name;
        constraintObjects.push_back(obj);
        cell->tmg_id = id;
        return id;
    }
}

TimingConstrObjectId BaseCtx::timingPortObject(CellInfo *cell, IdString port)
{
    if (cell->ports.at(port).tmg_id != TimingConstrObjectId()) {
        return cell->ports.at(port).tmg_id;
    } else {
        TimingConstraintObject obj;
        TimingConstrObjectId id;
        id.index = int(constraintObjects.size());
        obj.id = id;
        obj.type = TimingConstraintObject::CELL_PORT;
        obj.entity = cell->name;
        obj.port = port;
        constraintObjects.push_back(obj);
        cell->ports.at(port).tmg_id = id;
        return id;
    }
}

Property::Property() : is_string(false), str(""), intval(0) {}

Property::Property(int64_t intval, int width) : is_string(false), intval(intval)
{
    str.reserve(width);
    for (int i = 0; i < width; i++)
        str.push_back((intval & (1ULL << i)) ? S1 : S0);
}

Property::Property(const std::string &strval) : is_string(true), str(strval), intval(0xDEADBEEF) {}

Property::Property(State bit) : is_string(false), str(std::string("") + char(bit)), intval(bit == S1) {}

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

std::string Property::to_string() const
{
    if (is_string) {
        std::string result = str;
        int state = 0;
        for (char c : str) {
            if (state == 0) {
                if (c == '0' || c == '1' || c == 'x' || c == 'z')
                    state = 0;
                else if (c == ' ')
                    state = 1;
                else
                    state = 2;
            } else if (state == 1 && c != ' ')
                state = 2;
        }
        if (state < 2)
            result += " ";
        return result;
    } else {
        return std::string(str.rbegin(), str.rend());
    }
}

Property Property::from_string(const std::string &s)
{
    Property p;

    size_t cursor = s.find_first_not_of("01xz");
    if (cursor == std::string::npos) {
        p.str = std::string(s.rbegin(), s.rend());
        p.is_string = false;
        p.update_intval();
    } else if (s.find_first_not_of(' ', cursor) == std::string::npos) {
        p = Property(s.substr(0, s.size() - 1));
    } else {
        p = Property(s);
    }
    return p;
}

void BaseCtx::addConstraint(std::unique_ptr<TimingConstraint> constr)
{
    for (auto fromObj : constr->from)
        constrsFrom.emplace(fromObj, constr.get());
    for (auto toObj : constr->to)
        constrsTo.emplace(toObj, constr.get());
    IdString name = constr->name;
    constraints[name] = std::move(constr);
}

void BaseCtx::removeConstraint(IdString constrName)
{
    TimingConstraint *constr = constraints[constrName].get();
    for (auto fromObj : constr->from) {
        auto fromConstrs = constrsFrom.equal_range(fromObj);
        constrsFrom.erase(std::find(fromConstrs.first, fromConstrs.second, std::make_pair(fromObj, constr)));
    }
    for (auto toObj : constr->to) {
        auto toConstrs = constrsFrom.equal_range(toObj);
        constrsFrom.erase(std::find(toConstrs.first, toConstrs.second, std::make_pair(toObj, constr)));
    }
    constraints.erase(constrName);
}

const char *BaseCtx::nameOfBel(BelId bel) const
{
    const Context *ctx = getCtx();
    return ctx->getBelName(bel).c_str(ctx);
}

const char *BaseCtx::nameOfWire(WireId wire) const
{
    const Context *ctx = getCtx();
    return ctx->getWireName(wire).c_str(ctx);
}

const char *BaseCtx::nameOfPip(PipId pip) const
{
    const Context *ctx = getCtx();
    return ctx->getPipName(pip).c_str(ctx);
}

const char *BaseCtx::nameOfGroup(GroupId group) const
{
    const Context *ctx = getCtx();
    return ctx->getGroupName(group).c_str(ctx);
}

WireId Context::getNetinfoSourceWire(const NetInfo *net_info) const
{
    if (net_info->driver.cell == nullptr)
        return WireId();

    auto src_bel = net_info->driver.cell->bel;

    if (src_bel == BelId())
        return WireId();

    IdString driver_port = net_info->driver.port;

    auto driver_port_it = net_info->driver.cell->pins.find(driver_port);
    if (driver_port_it != net_info->driver.cell->pins.end())
        driver_port = driver_port_it->second;

    return getBelPinWire(src_bel, driver_port);
}

WireId Context::getNetinfoSinkWire(const NetInfo *net_info, const PortRef &user_info) const
{
    auto dst_bel = user_info.cell->bel;

    if (dst_bel == BelId())
        return WireId();

    IdString user_port = user_info.port;

    auto user_port_it = user_info.cell->pins.find(user_port);

    if (user_port_it != user_info.cell->pins.end())
        user_port = user_port_it->second;

    return getBelPinWire(dst_bel, user_port);
}

delay_t Context::getNetinfoRouteDelay(const NetInfo *net_info, const PortRef &user_info) const
{
#ifdef ARCH_ECP5
    if (net_info->is_global)
        return 0;
#endif

    if (net_info->wires.empty())
        return predictDelay(net_info, user_info);

    WireId src_wire = getNetinfoSourceWire(net_info);
    if (src_wire == WireId())
        return 0;

    WireId dst_wire = getNetinfoSinkWire(net_info, user_info);
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
        return delay + getWireDelay(src_wire).maxDelay();

    return predictDelay(net_info, user_info);
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
        x = xorshift32(x + xorshift32(getDelayChecksum(ni.driver.budget)));

        for (auto &u : ni.users) {
            if (u.cell)
                x = xorshift32(x + xorshift32(u.cell->name.index));
            x = xorshift32(x + xorshift32(u.port.index));
            x = xorshift32(x + xorshift32(getDelayChecksum(u.budget)));
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

        uint32_t pin_x_sum = 0;
        for (auto &a : ci.pins) {
            uint32_t pin_x = 123456789;
            pin_x = xorshift32(pin_x + xorshift32(a.first.index));
            pin_x = xorshift32(pin_x + xorshift32(a.second.index));
            pin_x_sum += pin_x;
        }
        x = xorshift32(x + xorshift32(pin_x_sum));

        cksum_cells_sum += x;
    }
    cksum = xorshift32(cksum + xorshift32(cksum_cells_sum));

    return cksum;
}

void Context::check() const
{
    for (auto &n : nets) {
        auto ni = n.second.get();
        NPNR_ASSERT(n.first == ni->name);
        for (auto &w : ni->wires) {
            NPNR_ASSERT(ni == getBoundWireNet(w.first));
            if (w.second.pip != PipId()) {
                NPNR_ASSERT(w.first == getPipDstWire(w.second.pip));
                NPNR_ASSERT(ni == getBoundPipNet(w.second.pip));
            }
        }
        if (ni->driver.cell != nullptr)
            NPNR_ASSERT(ni->driver.cell->ports.at(ni->driver.port).net == ni);
        for (auto user : ni->users) {
            NPNR_ASSERT(user.cell->ports.at(user.port).net == ni);
        }
    }

    for (auto w : getWires()) {
        auto ni = getBoundWireNet(w);
        if (ni != nullptr) {
            NPNR_ASSERT(ni->wires.count(w));
        }
    }

    for (auto &c : cells) {
        auto ci = c.second.get();
        NPNR_ASSERT(c.first == ci->name);
        if (ci->bel != BelId())
            NPNR_ASSERT(getBoundBelCell(c.second->bel) == ci);
        for (auto &port : c.second->ports) {
            NetInfo *net = port.second.net;
            if (net != nullptr) {
                NPNR_ASSERT(nets.find(net->name) != nets.end());
                if (port.second.type == PORT_OUT) {
                    NPNR_ASSERT(net->driver.cell == c.second.get() && net->driver.port == port.first);
                } else if (port.second.type == PORT_IN) {
                    NPNR_ASSERT(std::count_if(net->users.begin(), net->users.end(), [&](const PortRef &pr) {
                                    return pr.cell == c.second.get() && pr.port == port.first;
                                }) == 1);
                }
            }
        }
    }
}

void BaseCtx::addClock(IdString net, float freq)
{
    std::unique_ptr<ClockConstraint> cc(new ClockConstraint());
    cc->period = getCtx()->getDelayFromNS(1000 / freq);
    cc->high = getCtx()->getDelayFromNS(500 / freq);
    cc->low = getCtx()->getDelayFromNS(500 / freq);
    if (!net_aliases.count(net)) {
        log_warning("net '%s' does not exist in design, ignoring clock constraint\n", net.c_str(this));
    } else {
        getNetByAlias(net)->clkconstr = std::move(cc);
        log_info("constraining clock net '%s' to %.02f MHz\n", net.c_str(this), freq);
    }
}

void BaseCtx::createRectangularRegion(IdString name, int x0, int y0, int x1, int y1)
{
    std::unique_ptr<Region> new_region(new Region());
    new_region->name = name;
    new_region->constr_bels = true;
    new_region->constr_pips = false;
    new_region->constr_wires = false;
    for (int x = x0; x <= x1; x++) {
        for (int y = y0; y <= y1; y++) {
            for (auto bel : getCtx()->getBelsByTile(x, y))
                new_region->bels.insert(bel);
        }
    }
    region[name] = std::move(new_region);
}
void BaseCtx::addBelToRegion(IdString name, BelId bel) { region[name]->bels.insert(bel); }
void BaseCtx::constrainCellToRegion(IdString cell, IdString region_name)
{
    // Support hierarchical cells as well as leaf ones
    if (hierarchy.count(cell)) {
        auto &hc = hierarchy.at(cell);
        for (auto &lc : hc.leaf_cells)
            constrainCellToRegion(lc.second, region_name);
        for (auto &hsc : hc.hier_cells)
            constrainCellToRegion(hsc.second, region_name);
    }
    if (cells.count(cell))
        cells.at(cell)->region = region[region_name].get();
}
DecalXY BaseCtx::constructDecalXY(DecalId decal, float x, float y)
{
    DecalXY dxy;
    dxy.decal = decal;
    dxy.x = x;
    dxy.y = y;
    return dxy;
}

void BaseCtx::archInfoToAttributes()
{
    for (auto &cell : cells) {
        auto ci = cell.second.get();
        if (ci->bel != BelId()) {
            if (ci->attrs.find(id("BEL")) != ci->attrs.end()) {
                ci->attrs.erase(ci->attrs.find(id("BEL")));
            }
            ci->attrs[id("NEXTPNR_BEL")] = getCtx()->getBelName(ci->bel).str(this);
            ci->attrs[id("BEL_STRENGTH")] = (int)ci->belStrength;
        }
        if (ci->constr_x != ci->UNCONSTR)
            ci->attrs[id("CONSTR_X")] = ci->constr_x;
        if (ci->constr_y != ci->UNCONSTR)
            ci->attrs[id("CONSTR_Y")] = ci->constr_y;
        if (ci->constr_z != ci->UNCONSTR) {
            ci->attrs[id("CONSTR_Z")] = ci->constr_z;
            ci->attrs[id("CONSTR_ABS_Z")] = ci->constr_abs_z ? 1 : 0;
        }
        if (ci->constr_parent != nullptr)
            ci->attrs[id("CONSTR_PARENT")] = ci->constr_parent->name.str(this);
        if (!ci->constr_children.empty()) {
            std::string constr = "";
            for (auto &item : ci->constr_children) {
                if (!constr.empty())
                    constr += std::string(";");
                constr += item->name.c_str(this);
            }
            ci->attrs[id("CONSTR_CHILDREN")] = constr;
        }
    }
    for (auto &net : getCtx()->nets) {
        auto ni = net.second.get();
        std::string routing;
        bool first = true;
        for (auto &item : ni->wires) {
            if (!first)
                routing += ";";
            routing += getCtx()->getWireName(item.first).c_str(this);
            routing += ";";
            if (item.second.pip != PipId())
                routing += getCtx()->getPipName(item.second.pip).c_str(this);
            routing += ";" + std::to_string(item.second.strength);
            first = false;
        }
        ni->attrs[id("ROUTING")] = routing;
    }
}

void BaseCtx::attributesToArchInfo()
{
    for (auto &cell : cells) {
        auto ci = cell.second.get();
        auto val = ci->attrs.find(id("NEXTPNR_BEL"));
        if (val != ci->attrs.end()) {
            auto str = ci->attrs.find(id("BEL_STRENGTH"));
            PlaceStrength strength = PlaceStrength::STRENGTH_USER;
            if (str != ci->attrs.end())
                strength = (PlaceStrength)str->second.as_int64();

            BelId b = getCtx()->getBelByName(id(val->second.as_string()));
            getCtx()->bindBel(b, ci, strength);
        }

        val = ci->attrs.find(id("CONSTR_PARENT"));
        if (val != ci->attrs.end()) {
            auto parent = cells.find(id(val->second.str));
            if (parent != cells.end())
                ci->constr_parent = parent->second.get();
            else
                continue;
        }

        val = ci->attrs.find(id("CONSTR_X"));
        if (val != ci->attrs.end())
            ci->constr_x = val->second.as_int64();

        val = ci->attrs.find(id("CONSTR_Y"));
        if (val != ci->attrs.end())
            ci->constr_y = val->second.as_int64();

        val = ci->attrs.find(id("CONSTR_Z"));
        if (val != ci->attrs.end())
            ci->constr_z = val->second.as_int64();

        val = ci->attrs.find(id("CONSTR_ABS_Z"));
        if (val != ci->attrs.end())
            ci->constr_abs_z = val->second.as_int64() == 1;

        val = ci->attrs.find(id("CONSTR_PARENT"));
        if (val != ci->attrs.end()) {
            auto parent = cells.find(id(val->second.as_string()));
            if (parent != cells.end())
                ci->constr_parent = parent->second.get();
        }
        val = ci->attrs.find(id("CONSTR_CHILDREN"));
        if (val != ci->attrs.end()) {
            std::vector<std::string> strs;
            auto children = val->second.as_string();
            boost::split(strs, children, boost::is_any_of(";"));
            for (auto val : strs) {
                if (cells.count(id(val.c_str())))
                    ci->constr_children.push_back(cells.find(id(val.c_str()))->second.get());
            }
        }
    }
    for (auto &net : getCtx()->nets) {
        auto ni = net.second.get();
        auto val = ni->attrs.find(id("ROUTING"));
        if (val != ni->attrs.end()) {
            std::vector<std::string> strs;
            auto routing = val->second.as_string();
            boost::split(strs, routing, boost::is_any_of(";"));
            for (size_t i = 0; i < strs.size() / 3; i++) {
                std::string wire = strs[i * 3];
                std::string pip = strs[i * 3 + 1];
                PlaceStrength strength = (PlaceStrength)std::stoi(strs[i * 3 + 2]);
                if (pip.empty())
                    getCtx()->bindWire(getCtx()->getWireByName(id(wire)), ni, strength);
                else
                    getCtx()->bindPip(getCtx()->getPipByName(id(pip)), ni, strength);
            }
        }
    }
    getCtx()->assignArchInfo();
}

NetInfo *BaseCtx::createNet(IdString name)
{
    NPNR_ASSERT(!nets.count(name));
    NPNR_ASSERT(!net_aliases.count(name));
    std::unique_ptr<NetInfo> net{new NetInfo};
    net->name = name;
    net_aliases[name] = name;
    NetInfo *ptr = net.get();
    nets[name] = std::move(net);
    refreshUi();
    return ptr;
}

void BaseCtx::connectPort(IdString net, IdString cell, IdString port)
{
    NetInfo *net_info = getNetByAlias(net);
    CellInfo *cell_info = cells.at(cell).get();
    connect_port(getCtx(), net_info, cell_info, port);
}

void BaseCtx::disconnectPort(IdString cell, IdString port)
{
    CellInfo *cell_info = cells.at(cell).get();
    disconnect_port(getCtx(), cell_info, port);
}

void BaseCtx::ripupNet(IdString name)
{
    NetInfo *net_info = getNetByAlias(name);
    std::vector<WireId> to_unbind;
    for (auto &wire : net_info->wires)
        to_unbind.push_back(wire.first);
    for (auto &unbind : to_unbind)
        getCtx()->unbindWire(unbind);
}
void BaseCtx::lockNetRouting(IdString name)
{
    NetInfo *net_info = getNetByAlias(name);
    for (auto &wire : net_info->wires)
        wire.second.strength = STRENGTH_USER;
}

CellInfo *BaseCtx::createCell(IdString name, IdString type)
{
    NPNR_ASSERT(!cells.count(name));
    std::unique_ptr<CellInfo> cell{new CellInfo};
    cell->name = name;
    cell->type = type;
    CellInfo *ptr = cell.get();
    cells[name] = std::move(cell);
    refreshUi();
    return ptr;
}

void BaseCtx::copyBelPorts(IdString cell, BelId bel)
{
    CellInfo *cell_info = cells.at(cell).get();
    for (auto pin : getCtx()->getBelPins(bel)) {
        cell_info->ports[pin].name = pin;
        cell_info->ports[pin].type = getCtx()->getBelPinType(bel, pin);
    }
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
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
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

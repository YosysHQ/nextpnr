/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2020  Pepijn de Vos <pepijn@symbioticeda.com>
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

#include <boost/algorithm/string.hpp>
#include <cells.h>
#include <iostream>
#include <math.h>
#include <regex>
#include "design_utils.h"
#include "embed.h"
#include "gfx.h"
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

const PairPOD *pairLookup(const PairPOD *list, const size_t len, const int dest);

// GUI
void Arch::fixClockSpineDecals(void)
{
    for (auto sp : clockSpinesCache) {
        // row: (#of spine cells, wire_id)
        dict<int, std::pair<int, IdString>> rows;
        IdString min_x, max_x;
        min_x = max_x = *sp.second.begin();
        for (auto wire : sp.second) {
            WireInfo &wi = wire_info(wire);
            std::pair<int, IdString> &row = rows[wi.y];
            ++row.first;
            row.second = wire;
            if (wi.x < wire_info(min_x).x) {
                min_x = wire;
            } else {
                if (wi.x > wire_info(max_x).x) {
                    max_x = wire;
                }
            }
        }
        // central mux row owns the global decal
        int mux_row = -1;
        for (auto row : rows) {
            if (row.second.first == 1) {
                mux_row = row.first;
                break;
            }
        }
        // if there is no separate central mux than all decals are the same
        if (mux_row == -1) {
            mux_row = rows.begin()->first;
            WireInfo &wi = wire_info(rows.at(mux_row).second);
            GraphicElement &el_active = decal_graphics.at(wi.decalxy_active.decal).at(0);
            GraphicElement &el_inactive = decal_graphics.at(wi.decalxy_inactive.decal).at(0);
            el_active.y1 -= wi.y;
            el_active.y2 -= wi.y;
            el_inactive.y1 -= wi.y;
            el_inactive.y2 -= wi.y;
            el_active.x1 += wire_info(min_x).x;
            el_active.x2 += wire_info(max_x).x;
            el_inactive.x1 += wire_info(min_x).x;
            el_inactive.x2 += wire_info(max_x).x;
        } else {
            // change the global decal
            WireInfo &wi = wire_info(rows.at(mux_row).second);
            // clear spine decals
            float y = 0.;
            for (auto wire : sp.second) {
                if (wire == wi.name) {
                    continue;
                }
                wire_info(wire).decalxy_active = DecalXY();
                wire_info(wire).decalxy_inactive = DecalXY();
                y = wire_info(wire).y;
            }
            GraphicElement &el_active = decal_graphics.at(wi.decalxy_active.decal).at(0);
            GraphicElement &el_inactive = decal_graphics.at(wi.decalxy_inactive.decal).at(0);
            el_active.y1 -= y;
            el_active.y2 -= y;
            el_inactive.y1 -= y;
            el_inactive.y2 -= y;
            el_active.x1 += wire_info(min_x).x;
            el_active.x2 += wire_info(max_x).x;
            el_inactive.x1 += wire_info(min_x).x;
            el_inactive.x2 += wire_info(max_x).x;
        }
        refreshUi();
    }
}

void Arch::updateClockSpinesCache(IdString spine_id, IdString wire_id)
{
    std::vector<IdString> &sp = clockSpinesCache[spine_id];
    if (std::find(sp.begin(), sp.end(), wire_id) == sp.end()) {
        sp.push_back(wire_id);
    }
}

DecalXY Arch::getBelDecal(BelId bel) const
{
    CellInfo *ci = getBoundBelCell(bel);
    if (ci == nullptr) {
        return bels.at(bel).decalxy_inactive;
    } else {
        // LUT + used/unused DFF
        if (bels.at(bel).type == id_SLICE) {
            DecalXY decalxy = bels.at(bel).decalxy_active;
            if (!ci->params.at(id_FF_USED).as_bool()) {
                decalxy.decal = id_DECAL_LUT_UNUSED_DFF_ACTIVE;
                if (ci->params.count(id_ALU_MODE) != 0) {
                    decalxy.decal = id_DECAL_ALU_ACTIVE;
                }
            }
            return decalxy;
        }
    }
    return bels.at(bel).decalxy_active;
}

DecalXY Arch::getGroupDecal(GroupId grp) const { return groups.at(grp).decalxy; }

DecalXY Arch::getPipDecal(PipId pip) const
{
    if (getBoundPipNet(pip) == nullptr) {
        return pips.at(pip).decalxy_inactive;
    }
    return pips.at(pip).decalxy_active;
}

DecalXY Arch::getWireDecal(WireId wire) const
{
    static std::vector<IdString> clk_wires = {id_GB00, id_GB10, id_GB20, id_GB30, id_GB40, id_GB50, id_GB60, id_GB70};
    static std::vector<IdString> pip_dst = {id_CLK0, id_CLK1, id_CLK2, id_EW10, id_EW20, id_SN10, id_SN20};
    if (getBoundWireNet(wire) == nullptr) {
        if (std::find(clk_wires.begin(), clk_wires.end(), wires.at(wire).type) != clk_wires.end()) {
            for (auto dst : pip_dst) {
                // check if pip is used
                char pip_name[20];
                snprintf(pip_name, sizeof(pip_name), "%s_%s", wire.c_str(this), dst.c_str(this));
                if (pips.count(id(pip_name)) != 0) {
                    if (getBoundPipNet(id(pip_name)) != nullptr) {
                        return wires.at(wire).decalxy_active;
                    }
                }
            }
        } else {
            // spines
            if (clockSpinesCache.count(wires.at(wire).type) != 0) {
                std::vector<IdString> const &sp = clockSpinesCache.at(wires.at(wire).type);
                for (auto w : sp) {
                    if (getBoundWireNet(w) != nullptr) {
                        return wires.at(wire).decalxy_active;
                    }
                }
            }
        }
        return wires.at(wire).decalxy_inactive;
    }
    return wires.at(wire).decalxy_active;
}

bool Arch::allocate_longwire(NetInfo *ni, int lw_idx)
{
    NPNR_ASSERT(ni != nullptr);
    if (ni->driver.cell == nullptr) {
        return false;
    }
    if (ni->name == id("$PACKER_VCC_NET") || ni->name == id("$PACKER_GND_NET")) {
        return false;
    }
    // So far only for OBUF
    switch (ni->driver.cell->type.index) {
    case ID_ODDR:  /* fall-through*/
    case ID_ODDRC: /* fall-through*/
    case ID_IOBUF: /* fall-through*/
    case ID_TBUF:
        return false;
    case ID_OBUF:
        if (getCtx()->debug) {
            log_info("Long wire for IO %s\n", nameOf(ni));
        }
        ni = ni->driver.cell->ports.at(id_I).net;
        return allocate_longwire(ni, lw_idx);
        break;
    default:
        break;
    }

    if (getCtx()->debug) {
        log_info("Requested index:%d\n", lw_idx);
    }
    if (avail_longwires == 0 || (lw_idx != -1 && (avail_longwires & (1 << lw_idx)) == 0)) {
        return false;
    }
    int longwire = lw_idx;
    if (lw_idx == -1) {
        for (longwire = 7; longwire >= 0; --longwire) {
            if (avail_longwires & (1 << longwire)) {
                break;
            }
        }
    }
    avail_longwires &= ~(1 << longwire);

    // BUFS cell
    CellInfo *bufs;
    char buf[40];
    snprintf(buf, sizeof(buf), "$PACKER_BUFS%d", longwire);
    std::unique_ptr<CellInfo> new_cell = create_generic_cell(getCtx(), id_BUFS, buf);
    bufs = new_cell.get();
    cells[bufs->name] = std::move(new_cell);
    if (lw_idx != -1) {
        bufs->cluster = bufs->name;
        bufs->constr_z = lw_idx + BelZ::bufs_0_z;
        bufs->constr_abs_z = true;
        bufs->constr_children.clear();
    }

    // old driver -> bufs LW input net
    auto net = std::make_unique<NetInfo>(idf("$PACKER_BUFS_%c", longwire + 'A'));
    NetInfo *bufs_net = net.get();
    nets[net->name] = std::move(net);

    // split the net
    CellInfo *driver_cell = ni->driver.cell;
    IdString driver_port = ni->driver.port;
    driver_cell->disconnectPort(driver_port);

    bufs->connectPort(id_O, ni);
    bufs->connectPort(id_I, bufs_net);
    driver_cell->connectPort(driver_port, bufs_net);

    if (getCtx()->debug) {
        log_info("Long wire %d was allocated\n", longwire);
    }
    return true;
}

void Arch::auto_longwires() {}

void Arch::fix_longwire_bels()
{
    // After routing, it is clear which wires and in which bus SS00 and SS40 are used and
    // in which quadrant they are routed. Here we write it in the attributes.
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type != id_BUFS) {
            continue;
        }
        const NetInfo *ni = ci->getPort(id_O);
        if (ni == nullptr) {
            continue;
        }
        // bus wire is one of the wires
        // value does not matter, but the L/R parameter itself
        for (auto &wire : ni->wires) {
            WireId w = wires[wire.first].type;
            switch (w.hash()) {
            case ID_LWSPINETL0:
            case ID_LWSPINETL1:
            case ID_LWSPINETL2:
            case ID_LWSPINETL3:
            case ID_LWSPINETL4:
            case ID_LWSPINETL5:
            case ID_LWSPINETL6:
            case ID_LWSPINETL7:
            case ID_LWSPINEBL0:
            case ID_LWSPINEBL1:
            case ID_LWSPINEBL2:
            case ID_LWSPINEBL3:
            case ID_LWSPINEBL4:
            case ID_LWSPINEBL5:
            case ID_LWSPINEBL6:
            case ID_LWSPINEBL7:
                ci->setParam(id("L"), Property(w.str(this)));
                break;
            case ID_LWSPINETR0:
            case ID_LWSPINETR1:
            case ID_LWSPINETR2:
            case ID_LWSPINETR3:
            case ID_LWSPINETR4:
            case ID_LWSPINETR5:
            case ID_LWSPINETR6:
            case ID_LWSPINETR7:
            case ID_LWSPINEBR0:
            case ID_LWSPINEBR1:
            case ID_LWSPINEBR2:
            case ID_LWSPINEBR3:
            case ID_LWSPINEBR4:
            case ID_LWSPINEBR5:
            case ID_LWSPINEBR6:
            case ID_LWSPINEBR7:
                ci->setParam(id("R"), Property(w.str(this)));
                break;
            default:
                break;
            }
        }
    }
}

WireInfo &Arch::wire_info(IdString wire)
{
    auto w = wires.find(wire);
    if (w == wires.end())
        NPNR_ASSERT_FALSE_STR("no wire named " + wire.str(this));
    return w->second;
}

PipInfo &Arch::pip_info(IdString pip)
{
    auto p = pips.find(pip);
    if (p == pips.end())
        NPNR_ASSERT_FALSE_STR("no pip named " + pip.str(this));
    return p->second;
}

BelInfo &Arch::bel_info(IdString bel)
{
    auto b = bels.find(bel);
    if (b == bels.end())
        NPNR_ASSERT_FALSE_STR("no bel named " + bel.str(this));
    return b->second;
}

NetInfo &Arch::net_info(IdString net)
{
    auto b = nets.find(net);
    if (b == nets.end())
        NPNR_ASSERT_FALSE_STR("no net named " + net.str(this));
    return *b->second;
}

void Arch::addWire(IdString name, IdString type, int x, int y)
{
    NPNR_ASSERT(!wires.count(name));
    WireInfo &wi = wires[name];
    wi.name = name;
    wi.type = type;
    wi.x = x;
    wi.y = y;

    wire_ids.push_back(name);

    // Needed to ensure empty tile bel locations
    if (int(bels_by_tile.size()) <= x)
        bels_by_tile.resize(x + 1);
    if (int(bels_by_tile[x].size()) <= y)
        bels_by_tile[x].resize(y + 1);
    if (int(tileBelDimZ.size()) <= x)
        tileBelDimZ.resize(x + 1);
    if (int(tileBelDimZ[x].size()) <= y)
        tileBelDimZ[x].resize(y + 1);
}

void Arch::addPip(IdString name, IdString type, IdString srcWire, IdString dstWire, DelayQuad delay, Loc loc)
{
    NPNR_ASSERT(pips.count(name) == 0);
    PipInfo &pi = pips[name];
    pi.name = name;
    pi.type = type;
    pi.srcWire = srcWire;
    pi.dstWire = dstWire;
    pi.delay = delay;
    pi.loc = loc;

    wire_info(srcWire).downhill.push_back(name);
    wire_info(dstWire).uphill.push_back(name);
    pip_ids.push_back(name);

    if (int(tilePipDimZ.size()) <= loc.x)
        tilePipDimZ.resize(loc.x + 1);

    if (int(tilePipDimZ[loc.x].size()) <= loc.y)
        tilePipDimZ[loc.x].resize(loc.y + 1);

    // Needed to ensure empty tile bel locations
    if (int(bels_by_tile.size()) <= loc.x)
        bels_by_tile.resize(loc.x + 1);
    if (int(bels_by_tile[loc.x].size()) <= loc.y)
        bels_by_tile[loc.x].resize(loc.y + 1);
    if (int(tileBelDimZ.size()) <= loc.x)
        tileBelDimZ.resize(loc.x + 1);
    if (int(tileBelDimZ[loc.x].size()) <= loc.y)
        tileBelDimZ[loc.x].resize(loc.y + 1);

    gridDimX = std::max(gridDimX, loc.x + 1);
    gridDimY = std::max(gridDimY, loc.y + 1);
    tilePipDimZ[loc.x][loc.y] = std::max(tilePipDimZ[loc.x][loc.y], loc.z + 1);
}

void Arch::addGroup(IdString name)
{
    NPNR_ASSERT(groups.count(name) == 0);
    GroupInfo &gi = groups[name];
    gi.name = name;
}

void Arch::addBel(IdString name, IdString type, Loc loc, bool gb)
{
    NPNR_ASSERT(bels.count(name) == 0);
    NPNR_ASSERT(bel_by_loc.count(loc) == 0);
    BelInfo &bi = bels[name];
    bi.name = name;
    bi.type = type;
    bi.x = loc.x;
    bi.y = loc.y;
    bi.z = loc.z;
    bi.gb = gb;

    bel_ids.push_back(name);
    bel_by_loc[loc] = name;

    if (int(bels_by_tile.size()) <= loc.x)
        bels_by_tile.resize(loc.x + 1);

    if (int(bels_by_tile[loc.x].size()) <= loc.y)
        bels_by_tile[loc.x].resize(loc.y + 1);

    bels_by_tile[loc.x][loc.y].push_back(name);

    if (int(tileBelDimZ.size()) <= loc.x)
        tileBelDimZ.resize(loc.x + 1);

    if (int(tileBelDimZ[loc.x].size()) <= loc.y)
        tileBelDimZ[loc.x].resize(loc.y + 1);

    gridDimX = std::max(gridDimX, loc.x + 1);
    gridDimY = std::max(gridDimY, loc.y + 1);
    tileBelDimZ[loc.x][loc.y] = std::max(tileBelDimZ[loc.x][loc.y], loc.z + 1);
}

void Arch::addBelInput(IdString bel, IdString name, IdString wire)
{
    NPNR_ASSERT(bel_info(bel).pins.count(name) == 0);
    PinInfo &pi = bel_info(bel).pins[name];
    pi.name = name;
    pi.wire = wire;
    pi.type = PORT_IN;

    wire_info(wire).downhill_bel_pins.push_back(BelPin{bel, name});
    wire_info(wire).bel_pins.push_back(BelPin{bel, name});
}

void Arch::addBelOutput(IdString bel, IdString name, IdString wire)
{
    NPNR_ASSERT(bel_info(bel).pins.count(name) == 0);
    PinInfo &pi = bel_info(bel).pins[name];
    pi.name = name;
    pi.wire = wire;
    pi.type = PORT_OUT;

    wire_info(wire).uphill_bel_pin = BelPin{bel, name};
    wire_info(wire).bel_pins.push_back(BelPin{bel, name});
}

void Arch::addBelInout(IdString bel, IdString name, IdString wire)
{
    NPNR_ASSERT(bel_info(bel).pins.count(name) == 0);
    PinInfo &pi = bel_info(bel).pins[name];
    pi.name = name;
    pi.wire = wire;
    pi.type = PORT_INOUT;

    wire_info(wire).downhill_bel_pins.push_back(BelPin{bel, name});
    wire_info(wire).bel_pins.push_back(BelPin{bel, name});
}

void Arch::addGroupBel(IdString group, IdString bel) { groups[group].bels.push_back(bel); }

void Arch::addGroupWire(IdString group, IdString wire) { groups[group].wires.push_back(wire); }

void Arch::addGroupPip(IdString group, IdString pip) { groups[group].pips.push_back(pip); }

void Arch::addGroupGroup(IdString group, IdString grp) { groups[group].groups.push_back(grp); }

void Arch::addDecalGraphic(DecalId decal, const GraphicElement &graphic)
{
    decal_graphics[decal].push_back(graphic);
    refreshUi();
}

void Arch::setWireDecal(WireId wire, DecalXY active, DecalXY inactive)
{
    wire_info(wire).decalxy_active = active;
    wire_info(wire).decalxy_inactive = inactive;
    refreshUiWire(wire);
}

void Arch::setPipDecal(PipId pip, DecalXY active, DecalXY inactive)
{
    pip_info(pip).decalxy_active = active;
    pip_info(pip).decalxy_inactive = inactive;
    refreshUiPip(pip);
}

void Arch::setBelDecal(BelId bel, DecalXY active, DecalXY inactive)
{
    bel_info(bel).decalxy_active = active;
    bel_info(bel).decalxy_inactive = inactive;
    refreshUiBel(bel);
}

void Arch::setDefaultDecals(void)
{
#ifndef NO_GUI
    for (BelId bel : getBels()) {
        gfxSetBelDefaultDecal(this, bel_info(bel));
    }
    for (PipId pip : getPips()) {
        gfxSetPipDefaultDecal(this, pip_info(pip));
    }
    for (WireId wire : getWires()) {
        gfxSetWireDefaultDecal(this, wire_info(wire));
    }
    fixClockSpineDecals();
#endif
}

void Arch::setGroupDecal(GroupId group, DecalXY decalxy)
{
    groups[group].decalxy = decalxy;
    refreshUiGroup(group);
}

void Arch::setWireAttr(IdString wire, IdString key, const std::string &value) { wire_info(wire).attrs[key] = value; }

void Arch::setPipAttr(IdString pip, IdString key, const std::string &value) { pip_info(pip).attrs[key] = value; }

void Arch::setBelAttr(IdString bel, IdString key, const std::string &value) { bel_info(bel).attrs[key] = value; }

void Arch::setDelayScaling(double scale, double offset)
{
    args.delayScale = scale;
    args.delayOffset = offset;
}

void Arch::addCellTimingClass(IdString cell, IdString port, TimingPortClass cls)
{
    cellTiming[cell].portClasses[port] = cls;
}

void Arch::addCellTimingClock(IdString cell, IdString port) { cellTiming[cell].portClasses[port] = TMG_CLOCK_INPUT; }

void Arch::addCellTimingDelay(IdString cell, IdString fromPort, IdString toPort, DelayQuad delay)
{
    if (get_or_default(cellTiming[cell].portClasses, fromPort, TMG_IGNORE) == TMG_IGNORE)
        cellTiming[cell].portClasses[fromPort] = TMG_COMB_INPUT;
    if (get_or_default(cellTiming[cell].portClasses, toPort, TMG_IGNORE) == TMG_IGNORE)
        cellTiming[cell].portClasses[toPort] = TMG_COMB_OUTPUT;
    cellTiming[cell].combDelays[CellDelayKey{fromPort, toPort}] = delay;
}

void Arch::addCellTimingSetupHold(IdString cell, IdString port, IdString clock, DelayPair setup, DelayPair hold)
{
    TimingClockingInfo ci;
    ci.clock_port = clock;
    ci.edge = RISING_EDGE;
    ci.setup = setup;
    ci.hold = hold;
    cellTiming[cell].clockingInfo[port].push_back(ci);
    cellTiming[cell].portClasses[port] = TMG_REGISTER_INPUT;
}

void Arch::addCellTimingClockToOut(IdString cell, IdString port, IdString clock, DelayQuad clktoq)
{
    TimingClockingInfo ci;
    ci.clock_port = clock;
    ci.edge = RISING_EDGE;
    ci.clockToQ = clktoq;
    cellTiming[cell].clockingInfo[port].push_back(ci);
    cellTiming[cell].portClasses[port] = TMG_REGISTER_OUTPUT;
}

// ---------------------------------------------------------------

IdString Arch::apply_local_aliases(int row, int col, const DatabasePOD *db, IdString &wire)
{
    const TilePOD *tile = db->grid[row * db->cols + col].get();
    auto local_alias = pairLookup(tile->aliases.get(), tile->num_aliases, wire.index);
    IdString res_wire = IdString();
    if (local_alias != nullptr) {
        wire = IdString(local_alias->src_id);
        res_wire = idf("R%dC%d_%s", row + 1, col + 1, wire.c_str(this));
    }
    return res_wire;
}

// TODO represent wires more intelligently.
IdString Arch::wireToGlobal(int &row, int &col, const DatabasePOD *db, IdString &wire)
{
    const std::string &wirename = wire.str(this);
    if (wirename == "VCC" || wirename == "VSS") {
        row = 0;
        col = 0;
        return wire;
    }
    if (!isdigit(wirename[1]) || !isdigit(wirename[2]) || !isdigit(wirename[3])) {
        IdString res_wire = apply_local_aliases(row, col, db, wire);
        if (res_wire == IdString()) {
            return idf("R%dC%d_%s", row + 1, col + 1, wirename.c_str());
        }
        return res_wire;
    }
    char direction = wirename[0];
    int num = std::stoi(wirename.substr(1, 2));
    int segment = std::stoi(wirename.substr(3, 1));
    switch (direction) {
    case 'N':
        row += segment;
        break;
    case 'S':
        row -= segment;
        break;
    case 'E':
        col -= segment;
        break;
    case 'W':
        col += segment;
        break;
    default:
        return idf("R%dC%d_%s", row + 1, col + 1, wirename.c_str());
        break;
    }
    // wires wrap around the edges
    // assumes 0-based indexes
    if (row < 0) {
        row = -1 - row;
        direction = 'N';
    } else if (col < 0) {
        col = -1 - col;
        direction = 'W';
    } else if (row >= db->rows) {
        row = 2 * db->rows - 1 - row;
        direction = 'S';
    } else if (col >= db->cols) {
        col = 2 * db->cols - 1 - col;
        direction = 'E';
    }
    wire = idf("%c%d0", direction, num);
    // local aliases
    IdString res_wire = apply_local_aliases(row, col, db, wire);
    if (res_wire == IdString()) {
        res_wire = idf("R%dC%d_%c%d", row + 1, col + 1, direction, num);
    }
    return res_wire;
}

const PairPOD *pairLookup(const PairPOD *list, const size_t len, const int dest)
{
    for (size_t i = 0; i < len; i++) {
        const PairPOD *pair = &list[i];
        if (pair->dest_id == dest) {
            return pair;
        }
    }
    return nullptr;
}

const PinPOD *pinLookup(const PinPOD *list, const size_t len, const int idx)
{
    for (size_t i = 0; i < len; i++) {
        const PinPOD *pin = &list[i];
        if (pin->index_id == idx) {
            return pin;
        }
    }
    return nullptr;
}

bool aliasCompare(GlobalAliasPOD i, GlobalAliasPOD j)
{
    return (i.dest_row < j.dest_row) || (i.dest_row == j.dest_row && i.dest_col < j.dest_col) ||
           (i.dest_row == j.dest_row && i.dest_col == j.dest_col && i.dest_id < j.dest_id);
}

bool timingCompare(TimingPOD i, TimingPOD j) { return i.name_id < j.name_id; }

template <class T, class C> const T *genericLookup(const T *first, int len, const T val, C compare)
{
    auto res = std::lower_bound(first, first + len, val, compare);
    if (res - first != len && !compare(val, *res)) {
        return res;
    } else {
        return nullptr;
    }
}

template <class T, class C> const T *timingLookup(const T *first, int len, const T val, C compare)
{
    for (int i = 0; i < len; ++i) {
        auto res = &first[i];
        if (!(compare(*res, val) || compare(val, *res))) {
            return res;
        }
    }
    return nullptr;
}

DelayQuad delayLookup(const TimingPOD *first, int len, IdString name)
{
    TimingPOD needle;
    needle.name_id = name.index;
    const TimingPOD *timing = timingLookup(first, len, needle, timingCompare);
    DelayQuad delay;
    if (timing != nullptr) {
        delay.fall.max_delay = std::max(timing->ff, timing->rf) / 1000.;
        delay.fall.min_delay = std::min(timing->ff, timing->rf) / 1000.;
        delay.rise.max_delay = std::max(timing->rr, timing->fr) / 1000.;
        delay.rise.min_delay = std::min(timing->rr, timing->fr) / 1000.;
    } else {
        delay = DelayQuad(0);
    }
    return delay;
}

DelayQuad Arch::getWireTypeDelay(IdString wire)
{
    IdString len;
    IdString glbsrc;
    switch (wire.index) {
    case ID_X01:
    case ID_X02:
    case ID_X03:
    case ID_X04:
    case ID_X05:
    case ID_X06:
    case ID_X07:
    case ID_X08:
    case ID_I0:
    case ID_I1:
        len = id_X0;
        break;
    case ID_N100:
    case ID_N130:
    case ID_S100:
    case ID_S130:
    case ID_E100:
    case ID_E130:
    case ID_W100:
    case ID_W130:
    case ID_E110:
    case ID_W110:
    case ID_E120:
    case ID_W120:
    case ID_S110:
    case ID_N110:
    case ID_S120:
    case ID_N120:
    case ID_SN10:
    case ID_SN20:
    case ID_EW10:
    case ID_EW20:
    case ID_I01:
        len = id_FX1;
        break;
    case ID_N200:
    case ID_N210:
    case ID_N220:
    case ID_N230:
    case ID_N240:
    case ID_N250:
    case ID_N260:
    case ID_N270:
    case ID_S200:
    case ID_S210:
    case ID_S220:
    case ID_S230:
    case ID_S240:
    case ID_S250:
    case ID_S260:
    case ID_S270:
    case ID_E200:
    case ID_E210:
    case ID_E220:
    case ID_E230:
    case ID_E240:
    case ID_E250:
    case ID_E260:
    case ID_E270:
    case ID_W200:
    case ID_W210:
    case ID_W220:
    case ID_W230:
    case ID_W240:
    case ID_W250:
    case ID_W260:
    case ID_W270:
        len = id_X2;
        break;
    case ID_N800:
    case ID_N810:
    case ID_N820:
    case ID_N830:
    case ID_S800:
    case ID_S810:
    case ID_S820:
    case ID_S830:
    case ID_E800:
    case ID_E810:
    case ID_E820:
    case ID_E830:
    case ID_W800:
    case ID_W810:
    case ID_W820:
    case ID_W830:
        len = id_X8;
        break;
    case ID_LT02:
    case ID_LT13:
        glbsrc = id_SPINE_TAP_SCLK_0;
        break;
    case ID_LT01:
    case ID_LT04:
        glbsrc = id_SPINE_TAP_SCLK_1;
        break;
    case ID_LBO0:
    case ID_LBO1:
        glbsrc = id_TAP_BRANCH_SCLK;
        break;
    case ID_LB01:
    case ID_LB11:
    case ID_LB21:
    case ID_LB31:
    case ID_LB41:
    case ID_LB51:
    case ID_LB61:
    case ID_LB71:
        glbsrc = id_BRANCH_SCLK;
        break;
    case ID_GT00:
    case ID_GT10:
        glbsrc = id_SPINE_TAP_PCLK;
        break;
    case ID_GBO0:
    case ID_GBO1:
        glbsrc = id_TAP_BRANCH_PCLK;
        break;
    case ID_GB00:
    case ID_GB10:
    case ID_GB20:
    case ID_GB30:
    case ID_GB40:
    case ID_GB50:
    case ID_GB60:
    case ID_GB70:
        glbsrc = id_BRANCH_PCLK;
        break;
    default:
        if (wire.str(this).rfind("LWSPINE", 0) == 0) {
            glbsrc = IdString(ID_CENT_SPINE_SCLK);
        } else if (wire.str(this).rfind("SPINE", 0) == 0) {
            glbsrc = IdString(ID_CENT_SPINE_PCLK);
        } else if (wire.str(this).rfind("UNK", 0) == 0) {
            glbsrc = IdString(ID_PIO_CENT_PCLK);
        }
        break;
    }
    if (len != IdString()) {
        return delayLookup(speed->wire.timings.get(), speed->wire.num_timings, len);
    } else if (glbsrc != IdString()) {
        return delayLookup(speed->glbsrc.timings.get(), speed->glbsrc.num_timings, glbsrc);
    } else {
        return DelayQuad(0);
    }
}

static Loc getLoc(std::smatch match, int maxX, int maxY)
{
    int col = std::stoi(match[2]);
    int row = 1; // Top
    std::string side = match[1].str();
    if (side == "R") {
        row = col;
        col = maxX;
    } else if (side == "B") {
        row = maxY;
    } else if (side == "L") {
        row = col;
        col = 1;
    }
    int z = match[3].str()[0] - 'A';
    return Loc(col - 1, row - 1, z);
}

void Arch::read_cst(std::istream &in)
{
    // If two locations are specified separated by commas (for differential I/O buffers),
    // only the first location is actually recognized and used.
    // And pin A will be Positive and pin B will be Negative in any case.
    std::regex iobre = std::regex("IO_LOC +\"([^\"]+)\" +([^ ,;]+)(, *[^ ;]+)? *;.*[\\s\\S]*");
    std::regex portre = std::regex("IO_PORT +\"([^\"]+)\" +([^;]+;).*[\\s\\S]*");
    std::regex port_attrre = std::regex("([^ =;]+=[^ =;]+) *([^;]*;)");
    std::regex iobelre = std::regex("IO([TRBL])([0-9]+)\\[?([A-Z])\\]?");
    std::regex inslocre =
            std::regex("INS_LOC +\"([^\"]+)\" +R([0-9]+)C([0-9]+)\\[([0-9])\\]\\[([AB])\\] *;.*[\\s\\S]*");
    std::regex clockre = std::regex("CLOCK_LOC +\"([^\"]+)\" +BUF([GS])(\\[([0-7])\\])?[^;]*;.*[\\s\\S]*");
    std::smatch match, match_attr, match_pinloc;
    std::string line, pinline;
    enum
    {
        ioloc,
        ioport,
        insloc,
        clock
    } cst_type;

    settings.erase(id_cst);
    while (!in.eof()) {
        std::getline(in, line);
        cst_type = ioloc;
        if (!std::regex_match(line, match, iobre)) {
            if (std::regex_match(line, match, portre)) {
                cst_type = ioport;
            } else {
                if (std::regex_match(line, match, clockre)) {
                    cst_type = clock;
                } else {
                    if (std::regex_match(line, match, inslocre)) {
                        cst_type = insloc;
                    } else {
                        if ((!line.empty()) && (line.rfind("//", 0) == std::string::npos)) {
                            log_warning("Invalid constraint: %s\n", line.c_str());
                        }
                        continue;
                    }
                }
            }
        }

        IdString net = id(match[1]);
        auto it = cells.find(net);
        if (cst_type != clock && it == cells.end()) {
            log_info("Cell %s not found\n", net.c_str(this));
            continue;
        }
        switch (cst_type) {
        case clock: { // CLOCK name BUFG|S=#
            std::string which_clock = match[2];
            std::string lw = match[4];
            int lw_idx = -1;
            if (lw.length() > 0) {
                lw_idx = atoi(lw.c_str());
                log_info("lw_idx:%d\n", lw_idx);
            }
            if (which_clock.at(0) == 'S') {
                auto ni = nets.find(net);
                if (ni == nets.end()) {
                    log_info("Net %s not found\n", net.c_str(this));
                    continue;
                }
                if (!allocate_longwire(ni->second.get(), lw_idx)) {
                    log_info("Can't use the long wires. The %s network will use normal routing.\n", net.c_str(this));
                }
            } else {
                log_info("BUFG isn't supported\n");
                continue;
            }
        } break;
        case ioloc: { // IO_LOC name pin
            IdString pinname = id(match[2]);
            pinline = match[2];
            const PinPOD *belname = pinLookup(package->pins.get(), package->num_pins, pinname.index);
            if (belname != nullptr) {
                std::string bel = IdString(belname->loc_id).str(this);
                it->second->setAttr(IdString(ID_BEL), bel);
            } else {
                if (std::regex_match(pinline, match_pinloc, iobelre)) {
                    // may be it's IOx#[AB] style?
                    Loc loc = getLoc(match_pinloc, getGridDimX(), getGridDimY());
                    BelId bel = getBelByLocation(loc);
                    if (bel == BelId()) {
                        log_error("Pin %s not found (TRBL style). \n", pinline.c_str());
                    }
                    std::string belname = getCtx()->nameOfBel(bel);
                    it->second->setAttr(IdString(ID_BEL), belname);
                } else {
                    log_error("Pin %s not found (pin# style)\n", pinname.c_str(this));
                }
            }
        } break;
        case insloc: { // INS_LOC
            int slice = std::stoi(match[4].str()) * 2;
            if (match[5].str() == "B") {
                ++slice;
            }
            std::string belname = std::string("R") + match[2].str() + "C" + match[3].str() + stringf("_SLICE%d", slice);
            it->second->setAttr(IdString(ID_BEL), belname);
        } break;
        default: { // IO_PORT attr=value
            std::string attr_val = match[2];
            while (std::regex_match(attr_val, match_attr, port_attrre)) {
                std::string attr = "&";
                attr += match_attr[1];
                boost::algorithm::to_upper(attr);
                it->second->setAttr(id(attr), 1);
                attr_val = match_attr[2];
            }
        }
        }
    }
    settings[id_cst] = 1;
}

// Add all MUXes for the cell
void Arch::addMuxBels(const DatabasePOD *db, int row, int col)
{
    IdString belname, bel_id;
    char buf[40];
    int z;

    // make all wide luts with these parameters
    struct
    {
        char type;         // MUX type 5,6,7,8
        char bel_idx;      // just bel name suffix
        char in_prefix[2]; // input from F or OF
        char in_idx[2];    // input from bel with idx
    } const mux_names[] = {{'5', '0', "", {'0', '1'}},  {'6', '0', "O", {'2', '0'}}, {'5', '1', "", {'2', '3'}},
                           {'7', '0', "O", {'5', '1'}}, {'5', '2', "", {'4', '5'}},  {'6', '1', "O", {'6', '4'}},
                           {'5', '3', "", {'6', '7'}},  {'8', '0', "O", {'3', '3'}}};

    // 4 MUX2_LUT5, 2 MUX2_LUT6, 1 MUX2_LUT7, 1 MUX2_LUT8
    for (int j = 0; j < 8; ++j) {
        z = j + BelZ::mux_0_z;

        int grow = row + 1;
        int gcol = col + 1;

        // no MUX2_LUT8 in the last column
        if (j == 7 && col == getGridDimX() - 2) {
            continue;
        }

        // bel
        snprintf(buf, 40, "R%dC%d_MUX2_LUT%c%c", grow, gcol, mux_names[j].type, mux_names[j].bel_idx);
        belname = id(buf);
        snprintf(buf, 40, "MUX2_LUT%c", mux_names[j].type);
        bel_id = id(buf);
        addBel(belname, bel_id, Loc(col, row, z), false);

        // dummy wires
        snprintf(buf, 40, "I0MUX%d", j);
        IdString id_wire_i0 = id(buf);
        IdString wire_i0_name = wireToGlobal(row, col, db, id_wire_i0);
        addWire(wire_i0_name, id_wire_i0, col, row);

        snprintf(buf, 40, "I1MUX%d", j);
        IdString id_wire_i1 = id(buf);
        IdString wire_i1_name = wireToGlobal(row, col, db, id_wire_i1);
        addWire(wire_i1_name, id_wire_i1, col, row);

        // dummy right pip
        DelayQuad delay = getWireTypeDelay(id_I0);
        snprintf(buf, 40, "%sF%c", mux_names[j].in_prefix, mux_names[j].in_idx[1]);
        IdString id_src_F = id(buf);
        IdString src_F = wireToGlobal(row, col, db, id_src_F);
        snprintf(buf, 40, "R%dC%d_%s_DUMMY_%s", grow, gcol, id_src_F.c_str(this), id_wire_i1.c_str(this));
        addPip(id(buf), id_wire_i1, src_F, wire_i1_name, delay, Loc(col, row, 0));

        // dummy left pip
        snprintf(buf, 40, "%sF%c", mux_names[j].in_prefix, mux_names[j].in_idx[0]);
        id_src_F = id(buf);
        // LUT8's I0 is wired to the right cell
        int src_col = col;
        if (j == 7) {
            ++src_col;
            delay = getWireTypeDelay(id_I01);
        }
        src_F = wireToGlobal(row, src_col, db, id_src_F);
        snprintf(buf, 40, "R%dC%d_%s_DUMMY_%s", grow, gcol, id_src_F.c_str(this), id_wire_i0.c_str(this));
        addPip(id(buf), id_wire_i0, src_F, wire_i0_name, delay, Loc(col, row, 0));

        // the MUX ports
        snprintf(buf, 40, "R%dC%d_OF%d", grow, gcol, j);
        addBelOutput(belname, id_OF, id(buf));
        snprintf(buf, 40, "R%dC%d_SEL%d", grow, gcol, j);
        addBelInput(belname, id_SEL, id(buf));
        snprintf(buf, 40, "R%dC%d_I0MUX%d", grow, gcol, j);
        addBelInput(belname, id_I0, id(buf));
        snprintf(buf, 40, "R%dC%d_I1MUX%d", grow, gcol, j);
        addBelInput(belname, id_I1, id(buf));
    }
}

void Arch::add_pllvr_ports(DatabasePOD const *db, BelsPOD const *bel, IdString belname, int row, int col)
{
    IdString portname;

    for (int pid :
         {ID_CLKIN,   ID_CLKFB,   ID_FBDSEL0, ID_FBDSEL1, ID_FBDSEL2, ID_FBDSEL3, ID_FBDSEL4, ID_FBDSEL5, ID_IDSEL0,
          ID_IDSEL1,  ID_IDSEL2,  ID_IDSEL3,  ID_IDSEL4,  ID_IDSEL5,  ID_ODSEL0,  ID_ODSEL1,  ID_ODSEL2,  ID_ODSEL3,
          ID_ODSEL4,  ID_ODSEL5,  ID_VREN,    ID_PSDA0,   ID_PSDA1,   ID_PSDA2,   ID_PSDA3,   ID_DUTYDA0, ID_DUTYDA1,
          ID_DUTYDA2, ID_DUTYDA3, ID_FDLY0,   ID_FDLY1,   ID_FDLY2,   ID_FDLY3,   ID_RESET,   ID_RESET_P}) {
        portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, pid)->src_id);
        IdString wire = idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
        if (!wires.count(wire)) {
            GlobalAliasPOD alias;
            alias.dest_col = col;
            alias.dest_row = row;
            alias.dest_id = portname.hash();
            auto alias_src = genericLookup(db->aliases.get(), db->num_aliases, alias, aliasCompare);
            NPNR_ASSERT(alias_src != nullptr);
            int srcrow = alias_src->src_row;
            int srccol = alias_src->src_col;
            IdString srcid = IdString(alias_src->src_id);
            wire = wireToGlobal(srcrow, srccol, db, srcid);
            if (!wires.count(wire)) {
                addWire(wire, srcid, srccol, srcrow);
            }
        }
        addBelInput(belname, IdString(pid), wire);
    }
    for (int pid : {ID_LOCK, ID_CLKOUT, ID_CLKOUTP, ID_CLKOUTD, ID_CLKOUTD3}) {
        portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, pid)->src_id);
        addBelOutput(belname, IdString(pid), idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
    }
}

void Arch::add_rpll_ports(DatabasePOD const *db, BelsPOD const *bel, IdString belname, int row, int col)
{
    IdString portname;

    for (int pid :
         {ID_CLKIN,   ID_CLKFB,  ID_FBDSEL0, ID_FBDSEL1, ID_FBDSEL2, ID_FBDSEL3, ID_FBDSEL4, ID_FBDSEL5, ID_IDSEL0,
          ID_IDSEL1,  ID_IDSEL2, ID_IDSEL3,  ID_IDSEL4,  ID_IDSEL5,  ID_ODSEL0,  ID_ODSEL1,  ID_ODSEL2,  ID_ODSEL3,
          ID_ODSEL4,  ID_ODSEL5, ID_PSDA0,   ID_PSDA1,   ID_PSDA2,   ID_PSDA3,   ID_DUTYDA0, ID_DUTYDA1, ID_DUTYDA2,
          ID_DUTYDA3, ID_FDLY0,  ID_FDLY1,   ID_FDLY2,   ID_FDLY3,   ID_RESET,   ID_RESET_P}) {
        const PairPOD *port = pairLookup(bel->ports.get(), bel->num_ports, pid);
        // old base
        if (port == nullptr) {
            log_warning("When building nextpnr, obsolete old apicula bases were used. Probably not working properly "
                        "with PLL.\n");
            return;
        }
        portname = IdString(port->src_id);
        IdString wire = idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
        if (!wires.count(wire)) {
            GlobalAliasPOD alias;
            alias.dest_col = col;
            alias.dest_row = row;
            alias.dest_id = portname.hash();
            auto alias_src = genericLookup(db->aliases.get(), db->num_aliases, alias, aliasCompare);
            NPNR_ASSERT(alias_src != nullptr);
            int srcrow = alias_src->src_row;
            int srccol = alias_src->src_col;
            IdString srcid = IdString(alias_src->src_id);
            wire = wireToGlobal(srcrow, srccol, db, srcid);
            if (!wires.count(wire)) {
                addWire(wire, srcid, srccol, srcrow);
            }
        }
        addBelInput(belname, IdString(pid), wire);
    }
    for (int pid : {ID_LOCK, ID_CLKOUT, ID_CLKOUTP, ID_CLKOUTD, ID_CLKOUTD3}) {
        portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, pid)->src_id);
        IdString wire = idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
        if (!wires.count(wire)) {
            GlobalAliasPOD alias;
            alias.dest_col = col;
            alias.dest_row = row;
            alias.dest_id = portname.hash();
            auto alias_src = genericLookup(db->aliases.get(), db->num_aliases, alias, aliasCompare);
            NPNR_ASSERT(alias_src != nullptr);
            int srcrow = alias_src->src_row;
            int srccol = alias_src->src_col;
            IdString srcid = IdString(alias_src->src_id);
            wire = wireToGlobal(srcrow, srccol, db, srcid);
            if (!wires.count(wire)) {
                addWire(wire, srcid, srccol, srcrow);
            }
        }
        addBelOutput(belname, IdString(pid), wire);
    }
}

static bool skip_aux_oser16(std::string device, int row, int col)
{
    if (device == "GW1NSR-4C") {
        switch (col) {
        case 2:  /* fall-through*/
        case 4:  /* fall-through*/
        case 6:  /* fall-through*/
        case 8:  /* fall-through*/
        case 9:  /* fall-through*/
        case 11: /* fall-through*/
        case 13: /* fall-through*/
        case 15: /* fall-through*/
        case 17: /* fall-through*/
        case 18: /* fall-through*/
        case 20: /* fall-through*/
        case 22: /* fall-through*/
        case 24: /* fall-through*/
        case 26: /* fall-through*/
        case 27: /* fall-through*/
        case 29: /* fall-through*/
        case 31: /* fall-through*/
        case 33: /* fall-through*/
        case 35:
            return true;
        default:
            break;
        }
    }
    if (device == "GW1NR-9" || device == "GW1NR-9C") {
        switch (col) {
        case 2:  /* fall-through*/
        case 4:  /* fall-through*/
        case 6:  /* fall-through*/
        case 8:  /* fall-through*/
        case 9:  /* fall-through*/
        case 11: /* fall-through*/
        case 13: /* fall-through*/
        case 15: /* fall-through*/
        case 17: /* fall-through*/
        case 18: /* fall-through*/
        case 19: /* fall-through*/
        case 21: /* fall-through*/
        case 23: /* fall-through*/
        case 25: /* fall-through*/
        case 27: /* fall-through*/
        case 28: /* fall-through*/
        case 29: /* fall-through*/
        case 31: /* fall-through*/
        case 33: /* fall-through*/
        case 35: /* fall-through*/
        case 36: /* fall-through*/
        case 37: /* fall-through*/
        case 39: /* fall-through*/
        case 41: /* fall-through*/
        case 43: /* fall-through*/
        case 45:
            return true;
        default:
            break;
        }
    }
    return false;
}

WireId Arch::get_make_port_wire(const DatabasePOD *db, const BelsPOD *bel, int row, int col, IdString port)
{
    IdString wirename = IdString(pairLookup(bel->ports.get(), bel->num_ports, port.hash())->src_id);
    IdString wire = idf("R%dC%d_%s", row + 1, col + 1, wirename.c_str(this));
    if (!wires.count(wire)) {
        GlobalAliasPOD alias;
        alias.dest_col = col;
        alias.dest_row = row;
        alias.dest_id = port.hash();
        auto alias_src = genericLookup(db->aliases.get(), db->num_aliases, alias, aliasCompare);
        NPNR_ASSERT(alias_src != nullptr);
        int srcrow = alias_src->src_row;
        int srccol = alias_src->src_col;
        IdString srcid = IdString(alias_src->src_id);
        wire = wireToGlobal(srcrow, srccol, db, srcid);
        if (!wires.count(wire)) {
            addWire(wire, srcid, srccol, srcrow);
        }
    }
    return wire;
}

Arch::Arch(ArchArgs args) : args(args)
{
    family = args.family;

    max_clock = 6;
    if (family == "GW1NZ-1") {
        max_clock = 3;
    }

    // Load database
    std::string chipdb = stringf("gowin/chipdb-%s.bin", family.c_str());
    auto db = reinterpret_cast<const DatabasePOD *>(get_chipdb(chipdb));
    if (db == nullptr) {
        log_error("Failed to load chipdb '%s'\n", chipdb.c_str());
    }
    if (db->version != chipdb_version) {
        log_error("Incorrect chipdb version %u is used. Version %u is required\n", db->version, chipdb_version);
    }
    if (db->family.get() != family) {
        log_error("Database is for family '%s' but provided device is family '%s'.\n", db->family.get(),
                  family.c_str());
    }
    // setup id strings
    for (size_t i = 0; i < db->num_ids; i++) {
        IdString::initialize_add(this, db->id_strs[i].get(), uint32_t(i) + db->num_constids);
    }

    // Empty decal
    addDecalGraphic(IdString(), GraphicElement());

    if (args.gui) {
#ifndef NO_GUI
        // decals
        gfxCreateBelDecals(this);
#endif
    }

    // setup package
    IdString package_name;
    IdString device_id;
    IdString speed_id;
    for (unsigned int i = 0; i < db->num_partnumbers; i++) {
        auto partnumber = &db->partnumber_packages[i];
        // std::cout << IdString(partnumber->name_id).str(this) << IdString(partnumber->package_id).str(this) <<
        // std::endl;
        if (IdString(partnumber->name_id) == id(args.partnumber)) {
            package_name = IdString(partnumber->package_id);
            device_id = IdString(partnumber->device_id);
            speed_id = IdString(partnumber->speed_id);
            break;
        }
    }
    if (package_name == IdString()) {
        log_error("Unsupported partnumber '%s'.\n", args.partnumber.c_str());
    }

    // setup timing info
    speed = nullptr;
    for (unsigned int i = 0; i < db->num_speeds; i++) {
        const TimingClassPOD *tc = &db->speeds[i];
        // std::cout << IdString(tc->name_id).str(this) << std::endl;
        if (IdString(tc->name_id) == speed_id) {
            speed = tc->groups.get();
            break;
        }
    }
    if (speed == nullptr) {
        log_error("Unsupported speed grade '%s'.\n", speed_id.c_str(this));
    }

    const VariantPOD *variant = nullptr;
    for (unsigned int i = 0; i < db->num_variants; i++) {
        auto var = &db->variants[i];
        // std::cout << IdString(var->name_id).str(this) << std::endl;
        if (IdString(var->name_id) == device_id) {
            variant = var;
            break;
        }
    }
    if (variant == nullptr) {
        log_error("Unsupported device grade '%s'.\n", device_id.c_str(this));
    }

    package = nullptr;
    for (unsigned int i = 0; i < variant->num_packages; i++) {
        auto pkg = &variant->packages[i];
        // std::cout << IdString(pkg->name_id).str(this) << std::endl;
        if (IdString(pkg->name_id) == package_name) {
            package = pkg;
            break;
        }
        // for (int j=0; j < pkg->num_pins; j++) {
        //     auto pin = pkg->pins[j];
        //     std::cout << IdString(pin.src_id).str(this) << " " << IdString(pin.dest_id).str(this) << std::endl;
        // }
    }

    if (package == nullptr) {
        log_error("Unsupported package '%s'.\n", package_name.c_str(this));
    }

    //
    log_info("Series:%s Device:%s Package:%s Speed:%s\n", family.c_str(), device_id.c_str(this),
             package_name.c_str(this), speed_id.c_str(this));

    device = device_id.str(this);

    // setup db
    // add global VCC and GND bels
    addBel(id_GND, id_GND, Loc(0, 0, BelZ::gnd_0_z), true);
    addWire(id_VSS, id_VSS, 0, 0);
    addBelOutput(id_GND, id_G, id_VSS);
    addBel(id_VCC, id_VCC, Loc(0, 0, BelZ::vcc_0_z), true);
    addWire(id_VCC, id_VCC, 0, 0);
    addBelOutput(id_VCC, id_V, id_VCC);
    char buf[32];
    // The reverse order of the enumeration simplifies the creation
    // of MUX2_LUT8s: they need the existence of the wire on the right.
    for (int i = db->rows * db->cols - 1; i >= 0; --i) {
        IdString grpname;
        int row = i / db->cols;
        int col = i % db->cols;
        const TilePOD *tile = db->grid[i].get();
        if (args.gui) {
#ifndef NO_GUI
            // CRU decal
            snprintf(buf, 32, "R%dC%d_CRU", row + 1, col + 1);
            grpname = id(buf);
            addGroup(grpname);
            setGroupDecal(grpname, gfxGetCruGroupDecalXY(col, row));
#endif
        }
        // setup wires
        const PairPOD *pips[2] = {tile->pips.get(), tile->clock_pips.get()};
        unsigned int num_pips[2] = {tile->num_pips, tile->num_clock_pips};
        for (int p = 0; p < 2; p++) {
            for (unsigned int j = 0; j < num_pips[p]; j++) {
                const PairPOD pip = pips[p][j];
                int destrow = row;
                int destcol = col;
                IdString destid(pip.dest_id), gdestid(pip.dest_id);
                IdString gdestname = wireToGlobal(destrow, destcol, db, gdestid);
                if (!wires.count(gdestname))
                    addWire(gdestname, destid, destcol, destrow);
                int srcrow = row;
                int srccol = col;
                IdString srcid(pip.src_id), gsrcid(pip.src_id);
                IdString gsrcname = wireToGlobal(srcrow, srccol, db, gsrcid);
                if (!wires.count(gsrcname))
                    addWire(gsrcname, srcid, srccol, srcrow);
            }
        }
        for (unsigned int j = 0; j < tile->num_bels; j++) {
            const BelsPOD *bel = &tile->bels[j];
            IdString belname;
            IdString portname;
            int z = 0;
            bool dff = true;
            switch (static_cast<ConstIds>(bel->type_id)) {
            case ID_PLLVR:
                belname = idf("R%dC%d_PLLVR", row + 1, col + 1);
                addBel(belname, id_PLLVR, Loc(col, row, BelZ::pllvr_z), false);
                add_pllvr_ports(db, bel, belname, row, col);
                break;
            case ID_RPLLA:
                belname = idf("R%dC%d_rPLL", row + 1, col + 1);
                addBel(belname, id_rPLL, Loc(col, row, BelZ::pll_z), false);
                add_rpll_ports(db, bel, belname, row, col);
                break;
            case ID_BUFS7:
                z++; /* fall-through*/
            case ID_BUFS6:
                z++; /* fall-through*/
            case ID_BUFS5:
                z++; /* fall-through*/
            case ID_BUFS4:
                z++; /* fall-through*/
            case ID_BUFS3:
                z++; /* fall-through*/
            case ID_BUFS2:
                z++; /* fall-through*/
            case ID_BUFS1:
                z++; /* fall-through*/
            case ID_BUFS0:
                snprintf(buf, 32, "R%dC%d_BUFS%d", row + 1, col + 1, z);
                belname = id(buf);
                addBel(belname, id_BUFS, Loc(col, row, BelZ::bufs_0_z + z), false);
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, ID_I)->src_id);
                snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                addBelInput(belname, id_I, id(buf));
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, ID_O)->src_id);
                snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                addBelOutput(belname, id_O, id(buf));
                break;
            case ID_GSR0:
                snprintf(buf, 32, "R%dC%d_GSR0", row + 1, col + 1);
                belname = id(buf);
                addBel(belname, id_GSR, Loc(col, row, 0), false);
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, ID_GSRI)->src_id);
                snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                addBelInput(belname, id_GSRI, id(buf));
                break;
            case ID_OSC:  /* fall-through*/
            case ID_OSCH: /* fall-through*/
            case ID_OSCW:
                belname = idf("R%dC%d_%s", row + 1, col + 1, IdString(bel->type_id).c_str(this));
                addBel(belname, IdString(bel->type_id), Loc(col, row, BelZ::osc_z), false);
                addBelOutput(belname, id_OSCOUT, get_make_port_wire(db, bel, row, col, id_OSCOUT));
                break;
            case ID_OSCF: /* fall-through*/
            case ID_OSCZ: /* fall-through*/
            case ID_OSCO:
                belname = idf("R%dC%d_%s", row + 1, col + 1, IdString(bel->type_id).c_str(this));
                addBel(belname, IdString(bel->type_id), Loc(col, row, BelZ::osc_z), false);
                addBelOutput(belname, id_OSCOUT, get_make_port_wire(db, bel, row, col, id_OSCOUT));
                addBelInput(belname, id_OSCEN, get_make_port_wire(db, bel, row, col, id_OSCEN));
                break;
            case ID_RAM16:
                snprintf(buf, 32, "R%dC%d_RAMW", row + 1, col + 1);
                belname = id(buf);
                addBel(belname, id_RAMW, Loc(col, row, BelZ::lutram_0_z), false);

                snprintf(buf, 32, "R%dC%d_A%d", row + 1, col + 1, 4);
                addBelInput(belname, id_A4, id(buf));
                snprintf(buf, 32, "R%dC%d_B%d", row + 1, col + 1, 4);
                addBelInput(belname, id_B4, id(buf));
                snprintf(buf, 32, "R%dC%d_C%d", row + 1, col + 1, 4);
                addBelInput(belname, id_C4, id(buf));
                snprintf(buf, 32, "R%dC%d_D%d", row + 1, col + 1, 4);
                addBelInput(belname, id_D4, id(buf));

                snprintf(buf, 32, "R%dC%d_A%d", row + 1, col + 1, 5);
                addBelInput(belname, id_A5, id(buf));
                snprintf(buf, 32, "R%dC%d_B%d", row + 1, col + 1, 5);
                addBelInput(belname, id_B5, id(buf));
                snprintf(buf, 32, "R%dC%d_C%d", row + 1, col + 1, 5);
                addBelInput(belname, id_C5, id(buf));
                snprintf(buf, 32, "R%dC%d_D%d", row + 1, col + 1, 5);
                addBelInput(belname, id_D5, id(buf));

                snprintf(buf, 32, "R%dC%d_CLK%d", row + 1, col + 1, 2);
                addBelInput(belname, id_CLK, id(buf));
                snprintf(buf, 32, "R%dC%d_LSR%d", row + 1, col + 1, 2);
                addBelInput(belname, id_LSR, id(buf));
                snprintf(buf, 32, "R%dC%d_CE%d", row + 1, col + 1, 2);
                addBelInput(belname, id_CE, id(buf));
                break;
            // fall through the ++
            case ID_LUT7:
                z++;
                dff = false; /* fall-through*/
            case ID_LUT6:
                z++;
                dff = false; /* fall-through*/
            case ID_LUT5:
                z++; /* fall-through*/
            case ID_LUT4:
                z++; /* fall-through*/
            case ID_LUT3:
                z++; /* fall-through*/
            case ID_LUT2:
                z++; /* fall-through*/
            case ID_LUT1:
                z++; /* fall-through*/
            case ID_LUT0:
                // common LUT+DFF code
                snprintf(buf, 32, "R%dC%d_SLICE%d", row + 1, col + 1, z);
                belname = id(buf);
                addBel(belname, id_SLICE, Loc(col, row, z), false);
                snprintf(buf, 32, "R%dC%d_F%d", row + 1, col + 1, z);
                addBelOutput(belname, id_F, id(buf));
                snprintf(buf, 32, "R%dC%d_A%d", row + 1, col + 1, z);
                addBelInput(belname, id_A, id(buf));
                snprintf(buf, 32, "R%dC%d_B%d", row + 1, col + 1, z);
                addBelInput(belname, id_B, id(buf));
                snprintf(buf, 32, "R%dC%d_C%d", row + 1, col + 1, z);
                addBelInput(belname, id_C, id(buf));
                snprintf(buf, 32, "R%dC%d_D%d", row + 1, col + 1, z);
                addBelInput(belname, id_D, id(buf));
                if (dff) {
                    snprintf(buf, 32, "R%dC%d_CLK%d", row + 1, col + 1, z / 2);
                    addBelInput(belname, id_CLK, id(buf));
                    snprintf(buf, 32, "R%dC%d_LSR%d", row + 1, col + 1, z / 2);
                    addBelInput(belname, id_LSR, id(buf));
                    snprintf(buf, 32, "R%dC%d_CE%d", row + 1, col + 1, z / 2);
                    addBelInput(belname, id_CE, id(buf));
                    snprintf(buf, 32, "R%dC%d_Q%d", row + 1, col + 1, z);
                    addBelOutput(belname, id_Q, id(buf));
                }
                if (z == 0) {
                    addMuxBels(db, row, col);
                }
                if (z % 2 == 0) {
                    snprintf(buf, 32, "R%dC%d_LUT_GRP%d", row + 1, col + 1, z);
                    grpname = id(buf);
                    if (args.gui) {
#ifndef NO_GUI
                        addGroup(grpname);
                        setGroupDecal(grpname, gfxGetLutGroupDecalXY(col, row, z >> 1));
#endif
                    }
                }
                break;
            case ID_IOBJ:
                z++; /* fall-through*/
            case ID_IOBI:
                z++; /* fall-through*/
            case ID_IOBH:
                z++; /* fall-through*/
            case ID_IOBG:
                z++; /* fall-through*/
            case ID_IOBF:
                z++; /* fall-through*/
            case ID_IOBE:
                z++; /* fall-through*/
            case ID_IOBD:
                z++; /* fall-through*/
            case ID_IOBC:
                z++; /* fall-through*/
            case ID_IOBB:
                z++; /* fall-through*/
            case ID_IOBA: {
                snprintf(buf, 32, "R%dC%d_IOB%c", row + 1, col + 1, 'A' + z);
                belname = id(buf);
                addBel(belname, id_IOB, Loc(col, row, z), false);
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, ID_O)->src_id);
                snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                addBelOutput(belname, id_O, id(buf));
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, ID_I)->src_id);
                snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                addBelInput(belname, id_I, id(buf));
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, ID_OE)->src_id);
                snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                addBelInput(belname, id_OEN, id(buf));
                // GW1NR-9 quirk
                const PairPOD *quirk_port = pairLookup(bel->ports.get(), bel->num_ports, ID_GW9_ALWAYS_LOW0);
                if (quirk_port != nullptr) {
                    gw1n9_quirk = true;
                    portname = IdString(quirk_port->src_id);
                    snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                    addBelInput(belname, id_GW9_ALWAYS_LOW0, id(buf));
                }
                quirk_port = pairLookup(bel->ports.get(), bel->num_ports, ID_GW9_ALWAYS_LOW1);
                if (quirk_port != nullptr) {
                    gw1n9_quirk = true;
                    portname = IdString(quirk_port->src_id);
                    snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                    addBelInput(belname, id_GW9_ALWAYS_LOW1, id(buf));
                }
                if (!z && device_id == id("GW1NR-9C")) {
                    addBelInput(belname, id_GW9C_ALWAYS_LOW0, idf("R%dC%d_C6", row + 1, col + 1));
                    addBelInput(belname, id_GW9C_ALWAYS_LOW1, idf("R%dC%d_D6", row + 1, col + 1));
                }
            } break;
                // Simplified IO
            case ID_IOBJS:
                z++; /* fall-through*/
            case ID_IOBIS:
                z++; /* fall-through*/
            case ID_IOBHS:
                z++; /* fall-through*/
            case ID_IOBGS:
                z++; /* fall-through*/
            case ID_IOBFS:
                z++; /* fall-through*/
            case ID_IOBES:
                z++; /* fall-through*/
            case ID_IOBDS:
                z++; /* fall-through*/
            case ID_IOBCS:
                z++; /* fall-through*/
            case ID_IOBBS:
                z++; /* fall-through*/
            case ID_IOBAS:
                snprintf(buf, 32, "R%dC%d_IOB%c", row + 1, col + 1, 'A' + z);
                belname = id(buf);
                addBel(belname, id_IOBS, Loc(col, row, z), false);
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, ID_O)->src_id);
                snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                addBelOutput(belname, id_O, id(buf));
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, ID_I)->src_id);
                snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                addBelInput(belname, id_I, id(buf));
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, ID_OE)->src_id);
                snprintf(buf, 32, "R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                addBelInput(belname, id_OEN, id(buf));
                break;

                // IO logic
            case ID_IOLOGICB:
                z++; /* fall-through*/
            case ID_IOLOGICA: {
                belname = idf("R%dC%d_IOLOGIC%c", row + 1, col + 1, 'A' + z);
                addBel(belname, id_IOLOGIC, Loc(col, row, BelZ::iologic_z + z), false);

                IdString const iologic_in_ports[] = {id_TX,    id_TX0,  id_TX1,   id_TX2,    id_TX3,   id_RESET,
                                                     id_CALIB, id_PCLK, id_D,     id_D0,     id_D1,    id_D2,
                                                     id_D3,    id_D4,   id_D5,    id_D6,     id_D7,    id_D8,
                                                     id_D9,    id_CLK,  id_CLEAR, id_DAADJ0, id_DAADJ1};
                for (IdString port : iologic_in_ports) {
                    const PairPOD *portid = pairLookup(bel->ports.get(), bel->num_ports, port.hash());
                    if (portid != nullptr) {
                        portname = IdString(portid->src_id);
                        addBelInput(belname, port, idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
                    }
                }
                IdString const iologic_out_ports[] = {id_Q,  id_Q0, id_Q1, id_Q2, id_Q3, id_Q4,
                                                      id_Q5, id_Q6, id_Q7, id_Q8, id_Q9};
                for (IdString port : iologic_out_ports) {
                    portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, port.hash())->src_id);
                    addBelOutput(belname, port, idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
                }
                auto fclk = pairLookup(bel->ports.get(), bel->num_ports, ID_FCLK);
                // XXX as long as there is no special processing of the pins
                if (fclk != nullptr) {
                    portname = IdString(fclk->src_id);
                    IdString wire = idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                    if (!wires.count(wire)) {
                        GlobalAliasPOD alias;
                        alias.dest_col = col;
                        alias.dest_row = row;
                        alias.dest_id = portname.hash();
                        auto alias_src = genericLookup(db->aliases.get(), db->num_aliases, alias, aliasCompare);
                        if (alias_src != nullptr) {
                            int srcrow = alias_src->src_row;
                            int srccol = alias_src->src_col;
                            IdString srcid = IdString(alias_src->src_id);
                            wire = wireToGlobal(srcrow, srccol, db, srcid);
                            if (!wires.count(wire)) {
                                addWire(wire, srcid, srccol, srcrow);
                            }
                            addBelInput(belname, id_FCLK, wire);
                        }
                        // XXX here we are creating an
                        // IOLOGIC with a missing FCLK input. This is so
                        // because bels with the same type can be placed in
                        // on the chip where there is no pin, so no
                        // IOLOGIC makes sense. But since each type is
                        // described only once in the database we can't really
                        // mark these special bel somehow.
                        // By creating an IOLOGIC without an FCLK input we
                        // create a routing error later, so that "bad"
                        // locations are handled.
                    } else {
                        addBelInput(belname, id_FCLK, idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
                    }
                }
            } break;
            case ID_OSER16: {
                if (skip_aux_oser16(device, row, col)) {
                    break;
                }
                belname = idf("R%dC%d_OSER16", row + 1, col + 1);
                addBel(belname, id_OSER16, Loc(col, row, BelZ::oser16_z), false);

                const IdString oser16_in_ports[] = {id_RESET, id_PCLK, id_D0,  id_D1,  id_D2,  id_D3,
                                                    id_D4,    id_D5,   id_D6,  id_D7,  id_D8,  id_D9,
                                                    id_D10,   id_D11,  id_D12, id_D13, id_D14, id_D15};
                for (IdString port : oser16_in_ports) {
                    portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, port.hash())->src_id);
                    addBelInput(belname, port, idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
                }
                portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, id_Q0.hash())->src_id);
                addBelOutput(belname, id_Q, idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
                auto fclk = pairLookup(bel->ports.get(), bel->num_ports, ID_FCLK);
                // XXX as long as there is no special processing of the pins
                if (fclk != nullptr) {
                    portname = IdString(fclk->src_id);
                    IdString wire = idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                    if (!wires.count(wire)) {
                        GlobalAliasPOD alias;
                        alias.dest_col = col;
                        alias.dest_row = row;
                        alias.dest_id = portname.hash();
                        auto alias_src = genericLookup(db->aliases.get(), db->num_aliases, alias, aliasCompare);
                        if (alias_src != nullptr) {
                            int srcrow = alias_src->src_row;
                            int srccol = alias_src->src_col;
                            IdString srcid = IdString(alias_src->src_id);
                            wire = wireToGlobal(srcrow, srccol, db, srcid);
                            if (!wires.count(wire)) {
                                addWire(wire, srcid, srccol, srcrow);
                            }
                            addBelInput(belname, id_FCLK, wire);
                        }
                    } else {
                        addBelInput(belname, id_FCLK, idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
                    }
                }
            } break;
            case ID_IDES16: {
                if (skip_aux_oser16(device, row, col)) {
                    break;
                }
                belname = idf("R%dC%d_IDES16", row + 1, col + 1);
                addBel(belname, id_IDES16, Loc(col, row, BelZ::ides16_z), false);

                IdString const ides16_in_ports[] = {id_RESET, id_PCLK, id_CALIB, id_D};
                for (IdString port : ides16_in_ports) {
                    portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, port.hash())->src_id);
                    addBelInput(belname, port, idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
                }
                IdString const ides16_out_ports[] = {id_Q0, id_Q1, id_Q2,  id_Q3,  id_Q4,  id_Q5,  id_Q6,  id_Q7,
                                                     id_Q8, id_Q9, id_Q10, id_Q11, id_Q12, id_Q13, id_Q14, id_Q15};
                for (IdString port : ides16_out_ports) {
                    portname = IdString(pairLookup(bel->ports.get(), bel->num_ports, port.hash())->src_id);
                    addBelOutput(belname, port, idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
                }
                auto fclk = pairLookup(bel->ports.get(), bel->num_ports, ID_FCLK);
                // XXX as long as there is no special processing of the pins
                if (fclk != nullptr) {
                    portname = IdString(fclk->src_id);
                    IdString wire = idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this));
                    if (!wires.count(wire)) {
                        GlobalAliasPOD alias;
                        alias.dest_col = col;
                        alias.dest_row = row;
                        alias.dest_id = portname.hash();
                        auto alias_src = genericLookup(db->aliases.get(), db->num_aliases, alias, aliasCompare);
                        if (alias_src != nullptr) {
                            int srcrow = alias_src->src_row;
                            int srccol = alias_src->src_col;
                            IdString srcid = IdString(alias_src->src_id);
                            wire = wireToGlobal(srcrow, srccol, db, srcid);
                            if (!wires.count(wire)) {
                                addWire(wire, srcid, srccol, srcrow);
                            }
                            addBelInput(belname, id_FCLK, wire);
                        }
                    } else {
                        addBelInput(belname, id_FCLK, idf("R%dC%d_%s", row + 1, col + 1, portname.c_str(this)));
                    }
                }
            } break;
            default:
                break;
            }
        }
    }

    // IO pin configs
    for (unsigned int i = 0; i < package->num_pins; i++) {
        const PinPOD *pin = &package->pins[i];
        if (pin->num_cfgs == 0) {
            continue;
        }
        auto b = bels.find(IdString(pin->loc_id));
        if (b == bels.end()) {
            // Not all pins are transmitted, e.g. MODE, DONE etc.
            continue;
        }
        std::vector<IdString> &cfgs = b->second.pin_cfgs;
        for (unsigned int j = 0; j < pin->num_cfgs; ++j) {
            cfgs.push_back(IdString(pin->cfgs[j]));
        }
    }

    // setup pips
    for (int i = 0; i < db->rows * db->cols; i++) {
        int row = i / db->cols;
        int col = i % db->cols;
        const TilePOD *tile = db->grid[i].get();
        const PairPOD *pips[2] = {tile->pips.get(), tile->clock_pips.get()};
        unsigned int num_pips[2] = {tile->num_pips, tile->num_clock_pips};
        for (int p = 0; p < 2; p++) {
            for (unsigned int j = 0; j < num_pips[p]; j++) {
                const PairPOD pip = pips[p][j];
                int destrow = row;
                int destcol = col;
                IdString destid(pip.dest_id), gdestid(pip.dest_id);
                IdString gdestname = wireToGlobal(destrow, destcol, db, gdestid);
                int srcrow = row;
                int srccol = col;
                IdString srcid(pip.src_id), gsrcid(pip.src_id);
                IdString gsrcname = wireToGlobal(srcrow, srccol, db, gsrcid);

                snprintf(buf, 32, "R%dC%d_%s_%s", row + 1, col + 1, srcid.c_str(this), destid.c_str(this));
                IdString pipname = id(buf);
                DelayQuad delay = getWireTypeDelay(destid);
                // global alias
                srcid = IdString(pip.src_id);
                GlobalAliasPOD alias;
                alias.dest_col = srccol;
                alias.dest_row = srcrow;
                alias.dest_id = srcid.index;
                auto alias_src = genericLookup(db->aliases.get(), db->num_aliases, alias, aliasCompare);
                if (alias_src != nullptr) {
                    srccol = alias_src->src_col;
                    srcrow = alias_src->src_row;
                    srcid = IdString(alias_src->src_id);
                    gsrcname = wireToGlobal(srcrow, srccol, db, srcid);
                }
                addPip(pipname, destid, gsrcname, gdestname, delay, Loc(col, row, j));
            }
        }
    }
    if (args.gui) {
        setDefaultDecals();
    }

    // Permissible combinations of modes in a single slice
    dff_comp_mode[id_DFF] = id_DFF;
    dff_comp_mode[id_DFFE] = id_DFFE;
    dff_comp_mode[id_DFFS] = id_DFFR;
    dff_comp_mode[id_DFFR] = id_DFFS;
    dff_comp_mode[id_DFFSE] = id_DFFRE;
    dff_comp_mode[id_DFFRE] = id_DFFSE;
    dff_comp_mode[id_DFFP] = id_DFFC;
    dff_comp_mode[id_DFFC] = id_DFFP;
    dff_comp_mode[id_DFFPE] = id_DFFCE;
    dff_comp_mode[id_DFFCE] = id_DFFPE;
    dff_comp_mode[id_DFFNS] = id_DFFNR;
    dff_comp_mode[id_DFFNR] = id_DFFNS;
    dff_comp_mode[id_DFFNSE] = id_DFFNRE;
    dff_comp_mode[id_DFFNRE] = id_DFFNSE;
    dff_comp_mode[id_DFFNP] = id_DFFNC;
    dff_comp_mode[id_DFFNC] = id_DFFNP;
    dff_comp_mode[id_DFFNPE] = id_DFFNCE;
    dff_comp_mode[id_DFFNCE] = id_DFFNPE;

    BaseArch::init_cell_types();
    BaseArch::init_bel_buckets();
}

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);
#include "constids.inc"
#undef X
}

// ---------------------------------------------------------------

BelId Arch::getBelByName(IdStringList name) const
{
    if (bels.count(name[0]))
        return name[0];
    return BelId();
}

IdStringList Arch::getBelName(BelId bel) const { return IdStringList(bel); }

Loc Arch::getBelLocation(BelId bel) const
{
    auto &info = bels.at(bel);
    return Loc(info.x, info.y, info.z);
}

BelId Arch::getBelByLocation(Loc loc) const
{
    auto it = bel_by_loc.find(loc);
    if (it != bel_by_loc.end())
        return it->second;
    return BelId();
}

const std::vector<BelId> &Arch::getBelsByTile(int x, int y) const { return bels_by_tile.at(x).at(y); }

bool Arch::haveBelType(int x, int y, IdString bel_type)
{
    for (auto bel : getBelsByTile(x, y)) {
        BelInfo bi = bel_info(bel);
        if (bi.type == bel_type) {
            return true;
        }
    }
    return false;
}

bool Arch::getBelGlobalBuf(BelId bel) const { return bels.at(bel).gb; }

void Arch::bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
{
    bels.at(bel).bound_cell = cell;
    cell->bel = bel;
    cell->belStrength = strength;
    refreshUiBel(bel);
}

void Arch::unbindBel(BelId bel)
{
    bels.at(bel).bound_cell->bel = BelId();
    bels.at(bel).bound_cell->belStrength = STRENGTH_NONE;
    bels.at(bel).bound_cell = nullptr;
    refreshUiBel(bel);
}

bool Arch::checkBelAvail(BelId bel) const { return bels.at(bel).bound_cell == nullptr; }

CellInfo *Arch::getBoundBelCell(BelId bel) const { return bels.at(bel).bound_cell; }

CellInfo *Arch::getConflictingBelCell(BelId bel) const { return bels.at(bel).bound_cell; }

const std::vector<BelId> &Arch::getBels() const { return bel_ids; }

IdString Arch::getBelType(BelId bel) const { return bels.at(bel).type; }

const std::map<IdString, std::string> &Arch::getBelAttrs(BelId bel) const { return bels.at(bel).attrs; }

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    const auto &bdata = bels.at(bel);
    if (!bdata.pins.count(pin))
        log_error("bel '%s' has no pin '%s'\n", bel.c_str(this), pin.c_str(this));
    return bdata.pins.at(pin).wire;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const { return bels.at(bel).pins.at(pin).type; }

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    for (auto &it : bels.at(bel).pins)
        ret.push_back(it.first);
    return ret;
}

std::array<IdString, 1> Arch::getBelPinsForCellPin(const CellInfo *cell_info, IdString pin) const { return {pin}; }

// ---------------------------------------------------------------

WireId Arch::getWireByName(IdStringList name) const
{
    if (wires.count(name[0]))
        return name[0];
    return WireId();
}

IdStringList Arch::getWireName(WireId wire) const { return IdStringList(wire); }

IdString Arch::getWireType(WireId wire) const { return wires.at(wire).type; }

const std::map<IdString, std::string> &Arch::getWireAttrs(WireId wire) const { return wires.at(wire).attrs; }

void Arch::bindWire(WireId wire, NetInfo *net, PlaceStrength strength)
{
    wires.at(wire).bound_net = net;
    net->wires[wire].pip = PipId();
    net->wires[wire].strength = strength;
    refreshUiWire(wire);
}

void Arch::unbindWire(WireId wire)
{
    auto &net_wires = wires.at(wire).bound_net->wires;

    auto pip = net_wires.at(wire).pip;
    if (pip != PipId()) {
        pips.at(pip).bound_net = nullptr;
        refreshUiPip(pip);
    }

    net_wires.erase(wire);
    wires.at(wire).bound_net = nullptr;
    refreshUiWire(wire);
}

bool Arch::checkWireAvail(WireId wire) const { return wires.at(wire).bound_net == nullptr; }

NetInfo *Arch::getBoundWireNet(WireId wire) const { return wires.at(wire).bound_net; }

NetInfo *Arch::getConflictingWireNet(WireId wire) const { return wires.at(wire).bound_net; }

const std::vector<BelPin> &Arch::getWireBelPins(WireId wire) const { return wires.at(wire).bel_pins; }

const std::vector<WireId> &Arch::getWires() const { return wire_ids; }

// ---------------------------------------------------------------

PipId Arch::getPipByName(IdStringList name) const
{
    if (pips.count(name[0]))
        return name[0];
    return PipId();
}

IdStringList Arch::getPipName(PipId pip) const { return IdStringList(pip); }

IdString Arch::getPipType(PipId pip) const { return pips.at(pip).type; }

const std::map<IdString, std::string> &Arch::getPipAttrs(PipId pip) const { return pips.at(pip).attrs; }

void Arch::bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
{
    WireId wire = pips.at(pip).dstWire;
    pips.at(pip).bound_net = net;
    wires.at(wire).bound_net = net;
    net->wires[wire].pip = pip;
    net->wires[wire].strength = strength;
    refreshUiPip(pip);
    refreshUiWire(wire);
}

void Arch::unbindPip(PipId pip)
{
    WireId wire = pips.at(pip).dstWire;
    wires.at(wire).bound_net->wires.erase(wire);
    pips.at(pip).bound_net = nullptr;
    wires.at(wire).bound_net = nullptr;
    refreshUiPip(pip);
    refreshUiWire(wire);
}

bool Arch::checkPipAvail(PipId pip) const { return pips.at(pip).bound_net == nullptr; }

NetInfo *Arch::getBoundPipNet(PipId pip) const { return pips.at(pip).bound_net; }

NetInfo *Arch::getConflictingPipNet(PipId pip) const { return pips.at(pip).bound_net; }

WireId Arch::getConflictingPipWire(PipId pip) const { return pips.at(pip).bound_net ? pips.at(pip).dstWire : WireId(); }

const std::vector<PipId> &Arch::getPips() const { return pip_ids; }

Loc Arch::getPipLocation(PipId pip) const { return pips.at(pip).loc; }

WireId Arch::getPipSrcWire(PipId pip) const { return pips.at(pip).srcWire; }

WireId Arch::getPipDstWire(PipId pip) const { return pips.at(pip).dstWire; }

DelayQuad Arch::getPipDelay(PipId pip) const { return pips.at(pip).delay; }

const std::vector<PipId> &Arch::getPipsDownhill(WireId wire) const { return wires.at(wire).downhill; }

const std::vector<PipId> &Arch::getPipsUphill(WireId wire) const { return wires.at(wire).uphill; }

// ---------------------------------------------------------------

GroupId Arch::getGroupByName(IdStringList name) const { return name[0]; }

IdStringList Arch::getGroupName(GroupId group) const { return IdStringList(group); }

std::vector<GroupId> Arch::getGroups() const
{
    std::vector<GroupId> ret;
    for (auto &it : groups)
        ret.push_back(it.first);
    return ret;
}

const std::vector<BelId> &Arch::getGroupBels(GroupId group) const { return groups.at(group).bels; }

const std::vector<WireId> &Arch::getGroupWires(GroupId group) const { return groups.at(group).wires; }

const std::vector<PipId> &Arch::getGroupPips(GroupId group) const { return groups.at(group).pips; }

const std::vector<GroupId> &Arch::getGroupGroups(GroupId group) const { return groups.at(group).groups; }

// ---------------------------------------------------------------

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    const WireInfo &s = wires.at(src);
    const WireInfo &d = wires.at(dst);
    int dx = abs(s.x - d.x);
    int dy = abs(s.y - d.y);
    return (dx + dy) * args.delayScale + args.delayOffset;
}

delay_t Arch::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    NPNR_UNUSED(src_pin);
    NPNR_UNUSED(dst_pin);
    auto driver_loc = getBelLocation(src_bel);
    auto sink_loc = getBelLocation(dst_bel);

    int dx = abs(sink_loc.x - driver_loc.x);
    int dy = abs(sink_loc.y - driver_loc.y);
    return (dx + dy) * args.delayScale + args.delayOffset;
}

BoundingBox Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    BoundingBox bb;

    int src_x = wires.at(src).x;
    int src_y = wires.at(src).y;
    int dst_x = wires.at(dst).x;
    int dst_y = wires.at(dst).y;

    bb.x0 = src_x;
    bb.y0 = src_y;
    bb.x1 = src_x;
    bb.y1 = src_y;

    auto extend = [&](int x, int y) {
        bb.x0 = std::min(bb.x0, x);
        bb.x1 = std::max(bb.x1, x);
        bb.y0 = std::min(bb.y0, y);
        bb.y1 = std::max(bb.y1, y);
    };
    extend(dst_x, dst_y);
    return bb;
}

// ---------------------------------------------------------------

bool Arch::place()
{
    std::string placer = str_or_default(settings, id_placer, defaultPlacer);
    bool retVal;
    if (placer == "heap") {
        bool have_iobuf_or_constr = false;
        for (auto &cell : cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type == id_IOB || ci->bel != BelId() || ci->attrs.count(id_BEL)) {
                have_iobuf_or_constr = true;
                break;
            }
        }
        if (!have_iobuf_or_constr) {
            log_warning("Unable to use HeAP due to a lack of IO buffers or constrained cells as anchors; reverting to "
                        "SA.\n");
            retVal = placer1(getCtx(), Placer1Cfg(getCtx()));
        } else {
            PlacerHeapCfg cfg(getCtx());
            cfg.ioBufTypes.insert(id_IOB);
            cfg.beta = 0.5;
            retVal = placer_heap(getCtx(), cfg);
        }
        getCtx()->settings[id_place] = 1;
        archInfoToAttributes();
    } else if (placer == "sa") {
        retVal = placer1(getCtx(), Placer1Cfg(getCtx()));
        getCtx()->settings[id_place] = 1;
        archInfoToAttributes();
        return retVal;
    } else {
        log_error("Gowin architecture does not support placer '%s'\n", placer.c_str());
    }
    // debug placement
    if (getCtx()->debug) {
        for (auto &cell : getCtx()->cells) {
            log_info("Placed: %s -> %s\n", cell.first.c_str(getCtx()), getCtx()->nameOfBel(cell.second->bel));
        }
    }
    return retVal;
}

static bool is_spec_iob(const Context *ctx, const CellInfo *cell, IdString pin_name)
{
    if (!is_iob(ctx, cell)) {
        return false;
    }
    std::vector<IdString> const &cfgs = ctx->bels.at(cell->bel).pin_cfgs;
    bool have_pin = std::find(cfgs.begin(), cfgs.end(), pin_name) != cfgs.end();
    return have_pin;
}

static bool is_RPLL_T_IN_iob(const Context *ctx, const CellInfo *cell)
{
    return is_spec_iob(ctx, cell, ctx->id("RPLL_T_IN"));
}

static bool is_LPLL_T_IN_iob(const Context *ctx, const CellInfo *cell)
{
    return is_spec_iob(ctx, cell, ctx->id("LPLL_T_IN"));
}

static bool is_RPLL_T_FB_iob(const Context *ctx, const CellInfo *cell)
{
    return is_spec_iob(ctx, cell, ctx->id("RPLL_T_FB"));
}

static bool is_LPLL_T_FB_iob(const Context *ctx, const CellInfo *cell)
{
    return is_spec_iob(ctx, cell, ctx->id("LPLL_T_FB"));
}

bool Arch::is_GCLKT_iob(const CellInfo *cell)
{
    for (int i = 0; i < 6; ++i) {
        if (is_spec_iob(getCtx(), cell, idf("GCLKT_%d", i))) {
            return true;
        }
    }
    return false;
}

void Arch::bind_pll_to_bel(CellInfo *ci, PLL loc)
{
    BelId bel;
    switch (ci->type.hash()) {
    case ID_PLLVR:
        bel = loc == PLL::left ? id("R1C28_PLLVR") : id("R1C37_PLLVR");
        break;
    case ID_rPLL:
        if (family == "GW1N-1" || family == "GW1NZ-1") {
            if (loc == PLL::left) {
                return;
            }
            bel = id("R1C18_rPLL");
            break;
        }
        if (family == "GW1NS-2") {
            if (loc == PLL::left) {
                return;
            }
            bel = id("R10C20_rPLL");
            break;
        }
        if (family == "GW1N-4") {
            bel = loc == PLL::left ? id("R1C10_rPLL") : id("R1C28_rPLL");
            break;
        }
        if (family == "GW1NR-9C" || family == "GW1NR-9") {
            bel = loc == PLL::left ? id("R10C1_rPLL") : id("R10C47_rPLL");
            break;
        }
        return;
    default:
        return;
    }
    if (checkBelAvail(bel) || ci->belStrength != STRENGTH_LOCKED) {
        if (ci->bel == bel) {
            unbindBel(bel);
        } else {
            if (!checkBelAvail(bel) && ci->belStrength != STRENGTH_LOCKED) {
                CellInfo *other_ci = getBoundBelCell(bel);
                unbindBel(bel);
                BelId our_bel = ci->bel;
                unbindBel(our_bel);
                bindBel(our_bel, other_ci, STRENGTH_LOCKED);
            }
        }
        ci->disconnectPort(id_CLKIN);
        ci->setParam(id_INSEL, Property("CLKIN0"));
        bindBel(bel, ci, STRENGTH_LOCKED);
    }
}

// If the PLL input can be connected using a direct wire, then do so,
// bypassing conventional routing.
void Arch::fix_pll_nets(Context *ctx)
{
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type != id_rPLL && ci->type != id_PLLVR) {
            continue;
        }
        // *** CLKIN
        do {
            if (!port_used(ci, id_CLKIN)) {
                ci->setParam(id_INSEL, Property("UNKNOWN"));
                break;
            }
            NetInfo *net = ci->getPort(id_CLKIN);
            if (net->name == id("$PACKER_VCC_NET") || net->name == id("$PACKER_GND_NET")) {
                ci->setParam(id_INSEL, Property("UNKNOWN"));
                break;
            }
            if (net_driven_by(ctx, net, is_RPLL_T_IN_iob, id_O) != nullptr) {
                bind_pll_to_bel(ci, PLL::right);
                break;
            }
            if (net_driven_by(ctx, net, is_LPLL_T_IN_iob, id_O) != nullptr) {
                bind_pll_to_bel(ci, PLL::left);
                break;
            }
            // XXX do special bels (HCLK etc)
            // This is general routing through CLK0 pip
            ci->setParam(id_INSEL, Property("CLKIN1"));
        } while (0);

        do {
            // *** CLKFB
            if (str_or_default(ci->params, id_CLKFB_SEL, "internal") == "internal") {
                ci->setParam(id_FBSEL, Property("CLKFB3"));
                continue;
            }
            if (!port_used(ci, id_CLKFB)) {
                ci->setParam(id_FBSEL, Property("UNKNOWN"));
                continue;
            }
            NetInfo *net = ci->getPort(id_CLKFB);
            if (net->name == id("$PACKER_VCC_NET") || net->name == id("$PACKER_GND_NET")) {
                ci->setParam(id_FBSEL, Property("UNKNOWN"));
                continue;
            }
            // XXX Redesign for chips other than N-1 and NS-4
            if (net_driven_by(ctx, net, is_RPLL_T_FB_iob, id_O) != nullptr) {
                ci->disconnectPort(id_CLKFB);
                ci->setParam(id_FBSEL, Property("CLKFB2"));
                break;
            }
            if (net_driven_by(ctx, net, is_LPLL_T_FB_iob, id_O) != nullptr) {
                ci->disconnectPort(id_CLKFB);
                ci->setParam(id_FBSEL, Property("CLKFB2"));
                break;
            }
            // XXX do special bels (HCLK etc)
            // This is general routing through CLK2 pip
            ci->setParam(id_FBSEL, Property("CLKFB0"));
        } while (0);
    }
}

// mark with hclk is used
void Arch::mark_used_hclk(Context *ctx)
{
    pool<IdString> aux_cells;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type != id_IOLOGIC) {
            continue;
        }
        if (ci->attrs.count(id_IOLOGIC_FCLK)) {
            continue;
        }
        // if it's an aux cell
        if (ci->attrs.count(id_IOLOGIC_MASTER_CELL)) {
            aux_cells.insert(ci->name);
            continue;
        }
        ci->setAttr(id_IOLOGIC_FCLK, Property("UNKNOWN"));

        // *** FCLK
        if (port_used(ci, id_FCLK)) {
            NetInfo const *net = ci->getPort(id_FCLK);
            for (auto const &user : net->users) {
                if (user.cell != ci) {
                    continue;
                }
                if (user.port != id_FCLK) {
                    continue;
                }
                WireId dstWire = ctx->getNetinfoSinkWire(net, user, 0);
                if (ctx->verbose) {
                    log_info("   Cell:%s, port:%s, wire:%s\n", user.cell->name.c_str(this), user.port.c_str(this),
                             dstWire.c_str(this));
                }
                for (PipId pip : getPipsUphill(dstWire)) {
                    if (!checkPipAvail(pip)) {
                        WireId src_wire = getPipSrcWire(pip);
                        ci->setAttr(id_IOLOGIC_FCLK, Property(wire_info(src_wire).type.str(this)));
                    }
                }
            }
        }
    }
    for (auto acell : aux_cells) {
        IdString main_cell = ctx->id(ctx->cells.at(acell)->attrs.at(id_IOLOGIC_MASTER_CELL).as_string());
        Property &fclk = ctx->cells.at(main_cell)->attrs.at(id_IOLOGIC_FCLK);
        ctx->cells.at(acell)->setAttr(id_IOLOGIC_FCLK, fclk);
    }
}

void Arch::pre_route(Context *ctx)
{
    fix_pll_nets(ctx);
    if (bool_or_default(settings, id("arch.enable-globals"))) {
        mark_gowin_globals(ctx);
    }
}

void Arch::post_route(Context *ctx)
{
    fix_longwire_bels();
    mark_used_hclk(ctx);
}

bool Arch::route()
{
    std::string router = str_or_default(settings, id_router, defaultRouter);
    Context *ctx = getCtx();

    pre_route(ctx);
    if (bool_or_default(settings, id("arch.enable-globals"))) {
        route_gowin_globals(ctx);
    }

    bool result;
    if (router == "router1") {
        result = router1(ctx, Router1Cfg(ctx));
    } else if (router == "router2") {
        router2(ctx, Router2Cfg(ctx));
        result = true;
    } else {
        log_error("Gowin architecture does not support router '%s'\n", router.c_str());
    }
    getCtx()->settings[id_route] = 1;
    archInfoToAttributes();
    post_route(ctx);
    return result;
}

// ---------------------------------------------------------------
std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const
{
    if (!decal_graphics.count(decal)) {
        // XXX
        return std::vector<GraphicElement>();
    }
    return decal_graphics.at(decal);
}

// ---------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    if (!cellTiming.count(cell->name))
        return false;
    const auto &tmg = cellTiming.at(cell->name);
    auto fnd = tmg.combDelays.find(CellDelayKey{fromPort, toPort});
    if (fnd != tmg.combDelays.end()) {
        delay = fnd->second;
        return true;
    } else {
        return false;
    }
}

// Get the port class, also setting clockPort if applicable
TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    if (!cellTiming.count(cell->name))
        return TMG_IGNORE;
    const auto &tmg = cellTiming.at(cell->name);
    if (tmg.clockingInfo.count(port))
        clockInfoCount = int(tmg.clockingInfo.at(port).size());
    else
        clockInfoCount = 0;
    return get_or_default(tmg.portClasses, port, TMG_IGNORE);
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    NPNR_ASSERT(cellTiming.count(cell->name));
    const auto &tmg = cellTiming.at(cell->name);
    NPNR_ASSERT(tmg.clockingInfo.count(port));
    return tmg.clockingInfo.at(port).at(index);
}

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    Loc loc = getBelLocation(bel);

    std::vector<const CellInfo *> cells;
    for (auto tbel : getBelsByTile(loc.x, loc.y)) {
        CellInfo *bound = getBoundBelCell(tbel);
        if (bound != nullptr)
            cells.push_back(bound);
    }

    return cellsCompatible(cells.data(), int(cells.size()));
}

const std::string Arch::defaultPlacer = "heap";

const std::vector<std::string> Arch::availablePlacers = {"sa", "heap"};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

void Arch::assignArchInfo()
{
    for (auto &cell : getCtx()->cells) {
        IdString cname = cell.first;
        CellInfo *ci = cell.second.get();
        DelayQuad delay = DelayQuad(0);
        ci->is_slice = false;
        switch (ci->type.index) {
        case ID_SLICE: {
            ci->is_slice = true;
            ci->ff_used = ci->params.at(id_FF_USED).as_bool();
            ci->ff_type = id(ci->params.at(id_FF_TYPE).as_string());
            ci->slice_clk = ci->getPort(id_CLK);
            ci->slice_ce = ci->getPort(id_CE);
            ci->slice_lsr = ci->getPort(id_LSR);

            // add timing paths
            addCellTimingClock(cname, id_CLK);
            addCellTimingClass(cname, id_CE, TMG_REGISTER_INPUT);
            addCellTimingClass(cname, id_LSR, TMG_REGISTER_INPUT);
            IdString ports[4] = {id_A, id_B, id_C, id_D};
            for (int i = 0; i < 4; i++) {
                DelayPair setup =
                        delayLookup(speed->dff.timings.get(), speed->dff.num_timings, id_clksetpos).delayPair();
                DelayPair hold =
                        delayLookup(speed->dff.timings.get(), speed->dff.num_timings, id_clkholdpos).delayPair();
                addCellTimingSetupHold(cname, ports[i], id_CLK, setup, hold);
            }
            DelayQuad clkout = delayLookup(speed->dff.timings.get(), speed->dff.num_timings, id_clk_qpos);
            addCellTimingClockToOut(cname, id_Q, id_CLK, clkout);
            IdString port_delay[4] = {id_a_f, id_b_f, id_c_f, id_d_f};
            for (int i = 0; i < 4; i++) {
                DelayQuad delay = delayLookup(speed->lut.timings.get(), speed->lut.num_timings, port_delay[i]);
                addCellTimingDelay(cname, ports[i], id_F, delay);
            }
            break;
        }
        case ID_MUX2_LUT8:
            delay = delay + delayLookup(speed->lut.timings.get(), speed->lut.num_timings, id_fx_ofx1);
            /* FALLTHRU */
        case ID_MUX2_LUT7:
            delay = delay + delayLookup(speed->lut.timings.get(), speed->lut.num_timings, id_fx_ofx1);
            /* FALLTHRU */
        case ID_MUX2_LUT6:
            delay = delay + delayLookup(speed->lut.timings.get(), speed->lut.num_timings, id_fx_ofx1);
            /* FALLTHRU */
        case ID_MUX2_LUT5: {
            delay = delay + delayLookup(speed->lut.timings.get(), speed->lut.num_timings, id_fx_ofx1);
            addCellTimingDelay(cname, id_I0, id_OF, delay);
            addCellTimingDelay(cname, id_I1, id_OF, delay);
            addCellTimingClass(cname, id_SEL, TMG_COMB_INPUT);
            break;
        }
        case ID_IOB:
            /* FALLTHRU */
        case ID_IOBS:
            addCellTimingClass(cname, id_I, TMG_ENDPOINT);
            addCellTimingClass(cname, id_O, TMG_STARTPOINT);
            break;
        case ID_BUFS:
            addCellTimingClass(cname, id_I, TMG_ENDPOINT);
            addCellTimingClass(cname, id_O, TMG_STARTPOINT);
            break;
        default:
            break;
        }
    }
}

bool Arch::cellsCompatible(const CellInfo **cells, int count) const
{
    const NetInfo *clk[4] = {nullptr, nullptr, nullptr, nullptr};
    const NetInfo *ce[4] = {nullptr, nullptr, nullptr, nullptr};
    const NetInfo *lsr[4] = {nullptr, nullptr, nullptr, nullptr};
    IdString mode[4] = {IdString(), IdString(), IdString(), IdString()};
    for (int i = 0; i < count; i++) {
        const CellInfo *ci = cells[i];
        if (ci->is_slice) {
            Loc loc = getBelLocation(ci->bel);
            int cls = loc.z / 2;
            if (loc.z >= 6 && ci->ff_used) // top slice have no ff
                return false;
            if (clk[cls] == nullptr)
                clk[cls] = ci->slice_clk;
            else if (clk[cls] != ci->slice_clk)
                return false;
            if (ce[cls] == nullptr)
                ce[cls] = ci->slice_ce;
            else if (ce[cls] != ci->slice_ce)
                return false;
            if (lsr[cls] == nullptr)
                lsr[cls] = ci->slice_lsr;
            else if (lsr[cls] != ci->slice_lsr)
                return false;
            if (mode[cls] == IdString())
                mode[cls] = ci->ff_type;
            else if (mode[cls] != ci->ff_type) {
                auto res = dff_comp_mode.find(mode[cls]);
                if (res == dff_comp_mode.end() || res->second != ci->ff_type)
                    return false;
            }
        }
    }
    return true;
}

void Arch::route_gowin_globals(Context *ctx) { globals_router.route_globals(ctx); }

void Arch::mark_gowin_globals(Context *ctx) { globals_router.mark_globals(ctx); }
// ---------------------------------------------------------------
void Arch::pre_pack(Context *ctx)
{
    if (bool_or_default(settings, id("arch.enable-auto-longwires"))) {
        auto_longwires();
    }
}

void Arch::post_pack(Context *ctx) {}

NEXTPNR_NAMESPACE_END

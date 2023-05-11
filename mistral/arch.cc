/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Lofty <dan.ravensloft@gmail.com>
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

#include <algorithm>

#include "log.h"
#include "nextpnr.h"

#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "timing.h"
#include "util.h"

#include "cyclonev.h"

NEXTPNR_NAMESPACE_BEGIN

using namespace mistral;

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);

#include "constids.inc"

#undef X
}

CycloneV::rnode_t Arch::find_rnode(CycloneV::block_type_t bt, int x, int y, CycloneV::port_type_t port, int bi,
                                   int pi) const
{
    auto pn1 = CycloneV::pnode(bt, x, y, port, bi, pi);
    auto rn1 = cyclonev->pnode_to_rnode(pn1);
    if (rn1)
        return rn1;

    if (bt == CycloneV::GPIO) {
        auto pn2 = cyclonev->p2p_to(pn1);
        if (!pn2) {
            auto pnv = cyclonev->p2p_from(pn1);
            if (!pnv.empty())
                pn2 = pnv[0];
        }
        auto pn3 = cyclonev->hmc_get_bypass(pn2);
        auto rn2 = cyclonev->pnode_to_rnode(pn3);
        return rn2;
    }

    return 0;
}

WireId Arch::get_port(CycloneV::block_type_t bt, int x, int y, int bi, CycloneV::port_type_t port, int pi) const
{
    auto rn = find_rnode(bt, x, y, port, bi, pi);
    if (rn)
        return WireId(rn);

    log_error("Trying to connect unknown node %s\n", CycloneV::pn2s(CycloneV::pnode(bt, x, y, port, bi, pi)).c_str());
}

bool Arch::has_port(CycloneV::block_type_t bt, int x, int y, int bi, CycloneV::port_type_t port, int pi) const
{
    return find_rnode(bt, x, y, port, bi, pi) != 0;
}

Arch::Arch(ArchArgs args)
{
    this->args = args;
    this->cyclonev = mistral::CycloneV::get_model(args.device);
    NPNR_ASSERT(this->cyclonev != nullptr);

    // Setup fast identifier maps
    for (int i = 0; i < 1024; i++) {
        IdString int_id = idf("%d", i);
        int2id.push_back(int_id);
        id2int[int_id] = i;
    }

    for (int t = int(CycloneV::NONE); t <= int(CycloneV::DCMUX); t++) {
        IdString rnode_id = id(CycloneV::rnode_type_names[t]);
        rn_t2id.push_back(rnode_id);
        id2rn_t[rnode_id] = CycloneV::rnode_type_t(t);
    }

    log_info("Initialising bels...\n");
    bels_by_tile.resize(cyclonev->get_tile_sx() * cyclonev->get_tile_sy());

    for (auto lab_pos : cyclonev->lab_get_pos())
        create_lab(CycloneV::pos2x(lab_pos), CycloneV::pos2y(lab_pos), /*is_mlab=*/false);

    for (auto mlab_pos : cyclonev->mlab_get_pos())
        create_lab(CycloneV::pos2x(mlab_pos), CycloneV::pos2y(mlab_pos), /*is_mlab=*/true);

    for (auto gpio_pos : cyclonev->gpio_get_pos())
        create_gpio(CycloneV::pos2x(gpio_pos), CycloneV::pos2y(gpio_pos));

    for (auto cmuxh_pos : cyclonev->cmuxh_get_pos())
        create_clkbuf(CycloneV::pos2x(cmuxh_pos), CycloneV::pos2y(cmuxh_pos));

    create_control(CycloneV::pos2x(cyclonev->ctrl_get_pos()[0]), CycloneV::pos2y(cyclonev->ctrl_get_pos()[0]));

    auto hps_pos = cyclonev->hps_get_pos();
    if (!hps_pos.empty()) {
        create_hps_mpu_general_purpose(CycloneV::pos2x(hps_pos[CycloneV::I_HPS_MPU_GENERAL_PURPOSE]),
                                       CycloneV::pos2y(hps_pos[CycloneV::I_HPS_MPU_GENERAL_PURPOSE]));
    }

    for (auto m10k_pos : cyclonev->m10k_get_pos())
        create_m10k(CycloneV::pos2x(m10k_pos), CycloneV::pos2y(m10k_pos));

    // This import takes about 5s, perhaps long term we can speed it up, e.g. defer to Mistral more...
    log_info("Initialising routing graph...\n");
    int pip_count = 0;
    for (const auto &rnode : cyclonev->rnodes()) {
        WireId dst_wire(rnode.id());
        for (const auto &src : rnode.sources()) {
            WireId src_wire(src);
            wires[dst_wire].wires_uphill.push_back(src_wire);
            wires[src_wire].wires_downhill.push_back(dst_wire);
            ++pip_count;
        }
    }

    log_info("    imported %d wires and %d pips\n", int(wires.size()), pip_count);

    BaseArch::init_cell_types();
    BaseArch::init_bel_buckets();
}

int Arch::getTileBelDimZ(int x, int y) const
{
    // This seems like a reasonable upper bound
    return 256;
}

BelId Arch::getBelByName(IdStringList name) const
{
    BelId bel;
    NPNR_ASSERT(name.size() == 4);
    int x = id2int.at(name[1]);
    int y = id2int.at(name[2]);
    int z = id2int.at(name[3]);

    bel.pos = CycloneV::xy2pos(x, y);
    bel.z = z;

    NPNR_ASSERT(name[0] == getBelType(bel));

    return bel;
}

IdStringList Arch::getBelName(BelId bel) const
{
    int x = CycloneV::pos2x(bel.pos);
    int y = CycloneV::pos2y(bel.pos);
    int z = bel.z & 0xFF;

    std::array<IdString, 4> ids{
            getBelType(bel),
            int2id.at(x),
            int2id.at(y),
            int2id.at(z),
    };

    return IdStringList(ids);
}

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    auto &data = bel_data(bel);
    if (data.type.in(id_MISTRAL_COMB, id_MISTRAL_MCOMB)) {
        return is_alm_legal(data.lab_data.lab, data.lab_data.alm) && check_lab_input_count(data.lab_data.lab) &&
               check_mlab_groups(data.lab_data.lab);
    } else if (data.type == id_MISTRAL_FF) {
        return is_alm_legal(data.lab_data.lab, data.lab_data.alm) && check_lab_input_count(data.lab_data.lab) &&
               is_lab_ctrlset_legal(data.lab_data.lab) && check_mlab_groups(data.lab_data.lab);
    }
    return true;
}

void Arch::update_bel(BelId bel)
{
    auto &data = bel_data(bel);
    if (data.type.in(id_MISTRAL_COMB, id_MISTRAL_MCOMB, id_MISTRAL_FF)) {
        update_alm_input_count(data.lab_data.lab, data.lab_data.alm);
    }
}

WireId Arch::getWireByName(IdStringList name) const
{
    // non-mistral wires
    auto found_npnr = npnr_wirebyname.find(name);
    if (found_npnr != npnr_wirebyname.end())
        return found_npnr->second;
    // mistral wires
    NPNR_ASSERT(name.size() == 4);
    CycloneV::rnode_type_t ty = id2rn_t.at(name[0]);
    int x = id2int.at(name[1]);
    int y = id2int.at(name[2]);
    int z = id2int.at(name[3]);
    return WireId(CycloneV::rnode(ty, x, y, z));
}

IdStringList Arch::getWireName(WireId wire) const
{
    if (wire.is_nextpnr_created()) {
        // non-mistral wires
        std::array<IdString, 4> ids{
                id_WIRE,
                int2id.at(CycloneV::rn2x(wire.node)),
                int2id.at(CycloneV::rn2y(wire.node)),
                wires.at(wire).name_override,
        };
        return IdStringList(ids);
    } else {
        std::array<IdString, 4> ids{
                rn_t2id.at(CycloneV::rn2t(wire.node)),
                int2id.at(CycloneV::rn2x(wire.node)),
                int2id.at(CycloneV::rn2y(wire.node)),
                int2id.at(CycloneV::rn2z(wire.node)),
        };
        return IdStringList(ids);
    }
}

PipId Arch::getPipByName(IdStringList name) const
{
    WireId src = getWireByName(name.slice(0, 4));
    WireId dst = getWireByName(name.slice(4, 8));
    NPNR_ASSERT(src != WireId());
    NPNR_ASSERT(dst != WireId());
    return PipId(src.node, dst.node);
}

IdStringList Arch::getPipName(PipId pip) const
{
    return IdStringList::concat(getWireName(getPipSrcWire(pip)), getWireName(getPipDstWire(pip)));
}

std::vector<BelId> Arch::getBelsByTile(int x, int y) const
{
    // This should probably be redesigned, but it's a hack.
    std::vector<BelId> bels;
    if (x >= 0 && x < cyclonev->get_tile_sx() && y >= 0 && y < cyclonev->get_tile_sy()) {
        for (size_t i = 0; i < bels_by_tile.at(pos2idx(x, y)).size(); i++)
            bels.push_back(BelId(CycloneV::xy2pos(x, y), i));
    }

    return bels;
}

IdString Arch::getBelType(BelId bel) const { return bel_data(bel).type; }

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> pins;
    for (auto &p : bel_data(bel).pins)
        pins.push_back(p.first);
    return pins;
}

bool Arch::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    // Any combinational cell type can - theoretically - be placed at a combinational ALM bel
    // The precise legality mechanics will be dealt with in isBelLocationValid.
    IdString bel_type = getBelType(bel);
    if (bel_type == id_MISTRAL_COMB)
        return is_comb_cell(cell_type);
    else if (bel_type == id_MISTRAL_MCOMB)
        return is_comb_cell(cell_type) || (cell_type == id_MISTRAL_MLAB);
    else if (bel_type == id_MISTRAL_IO)
        return is_io_cell(cell_type);
    else if (bel_type == id_MISTRAL_CLKENA)
        return is_clkbuf_cell(cell_type);
    else
        return bel_type == cell_type;
}

BelBucketId Arch::getBelBucketForCellType(IdString cell_type) const
{
    if (is_comb_cell(cell_type) || cell_type == id_MISTRAL_MLAB)
        return id_MISTRAL_COMB;
    else if (is_io_cell(cell_type))
        return id_MISTRAL_IO;
    else if (is_clkbuf_cell(cell_type))
        return id_MISTRAL_CLKENA;
    else
        return cell_type;
}

BelBucketId Arch::getBelBucketForBel(BelId bel) const
{
    IdString bel_type = getBelType(bel);
    if (bel_type == id_MISTRAL_MCOMB)
        return id_MISTRAL_COMB;
    else
        return bel_type;
}

BelId Arch::bel_by_block_idx(int x, int y, IdString type, int block_index) const
{
    auto &bels = bels_by_tile.at(pos2idx(x, y));
    for (size_t i = 0; i < bels.size(); i++) {
        auto &bel_data = bels.at(i);
        if (bel_data.type == type && bel_data.block_index == block_index)
            return BelId(CycloneV::xy2pos(x, y), i);
    }
    return BelId();
}

BelId Arch::add_bel(int x, int y, IdString name, IdString type)
{
    auto &bels = bels_by_tile.at(pos2idx(x, y));
    BelId id = BelId(CycloneV::xy2pos(x, y), bels.size());
    all_bels.push_back(id);
    bels.emplace_back();
    auto &bel = bels.back();
    bel.name = name;
    bel.type = type;
    // TODO: buckets (for example LABs and MLABs in the same bucket)
    bel.bucket = type;
    return id;
}

WireId Arch::add_wire(int x, int y, IdString name, uint64_t flags)
{
    std::array<IdString, 4> ids{
            id_WIRE,
            int2id.at(x),
            int2id.at(y),
            name,
    };
    IdStringList full_name(ids);
    auto existing = npnr_wirebyname.find(full_name);
    if (existing != npnr_wirebyname.end()) {
        // Already exists, don't create anything
        return existing->second;
    } else {
        // Determine a unique ID for the wire
        int z = 0;
        WireId id;
        while (wires.count(id = WireId(CycloneV::rnode(CycloneV::rnode_type_t((z >> 10) + 128), x, y, (z & 0x3FF)))))
            z++;
        wires[id].name_override = name;
        wires[id].flags = flags;
        npnr_wirebyname[full_name] = id;
        return id;
    }
}

void Arch::reserve_route(WireId src, WireId dst)
{
    auto &dst_data = wires.at(dst);
    int idx = -1;

    for (int i = 0; i < int(dst_data.wires_uphill.size()); i++) {
        if (dst_data.wires_uphill.at(i) == src) {
            idx = i;
            break;
        }
    }

    NPNR_ASSERT(idx != -1);

    dst_data.flags = WireInfo::RESERVED_ROUTE | unsigned(idx);
}

bool Arch::wires_connected(WireId src, WireId dst) const
{
    PipId pip(src.node, dst.node);
    return getBoundPipNet(pip) != nullptr;
}

PipId Arch::add_pip(WireId src, WireId dst)
{
    wires[src].wires_downhill.push_back(dst);
    wires[dst].wires_uphill.push_back(src);
    return PipId(src.node, dst.node);
}

void Arch::add_bel_pin(BelId bel, IdString pin, PortType dir, WireId wire)
{
    auto &b = bel_data(bel);
    NPNR_ASSERT(!b.pins.count(pin));
    b.pins[pin].dir = dir;
    b.pins[pin].wire = wire;

    BelPin bel_pin;
    bel_pin.bel = bel;
    bel_pin.pin = pin;
    wires[wire].bel_pins.push_back(bel_pin);
}

void Arch::assign_default_pinmap(CellInfo *cell)
{
    if (cell->type == id_MISTRAL_M10K)
        return; // M10Ks always have a custom pinmap
    for (auto &port : cell->ports) {
        auto &pinmap = cell->pin_data[port.first].bel_pins;
        if (!pinmap.empty())
            continue; // already mapped
        if (is_comb_cell(cell->type) && comb_pinmap.count(port.first))
            pinmap.push_back(comb_pinmap.at(port.first)); // default comb mapping for placer purposes
        else
            pinmap.push_back(port.first); // default: assume bel pin named the same as cell pin
    }
}

void Arch::assignArchInfo()
{
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (is_comb_cell(ci->type) || ci->type == id_MISTRAL_MLAB)
            assign_comb_info(ci);
        else if (ci->type == id_MISTRAL_FF)
            assign_ff_info(ci);
        assign_default_pinmap(ci);
    }
}

BoundingBox Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    BoundingBox bounds;
    int src_x = CycloneV::rn2x(src.node);
    int src_y = CycloneV::rn2y(src.node);
    int dst_x = CycloneV::rn2x(dst.node);
    int dst_y = CycloneV::rn2y(dst.node);
    bounds.x0 = std::min(src_x, dst_x);
    bounds.y0 = std::min(src_y, dst_y);
    bounds.x1 = std::max(src_x, dst_x);
    bounds.y1 = std::max(src_y, dst_y);
    return bounds;
}

bool Arch::place()
{
    std::string placer = str_or_default(settings, id_placer, defaultPlacer);

    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.ioBufTypes.insert(id_MISTRAL_IO);
        cfg.ioBufTypes.insert(id_MISTRAL_IB);
        cfg.ioBufTypes.insert(id_MISTRAL_OB);
        cfg.cellGroups.emplace_back();
        cfg.cellGroups.back().insert({id_MISTRAL_COMB});
        cfg.cellGroups.back().insert({id_MISTRAL_FF});

        // The Cyclone V is asymmetrical enough that it's somewhat beneficial to prefer connecting things horizontally.
        cfg.hpwl_scale_x = 1;
        cfg.hpwl_scale_y = 2;

        cfg.beta = 0.5; // TODO: find a good value of beta for sensible ALM spreading
        cfg.criticalityExponent = 7;
        if (!placer_heap(getCtx(), cfg))
            return false;
    } else if (placer == "sa") {
        if (!placer1(getCtx(), Placer1Cfg(getCtx())))
            return false;
    } else {
        log_error("Mistral architecture does not support placer '%s'\n", placer.c_str());
    }

    getCtx()->attrs[id_step] = std::string("place");
    archInfoToAttributes();
    return true;
}

bool Arch::route()
{
    lab_pre_route();

    route_globals();

    std::string router = str_or_default(settings, id_router, defaultRouter);
    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("Mistral architecture does not support router '%s'\n", router.c_str());
    }
    getCtx()->attrs[id_step] = std::string("route");
    archInfoToAttributes();
    return result;
}

const std::string Arch::defaultPlacer = "heap";

const std::vector<std::string> Arch::availablePlacers = {"sa", "heap"};

const std::string Arch::defaultRouter = "router2";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

NEXTPNR_NAMESPACE_END

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#include "fpga_interchange.h"
#include <capnp/message.h>
#include <sstream>
#include <capnp/serialize.h>
#include <kj/std/iostream.h>
#include "PhysicalNetlist.capnp.h"
#include "LogicalNetlist.capnp.h"
#include "zlib.h"
#include "frontend_base.h"

NEXTPNR_NAMESPACE_BEGIN

static void write_message(::capnp::MallocMessageBuilder & message, const std::string &filename) {
    kj::Array<capnp::word> words = messageToFlatArray(message);
    kj::ArrayPtr<kj::byte> bytes = words.asBytes();

    gzFile file = gzopen(filename.c_str(), "w");
    NPNR_ASSERT(file != Z_NULL);

    NPNR_ASSERT(gzwrite(file, &bytes[0], bytes.size()) == (int)bytes.size());
    NPNR_ASSERT(gzclose(file) == Z_OK);
}

struct StringEnumerator {
    std::vector<std::string> strings;
    std::unordered_map<std::string, size_t> string_to_index;

    size_t get_index(const std::string &s) {
        auto result = string_to_index.emplace(s, strings.size());
        if(result.second) {
            // This string was inserted, append.
            strings.push_back(s);
        }

        return result.first->second;
    }
};

static PhysicalNetlist::PhysNetlist::RouteBranch::Builder emit_branch(
        const Context * ctx,
        StringEnumerator * strings,
        const std::unordered_map<PipId, PlaceStrength> &pip_place_strength,
        PipId pip,
        PhysicalNetlist::PhysNetlist::RouteBranch::Builder branch) {
    if(ctx->is_pip_synthetic(pip)) {
        log_error("FPGA interchange should not emit synthetic pip %s\n", ctx->nameOfPip(pip));
    }

    const PipInfoPOD & pip_data = pip_info(ctx->chip_info, pip);
    const TileTypeInfoPOD & tile_type = loc_info(ctx->chip_info, pip);
    const TileInstInfoPOD & tile = ctx->chip_info->tiles[pip.tile];

    if(pip_data.site == -1) {
        // This is a PIP
        auto pip_obj = branch.getRouteSegment().initPip();
        pip_obj.setTile(strings->get_index(tile.name.get()));

        // FIXME: This might be broken for reverse bi-pips.  Re-visit this one.
        //
        // pip_data might need to mark that it is a reversed bi-pip, so the
        // pip emission for the physical netlist would be:
        //
        //  wire0: dst_wire
        //  wire1: src_wire
        //  forward: false
        //
        IdString src_wire_name = IdString(tile_type.wire_data[pip_data.src_index].name);
        IdString dst_wire_name = IdString(tile_type.wire_data[pip_data.dst_index].name);
        pip_obj.setWire0(strings->get_index(src_wire_name.str(ctx)));
        pip_obj.setWire1(strings->get_index(dst_wire_name.str(ctx)));
        pip_obj.setForward(true);
        pip_obj.setIsFixed(pip_place_strength.at(pip) >= STRENGTH_FIXED);

        return branch;
    } else {
        BelId bel;
        bel.tile = pip.tile;
        bel.index = pip_data.bel;

        const BelInfoPOD & bel_data = bel_info(ctx->chip_info, bel);

        IdStringList bel_name = ctx->getBelName(bel);
        NPNR_ASSERT(bel_name.size() == 2);
        std::string site_and_type = bel_name[0].str(ctx);
        auto pos = site_and_type.find_first_of('.');
        NPNR_ASSERT(pos != std::string::npos);

        std::string site_name = site_and_type.substr(0, pos);
        int site_idx = strings->get_index(site_name);

        if(bel_data.category == BEL_CATEGORY_LOGIC) {
            // This is a psuedo site-pip.
            auto in_bel_pin = branch.getRouteSegment().initBelPin();
            WireId src_wire = ctx->getPipSrcWire(pip);
            WireId dst_wire = ctx->getPipDstWire(pip);

            NPNR_ASSERT(src_wire.index == bel_data.wires[pip_data.extra_data]);

            IdString src_pin(bel_data.ports[pip_data.extra_data]);

            IdString dst_pin;
            for(IdString pin : ctx->getBelPins(bel)) {
                if(ctx->getBelPinWire(bel, pin) == dst_wire) {
                    NPNR_ASSERT(dst_pin == IdString());
                    dst_pin = pin;
                }
            }

            NPNR_ASSERT(src_pin != IdString());
            NPNR_ASSERT(dst_pin != IdString());

            int bel_idx = strings->get_index(bel_name[1].str(ctx));
            in_bel_pin.setSite(site_idx);
            in_bel_pin.setBel(bel_idx);
            in_bel_pin.setPin(strings->get_index(src_pin.str(ctx)));

            auto subbranch = branch.initBranches(1);
            auto bel_pin_branch = subbranch[0];
            auto out_bel_pin = bel_pin_branch.getRouteSegment().initBelPin();
            out_bel_pin.setSite(site_idx);
            out_bel_pin.setBel(bel_idx);
            out_bel_pin.setPin(strings->get_index(dst_pin.str(ctx)));

            return bel_pin_branch;
        } else if(bel_data.category == BEL_CATEGORY_ROUTING) {
            // This is a site-pip.
            IdStringList pip_name = ctx->getPipName(pip);

            auto site_pip = branch.getRouteSegment().initSitePIP();
            site_pip.setSite(site_idx);
            site_pip.setBel(strings->get_index(pip_name[1].str(ctx)));
            site_pip.setPin(strings->get_index(pip_name[2].str(ctx)));
            site_pip.setIsFixed(pip_place_strength.at(pip) >= STRENGTH_FIXED);

            // FIXME: Mark inverter state.
            // This is required for US/US+ inverters, because those inverters
            // only have 1 input.

            return branch;
        } else {
            NPNR_ASSERT(bel_data.category == BEL_CATEGORY_SITE_PORT);

            // This is a site port.
            const TileWireInfoPOD &tile_wire = tile_type.wire_data[pip_data.src_index];

            int site_pin_idx = strings->get_index(bel_name[1].str(ctx));

            if(tile_wire.site == -1) {
                // This site port is routing -> site.
                auto site_pin = branch.getRouteSegment().initSitePin();
                site_pin.setSite(site_idx);
                site_pin.setPin(site_pin_idx);

                auto subbranch = branch.initBranches(1);
                auto bel_pin_branch = subbranch[0];
                auto bel_pin = bel_pin_branch.getRouteSegment().initBelPin();
                bel_pin.setSite(site_idx);
                bel_pin.setBel(site_pin_idx);
                bel_pin.setPin(site_pin_idx);

                return bel_pin_branch;
            } else {
                // This site port is site -> routing.
                auto bel_pin = branch.getRouteSegment().initBelPin();
                bel_pin.setSite(site_idx);
                bel_pin.setBel(site_pin_idx);
                bel_pin.setPin(site_pin_idx);

                auto subbranch = branch.initBranches(1);
                auto site_pin_branch = subbranch[0];
                auto site_pin = site_pin_branch.getRouteSegment().initSitePin();
                site_pin.setSite(site_idx);
                site_pin.setPin(site_pin_idx);

                return site_pin_branch;
            }
        }
    }
}


static void init_bel_pin(
        const Context * ctx,
        StringEnumerator * strings,
        const BelPin &bel_pin,
        PhysicalNetlist::PhysNetlist::RouteBranch::Builder branch) {
    if(ctx->is_bel_synthetic(bel_pin.bel)) {
        log_error("FPGA interchange should not emit synthetic BEL pin %s/%s\n",
                ctx->nameOfBel(bel_pin.bel), bel_pin.pin.c_str(ctx));
    }

    BelId bel = bel_pin.bel;
    IdString pin_name = bel_pin.pin;

    IdStringList bel_name = ctx->getBelName(bel);
    NPNR_ASSERT(bel_name.size() == 2);
    std::string site_and_type = bel_name[0].str(ctx);
    auto pos = site_and_type.find_first_of('.');
    NPNR_ASSERT(pos != std::string::npos);

    std::string site_name = site_and_type.substr(0, pos);

    const BelInfoPOD & bel_data = bel_info(ctx->chip_info, bel);
    if(bel_data.category == BEL_CATEGORY_LOGIC) {
        // This is a boring old logic BEL.
        auto out_bel_pin = branch.getRouteSegment().initBelPin();

        out_bel_pin.setSite(strings->get_index(site_name));
        out_bel_pin.setBel(strings->get_index(bel_name[1].str(ctx)));
        out_bel_pin.setPin(strings->get_index(pin_name.str(ctx)));
    } else {
        // This is a local site inverter.  This is represented with a
        // $nextpnr_inv, and this BEL pin is the input to that inverter.
        NPNR_ASSERT(bel_data.category == BEL_CATEGORY_ROUTING);
        auto out_pip = branch.getRouteSegment().initSitePIP();

        out_pip.setSite(strings->get_index(site_name));
        out_pip.setBel(strings->get_index(bel_name[1].str(ctx)));
        out_pip.setPin(strings->get_index(pin_name.str(ctx)));
        out_pip.setIsInverting(true);
    }
}


static void emit_net(
        const Context * ctx,
        StringEnumerator * strings,
        const std::unordered_map<WireId, std::vector<PipId>> &pip_downhill,
        const std::unordered_map<WireId, std::vector<BelPin>> &sinks,
        std::unordered_set<PipId> *pips,
        const std::unordered_map<PipId, PlaceStrength> &pip_place_strength,
        WireId wire, PhysicalNetlist::PhysNetlist::RouteBranch::Builder branch) {
    size_t number_branches = 0;

    auto downhill_iter = pip_downhill.find(wire);
    if(downhill_iter != pip_downhill.end()) {
        number_branches += downhill_iter->second.size();
    }

    auto sink_iter = sinks.find(wire);
    if(sink_iter != sinks.end()) {
        number_branches += sink_iter->second.size();
    }

    size_t branch_index = 0;
    auto branches = branch.initBranches(number_branches);

    if(downhill_iter != pip_downhill.end()) {
        const std::vector<PipId> & wire_pips = downhill_iter->second;
        for(size_t i = 0; i < wire_pips.size(); ++i) {
            PipId pip = wire_pips.at(i);
            NPNR_ASSERT(pips->erase(pip) == 1);
            PhysicalNetlist::PhysNetlist::RouteBranch::Builder leaf_branch = emit_branch(
                ctx, strings, pip_place_strength, pip, branches[branch_index++]);

            emit_net(ctx, strings, pip_downhill, sinks, pips,
                    pip_place_strength,
                    ctx->getPipDstWire(pip), leaf_branch);
        }
    }

    if(sink_iter != sinks.end()) {
        for(const auto bel_pin : sink_iter->second) {
            auto leaf_branch = branches[branch_index++];
            init_bel_pin(ctx, strings, bel_pin, leaf_branch);
        }
    }
}

// Given a site wire, find the source BEL pin.
//
// All site wires should have exactly 1 source BEL pin.
//
// FIXME: Consider making sure that wire_data.bel_pins[0] is always the
// source BEL pin in the BBA generator.
static BelPin find_source(const Context *ctx, WireId source_wire) {
    const TileTypeInfoPOD & tile_type = loc_info(ctx->chip_info, source_wire);
    const TileWireInfoPOD & wire_data = tile_type.wire_data[source_wire.index];

    // Make sure this is a site wire, otherwise something odd is happening
    // here.
    if(wire_data.site == -1) {
        return BelPin();
    }

    BelPin source_bel_pin;
    for(const BelPin & bel_pin : ctx->getWireBelPins(source_wire)) {
        if(ctx->getBelPinType(bel_pin.bel, bel_pin.pin) == PORT_OUT) {
            // Synthetic BEL's (like connection to the VCC/GND network) are
            // ignored here, because synthetic BEL's don't exists outside of
            // the BBA.
            if(ctx->is_bel_synthetic(bel_pin.bel)) {
                continue;
            }

            NPNR_ASSERT(source_bel_pin.bel == BelId());
            source_bel_pin = bel_pin;
        }
    }

    NPNR_ASSERT(source_bel_pin.bel != BelId());
    NPNR_ASSERT(source_bel_pin.pin != IdString());

    return source_bel_pin;
}

// Initial a local signal source (usually VCC/GND).
static PhysicalNetlist::PhysNetlist::RouteBranch::Builder init_local_source(
        const Context *ctx,
        StringEnumerator * strings,
        PhysicalNetlist::PhysNetlist::RouteBranch::Builder source_branch,
        PipId root,
        const std::unordered_map<PipId, PlaceStrength> &pip_place_strength,
        WireId *root_wire) {
    WireId source_wire = ctx->getPipSrcWire(root);
    BelPin source_bel_pin = find_source(ctx, source_wire);
    if(source_bel_pin.bel != BelId()) {
        // This branch should first emit the BEL pin that is the source, followed
        // by the pip that brings the source to the net.
        init_bel_pin(ctx, strings, source_bel_pin, source_branch);

        source_branch = source_branch.initBranches(1)[0];
    }
    *root_wire = ctx->getPipDstWire(root);
    return emit_branch(ctx, strings, pip_place_strength, root, source_branch);
}

static void find_non_synthetic_edges(const Context * ctx, WireId root_wire,
        const std::unordered_map<WireId, std::vector<PipId>> &pip_downhill,
        std::vector<PipId> *root_pips) {
    std::vector<WireId> wires_to_expand;

    wires_to_expand.push_back(root_wire);
    while(!wires_to_expand.empty()) {
        WireId wire = wires_to_expand.back();
        wires_to_expand.pop_back();

        auto downhill_iter = pip_downhill.find(wire);
        if(downhill_iter == pip_downhill.end()) {
            if(root_wire != wire) {
                log_warning("Wire %s never entered the real fabric?\n",
                        ctx->nameOfWire(wire));
            }
            continue;
        }

        for(PipId pip : pip_downhill.at(wire)) {
            if(!ctx->is_pip_synthetic(pip)) {
                // Stop following edges that are non-synthetic, they will be
                // followed during emit_net
                root_pips->push_back(pip);
            } else {
                // Continue to follow synthetic edges.
                wires_to_expand.push_back(ctx->getPipDstWire(pip));
            }
        }
    }
}

void FpgaInterchange::write_physical_netlist(const Context * ctx, const std::string &filename) {
    ::capnp::MallocMessageBuilder message;

    PhysicalNetlist::PhysNetlist::Builder phys_netlist = message.initRoot<PhysicalNetlist::PhysNetlist>();

    phys_netlist.setPart(ctx->get_part());

    std::unordered_set<IdString> placed_cells;
    for(const auto & cell_pair : ctx->cells) {
        const CellInfo & cell = *cell_pair.second;
        if(cell.bel == BelId()) {
            // This cell was not placed!
            continue;
        }
        NPNR_ASSERT(cell_pair.first == cell.name);
        NPNR_ASSERT(placed_cells.emplace(cell.name).second);
    }

    StringEnumerator strings;

    IdString nextpnr_inv = ctx->id("$nextpnr_inv");

    size_t number_placements = 0;
    for(auto & cell_name : placed_cells) {
        const CellInfo & cell = *ctx->cells.at(cell_name);

        if(cell.type == nextpnr_inv) {
            continue;
        }

        if(cell.bel == BelId()) {
            continue;
        }

        if(!ctx->isBelLocationValid(cell.bel)) {
            log_error("Cell '%s' is placed at BEL '%s', but this location is currently invalid.  Not writing physical netlist.\n",
                    cell.name.c_str(ctx), ctx->nameOfBel(cell.bel));
        }

        if(ctx->is_bel_synthetic(cell.bel)) {
            continue;
        }

        number_placements += 1;
    }

    std::vector<IdString> ports;

    std::unordered_map<std::string, std::string> sites;
    auto placements = phys_netlist.initPlacements(number_placements);
    auto placement_iter = placements.begin();

    for(auto & cell_name : placed_cells) {
        const CellInfo & cell = *ctx->cells.at(cell_name);

        if(cell.type == nextpnr_inv) {
            continue;
        }

        if(cell.bel == BelId()) {
            continue;
        }

        NPNR_ASSERT(ctx->isBelLocationValid(cell.bel));

        if(ctx->is_bel_synthetic(cell.bel)) {
            continue;
        }

        IdStringList bel_name = ctx->getBelName(cell.bel);
        NPNR_ASSERT(bel_name.size() == 2);
        std::string site_and_type = bel_name[0].str(ctx);
        auto pos = site_and_type.find_first_of('.');
        NPNR_ASSERT(pos != std::string::npos);

        std::string site_name = site_and_type.substr(0, pos);
        std::string site_type = site_and_type.substr(pos + 1);

        auto result = sites.emplace(site_name, site_type);
        if(!result.second) {
            NPNR_ASSERT(result.first->second == site_type);
        }

        auto placement = *placement_iter++;

        placement.setCellName(strings.get_index(cell.name.str(ctx)));
        if(ctx->io_port_types.count(cell.type)) {
            // Always mark IO ports as type <PORT>.
            placement.setType(strings.get_index("<PORT>"));
            ports.push_back(cell.name);
        } else {
            placement.setType(strings.get_index(cell.type.str(ctx)));
        }
        placement.setSite(strings.get_index(site_name));

        size_t bel_index = strings.get_index(bel_name[1].str(ctx));
        placement.setBel(bel_index);
        placement.setIsBelFixed(cell.belStrength >= STRENGTH_FIXED);
        placement.setIsSiteFixed(cell.belStrength >= STRENGTH_FIXED);

        if(!ctx->io_port_types.count(cell.type)) {
            // Don't emit pin map for ports.
            size_t pin_count = 0;
            for(const auto & pin : cell.cell_bel_pins) {
                if(cell.const_ports.count(pin.first)) {
                    continue;
                }
                pin_count += pin.second.size();
            }

            auto pins = placement.initPinMap(pin_count);
            auto pin_iter = pins.begin();

            for(const auto & cell_to_bel_pins : cell.cell_bel_pins) {
                if(cell.const_ports.count(cell_to_bel_pins.first)) {
                    continue;
                }

                std::string cell_pin = cell_to_bel_pins.first.str(ctx);
                size_t cell_pin_index = strings.get_index(cell_pin);

                for(const auto & bel_pin : cell_to_bel_pins.second) {
                    auto pin_output = *pin_iter++;
                    pin_output.setCellPin(cell_pin_index);
                    pin_output.setBel(bel_index);
                    pin_output.setBelPin(strings.get_index(bel_pin.str(ctx)));
                }
            }
        }
    }

    auto phys_cells = phys_netlist.initPhysCells(ports.size());
    auto phys_cells_iter = phys_cells.begin();
    for(IdString port : ports) {
        auto phys_cell = *phys_cells_iter++;
        phys_cell.setCellName(strings.get_index(port.str(ctx)));
        phys_cell.setPhysType(PhysicalNetlist::PhysNetlist::PhysCellType::PORT);
    }

    auto nets = phys_netlist.initPhysNets(ctx->nets.size());
    auto net_iter = nets.begin();
    for(auto & net_pair : ctx->nets) {
        auto &net = *net_pair.second;
        auto net_out = *net_iter++;

        const CellInfo *driver_cell = net.driver.cell;

        // Handle GND and VCC nets.
        if(driver_cell->bel == ctx->get_gnd_bel()) {
            IdString gnd_net_name(ctx->chip_info->constants->gnd_net_name);
            net_out.setName(strings.get_index(gnd_net_name.str(ctx)));
            net_out.setType(PhysicalNetlist::PhysNetlist::NetType::GND);
        } else if(driver_cell->bel == ctx->get_vcc_bel()) {
            IdString vcc_net_name(ctx->chip_info->constants->vcc_net_name);
            net_out.setName(strings.get_index(vcc_net_name.str(ctx)));
            net_out.setType(PhysicalNetlist::PhysNetlist::NetType::VCC);
        } else {
            net_out.setName(strings.get_index(net.name.str(ctx)));
        }

        std::unordered_map<WireId, BelPin> root_wires;
        std::unordered_map<WireId, std::vector<PipId>> pip_downhill;
        std::unordered_set<PipId> pips;

        if (driver_cell != nullptr && driver_cell->bel != BelId() && ctx->isBelLocationValid(driver_cell->bel)) {
            for(IdString bel_pin_name : driver_cell->cell_bel_pins.at(net.driver.port)) {
                BelPin driver_bel_pin;
                driver_bel_pin.bel = driver_cell->bel;
                driver_bel_pin.pin = bel_pin_name;

                WireId driver_wire = ctx->getBelPinWire(driver_bel_pin.bel, bel_pin_name);
                if(driver_wire != WireId()) {
                    root_wires[driver_wire] = driver_bel_pin;
                }
            }
        }

        std::unordered_map<WireId, std::vector<BelPin>> sinks;
        for(const auto &port_ref : net.users) {
            if(port_ref.cell != nullptr && port_ref.cell->bel != BelId() && ctx->isBelLocationValid(port_ref.cell->bel)) {
                auto pin_iter = port_ref.cell->cell_bel_pins.find(port_ref.port);
                if(pin_iter == port_ref.cell->cell_bel_pins.end()) {
                    log_warning("Cell %s port %s on net %s is legal, but has no BEL pins?\n",
                            port_ref.cell->name.c_str(ctx),
                            port_ref.port.c_str(ctx),
                            net.name.c_str(ctx));
                    continue;
                }

                for(IdString bel_pin_name : pin_iter->second) {
                    BelPin sink_bel_pin;
                    sink_bel_pin.bel = port_ref.cell->bel;
                    sink_bel_pin.pin = bel_pin_name;

                    WireId sink_wire = ctx->getBelPinWire(sink_bel_pin.bel, bel_pin_name);
                    if(sink_wire != WireId()) {
                        sinks[sink_wire].push_back(sink_bel_pin);
                    }
                }
            }
        }

        std::unordered_map<PipId, PlaceStrength> pip_place_strength;

        for(auto &wire_pair : net.wires) {
            WireId downhill_wire = wire_pair.first;
            PipId pip = wire_pair.second.pip;
            PlaceStrength strength = wire_pair.second.strength;
            pip_place_strength[pip] = strength;
            if(pip != PipId()) {
                pips.emplace(pip);

                WireId uphill_wire = ctx->getPipSrcWire(pip);
                NPNR_ASSERT(downhill_wire != uphill_wire);
                pip_downhill[uphill_wire].push_back(pip);
            } else {
                // This is a root wire.
                NPNR_ASSERT(root_wires.count(downhill_wire));
            }
        }

        std::vector<PipId> root_pips;
        std::vector<WireId> roots_to_remove;

        for(const auto & root_pair : root_wires) {
            WireId root_wire = root_pair.first;
            BelPin src_bel_pin = root_pair.second;

            if(!ctx->is_bel_synthetic(src_bel_pin.bel)) {
                continue;
            }

            roots_to_remove.push_back(root_wire);
            find_non_synthetic_edges(ctx, root_wire, pip_downhill, &root_pips);
        }

        // Remove wires that have a synthetic root.
        for(WireId wire : roots_to_remove) {
            NPNR_ASSERT(root_wires.erase(wire) == 1);
        }

        auto sources = net_out.initSources(root_wires.size() + root_pips.size());
        auto source_iter = sources.begin();

        for(const auto & root_pair : root_wires) {
            WireId root_wire = root_pair.first;
            BelPin src_bel_pin = root_pair.second;

            PhysicalNetlist::PhysNetlist::RouteBranch::Builder source_branch = *source_iter++;
            init_bel_pin(ctx, &strings, src_bel_pin, source_branch);

            emit_net(ctx, &strings, pip_downhill, sinks, &pips, pip_place_strength, root_wire, source_branch);
        }

        for(const PipId root : root_pips) {
            PhysicalNetlist::PhysNetlist::RouteBranch::Builder source_branch = *source_iter++;

            NPNR_ASSERT(pips.erase(root) == 1);
            WireId root_wire;
            source_branch = init_local_source(ctx, &strings, source_branch, root, pip_place_strength, &root_wire);
            emit_net(ctx, &strings, pip_downhill, sinks, &pips, pip_place_strength, root_wire, source_branch);
        }

        // Any pips that were not part of a tree starting from the source are
        // stubs.
        size_t real_pips = 0;
        for(PipId pip : pips) {
            if(ctx->is_pip_synthetic(pip)) {
                continue;
            }
            real_pips += 1;
        }
        auto stubs = net_out.initStubs(real_pips);
        auto stub_iter = stubs.begin();
        for(PipId pip : pips) {
            if(ctx->is_pip_synthetic(pip)) {
                continue;
            }
            emit_branch(ctx, &strings, pip_place_strength, pip, *stub_iter++);
        }
    }

    auto site_instances = phys_netlist.initSiteInsts(sites.size());
    auto site_inst_iter = site_instances.begin();
    auto site_iter = sites.begin();
    while(site_iter != sites.end()) {
        NPNR_ASSERT(site_inst_iter != site_instances.end());

        auto site_instance = *site_inst_iter;
        site_instance.setSite(strings.get_index(site_iter->first));
        site_instance.setType(strings.get_index(site_iter->second));

        ++site_inst_iter;
        ++site_iter;
    }

    auto str_list = phys_netlist.initStrList(strings.strings.size());
    for(size_t i = 0; i < strings.strings.size(); ++i) {
        str_list.set(i, strings.strings[i]);
    }

    write_message(message, filename);
}

struct LogicalNetlistImpl;

size_t get_port_width(LogicalNetlist::Netlist::Port::Reader port) {
    if(port.isBit()) {
        return 1;
    } else {
        auto bus = port.getBus();
        if(bus.getBusStart() < bus.getBusEnd()) {
            return bus.getBusEnd() - bus.getBusStart() + 1;
        } else {
            return bus.getBusStart() - bus.getBusEnd() + 1;
        }
    }
}

struct PortKey {
    PortKey(int32_t inst_idx, uint32_t port_idx) : inst_idx(inst_idx), port_idx(port_idx) {}
    int32_t inst_idx;
    uint32_t port_idx;

    bool operator == (const PortKey &other) const {
        return inst_idx == other.inst_idx && port_idx == other.port_idx;
    }
};

NEXTPNR_NAMESPACE_END

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX PortKey>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX PortKey &key) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<int32_t>()(key.inst_idx));
        boost::hash_combine(seed, std::hash<uint32_t>()(key.port_idx));
        return seed;
    }
};

NEXTPNR_NAMESPACE_BEGIN

struct ModuleReader {
    const LogicalNetlistImpl *root;

    bool is_top;
    LogicalNetlist::Netlist::CellInstance::Reader cell_inst;
    LogicalNetlist::Netlist::Cell::Reader cell;
    LogicalNetlist::Netlist::CellDeclaration::Reader cell_decl;

    std::unordered_map<int32_t, LogicalNetlist::Netlist::Net::Reader> net_indicies;
    std::unordered_map<int32_t, std::string> disconnected_nets;
    std::unordered_map<PortKey, std::vector<int32_t>> connections;

    ModuleReader(const LogicalNetlistImpl *root,
            LogicalNetlist::Netlist::CellInstance::Reader cell_inst, bool is_top);

    size_t translate_port_index(LogicalNetlist::Netlist::PortInstance::Reader port_inst) const;
};

struct PortReader {
    const ModuleReader * module;
    size_t port_idx;
};

struct CellReader {
    const ModuleReader * module;
    size_t inst_idx;
};

struct NetReader {
    NetReader(const ModuleReader * module, size_t net_idx) : module(module), net_idx(net_idx) {
        scratch.resize(1);
        scratch[0] = net_idx;
    }

    const ModuleReader * module;
    size_t net_idx;
    LogicalNetlist::Netlist::PropertyMap::Reader property_map;
    std::vector<int32_t> scratch;
};

struct LogicalNetlistImpl
{
    LogicalNetlist::Netlist::Reader root;
    std::vector<std::string> strings;

    typedef const ModuleReader ModuleDataType;
    typedef const PortReader& ModulePortDataType;
    typedef const CellReader& CellDataType;
    typedef const NetReader& NetnameDataType;
    typedef const std::vector<int32_t>& BitVectorDataType;

    LogicalNetlistImpl(LogicalNetlist::Netlist::Reader root) : root(root) {
        strings.reserve(root.getStrList().size());
        for(auto s : root.getStrList()) {
            strings.push_back(s);
        }
    }

    template <typename TFunc> void foreach_module(TFunc Func) const
    {
        for (const auto &cell_inst : root.getInstList()) {
            ModuleReader module(this, cell_inst, /*is_top=*/false);
            Func(strings.at(cell_inst.getName()), module);
        }

        auto top = root.getTopInst();
        ModuleReader top_module(this, top, /*is_top=*/true);
        Func(strings.at(top.getName()), top_module);
    }

    template <typename TFunc> void foreach_port(const ModuleReader &mod, TFunc Func) const
    {
        for(auto port_idx : mod.cell_decl.getPorts()) {
            PortReader port_reader;
            port_reader.module = &mod;
            port_reader.port_idx = port_idx;
            auto port = root.getPortList()[port_idx];
            Func(strings.at(port.getName()), port_reader);
        }
    }

    template <typename TFunc> void foreach_cell(const ModuleReader &mod, TFunc Func) const
    {
        for (auto cell_inst_idx : mod.cell.getInsts()) {
            auto cell_inst = root.getInstList()[cell_inst_idx];
            CellReader cell_reader;
            cell_reader.module = &mod;
            cell_reader.inst_idx = cell_inst_idx;
            Func(strings.at(cell_inst.getName()), cell_reader);
        }
    }

    template <typename TFunc> void foreach_netname(const ModuleReader &mod, TFunc Func) const
    {
        // std::unordered_map<int32_t, LogicalNetlist::Netlist::Net::Reader> net_indicies;
        for(auto net_pair : mod.net_indicies) {
            NetReader net_reader(&mod, net_pair.first);
            auto net = net_pair.second;
            net_reader.property_map = net.getPropMap();
            Func(strings.at(net.getName()), net_reader);
        }

        // std::unordered_map<int32_t, IdString> disconnected_nets;
        for(auto net_pair : mod.disconnected_nets) {
            NetReader net_reader(&mod, net_pair.first);
            Func(net_pair.second, net_reader);
        }
    }

    PortType get_port_type_for_direction(LogicalNetlist::Netlist::Direction dir) const {
        if(dir == LogicalNetlist::Netlist::Direction::INPUT) {
            return PORT_IN;
        } else if (dir == LogicalNetlist::Netlist::Direction::INOUT) {
            return PORT_INOUT;
        } else if (dir == LogicalNetlist::Netlist::Direction::OUTPUT) {
            return PORT_OUT;
        } else {
            NPNR_ASSERT_FALSE("invalid logical netlist port direction");
        }
    }

    PortType get_port_dir(const PortReader &port_reader) const {
        auto port = root.getPortList()[port_reader.port_idx];
        auto dir = port.getDir();
        return get_port_type_for_direction(dir);
    }

    const std::string &get_cell_type(const CellReader &cell) const {
        auto cell_inst = root.getInstList()[cell.inst_idx];
        auto cell_def = root.getCellList()[cell_inst.getCell()];
        auto cell_decl = root.getCellDecls()[cell_def.getIndex()];
        return strings.at(cell_decl.getName());
    }


    template <typename TFunc> void foreach_port_dir(const CellReader &cell, TFunc Func) const {
        auto cell_inst = root.getInstList()[cell.inst_idx];
        auto cell_def = root.getCellList()[cell_inst.getCell()];
        auto cell_decl = root.getCellDecls()[cell_def.getIndex()];

        for(auto port_idx : cell_decl.getPorts()) {
            auto port = root.getPortList()[port_idx];
            Func(strings.at(port.getName()), get_port_type_for_direction(port.getDir()));
        }
    }

    template <typename TFunc> void foreach_prop_map(LogicalNetlist::Netlist::PropertyMap::Reader prop_map, TFunc Func) const {
        for(auto prop : prop_map.getEntries()) {
            if(prop.isTextValue()) {
                Func(strings.at(prop.getKey()), Property(strings.at(prop.getTextValue())));
            } else if(prop.isIntValue()) {
                Func(strings.at(prop.getKey()), Property(prop.getIntValue()));
            } else {
                NPNR_ASSERT(prop.isBoolValue());
                Func(strings.at(prop.getKey()), Property(prop.getBoolValue()));
            }
        }
    }

    template <typename TFunc> void foreach_attr(const ModuleReader &mod, TFunc Func) const {
        if(mod.is_top) {
            // Emit attribute "top" for top instance.
            Func("top", Property(1));
        }

        auto cell_def = root.getCellList()[mod.cell_inst.getCell()];
        auto cell_decl = root.getCellDecls()[cell_def.getIndex()];
        foreach_prop_map(cell_decl.getPropMap(), Func);
    }

    template <typename TFunc> void foreach_attr(const CellReader &cell, TFunc Func) const {
        auto cell_inst = root.getInstList()[cell.inst_idx];
        foreach_prop_map(cell_inst.getPropMap(), Func);
    }

    template <typename TFunc> void foreach_attr(const PortReader &port_reader, TFunc Func) const {
        auto port = root.getPortList()[port_reader.port_idx];
        foreach_prop_map(port.getPropMap(), Func);
    }

    template <typename TFunc> void foreach_attr(const NetReader &net_reader, TFunc Func) const {
        foreach_prop_map(net_reader.property_map, Func);
    }

    template <typename TFunc> void foreach_param(const CellReader &cell_reader, TFunc Func) const
    {
        auto cell_inst = root.getInstList()[cell_reader.inst_idx];
        foreach_prop_map(cell_inst.getPropMap(), Func);
    }

    template <typename TFunc> void foreach_setting(const ModuleReader &obj, TFunc Func) const
    {
        foreach_prop_map(root.getPropMap(), Func);
    }

    template <typename TFunc> void foreach_port_conn(const CellReader &cell, TFunc Func) const
    {
        auto cell_inst = root.getInstList()[cell.inst_idx];
        auto cell_def = root.getCellList()[cell_inst.getCell()];
        auto cell_decl = root.getCellDecls()[cell_def.getIndex()];

        for(auto port_idx : cell_decl.getPorts()) {
            auto port = root.getPortList()[port_idx];
            PortKey port_key(cell.inst_idx, port_idx);
            const std::vector<int32_t> &connections = cell.module->connections.at(port_key);
            Func(strings.at(port.getName()), connections);
        }
    }

    int get_array_offset(const NetReader &port_reader) const
    {
        return 0;
    }

    bool is_array_upto(const NetReader &port_reader) const {
        return false;
    }

    int get_array_offset(const PortReader &port_reader) const
    {
        auto port = root.getPortList()[port_reader.port_idx];
        if(port.isBus()) {
            auto bus = port.getBus();
            return std::min(bus.getBusStart(), bus.getBusEnd());
        } else {
            return 0;
        }
    }

    bool is_array_upto(const PortReader &port_reader) const {
        auto port = root.getPortList()[port_reader.port_idx];
        if(port.isBus()) {
            auto bus = port.getBus();
            return bus.getBusStart() < bus.getBusEnd();
        } else {
            return false;
        }
    }

    const std::vector<int32_t> &get_port_bits(const PortReader &port_reader) const {
        PortKey port_key(-1, port_reader.port_idx);
        return port_reader.module->connections.at(port_key);
    }

    const std::vector<int32_t> &get_net_bits(const NetReader &net) const {
        return net.scratch;
    }

    int get_vector_length(const std::vector<int32_t> &bits) const { return int(bits.size()); }

    bool is_vector_bit_constant(const std::vector<int32_t> &bits, int i) const
    {
        // Note: This appears weird, but is correct.  This is because VCC/GND
        // nets are not handled in frontend_base for FPGA interchange.
        return false;
    }

    char get_vector_bit_constval(const std::vector<int32_t>&bits, int i) const
    {
        // Unreachable!
        NPNR_ASSERT(false);
    }

    int get_vector_bit_signal(const std::vector<int32_t>&bits, int i) const
    {
        return bits.at(i);
    }
};

ModuleReader::ModuleReader(const LogicalNetlistImpl *root,
            LogicalNetlist::Netlist::CellInstance::Reader cell_inst, bool is_top) :
            root(root), is_top(is_top), cell_inst(cell_inst) {
    cell = root->root.getCellList()[cell_inst.getCell()];
    cell_decl = root->root.getCellDecls()[cell.getIndex()];

    // Auto-assign all ports to a net index, and then re-assign based on the
    // nets.
    int net_idx = 2;

    auto ports = root->root.getPortList();
    for(auto port_idx : cell_decl.getPorts()) {
        auto port = ports[port_idx];
        size_t port_width = get_port_width(port);

        PortKey port_key(-1, port_idx);
        auto result = connections.emplace(port_key, std::vector<int32_t>());
        NPNR_ASSERT(result.second);

        std::vector<int32_t> & port_connections = result.first->second;
        port_connections.resize(port_width);
        for(size_t i = 0; i < port_width; ++i) {
            port_connections[i] = net_idx++;
        }
    }

    for(auto inst_idx : cell.getInsts()) {
        auto inst = root->root.getInstList()[inst_idx];
        auto inst_cell = root->root.getCellList()[inst.getCell()];
        auto inst_cell_decl = root->root.getCellDecls()[inst_cell.getIndex()];

        auto inst_ports = inst_cell_decl.getPorts();
        for(auto inst_port_idx : inst_ports) {
            PortKey port_key(inst_idx, inst_port_idx);
            auto result = connections.emplace(port_key, std::vector<int32_t>());
            NPNR_ASSERT(result.second);

            auto inst_port = ports[inst_port_idx];
            size_t port_width = get_port_width(inst_port);

            std::vector<int32_t> & port_connections = result.first->second;
            port_connections.resize(port_width);
            for(size_t i = 0; i < port_width; ++i) {
                port_connections[i] = net_idx++;
            }
        }
    }

    auto nets = cell.getNets();
    for(size_t i = 0; i < nets.size(); ++i, ++net_idx) {
        auto net = nets[i];
        net_indicies[net_idx] = net;

        for(auto port_inst : net.getPortInsts()) {
            int32_t inst_idx = -1;
            if(port_inst.isInst()) {
                inst_idx = port_inst.getInst();
            }

            PortKey port_key(inst_idx, port_inst.getPort());
            std::vector<int32_t> & port_connections = connections.at(port_key);

            size_t port_idx = translate_port_index(port_inst);
            port_connections.at(port_idx) = net_idx;
        }
    }

    for(const auto & port_connections : connections) {
        for(size_t i = 0; i < port_connections.second.size(); ++i) {
            int32_t net_idx = port_connections.second[i];

            auto iter = net_indicies.find(net_idx);
            if(iter == net_indicies.end()) {
                PortKey port_key = port_connections.first;
                auto port = ports[port_key.port_idx];
                if(port_key.inst_idx != -1 && port.getDir() != LogicalNetlist::Netlist::Direction::OUTPUT) {
                    log_error("Cell instance %s port %s is disconnected!\n",
                            root->strings.at(root->root.getInstList()[port_key.inst_idx].getName()).c_str(),
                            root->strings.at(ports[port_key.port_idx].getName()).c_str()
                        );
                }
                disconnected_nets[net_idx] = stringf("%s.%d", root->strings.at(port.getName()).c_str(), i);
            }
        }
    }
}

void FpgaInterchange::read_logical_netlist(Context * ctx, const std::string &filename) {
    gzFile file = gzopen(filename.c_str(), "r");
    NPNR_ASSERT(file != Z_NULL);

    std::vector<uint8_t> output_data;
    output_data.resize(4096);
    std::stringstream sstream(std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    while(true) {
        int ret = gzread(file, output_data.data(), output_data.size());
        NPNR_ASSERT(ret >= 0);
        if(ret > 0) {
            sstream.write((const char*)output_data.data(), ret);
            NPNR_ASSERT(sstream);
        } else {
            NPNR_ASSERT(ret == 0);
            int error;
            gzerror(file, &error);
            NPNR_ASSERT(error == Z_OK);
            break;
        }
    }

    NPNR_ASSERT(gzclose(file) == Z_OK);

    sstream.seekg(0);
    kj::std::StdInputStream istream(sstream);
    capnp::ReaderOptions reader_options;
    reader_options.traversalLimitInWords = 32llu*1024llu*1024llu*1024llu;
    capnp::InputStreamMessageReader message_reader(istream, reader_options);

    LogicalNetlist::Netlist::Reader netlist = message_reader.getRoot<LogicalNetlist::Netlist>();
    LogicalNetlistImpl netlist_reader(netlist);

    GenericFrontend<LogicalNetlistImpl>(ctx, netlist_reader, /*split_io=*/false)();
}

size_t ModuleReader::translate_port_index(LogicalNetlist::Netlist::PortInstance::Reader port_inst) const {
    LogicalNetlist::Netlist::Port::Reader port = root->root.getPortList()[port_inst.getPort()];
    if(port_inst.getBusIdx().isSingleBit()) {
        NPNR_ASSERT(port.isBit());
        return 0;
    } else {
        NPNR_ASSERT(port.isBus());
        uint32_t idx = port_inst.getBusIdx().getIdx();
        size_t width = get_port_width(port);
        NPNR_ASSERT(idx < width);
        return width - 1 - idx;
    }
}

NEXTPNR_NAMESPACE_END


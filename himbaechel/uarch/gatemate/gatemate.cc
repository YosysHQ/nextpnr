/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include <utility>

#include "gatemate.h"
#include "log.h"
#include "placer_heap.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"

#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

GateMateImpl::~GateMateImpl() {};

po::options_description GateMateImpl::getUArchOptions()
{
    po::options_description specific("GateMate specific options");
    specific.add_options()("out", po::value<std::string>(), "textual configuration bitstream output file");
    specific.add_options()("ccf", po::value<std::string>(), "name of constraints file");
    specific.add_options()("allow-unconstrained", "allow unconstrained IOs");
    specific.add_options()("fpga_mode", po::value<std::string>(), "operation mode (1:lowpower, 2:economy, 3:speed)");
    specific.add_options()("time_mode", po::value<std::string>(), "timing mode (1:best, 2:typical, 3:worst)");
    specific.add_options()("strategy", po::value<std::string>(),
                           "multi-die clock placement strategy (mirror, full or clk1)");
    specific.add_options()("force_die", po::value<std::string>(), "force specific die (example 1A,1B...)");
    return specific;
}

static int parse_mode(const std::string &val, const std::map<std::string, int> &map, const char *error_msg)
{
    try {
        int i = std::stoi(val);
        if (i >= 1 && i <= 3)
            return i;
    } catch (...) {
        auto it = map.find(val);
        if (it != map.end())
            return it->second;
    }
    log_error("%s\n", error_msg);
}

void GateMateImpl::init_database(Arch *arch)
{
    const ArchArgs &args = arch->args;
    init_uarch_constids(arch);
    arch->load_chipdb(stringf("gatemate/chipdb-%s.bin", args.device.c_str()));
    arch->set_package("FBGA324");
    dies = std::stoi(args.device.substr(6));
    fpga_mode = 3;
    timing_mode = 3;

    static const std::map<std::string, int> fpga_map = {{"lowpower", 1}, {"economy", 2}, {"speed", 3}};
    static const std::map<std::string, int> timing_map = {{"best", 1}, {"typical", 2}, {"worst", 3}};

    if (args.options.count("fpga_mode"))
        fpga_mode = parse_mode(args.options["fpga_mode"].as<std::string>(), fpga_map,
                               "operation mode valid values are {1:lowpower, 2:economy, 3:speed}");
    if (args.options.count("time_mode"))
        timing_mode = parse_mode(args.options["time_mode"].as<std::string>(), timing_map,
                                 "timing mode valid values are {1:best, 2:typical, 3:worst}");

    std::string speed_grade = "";
    switch (timing_mode) {
    case 1:
        speed_grade = "best_";
        break;
    case 2:
        speed_grade = "typ_";
        break;
    default:
        speed_grade = "worst_";
        break;
    }
    log_info("Using timing mode '%s'\n", timing_mode == 1   ? "BEST"
                                         : timing_mode == 2 ? "TYPICAL"
                                         : timing_mode == 3 ? "WORST"
                                                            : "");

    switch (fpga_mode) {
    case 1:
        speed_grade += "lpr";
        break;
    case 2:
        speed_grade += "eco";
        break;
    default:
        speed_grade += "spd";
    }
    log_info("Using operation mode '%s'\n", fpga_mode == 1   ? "LOWPOWER"
                                            : fpga_mode == 2 ? "ECONOMY"
                                            : fpga_mode == 3 ? "SPEED"
                                                             : "");
    arch->set_speed_grade(speed_grade);
}

void GateMateImpl::init(Context *ctx)
{
    HimbaechelAPI::init(ctx);
    for (const auto &pad : ctx->package_info->pads) {
        available_pads.emplace(IdString(pad.package_pin));
        BelId bel = ctx->getBelByName(IdStringList::concat(IdString(pad.tile), IdString(pad.bel)));
        bel_to_pad.emplace(bel, &pad);
        locations.emplace(std::make_pair(IdString(pad.package_pin), tile_extra_data(bel.tile)->die),
                          ctx->getBelLocation(bel));
    }
    available_pads.emplace(ctx->id("SER_CLK"));
    available_pads.emplace(ctx->id("SER_CLK_N"));
    for (auto bel : ctx->getBels()) {
        auto *ptr = bel_extra_data(bel);
        std::map<IdString, const GateMateBelPinConstraintPOD *> pins;
        for (const auto &p : ptr->constraints)
            pins.emplace(IdString(p.name), &p);
        pin_to_constr.emplace(bel, pins);
        if (ctx->getBelType(bel).in(id_CLKIN, id_GLBOUT, id_PLL, id_USR_RSTN, id_CFG_CTRL, id_SERDES)) {
            locations.emplace(std::make_pair(ctx->getBelName(bel)[1], tile_extra_data(bel.tile)->die),
                              ctx->getBelLocation(bel));
        }
    }
    const auto &sp = reinterpret_cast<const GateMateSpeedGradeExtraDataPOD *>(ctx->speed_grade->extra_data.get());
    for (int i = 0; i < sp->timings.ssize(); i++) {
        timing.emplace(IdString(sp->timings[i].name), &sp->timings[i]);
    }
    for (int num = 0; num < 2; num++) {
        int index = (num == 0) ? 0 : 2;
        ram_signal_clk.emplace(ctx->idf("ENA[%d]", index), num);
        ram_signal_clk.emplace(ctx->idf("ENB[%d]", index), num + 2);
        ram_signal_clk.emplace(ctx->idf("GLWEA[%d]", index), num);
        ram_signal_clk.emplace(ctx->idf("GLWEB[%d]", index), num + 2);
        for (int i = 0; i < 20; i++) {
            ram_signal_clk.emplace(ctx->idf("WEA[%d]", i + num * 20), num);
            ram_signal_clk.emplace(ctx->idf("WEB[%d]", i + num * 20), num + 2);
        }

        for (int i = 0; i < 16; i++) {
            ram_signal_clk.emplace(ctx->idf("ADDRA%d[%d]", num, i), num);
            ram_signal_clk.emplace(ctx->idf("ADDRB%d[%d]", num, i), num + 2);
        }

        for (int i = 0; i < 20; i++) {
            ram_signal_clk.emplace(ctx->idf("DIA[%d]", i + num * 20), num);
            ram_signal_clk.emplace(ctx->idf("DOA[%d]", i + num * 20), num);
            ram_signal_clk.emplace(ctx->idf("DIB[%d]", i + num * 20), num + 2);
            ram_signal_clk.emplace(ctx->idf("DOB[%d]", i + num * 20), num + 2);
        }
    }

    const GateMateChipExtraDataPOD *extra =
            reinterpret_cast<const GateMateChipExtraDataPOD *>(ctx->chip_info->extra_data.get());

    const ArchArgs &args = ctx->args;
    std::string die_name;
    if (args.options.count("force_die"))
        die_name = args.options["force_die"].as<std::string>();
    bool found = false;
    int index = 0;
    for (auto &die : extra->dies) {
        IdString name(die.name);
        index_to_die[index] = name;
        die_to_index[name] = index++;
        ctx->createRectangularRegion(name, die.x1, die.y1, die.x2, die.y2);
        if (die_name == name.c_str(ctx)) {
            found = true;
            forced_die = name;
        }
    }
    if (!die_name.empty() && !found)
        log_error("Unable to select forced die '%s'.\n", die_name.c_str());
}

bool GateMateImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    CellInfo *cell = ctx->getBoundBelCell(bel);
    if (cell == nullptr) {
        return true;
    }

    if (getBelBucketForBel(bel) == id_CPE_FF) {
        Loc loc = ctx->getBelLocation(bel);
        const CellInfo *adj_half = ctx->getBoundBelCell(
                ctx->getBelByLocation(Loc(loc.x, loc.y, loc.z == CPE_FF_L_Z ? CPE_FF_U_Z : CPE_FF_L_Z)));
        if (adj_half) {
            const auto &half_data = fast_cell_info.at(cell->flat_index);
            if (half_data.used) {
                const auto &adj_data = fast_cell_info.at(adj_half->flat_index);
                if (adj_data.used) {
                    if (adj_data.config != half_data.config)
                        return false;
                    if (adj_data.ff_en != half_data.ff_en)
                        return false;
                    if (adj_data.ff_clk != half_data.ff_clk)
                        return false;
                    if (adj_data.ff_sr != half_data.ff_sr)
                        return false;
                }
            }
        }
        return true;
    } else if (getBelBucketForBel(bel) == id_RAM_HALF) {
        Loc loc = ctx->getBelLocation(bel);
        const CellInfo *adj_half =
                ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.z == RAM_HALF_L_Z ? loc.y - 8 : loc.y + 8,
                                                               loc.z == RAM_HALF_L_Z ? RAM_FULL_Z : RAM_HALF_L_Z)));
        if (adj_half) {
            const auto &half_data = fast_cell_info.at(cell->flat_index);
            if (half_data.used) {
                const auto &adj_data = fast_cell_info.at(adj_half->flat_index);
                if (adj_data.used) {
                    if (adj_data.config != half_data.config)
                        return false;
                }
            }
        }
        return true;
    }

    return true;
}

Loc GateMateImpl::getRelativeConstraint(Loc &root_loc, IdString id) const
{
    Loc child_loc;
    BelId root_bel = ctx->getBelByLocation(root_loc);
    if (pin_to_constr.count(root_bel)) {
        auto &constr = pin_to_constr.at(root_bel);
        if (constr.count(id)) {
            auto &p = constr.at(id);
            child_loc.x = root_loc.x + p->constr_x;
            child_loc.y = root_loc.y + p->constr_y;
            child_loc.z = p->constr_z;
        } else {
            log_error("Constrain info not available for pin '%s'.\n", id.c_str(ctx));
        }
    } else {
        log_error("Bel info not available for constraints.\n");
    }
    return child_loc;
}

bool GateMateImpl::getChildPlacement(const BaseClusterInfo *cluster, Loc root_loc,
                                     std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    for (auto child : cluster->constr_children) {
        Loc child_loc;
        if (child->constr_z >= PLACE_DB_CONSTR) {
            child_loc = getRelativeConstraint(root_loc, IdString(child->constr_z - PLACE_DB_CONSTR));
        } else {
            child_loc.x = root_loc.x + child->constr_x;
            child_loc.y = root_loc.y + child->constr_y;
            child_loc.z = child->constr_abs_z ? child->constr_z : (root_loc.z + child->constr_z);
        }
        if (child_loc.x < 0 || child_loc.x >= ctx->getGridDimX())
            return false;
        if (child_loc.y < 0 || child_loc.y >= ctx->getGridDimY())
            return false;
        BelId child_bel = ctx->getBelByLocation(child_loc);
        if (child_bel == BelId() || !this->isValidBelForCellType(child->type, child_bel))
            return false;
        placement.emplace_back(child, child_bel);
        if (!getChildPlacement(child, child_loc, placement))
            return false;
    }
    return true;
}

bool GateMateImpl::getClusterPlacement(ClusterId cluster, BelId root_bel,
                                       std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    CellInfo *root_cell = get_cluster_root(ctx, cluster);
    placement.clear();
    NPNR_ASSERT(root_bel != BelId());
    Loc root_loc = ctx->getBelLocation(root_bel);
    if (root_cell->constr_abs_z) {
        // Coerce root to absolute z constraint
        root_loc.z = root_cell->constr_z;
        root_bel = ctx->getBelByLocation(root_loc);
        if (root_bel == BelId() || !this->isValidBelForCellType(root_cell->type, root_bel))
            return false;
    }
    placement.emplace_back(root_cell, root_bel);
    return getChildPlacement(root_cell, root_loc, placement);
}

void GateMateImpl::prePlace() { assign_cell_info(); }

void GateMateImpl::postPlace()
{
    repack();
    ctx->assignArchInfo();
    used_cpes.resize(ctx->getGridDimX() * ctx->getGridDimY());
    for (auto &cell : ctx->cells) {
        // We need to skip CPE_MULT since using CP outputs is mandatory
        // even if output is actually not connected
        bool marked_used = cell.second.get()->type == id_CPE_MULT;
        // Can not use FF for OUT2 if CPE is used in bridge mode
        if (cell.second.get()->type == id_CPE_FF && ctx->getBelLocation(cell.second.get()->bel).z == CPE_FF_U_Z)
            marked_used = true;
        if (marked_used)
            used_cpes[cell.second.get()->bel.tile] = true;
    }
}
bool GateMateImpl::checkPipAvail(PipId pip) const
{
    const auto &extra_data = *pip_extra_data(pip);
    if (extra_data.type != PipExtra::PIP_EXTRA_MUX || (extra_data.flags & MUX_ROUTING) == 0)
        return true;
    if (used_cpes[pip.tile])
        return false;
    return true;
}

void GateMateImpl::preRoute()
{
    route_mult();
    route_clock();
    ctx->assignArchInfo();
}

void GateMateImpl::reassign_bridges(NetInfo *ni, const dict<WireId, PipMap> &net_wires, WireId wire,
                                    dict<WireId, IdString> &wire_to_net, int &num)
{
    wire_to_net.insert({wire, ni->name});

    for (auto pip : ctx->getPipsDownhill(wire)) {
        auto dst = ctx->getPipDstWire(pip);

        // Ignore wires not part of the net
        auto it = net_wires.find(dst);
        if (it == net_wires.end())
            continue;
        // Ignore pips if the wire is driven by another pip.
        if (pip != it->second.pip)
            continue;
        // Ignore wires already visited.
        if (wire_to_net.count(dst))
            continue;

        const auto &extra_data = *pip_extra_data(pip);
        // If not a bridge, just recurse.
        if (extra_data.type != PipExtra::PIP_EXTRA_MUX || !(extra_data.flags & MUX_ROUTING)) {
            reassign_bridges(ni, net_wires, dst, wire_to_net, num);
            continue;
        }

        // We have a bridge that needs to be translated to a bel.
        IdString name = ctx->idf("%s$bridge%d", ni->name.c_str(ctx), num);

        IdStringList id = ctx->getPipName(pip);
        Loc loc = ctx->getPipLocation(pip);
        BelId bel = ctx->getBelByLocation({loc.x, loc.y, CPE_BRIDGE_Z});
        CellInfo *cell = ctx->createCell(name, id_CPE_BRIDGE);
        ctx->bindBel(bel, cell, PlaceStrength::STRENGTH_FIXED);
        cell->params[id_C_BR] = Property(Property::State::S1, 1);
        cell->params[id_C_SN] = Property(extra_data.value, 3);

        NetInfo *new_net = ctx->createNet(ctx->idf("%s$muxout", name.c_str(ctx)));
        IdString in_port = ctx->idf("IN%d", extra_data.value + 1);

        cell->addInput(in_port);
        cell->connectPort(in_port, ni);

        cell->addOutput(id_MUXOUT);
        cell->connectPort(id_MUXOUT, new_net);

        num++;

        reassign_bridges(new_net, net_wires, dst, wire_to_net, num);
    }
}

void GateMateImpl::postRoute()
{
    int num = 0;

    pool<IdString> nets_with_bridges;

    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        for (auto &w : ni->wires) {
            if (w.second.pip != PipId()) {
                const auto &extra_data = *pip_extra_data(w.second.pip);
                if (extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_ROUTING)) {
                    nets_with_bridges.insert(ni->name);
                }
            }
        }
    }

    for (auto net_name : nets_with_bridges) {
        auto *ni = ctx->nets.at(net_name).get();
        auto net_wires = ni->wires; // copy wires to preserve across unbind/rebind.
        auto wire_to_net = dict<WireId, IdString>{};
        auto wire_to_port = dict<WireId, std::vector<PortRef>>{};

        for (auto &usr : ni->users)
            for (auto sink_wire : ctx->getNetinfoSinkWires(ni, usr)) {
                auto result = wire_to_port.find(sink_wire);
                if (result == wire_to_port.end())
                    wire_to_port.insert({sink_wire, std::vector<PortRef>{usr}});
                else
                    result->second.push_back(usr);
            }

        // traverse the routing tree to assign bridge nets to wires.
        reassign_bridges(ni, net_wires, ctx->getNetinfoSourceWire(ni), wire_to_net, num);

        for (auto &pair : net_wires)
            ctx->unbindWire(pair.first);

        for (auto &pair : net_wires) {
            auto wire = pair.first;
            auto pip = pair.second.pip;
            auto strength = pair.second.strength;
            auto *net = ctx->nets.at(wire_to_net.at(wire)).get();
            if (pip == PipId())
                ctx->bindWire(wire, net, strength);
            else
                ctx->bindPip(pip, net, strength);

            if (wire_to_port.count(wire)) {
                for (auto sink : wire_to_port.at(wire)) {
                    NPNR_ASSERT(sink.cell != nullptr && sink.port != IdString());
                    sink.cell->disconnectPort(sink.port);
                    sink.cell->connectPort(sink.port, net);
                }
            }
        }
    }
    log_info("Check CPEs..\n");
    dict<IdString,int> cfg;
    dict<IdString,IdString> port_mapping;
    auto add_input = [&](IdString orig_port, IdString port) {
        static dict<IdString,IdString> convert_port = {
             {ctx->id("CPE.IN1"),id_IN1},
             {ctx->id("CPE.IN2"),id_IN2},
             {ctx->id("CPE.IN3"),id_IN3},
             {ctx->id("CPE.IN4"),id_IN4},
             {ctx->id("CPE.IN5"),id_IN1},
             {ctx->id("CPE.IN6"),id_IN2},
             {ctx->id("CPE.IN7"),id_IN3},
             {ctx->id("CPE.IN8"),id_IN4},
             {ctx->id("CPE.PINY1"),id_PINY1},
             {ctx->id("CPE.CINX"),id_CINX},
             {ctx->id("CPE.PINX"),id_PINX}
            };
        if (convert_port.count(port)) {
            //printf("CONVERTED %s %s\n",orig_port.c_str(ctx), port.c_str(ctx));
            port_mapping.emplace(orig_port, convert_port[port]);
        };

    };
    auto check_input = [&](CellInfo *cell, IdString port) {
        if (cell->getPort(port)) {
            NetInfo *net = cell->getPort(port);
            WireId pin_wire = ctx->getBelPinWire(cell->bel, port);
            if (net->wires.count(pin_wire)) {
                //printf("Found pin connection\n");
                auto &p = net->wires.at(pin_wire);
                //printf("pip: %s -> %s \n",ctx->getPipName(p.pip)[1].c_str(ctx), ctx->getPipName(p.pip)[2].c_str(ctx));
                WireId src = ctx->getPipSrcWire(p.pip);

                const auto &extra_data = *pip_extra_data(p.pip);
                if (extra_data.type == PipExtra::PIP_EXTRA_MUX) {
                    //printf("name:%s %d\n",IdString(extra_data.name).c_str(ctx),extra_data.value);
                    cfg.emplace(IdString(extra_data.name),extra_data.value);
                    add_input(port, ctx->getWireName(src)[1]);
                }

                if (net->wires.count(src)) {
                    //printf("Found pin connection\n");
                    auto &p = net->wires.at(src);
                    //printf("pip: %s -> %s \n",ctx->getPipName(p.pip)[1].c_str(ctx), ctx->getPipName(p.pip)[2].c_str(ctx));
                    WireId src = ctx->getPipSrcWire(p.pip);
                    //printf("wire : %s\n",ctx->getWireName(src)[1].c_str(ctx));
                    const auto &extra_data = *pip_extra_data(p.pip);
                    if (extra_data.type == PipExtra::PIP_EXTRA_MUX) {
                        //printf("name:%s %d\n",IdString(extra_data.name).c_str(ctx),extra_data.value);
                        cfg.emplace(IdString(extra_data.name),extra_data.value);
                        add_input(port, ctx->getWireName(src)[1]);
                    }


                    if (net->wires.count(src)) {
                        //printf("Found pin connection\n");
                        auto &p = net->wires.at(src);
                        //printf("pip: %s -> %s \n",ctx->getPipName(p.pip)[1].c_str(ctx), ctx->getPipName(p.pip)[2].c_str(ctx));
                        WireId src = ctx->getPipSrcWire(p.pip);
                        //printf("wire : %s\n",ctx->getWireName(src)[1].c_str(ctx));
                        const auto &extra_data = *pip_extra_data(p.pip);
                        if (extra_data.type == PipExtra::PIP_EXTRA_MUX) {
                            //printf("name:%s %d\n",IdString(extra_data.name).c_str(ctx),extra_data.value);
                            cfg.emplace(IdString(extra_data.name),extra_data.value);
                            add_input(port, ctx->getWireName(src)[1]);
                        }

                    } else {
                        //printf("No pin connection\n");
                    }
                } else {
                    //printf("No pin connection\n");
                }

            } else {
                //printf("No pin connection\n");
            }
        }
    };
    auto swap_lut2_inputs = [&](int lut) -> int {
        // bit permutation: [3,1,2,0]
        return ((lut & 0b1000))       |     // b3 -> bit 3
            ((lut & 0b0010) << 1)  |     // b1 -> bit 2
            ((lut & 0b0100) >> 1)  |     // b2 -> bit 1
            ((lut & 0b0001));            // b0 -> bit 0
    };

    for (auto &cell : ctx->cells) {
        if (cell.second->type.in(id_CPE_L2T4)) {
            //printf("\n");
            cfg.clear();
            port_mapping.clear();
            //printf("type:%s name:%s\n",cell.second->type.c_str(ctx),cell.second->name.c_str(ctx));
            int l00 = int_or_default(cell.second->params, id_INIT_L00, 0);
            int l01 = int_or_default(cell.second->params, id_INIT_L01, 0);
            int l10 = int_or_default(cell.second->params, id_INIT_L10, 0);
            //printf("L00 %04b\n",l00);
            //printf("L01 %04b\n",l01);
            //printf("L10 %04b\n",l10);
            check_input(cell.second.get(), id_D0_00);
            check_input(cell.second.get(), id_D1_00);
            check_input(cell.second.get(), id_D0_01);
            check_input(cell.second.get(), id_D1_01);
            check_input(cell.second.get(), id_D0_10);
            check_input(cell.second.get(), id_D1_10);
            if (cfg.count(ctx->id("LUT2_11")) || cfg.count(ctx->id("LUT2_10"))) {
                //printf("LUT2 like\n");
                if (cfg.count(ctx->id("LUT2_11"))) { //lower
                    if (cfg.count(ctx->id("LUT2_02")) && !cfg.count(ctx->id("LUT2_03"))) {
                        // both inputs on 02
                        l00 = l10; // config is now in 02
                        l10 = 0b1010; // LUT_D0 - we propagate only
                    } else if (!cfg.count(ctx->id("LUT2_02")) && cfg.count(ctx->id("LUT2_03"))) {
                        // both inputs on 03
                        l01 = l10; // config is now in 03
                        l10 = 0b1010; // LUT_D0 - we propagate only
                    } else {
                        // one input on 02, other on 03 (or LUT1)
                        if (cfg.count(ctx->id("LUT2_02"))) {
                            l00 = 0b1010; // LUT_D0 - we propagate only
                        }
                        if (cfg.count(ctx->id("LUT2_03"))) {
                            l01 = 0b1010; // LUT_D0 - we propagate only
                        }
                    }

                    if (cfg.at(ctx->id("LUT2_11"))==1)
                        l10 = swap_lut2_inputs(l10);
                    if (cfg.count(ctx->id("LUT2_02")) && (cfg.at(ctx->id("LUT2_02"))==1))
                        l00 = swap_lut2_inputs(l00);
                    if (cfg.count(ctx->id("LUT2_03")) && (cfg.at(ctx->id("LUT2_03"))==1))
                        l01 = swap_lut2_inputs(l01);
                } else { // upper part
                    if (cfg.count(ctx->id("LUT2_00")) && !cfg.count(ctx->id("LUT2_01"))) {
                        // both inputs on 02
                        l00 = l10; // config is now in 02
                        l10 = 0b1010; // LUT_D0 - we propagate only
                    } else if (!cfg.count(ctx->id("LUT2_00")) && cfg.count(ctx->id("LUT2_01"))) {
                        // both inputs on 03
                        l01 = l10; // config is now in 03
                        l10 = 0b1010; // LUT_D0 - we propagate only
                    } else {
                        // one input on 02, other on 03 (or LUT1)
                        if (cfg.count(ctx->id("LUT2_00"))) {
                            l00 = 0b1010; // LUT_D0 - we propagate only
                        }
                        if (cfg.count(ctx->id("LUT2_01"))) {
                            l01 = 0b1010; // LUT_D0 - we propagate only
                        }
                    }

                    if (cfg.at(ctx->id("LUT2_10"))==1)
                        l10 = swap_lut2_inputs(l10);
                    if (cfg.count(ctx->id("LUT2_00")) && (cfg.at(ctx->id("LUT2_00"))==1))
                        l00 = swap_lut2_inputs(l00);
                    if (cfg.count(ctx->id("LUT2_01")) && (cfg.at(ctx->id("LUT2_01"))==1))
                        l01 = swap_lut2_inputs(l01);

                }
                //printf("updated\n=========\n");
                //printf("L00 %04b\n",l00);
                //printf("L01 %04b\n",l01);
                //printf("L10 %04b\n",l10);
                cell.second->params[id_INIT_L00] = Property(l00,4);
                cell.second->params[id_INIT_L01] = Property(l01,4);
                cell.second->params[id_INIT_L10] = Property(l10,4);
                
                cell.second->renamePort(id_D0_10, port_mapping[id_D0_10]);
                cell.second->renamePort(id_D1_10, port_mapping[id_D1_10]);
            } else {
                if (cfg.count(ctx->id("LUT2_00")) && cfg.at(ctx->id("LUT2_00"))==1) {
                    l00 = swap_lut2_inputs(l00);
                }    
                if (cfg.count(ctx->id("LUT2_01")) && cfg.at(ctx->id("LUT2_01"))==1) {
                    l01 = swap_lut2_inputs(l01);
                }
                if (cfg.count(ctx->id("LUT2_02")) && cfg.at(ctx->id("LUT2_02"))==1) {
                    l00 = swap_lut2_inputs(l00);
                }
                if (cfg.count(ctx->id("LUT2_03")) && cfg.at(ctx->id("LUT2_03"))==1) {
                    l01 = swap_lut2_inputs(l01);
                }

                cell.second->params[id_INIT_L00] = Property(l00,4);
                cell.second->params[id_INIT_L01] = Property(l01,4);
                cell.second->params[id_INIT_L10] = Property(l10,4);

                cell.second->renamePort(id_D0_00, port_mapping[id_D0_00]);
                cell.second->renamePort(id_D1_00, port_mapping[id_D1_00]);
                cell.second->renamePort(id_D0_01, port_mapping[id_D0_01]);
                cell.second->renamePort(id_D1_01, port_mapping[id_D1_01]);                    
            }
            if (cfg.count(ctx->id("CPE.C_I1"))) 
                cell.second->params[id_C_I1] = Property(1,1);
            if (cfg.count(ctx->id("CPE.C_I2"))) 
                cell.second->params[id_C_I2] = Property(1,1);
            if (cfg.count(ctx->id("CPE.C_I3"))) 
                cell.second->params[id_C_I3] = Property(1,1);
            if (cfg.count(ctx->id("CPE.C_I4"))) 
                cell.second->params[id_C_I4] = Property(1,1);

        }
    }
    ctx->assignArchInfo();

    const ArchArgs &args = ctx->args;
    if (args.options.count("out")) {
        write_bitstream(args.device, args.options["out"].as<std::string>());
    }
}

BoundingBox GateMateImpl::getRouteBoundingBox(WireId src, WireId dst) const
{
    int x0, y0, x1, y1;
    auto expand = [&](int x, int y) {
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    };
    tile_xy(ctx->chip_info, src.tile, x0, y0);
    x1 = x0;
    y1 = y0;
    int dx, dy;
    tile_xy(ctx->chip_info, dst.tile, dx, dy);
    expand(dx, dy);

    return {(x0 & 0xfffe), (y0 & 0xfffe), (x1 & 0xfffe) + 1, (y1 & 0xfffe) + 1};
}

void GateMateImpl::expandBoundingBox(BoundingBox &bb) const
{
    bb.x0 = std::max((bb.x0 & 0xfffe) - 4, 0);
    bb.y0 = std::max((bb.y0 & 0xfffe) - 4, 0);
    bb.x1 = std::min((bb.x1 & 0xfffe) + 5, ctx->getGridDimX());
    bb.y1 = std::min((bb.y1 & 0xfffe) + 5, ctx->getGridDimY());
}

void GateMateImpl::configurePlacerHeap(PlacerHeapCfg &cfg)
{
    cfg.chainRipup = true;
    cfg.placeAllAtOnce = true;
}

int GateMateImpl::get_dff_config(CellInfo *dff) const
{
    int val = 0;
    val |= int_or_default(dff->params, id_C_CPE_EN, 0);
    val <<= 2;
    val |= int_or_default(dff->params, id_C_CPE_CLK, 0);
    val <<= 2;
    val |= int_or_default(dff->params, id_C_CPE_RES, 0);
    val <<= 2;
    val |= int_or_default(dff->params, id_C_CPE_SET, 0);
    val <<= 2;
    val |= int_or_default(dff->params, id_C_EN_SR, 0);
    val <<= 1;
    val |= int_or_default(dff->params, id_C_L_D, 0);
    val <<= 1;
    val |= int_or_default(dff->params, id_FF_INIT, 0);
    return val;
}

int GateMateImpl::get_ram_config(CellInfo *ram) const
{
    int val = 0;
    val |= int_or_default(ram->params, id_RAM_cfg_ecc_enable, 0);
    val <<= 2;
    val |= int_or_default(ram->params, id_RAM_cfg_sram_mode, 0);
    return val;
}

void GateMateImpl::assign_cell_info()
{
    fast_cell_info.resize(ctx->cells.size());
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        auto &fc = fast_cell_info.at(ci->flat_index);
        if (getBelBucketForCellType(ci->type) == id_CPE_FF) {
            fc.ff_en = ci->getPort(id_EN);
            fc.ff_clk = ci->getPort(id_CLK);
            fc.ff_sr = ci->getPort(id_SR);
            fc.config = get_dff_config(ci);
            fc.used = true;
        }
        if (getBelBucketForCellType(ci->type) == id_RAM_HALF) {
            fc.config = get_ram_config(ci);
            fc.used = true;
        }
    }
}

// Bel bucket functions
IdString GateMateImpl::getBelBucketForCellType(IdString cell_type) const
{
    if (cell_type.in(id_CPE_IBUF, id_CPE_OBUF, id_CPE_TOBUF, id_CPE_IOBUF, id_CPE_LVDS_IBUF, id_CPE_LVDS_TOBUF,
                     id_CPE_LVDS_OBUF, id_CPE_LVDS_IOBUF))
        return id_GPIO;
    else if (cell_type.in(id_CPE_LT_U, id_CPE_LT_L, id_CPE_LT, id_CPE_L2T4))
        return id_CPE_LT;
    else if (cell_type.in(id_CPE_FF_U, id_CPE_FF_L, id_CPE_FF, id_CPE_LATCH))
        return id_CPE_FF;
    else if (cell_type.in(id_CPE_RAMIO, id_CPE_RAMI, id_CPE_RAMO))
        return id_CPE_RAMIO;
    else if (cell_type.in(id_RAM, id_RAM_HALF, id_RAM_HALF_DUMMY))
        return id_RAM_HALF;
    else
        return cell_type;
}

BelBucketId GateMateImpl::getBelBucketForBel(BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type.in(id_CPE_LT_U, id_CPE_LT_L))
        return id_CPE_LT;
    else if (bel_type.in(id_CPE_FF_U, id_CPE_FF_L))
        return id_CPE_FF;
    else if (bel_type.in(id_CPE_RAMIO_U, id_CPE_RAMIO_L))
        return id_CPE_RAMIO;
    else if (bel_type.in(id_RAM, id_RAM_HALF_L))
        return id_RAM_HALF;
    return bel_type;
}

bool GateMateImpl::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type == id_GPIO)
        return cell_type.in(id_CPE_IBUF, id_CPE_OBUF, id_CPE_TOBUF, id_CPE_IOBUF, id_CPE_LVDS_IBUF, id_CPE_LVDS_TOBUF,
                            id_CPE_LVDS_OBUF, id_CPE_LVDS_IOBUF);
    else if (bel_type == id_CPE_LT_U)
        return cell_type.in(id_CPE_LT_U, id_CPE_LT, id_CPE_L2T4, id_CPE_DUMMY);
    else if (bel_type == id_CPE_LT_L)
        return cell_type.in(id_CPE_LT_L, id_CPE_LT, id_CPE_L2T4, id_CPE_DUMMY);
    else if (bel_type == id_CPE_FF_U)
        return cell_type.in(id_CPE_FF_U, id_CPE_FF, id_CPE_LATCH);
    else if (bel_type == id_CPE_FF_L)
        return cell_type.in(id_CPE_FF_L, id_CPE_FF, id_CPE_LATCH);
    else if (bel_type.in(id_CPE_RAMIO_U, id_CPE_RAMIO_L))
        return cell_type.in(id_CPE_RAMIO, id_CPE_RAMI, id_CPE_RAMO);
    else if (bel_type == id_RAM)
        return cell_type.in(id_RAM_HALF, id_RAM);
    else if (bel_type == id_RAM_HALF_L)
        return cell_type.in(id_RAM_HALF, id_RAM_HALF_DUMMY);
    else
        return (bel_type == cell_type);
}

bool GateMateImpl::isPipInverting(PipId pip) const
{
    const auto &extra_data = *pip_extra_data(pip);
    return extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_INVERT);
}

const GateMateTileExtraDataPOD *GateMateImpl::tile_extra_data(int tile) const
{
    return reinterpret_cast<const GateMateTileExtraDataPOD *>(ctx->chip_info->tile_insts[tile].extra_data.get());
}

const GateMateBelExtraDataPOD *GateMateImpl::bel_extra_data(BelId bel) const
{
    return reinterpret_cast<const GateMateBelExtraDataPOD *>(chip_bel_info(ctx->chip_info, bel).extra_data.get());
}

const GateMatePipExtraDataPOD *GateMateImpl::pip_extra_data(PipId pip) const
{
    return reinterpret_cast<const GateMatePipExtraDataPOD *>(chip_pip_info(ctx->chip_info, pip).extra_data.get());
}

struct GateMateArch : HimbaechelArch
{
    GateMateArch() : HimbaechelArch("gatemate") {};
    bool match_device(const std::string &device) override
    {
        return device.size() > 6 && device.substr(0, 6) == "CCGM1A";
    }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device) override
    {
        return std::make_unique<GateMateImpl>();
    }
} gateMateArch;

NEXTPNR_NAMESPACE_END

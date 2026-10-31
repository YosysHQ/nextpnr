/*
 * nextpnr himbaechel uarch for the Altera Cyclone II EP2C35F672C6.
 *
 * The chip database comes from `cram chipdb` (tools/cram/src/chipdb.rs, which
 * documents the tile and wire naming this file relies on) via gen/arch_gen.py.
 *
 * Cells (from synth/cyclone2.ys):
 *   LCELL_COMB  LUT4 + carry: DATAA..DATAD, CIN -> COMBOUT, COUT; LUT_MASK.
 *               With CIN connected the LUT's C input is the carry, as in
 *               Quartus' sum_lutc_input = "cin".
 *   LCELL_FF    register fed by its own LE's LUT: DATAIN, CLK, ENA, ACLR, SCLR
 *               -> REGOUT (ENA gates SCLR, as in the hardware).
 *   CY2_IBUF / CY2_OBUF  pins, placed by `--vopt qsf=<file>` location
 *               assignments (`set_location_assignment PIN_x -to port`).
 *   CLKCTRL     global clock control, inserted here on every clock net.
 */

#include <algorithm>
#include <fstream>
#include <queue>
#include <regex>

#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "himbaechel_helpers.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/cyclone2/constids.inc"
#define HIMBAECHEL_GFXIDS "uarch/cyclone2/gfxids.inc"
#define HIMBAECHEL_UARCH cyclone2
#include "himbaechel_constids.h"
#include "himbaechel_gfxids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

const IdString lut_inputs[4] = {id_DATAA, id_DATAB, id_DATAC, id_DATAD};

struct Cyclone2Impl : HimbaechelAPI
{
    ~Cyclone2Impl() {};

    po::options_description getUArchOptions() override
    {
        po::options_description specific("Cyclone II options");
        specific.add_options()("qsf", po::value<std::string>(),
                               "Quartus-style settings file with `set_location_assignment PIN_x -to port` lines");
        specific.add_options()("lab-inputs", po::value<int>(),
                               "most signals from outside a LAB its LEs may use (default 34)");
        return specific;
    }

    void init_database(Arch *arch) override
    {
        init_uarch_constids(arch);
        arch->load_chipdb("cyclone2/chipdb-EP2C35F672C6.bin");
        arch->set_speed_grade("C6");
    }

    void init(Context *ctx) override
    {
        h.init(ctx);
        HimbaechelAPI::init(ctx);
        init_perm();
        if (ctx->args.options.count("lab-inputs"))
            max_lab_inputs = ctx->args.options.at("lab-inputs").as<int>();
    }

    std::string getDefaultRouter() const override { return "router2"; }

    // --- packing ---

    void pack() override
    {
        pack_io();
        pack_constants();
        pack_lut1();
        pack_lut_ff();
        pack_carry();
        pack_clocks();
    }

    dict<std::string, std::string> read_pin_assignments()
    {
        dict<std::string, std::string> port2pin;
        if (!ctx->args.options.count("qsf"))
            return port2pin;
        std::string path = ctx->args.options.at("qsf").as<std::string>();
        std::ifstream f(path);
        if (!f)
            log_error("cannot open %s\n", path.c_str());
        std::regex re(R"re(^\s*set_location_assignment\s+PIN_(\w+)\s+-to\s+"?([^"\s]+)"?)re");
        std::string line;
        while (std::getline(f, line)) {
            std::smatch m;
            if (std::regex_search(line, m, re))
                port2pin[m[2]] = m[1];
        }
        return port2pin;
    }

    void pack_io()
    {
        // Remember which top-level port each pin buffer serves, then drop
        // nextpnr's own port cells (yosys already inserted ours).
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type == ctx->id("$nextpnr_ibuf")) {
                NetInfo *o = ci->getPort(ctx->id("O"));
                if (o)
                    for (auto &usr : o->users)
                        if (usr.cell->type == id_CY2_IBUF)
                            usr.cell->setAttr(id_PORT, ci->name.str(ctx));
            } else if (ci->type == ctx->id("$nextpnr_obuf")) {
                NetInfo *i = ci->getPort(ctx->id("I"));
                if (i && i->driver.cell && i->driver.cell->type == id_CY2_OBUF)
                    i->driver.cell->setAttr(id_PORT, ci->name.str(ctx));
            } else if (ci->type == ctx->id("$nextpnr_iobuf")) {
                log_error("bidirectional port %s is not supported yet\n", ctx->nameOf(ci));
            }
        }
        h.remove_nextpnr_iobs({CellTypePort(id_CY2_IBUF, id_PAD), CellTypePort(id_CY2_OBUF, id_PAD)});

        dict<std::string, BelId> pin2bel;
        for (auto &pad : ctx->chip_info->packages[0].pads) {
            IdStringList name = IdStringList::concat(IdString(pad.tile), IdString(pad.bel));
            pin2bel[IdString(pad.package_pin).str(ctx)] = ctx->getBelByName(name);
        }
        auto port2pin = read_pin_assignments();
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (!ci->type.in(id_CY2_IBUF, id_CY2_OBUF))
                continue;
            if (!ci->attrs.count(id_PORT))
                log_error("pin buffer %s is not connected to a top-level port\n", ctx->nameOf(ci));
            std::string port = ci->attrs.at(id_PORT).as_string();
            if (!port2pin.count(port))
                log_error("no location assignment for port %s\n", port.c_str());
            // The pad side has no wire in the database; the bel is the pad.
            ci->disconnectPort(id_PAD);
            ci->ports.erase(id_PAD);
            std::string pin = port2pin.at(port);
            if (!pin2bel.count(pin))
                log_error("port %s: PIN_%s is not a user I/O pin\n", port.c_str(), pin.c_str());
            BelId bel = pin2bel.at(pin);
            if (!ctx->checkBelAvail(bel))
                log_error("port %s: PIN_%s is already used\n", port.c_str(), pin.c_str());
            ctx->bindBel(bel, ci, STRENGTH_LOCKED);
            log_info("    %s -> PIN_%s\n", port.c_str(), pin.c_str());
        }
    }

    // Constants have no driver in the fabric: LUT inputs are folded into
    // the mask, output pins get a constant output setting. Registers that
    // can never leave their power-up 0 (data, enable or clock stuck, clear
    // stuck on) are constants too, as quartus_map treats them ("stuck at
    // GND"), and so are LUTs left with no inputs; both propagate.
    void pack_constants()
    {
        std::vector<std::pair<NetInfo *, bool>> work;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (ni->driver.cell && ni->driver.cell->type.in(ctx->id("GND"), ctx->id("VCC")))
                work.emplace_back(ni, ni->driver.cell->type == ctx->id("VCC"));
        }
        pool<IdString> dead_cells, dead_nets;
        std::vector<CellInfo *> const_one_ff;
        int stuck_ffs = 0, const_luts = 0;
        auto make_const = [&](CellInfo *c, IdString port, bool v) {
            NetInfo *n = c->getPort(port);
            if (n)
                work.emplace_back(n, v);
        };
        while (!work.empty()) {
            auto [ni, value] = work.back();
            work.pop_back();
            if (dead_nets.count(ni->name))
                continue;
            std::vector<PortRef> users;
            for (auto &u : ni->users)
                users.push_back(u);
            for (auto &usr : users) {
                CellInfo *ci = usr.cell;
                if (dead_cells.count(ci->name))
                    continue;
                IdString p = usr.port;
                if (ci->type == id_LCELL_COMB && (p.in(id_DATAA, id_DATAB, id_DATAC, id_DATAD) || p == id_CIN)) {
                    // On DATAC, DATAD and CIN a 1 is only disconnected: it is
                    // what an unconnected input reads, and yosys ties every
                    // unused input to 1. Folding it would rewrite the carry
                    // half of the mask, which COUT reads with DATAD = 0, or
                    // for DATAC the LUT input C it shares with CIN. Anything
                    // else is folded, which is also what Quartus does with the
                    // mask. CIN only acts as LUT input C (and the carry LUT's
                    // C); folding it makes both independent of the hardware cin.
                    if (!value || p.in(id_DATAA, id_DATAB)) {
                        int bit = p == id_CIN ? 2 : lut_input_index(p);
                        uint64_t mask = ci->params.at(id_LUT_MASK).as_int64();
                        ci->setParam(id_LUT_MASK, Property(substitute(mask, bit, -1, value), 16));
                    }
                    ci->disconnectPort(p);
                    bool any = ci->getPort(id_CIN);
                    for (auto q : lut_inputs)
                        any |= ci->getPort(q) != nullptr;
                    if (!any) {
                        // The LUT is a constant: what it reads with every
                        // remaining (unconnected) input at 1, and DATAD = 0
                        // for COUT.
                        uint64_t mask = ci->params.at(id_LUT_MASK).as_int64();
                        make_const(ci, id_COMBOUT, (mask >> 15) & 1);
                        make_const(ci, id_COUT, (mask >> 7) & 1);
                        dead_cells.insert(ci->name);
                        const_luts++;
                    }
                } else if (ci->type == id_CY2_OBUF && p == id_DATAOUT) {
                    ci->setParam(id_OUT_CONST, Property(value ? 1 : 0, 1));
                    ci->disconnectPort(p);
                } else if (ci->type == id_LCELL_FF && ((p == id_ENA && value) || (p.in(id_ACLR, id_SCLR) && !value))) {
                    ci->disconnectPort(p); // the idle level
                } else if (ci->type == id_LCELL_FF && usr.port == id_DATAIN && value) {
                    ci->disconnectPort(p);
                    const_one_ff.push_back(ci);
                } else if (ci->type == id_LCELL_FF) {
                    // Data 0, never enabled, never clocked or always
                    // cleared: it stays at its power-up 0.
                    make_const(ci, id_REGOUT, false);
                    dead_cells.insert(ci->name);
                    stuck_ffs++;
                } else {
                    log_error("constant %d on %s.%s is not supported\n", value, ctx->nameOf(ci), ctx->nameOf(p));
                }
            }
            dead_nets.insert(ni->name);
            if (ni->driver.cell && ni->driver.cell->type.in(ctx->id("GND"), ctx->id("VCC")))
                dead_cells.insert(ni->driver.cell->name);
        }
        for (IdString c : dead_cells) {
            CellInfo *ci = ctx->cells.at(c).get();
            std::vector<IdString> ports;
            for (auto &port : ci->ports)
                ports.push_back(port.first);
            for (IdString port : ports)
                ci->disconnectPort(port);
            ctx->cells.erase(c);
        }
        for (IdString n : dead_nets)
            if (ctx->nets.count(n) && ctx->nets.at(n)->users.empty())
                ctx->nets.erase(n);
        for (CellInfo *ff : const_one_ff) {
            if (dead_cells.count(ff->name))
                continue;
            CellInfo *lut = ctx->createCell(ctx->idf("%s$const", ctx->nameOf(ff)), id_LCELL_COMB);
            add_lut_ports(lut);
            lut->setParam(id_LUT_MASK, Property(0xFFFF, 16));
            NetInfo *n = ctx->createNet(ctx->idf("%s$const_net", ctx->nameOf(ff)));
            lut->connectPort(id_COMBOUT, n);
            ff->connectPort(id_DATAIN, n);
        }
        log_info("Constants: %d stuck registers and %d input-less LUTs removed.\n", stuck_ffs, const_luts);
    }

    // quartus_map drops every one-input LUT (buffer or inverter) of an atom
    // netlist as a "redundant logic cell" and folds the inversion into
    // whatever it feeds: other LUTs' masks, an output pin's inverter
    // (IO_DATA_INVERT), a LAB's clock-enable / sync-clear inversion. Doing
    // the same keeps the open flow's netlist one Quartus would assemble
    // unchanged. What feeds a register's data input stays: it is the
    // register's own LUT (a feeder, which Quartus re-creates itself).
    static int lut_input_index(IdString port)
    {
        for (int i = 0; i < 4; i++)
            if (port == lut_inputs[i])
                return i;
        return -1;
    }

    // Mask of `lut` with input i replaced by (input j, or a constant when
    // j < 0) XOR inv.
    static uint64_t substitute(uint64_t mask, int i, int j, bool inv)
    {
        uint64_t out = 0;
        for (int idx = 0; idx < 16; idx++) {
            int v = (j >= 0 ? (idx >> j) & 1 : 0) ^ int(inv);
            int src = (idx & ~(1 << i)) | (v << i);
            out |= ((mask >> src) & 1) << idx;
        }
        return out;
    }

    static bool feeds_register_data(const CellInfo *lut)
    {
        const NetInfo *o = lut->getPort(id_COMBOUT);
        if (o)
            for (auto &u : o->users)
                if (u.cell->type == id_LCELL_FF && u.port == id_DATAIN)
                    return true;
        return false;
    }

    // An inverter in front of a register can't fold into it, and Quartus
    // would re-create it somewhere of its own choosing. Instead invert the
    // LUT driving the inverter's input and undo that on every other load,
    // when they all can take it; the inverter is then a plain buffer.
    bool push_inversion_back(CellInfo *inverter, NetInfo *in)
    {
        CellInfo *d = in->driver.cell;
        if (!d || d->type != id_LCELL_COMB || in->driver.port != id_COMBOUT || d->getPort(id_CIN) ||
            (d->getPort(id_COUT) && !d->getPort(id_COUT)->users.empty()))
            return false;
        for (auto &u : in->users) {
            if (u.cell == inverter)
                continue;
            bool ok = (u.cell->type == id_LCELL_COMB && lut_input_index(u.port) >= 0) ||
                      (u.cell->type == id_CY2_OBUF && u.port == id_DATAOUT) ||
                      (u.cell->type == id_LCELL_FF && u.port.in(id_ENA, id_SCLR));
            if (!ok)
                return false;
        }
        auto toggle = [](CellInfo *c, IdString flag) {
            bool v = c->params.count(flag) && c->params.at(flag).as_int64() != 0;
            c->setParam(flag, Property(int(!v), 1));
        };
        d->setParam(id_LUT_MASK, Property(~d->params.at(id_LUT_MASK).as_int64() & 0xFFFF, 16));
        for (auto &u : in->users) {
            if (u.cell == inverter)
                continue;
            if (u.cell->type == id_LCELL_COMB) {
                int i = lut_input_index(u.port);
                u.cell->setParam(id_LUT_MASK, Property(substitute(u.cell->params.at(id_LUT_MASK).as_int64(), i, i, true), 16));
            } else if (u.cell->type == id_CY2_OBUF) {
                toggle(u.cell, id_DATA_INVERT);
            } else {
                toggle(u.cell, u.port == id_ENA ? id_ENA_INV : id_SCLR_INV);
            }
        }
        return true;
    }

    void pack_lut1()
    {
        std::vector<IdString> dead;
        int folded = 0;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_LCELL_COMB)
                continue;
            uint64_t mask = ci->params.at(id_LUT_MASK).as_int64();
            int only = -1, n_in = 0;
            for (int i = 0; i < 4; i++)
                if (ci->getPort(lut_inputs[i])) {
                    only = i;
                    n_in++;
                }
            if (n_in != 1 || ci->getPort(id_CIN) || (ci->getPort(id_COUT) && !ci->getPort(id_COUT)->users.empty()))
                continue;
            const uint64_t bufs[4] = {0xAAAA, 0xCCCC, 0xF0F0, 0xFF00};
            if (mask != bufs[only] && mask != (~bufs[only] & 0xFFFF))
                continue;
            bool inv = mask != bufs[only];
            NetInfo *in = ci->getPort(lut_inputs[only]);
            if (inv && feeds_register_data(ci)) {
                if (push_inversion_back(ci, in)) {
                    inv = false;
                } else if (CellInfo *d = in->driver.cell; d && d->type == id_LCELL_COMB && in->driver.port == id_COMBOUT &&
                                                           !d->getPort(id_CIN) &&
                                                           !(d->getPort(id_COUT) && !d->getPort(id_COUT)->users.empty())) {
                    // Otherwise make the inverter an inverted copy of its
                    // driver: a real LUT Quartus keeps where it is.
                    ci->disconnectPort(lut_inputs[only]);
                    for (auto p : lut_inputs)
                        if (NetInfo *n = d->getPort(p)) {
                            if (!ci->ports.count(p))
                                ci->addInput(p);
                            ci->connectPort(p, n);
                        }
                    ci->setParam(id_LUT_MASK, Property(~d->params.at(id_LUT_MASK).as_int64() & 0xFFFF, 16));
                    continue;
                }
            }
            NetInfo *o = ci->getPort(id_COMBOUT);
            if (!o)
                continue;
            std::vector<PortRef> users;
            for (auto &u : o->users)
                users.push_back(u);
            for (auto &u : users) {
                CellInfo *c = u.cell;
                int i = c->type == id_LCELL_COMB ? lut_input_index(u.port) : -1;
                if (i >= 0) {
                    int j = -1;
                    for (int k = 0; k < 4; k++)
                        if (k != i && c->getPort(lut_inputs[k]) == in)
                            j = k;
                    uint64_t m = c->params.at(id_LUT_MASK).as_int64();
                    c->disconnectPort(u.port);
                    if (j >= 0) {
                        c->setParam(id_LUT_MASK, Property(substitute(m, i, j, inv), 16));
                    } else {
                        if (inv)
                            c->setParam(id_LUT_MASK, Property(substitute(m, i, i, true), 16));
                        c->connectPort(u.port, in);
                    }
                } else if (c->type == id_CY2_OBUF && u.port == id_DATAOUT) {
                    c->disconnectPort(u.port);
                    c->connectPort(u.port, in);
                    c->setParam(id_DATA_INVERT, Property(int(inv), 1));
                } else if (c->type == id_LCELL_FF && u.port == id_DATAIN && !inv) {
                    // A plain buffer in front of a register: the register
                    // takes the source on its sync load path instead.
                    c->disconnectPort(u.port);
                    c->connectPort(u.port, in);
                } else if (c->type == id_LCELL_FF && u.port.in(id_ENA, id_SCLR)) {
                    IdString flag = u.port == id_ENA ? id_ENA_INV : id_SCLR_INV;
                    c->disconnectPort(u.port);
                    c->connectPort(u.port, in);
                    c->setParam(flag, Property(int(inv), 1));
                } else {
                    continue;
                }
                folded++;
            }
            if (o->users.empty()) {
                ci->disconnectPort(lut_inputs[only]);
                ci->disconnectPort(id_COMBOUT);
                dead.push_back(ci->name);
            }
        }
        for (IdString n : dead)
            ctx->cells.erase(n);
        log_info("Folded %d one-input LUT loads, removed %d LUTs.\n", folded, int(dead.size()));
    }

    void add_lut_ports(CellInfo *lut)
    {
        for (auto p : lut_inputs)
            lut->addInput(p);
        lut->addInput(id_CIN);
        lut->addOutput(id_COMBOUT);
        lut->addOutput(id_COUT);
    }

    dict<IdString, CellInfo *> ff_of_lut;

    // A register driven by a LUT sits beside it (DATAIN is that LUT's
    // output). Any other register takes its data through the synchronous
    // load path instead: SDATA (the LE's input C) with the LAB's sload
    // tied high, the fitter's own register packing. That leaves the LUT
    // free and needs no feeder LUT (which quartus_map would delete).
    void pack_lut_ff()
    {
        std::vector<CellInfo *> ffs;
        for (auto &cell : ctx->cells)
            if (cell.second->type == id_LCELL_FF)
                ffs.push_back(cell.second.get());
        int sdata = 0;
        for (CellInfo *ff : ffs) {
            NetInfo *d = ff->getPort(id_DATAIN);
            if (!d)
                log_error("register %s has no data input\n", ctx->nameOf(ff));
            CellInfo *drv = d->driver.cell;
            if (!(drv && drv->type == id_LCELL_COMB && d->driver.port == id_COMBOUT && !ff_of_lut.count(drv->name))) {
                ff->disconnectPort(id_DATAIN);
                if (!ff->ports.count(id_SDATA))
                    ff->addInput(id_SDATA);
                ff->connectPort(id_SDATA, d);
                ff->setParam(id_SLOAD, Property(1, 1));
                sdata++;
                continue;
            }
            ff_of_lut[drv->name] = ff;
            drv->cluster = drv->name;
            ff->cluster = drv->name;
            ff->constr_x = 0;
            ff->constr_y = 0;
            ff->constr_z = 1;
            ff->constr_abs_z = false;
            drv->constr_children.push_back(ff);
        }
        log_info("Packed %d registers with their LUTs, %d on the sync load path.\n", int(ffs.size()) - sdata, sdata);
    }

    // Carry chains: LE k -> k+1 in a LAB, LE 15 -> LE 0 of the LAB below.
    // A chain starts at LE 0.
    void pack_carry()
    {
        int chains = 0;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_LCELL_COMB)
                continue;
            NetInfo *cin = ci->getPort(id_CIN);
            if (cin && cin->driver.cell)
                continue; // not a chain head
            NetInfo *cout = ci->getPort(id_COUT);
            if (!cout || cout->users.empty())
                continue;
            std::vector<CellInfo *> chain{ci};
            while (true) {
                NetInfo *co = chain.back()->getPort(id_COUT);
                if (!co || co->users.empty())
                    break;
                if (co->users.entries() != 1 || (*co->users.begin()).port != id_CIN)
                    log_error("carry out of %s must feed exactly one carry in\n", ctx->nameOf(chain.back()));
                chain.push_back((*co->users.begin()).cell);
            }
            for (CellInfo *c : chain)
                if (c->getPort(id_DATAC) && c->getPort(id_CIN))
                    log_error("%s uses both DATAC and CIN\n", ctx->nameOf(c));
            // Re-root: the head carries the whole chain and every paired
            // register.
            ci->cluster = ci->name;
            ci->constr_abs_z = true;
            ci->constr_z = 0;
            std::vector<CellInfo *> children;
            for (size_t i = 0; i < chain.size(); i++) {
                CellInfo *c = chain[i];
                int dy = -int(i / 16), z = 2 * int(i % 16);
                if (i > 0) {
                    c->cluster = ci->name;
                    c->constr_x = 0;
                    c->constr_y = dy;
                    c->constr_z = z;
                    c->constr_abs_z = true;
                    children.push_back(c);
                }
                for (CellInfo *ff : c->constr_children) {
                    ff->cluster = ci->name;
                    ff->constr_x = 0;
                    ff->constr_y = dy;
                    ff->constr_z = z + 1;
                    ff->constr_abs_z = true;
                    children.push_back(ff);
                }
                if (i > 0)
                    c->constr_children.clear();
            }
            ci->constr_children = children;
            normalise_carry_polarity(chain);
            chains++;
        }
        log_info("Packed %d carry chains.\n", chains);
    }

    // Quartus re-expresses every carry chain with the carry inverted after
    // each odd chain position (observed: counters from quartus_map, and our
    // own chains fed back to it as an atom netlist come out rewritten so),
    // in the last LE copies the sum into the unused carry half, and clears
    // a sum half nothing uses. Both
    // forms compute the same thing; using Quartus' makes the bitstream
    // byte-comparable with what it assembles from the same placement.
    void normalise_carry_polarity(const std::vector<CellInfo *> &chain)
    {
        for (size_t i = 0; i < chain.size(); i++) {
            CellInfo *c = chain[i];
            uint64_t mask = c->params.at(id_LUT_MASK).as_int64();
            bool in_inv = i >= 2 && i % 2 == 0, out_inv = i % 2 == 1;
            uint64_t out = 0;
            for (int idx = 0; idx < 16; idx++) {
                uint64_t v = (mask >> (in_inv ? idx ^ 4 : idx)) & 1;
                if (idx < 8 && out_inv)
                    v ^= 1;
                out |= v << idx;
            }
            NetInfo *co = c->getPort(id_COUT);
            if (i + 1 == chain.size() && (!co || co->users.empty()))
                out = (out & 0xFF00) | (out >> 8);
            // An unused sum half is all zeros (comparators: only the carry
            // out of the top is used).
            NetInfo *so = c->getPort(id_COMBOUT);
            if (!so || so->users.empty())
                out &= 0x00FF;
            c->setParam(id_LUT_MASK, Property(out, 16));
        }
    }

    void pack_clocks()
    {
        // INCLK wire of every clock control -> its bel.
        dict<WireId, BelId> inclk;
        for (BelId bel : ctx->getBels())
            if (ctx->getBelType(bel) == id_CLKCTRL)
                inclk[ctx->getBelPinWire(bel, id_INCLK)] = bel;

        std::vector<NetInfo *> clocks;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            for (auto &usr : ni->users)
                if (usr.cell->type == id_LCELL_FF && usr.port == id_CLK) {
                    clocks.push_back(ni);
                    break;
                }
        }
        for (NetInfo *ni : clocks) {
            CellInfo *drv = ni->driver.cell;
            if (!drv || drv->type != id_CY2_IBUF)
                log_error("clock %s must come straight from a pin\n", ctx->nameOf(ni));
            // Nearest free clock control the pin reaches through the graph.
            WireId start = ctx->getBelPinWire(drv->bel, id_COMBOUT);
            std::queue<std::pair<WireId, int>> q;
            pool<WireId> seen{start};
            q.emplace(start, 0);
            BelId found;
            while (!q.empty() && found == BelId()) {
                auto [w, depth] = q.front();
                q.pop();
                if (inclk.count(w) && ctx->checkBelAvail(inclk.at(w))) {
                    found = inclk.at(w);
                    break;
                }
                if (depth > 8)
                    continue;
                for (PipId p : ctx->getPipsDownhill(w)) {
                    WireId d = ctx->getPipDstWire(p);
                    if (seen.insert(d).second)
                        q.emplace(d, depth + 1);
                }
            }
            if (found == BelId())
                log_error("no free global clock control reachable from %s\n", ctx->nameOf(drv));
            CellInfo *cc = ctx->createCell(ctx->idf("%s$clkctrl", ctx->nameOf(ni)), id_CLKCTRL);
            cc->addInput(id_INCLK);
            cc->addOutput(id_OUTCLK);
            NetInfo *g = ctx->createNet(ctx->idf("%s$global", ctx->nameOf(ni)));
            std::vector<PortRef> users;
            for (auto &u : ni->users)
                users.push_back(u);
            for (auto &usr : users)
                if (usr.cell->type == id_LCELL_FF && usr.port == id_CLK) {
                    usr.cell->disconnectPort(id_CLK);
                    usr.cell->connectPort(id_CLK, g);
                }
            cc->connectPort(id_INCLK, ni);
            cc->connectPort(id_OUTCLK, g);
            ctx->bindBel(found, cc, STRENGTH_LOCKED);
            log_info("    clock %s -> %s\n", ctx->nameOf(ni), ctx->nameOfBel(found));
        }
    }

    // --- LUT input permutation ---
    //
    // In LAB tiles every physical LE input LE<k>.<P> has a pip to every
    // logical LUT input LE<k>.I<Q>. Carry LEs can't permute (C is the carry
    // in, D picks the sum or carry half), so only P == Q is open for them.
    int lab_type = -1;
    std::vector<int> perm_le; // pip index in the LAB tile type -> LE, or -1

    void init_perm()
    {
        for (BelId bel : ctx->getBels())
            if (ctx->getBelType(bel) == id_LCELL_COMB) {
                lab_type = ctx->chip_info->tile_insts[bel.tile].type;
                break;
            }
        const auto &tt = ctx->chip_info->tile_types[lab_type];
        perm_le.assign(tt.pips.ssize(), -1);
        for (int i = 0; i < tt.pips.ssize(); i++) {
            std::string dst = IdString(tt.wires[tt.pips[i].dst_wire].name).str(ctx);
            std::string src = IdString(tt.wires[tt.pips[i].src_wire].name).str(ctx);
            int k;
            char q, p;
            if (sscanf(dst.c_str(), "LE%d.I%c", &k, &q) == 2 && sscanf(src.c_str(), "LE%*d.%c", &p) == 1 && p != q &&
                src.size() == dst.size() - 1)
                perm_le[i] = k;
        }
    }

    bool checkPipAvail(PipId pip) const override
    {
        if (ctx->chip_info->tile_insts[pip.tile].type != lab_type || perm_le.at(pip.index) < 0)
            return true;
        Loc l = ctx->getPipLocation(pip);
        const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(l.x, l.y, 2 * perm_le[pip.index])));
        // Chain heads too: their D still splits sum from carry.
        return !(lut && (lut->getPort(id_CIN) || (lut->getPort(id_COUT) && !lut->getPort(id_COUT)->users.empty())));
    }

    // --- placement validity ---

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override
    {
        IdString t = ctx->getBelType(bel);
        if (!t.in(id_LCELL_COMB, id_LCELL_FF))
            return true;
        Loc l = ctx->getBelLocation(bel);
        // A register's data input is its LUT's output wire.
        int k = l.z / 2;
        const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(l.x, l.y, 2 * k + 1)));
        if (ff) {
            const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(l.x, l.y, 2 * k)));
            const NetInfo *sd = ff->getPort(id_SDATA);
            if (ff->getPort(id_DATAIN) && (!lut || lut->getPort(id_COMBOUT) != ff->getPort(id_DATAIN)))
                return false;
            // SDATA takes physical input C: the LUT has three left.
            if (sd && lut) {
                int others = 0;
                for (auto p : lut_inputs)
                    others += lut->getPort(p) && lut->getPort(p) != sd;
                if (others > 3)
                    return false;
            }
        }
        // LAB-wide register controls: two clocks, each with its own enable
        // (at most one enabled clock: the enable's control line decides
        // which LAB clock it pairs with, and two could collide), one
        // asynchronous and one synchronous clear.
        // A control is its net plus whether the LAB inverts it.
        using Ctl = std::pair<const NetInfo *, bool>;
        std::vector<std::pair<const NetInfo *, Ctl>> domains;
        Ctl ena{nullptr, false}, aclr{nullptr, false}, sclr{nullptr, false};
        auto one = [](Ctl &slot, Ctl n) {
            if (!n.first || slot == n)
                return true;
            if (slot.first)
                return false;
            slot = n;
            return true;
        };
        auto flag = [](const CellInfo *c, IdString p) {
            return c->params.count(p) && c->params.at(p).as_int64() != 0;
        };
        // A LAB's sload is tied high for SDATA registers, and the per-LE
        // sync enable takes the LAB's clear and load together: registers
        // using either must all use the same combination of the two.
        int sync_kind = 0; // bit 0: SDATA, bit 1: SCLR
        // Signals from outside the LAB arrive on its local lines (38, of
        // which each LE input reaches about half); LE outputs used in the
        // same LAB come back on their own LOCAL_LINE. Bounding the outside
        // inputs keeps a full LAB routable.
        if (!lab_inputs_fit(l.x, l.y))
            return false;
        for (int i = 0; i < 16; i++) {
            const CellInfo *c = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(l.x, l.y, 2 * i + 1)));
            if (!c)
                continue;
            int kind = (c->getPort(id_SDATA) ? 1 : 0) | (c->getPort(id_SCLR) ? 2 : 0);
            if (kind && sync_kind && kind != sync_kind)
                return false;
            sync_kind |= kind;
            Ctl e{c->getPort(id_ENA), flag(c, id_ENA_INV)};
            std::pair<const NetInfo *, Ctl> d{c->getPort(id_CLK), e};
            if (std::find(domains.begin(), domains.end(), d) == domains.end())
                domains.push_back(d);
            if (domains.size() > 2 || !one(ena, e) || !one(aclr, Ctl{c->getPort(id_ACLR), false}) ||
                !one(sclr, Ctl{c->getPort(id_SCLR), flag(c, id_SCLR_INV)})) {
                if (explain_invalid)
                    log_nonfatal_error("LAB X%dY%d: register controls don't fit\n", l.x, l.y);
                return false;
            }
        }
        return true;
    }

    // Measured on picorv32: 30 leaves 32-bit adders (two operands per LE)
    // unplaceable, 34 routes in ~80 router2 iterations, 38 in ~280, no
    // limit doesn't converge in 15 minutes.
    int max_lab_inputs = 34;

    bool lab_inputs_fit(int x, int y) const
    {
        std::vector<const NetInfo *> ext;
        auto add = [&](const NetInfo *n) {
            if (!n)
                return;
            const CellInfo *d = n->driver.cell;
            if (d && d->bel != BelId() && d->type.in(id_LCELL_COMB, id_LCELL_FF)) {
                Loc dl = ctx->getBelLocation(d->bel);
                if (dl.x == x && dl.y == y)
                    return;
            }
            if (std::find(ext.begin(), ext.end(), n) == ext.end())
                ext.push_back(n);
        };
        for (int z = 0; z < 32; z++) {
            const CellInfo *c = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z)));
            if (!c)
                continue;
            if (c->type == id_LCELL_COMB)
                for (auto p : lut_inputs)
                    add(c->getPort(p));
            else
                for (auto p : {id_SDATA, id_ENA, id_SCLR, id_ACLR})
                    add(c->getPort(p));
        }
        return int(ext.size()) <= max_lab_inputs;
    }

    IdString getBelBucketForCellType(IdString cell_type) const override
    {
        if (cell_type.in(id_CY2_IBUF, id_CY2_OBUF))
            return id_IOE;
        return cell_type;
    }

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        IdString bel_type = ctx->getBelType(bel);
        if (bel_type == id_IOE)
            return cell_type.in(id_CY2_IBUF, id_CY2_OBUF);
        return bel_type == cell_type;
    }

  private:
    HimbaechelHelpers h;
};

struct Cyclone2Arch : HimbaechelArch
{
    Cyclone2Arch() : HimbaechelArch("cyclone2") {};
    bool match_device(const std::string &device) override { return device == "EP2C35F672C6"; }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device) override
    {
        return std::make_unique<Cyclone2Impl>();
    }
} cyclone2Arch;

} // namespace

NEXTPNR_NAMESPACE_END

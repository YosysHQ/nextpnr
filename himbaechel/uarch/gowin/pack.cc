#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"

#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_helpers.h"

#include "gowin.h"
#include "gowin_utils.h"
#include "pack.h"

#include <cinttypes>

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct GowinPacker
{
    Context *ctx;
    HimbaechelHelpers h;
    GowinUtils gwu;

    GowinPacker(Context *ctx) : ctx(ctx)
    {
        h.init(ctx);
        gwu.init(ctx);
    }

    // ===================================
    // IO
    // ===================================
    // create IOB connections for gowin_pack
    // can be called repeatedly when switching inputs, disabled outputs do not change
    void make_iob_nets(CellInfo &iob)
    {
        for (const auto &port : iob.ports) {
            const NetInfo *net = iob.getPort(port.first);
            std::string connected_net = "NET";
            if (net != nullptr) {
                if (ctx->verbose) {
                    log_info("%s: %s - %s\n", ctx->nameOf(&iob), port.first.c_str(ctx), ctx->nameOf(net));
                }
                if (net->name == ctx->id("$PACKER_VCC")) {
                    connected_net = "VCC";
                } else if (net->name == ctx->id("$PACKER_GND")) {
                    connected_net = "GND";
                }
                iob.setParam(ctx->idf("NET_%s", port.first.c_str(ctx)), connected_net);
            }
        }
    }

    void config_simple_io(CellInfo &ci)
    {
        if (ci.type.in(id_TBUF, id_IOBUF)) {
            return;
        }
        log_info("simple:%s\n", ctx->nameOf(&ci));
        ci.addInput(id_OEN);
        if (ci.type == id_OBUF) {
            ci.connectPort(id_OEN, ctx->nets.at(ctx->id("$PACKER_GND")).get());
        } else {
            NPNR_ASSERT(ci.type == id_IBUF);
            ci.connectPort(id_OEN, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
        }
    }

    void config_bottom_row(CellInfo &ci, Loc loc, uint8_t cnd = Bottom_io_POD::NORMAL)
    {
        if (!gwu.has_bottom_io_cnds()) {
            return;
        }
        if (!ci.type.in(id_OBUF, id_TBUF, id_IOBUF)) {
            return;
        }
        if (loc.z != BelZ::IOBA_Z) {
            return;
        }
        auto connect_io_wire = [&](IdString port, IdString net_name) {
            // XXX it is very convenient that nothing terrible happens in case
            // of absence/presence of a port
            ci.disconnectPort(port);
            ci.addInput(port);
            if (net_name == id_VSS) {
                ci.connectPort(port, ctx->nets.at(ctx->id("$PACKER_GND")).get());
            } else {
                NPNR_ASSERT(net_name == id_VCC);
                ci.connectPort(port, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
            }
        };

        IdString wire_a_net = gwu.get_bottom_io_wire_a_net(cnd);
        connect_io_wire(id_BOTTOM_IO_PORT_A, wire_a_net);

        IdString wire_b_net = gwu.get_bottom_io_wire_b_net(cnd);
        connect_io_wire(id_BOTTOM_IO_PORT_B, wire_b_net);
    }

    // Attributes of deleted cells are copied
    void trim_nextpnr_iobs(void)
    {
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
                CellTypePort(id_IBUF, id_I),
                CellTypePort(id_OBUF, id_O),
                CellTypePort(id_TBUF, id_O),
                CellTypePort(id_IOBUF, id_IO),
        };
        std::vector<IdString> to_remove;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (!ci.type.in(ctx->id("$nextpnr_ibuf"), ctx->id("$nextpnr_obuf"), ctx->id("$nextpnr_iobuf")))
                continue;
            NetInfo *i = ci.getPort(id_I);
            if (i && i->driver.cell) {
                if (!top_ports.count(CellTypePort(i->driver)))
                    log_error("Top-level port '%s' driven by illegal port %s.%s\n", ctx->nameOf(&ci),
                              ctx->nameOf(i->driver.cell), ctx->nameOf(i->driver.port));
                for (const auto &attr : ci.attrs) {
                    i->driver.cell->setAttr(attr.first, attr.second);
                }
            }
            NetInfo *o = ci.getPort(id_O);
            if (o) {
                for (auto &usr : o->users) {
                    if (!top_ports.count(CellTypePort(usr)))
                        log_error("Top-level port '%s' driving illegal port %s.%s\n", ctx->nameOf(&ci),
                                  ctx->nameOf(usr.cell), ctx->nameOf(usr.port));
                    for (const auto &attr : ci.attrs) {
                        usr.cell->setAttr(attr.first, attr.second);
                    }
                    // network/port attributes that can be set in the
                    // restriction file and that need to be transferred to real
                    // networks before nextpnr buffers are removed.
                    NetInfo *dst_net = usr.cell->getPort(id_O);
                    if (dst_net != nullptr) {
                        for (const auto &attr : o->attrs) {
                            if (!attr.first.in(id_CLOCK)) {
                                continue;
                            }
                            dst_net->attrs[attr.first] = attr.second;
                        }
                    }
                }
            }
            NetInfo *io = ci.getPort(id_IO);
            if (io && io->driver.cell) {
                if (!top_ports.count(CellTypePort(io->driver)))
                    log_error("Top-level port '%s' driven by illegal port %s.%s\n", ctx->nameOf(&ci),
                              ctx->nameOf(io->driver.cell), ctx->nameOf(io->driver.port));
                for (const auto &attr : ci.attrs) {
                    io->driver.cell->setAttr(attr.first, attr.second);
                }
            }
            ci.disconnectPort(id_I);
            ci.disconnectPort(id_O);
            ci.disconnectPort(id_IO);
            to_remove.push_back(ci.name);
        }
        for (IdString cell_name : to_remove)
            ctx->cells.erase(cell_name);
    }

    BelId bind_io(CellInfo &ci)
    {
        BelId bel = ctx->getBelByNameStr(ci.attrs.at(id_BEL).as_string());
        if (bel == BelId()) {
            log_error("No bel named %s\n", ci.attrs.at(id_BEL).as_string().c_str());
        }
        if (!ctx->checkBelAvail(bel)) {
            log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci), ctx->nameOfBel(bel),
                      ctx->nameOf(ctx->getBoundBelCell(bel)));
        }
        ci.unsetAttr(id_BEL);
        ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_LOCKED);
        return bel;
    }

    void pack_iobs(void)
    {
        log_info("Pack IOBs...\n");
        trim_nextpnr_iobs();

        for (auto &cell : ctx->cells) {
            CellInfo &ci = *cell.second;
            if (!is_io(&ci)) {
                continue;
            }
            if (ci.attrs.count(id_BEL) == 0) {
                log_error("Unconstrained IO:%s\n", ctx->nameOf(&ci));
            }
            BelId io_bel = bind_io(ci);
            Loc io_loc = ctx->getBelLocation(io_bel);
            if (io_loc.y == ctx->getGridDimY() - 1) {
                config_bottom_row(ci, io_loc);
            }
            if (gwu.is_simple_io_bel(io_bel)) {
                config_simple_io(ci);
            }
            make_iob_nets(ci);
        }
    }

    // ===================================
    // Differential IO
    // ===================================
    static bool is_iob(const Context *ctx, CellInfo *cell) { return is_io(cell); }

    std::pair<CellInfo *, CellInfo *> get_pn_cells(const CellInfo &ci)
    {
        CellInfo *p, *n;
        switch (ci.type.hash()) {
        case ID_ELVDS_TBUF: /* fall-through */
        case ID_TLVDS_TBUF: /* fall-through */
        case ID_ELVDS_OBUF: /* fall-through */
        case ID_TLVDS_OBUF:
            p = net_only_drives(ctx, ci.ports.at(id_O).net, is_iob, id_I, true);
            n = net_only_drives(ctx, ci.ports.at(id_OB).net, is_iob, id_I, true);
            break;
        case ID_ELVDS_IBUF: /* fall-through */
        case ID_TLVDS_IBUF:
            p = net_driven_by(ctx, ci.ports.at(id_I).net, is_iob, id_O);
            n = net_driven_by(ctx, ci.ports.at(id_IB).net, is_iob, id_O);
            break;
        case ID_ELVDS_IOBUF: /* fall-through */
        case ID_TLVDS_IOBUF:
            p = net_only_drives(ctx, ci.ports.at(id_IO).net, is_iob, id_I);
            n = net_only_drives(ctx, ci.ports.at(id_IOB).net, is_iob, id_I);
            break;
        default:
            log_error("Bad diff IO '%s' type '%s'\n", ctx->nameOf(&ci), ci.type.c_str(ctx));
        }
        return std::make_pair(p, n);
    }

    void mark_iobs_as_diff(CellInfo &ci, std::pair<CellInfo *, CellInfo *> &pn_cells)
    {
        pn_cells.first->setParam(id_DIFF, std::string("P"));
        pn_cells.first->setParam(id_DIFF_TYPE, ci.type.str(ctx));
        pn_cells.second->setParam(id_DIFF, std::string("N"));
        pn_cells.second->setParam(id_DIFF_TYPE, ci.type.str(ctx));
    }

    void switch_diff_ports(CellInfo &ci, std::pair<CellInfo *, CellInfo *> &pn_cells,
                           std::vector<IdString> &nets_to_remove)
    {
        CellInfo *iob_p = pn_cells.first;
        CellInfo *iob_n = pn_cells.second;

        if (ci.type.in(id_TLVDS_TBUF, id_TLVDS_OBUF, id_ELVDS_TBUF, id_ELVDS_OBUF)) {
            nets_to_remove.push_back(ci.getPort(id_O)->name);
            ci.disconnectPort(id_O);
            nets_to_remove.push_back(ci.getPort(id_OB)->name);
            ci.disconnectPort(id_OB);
            nets_to_remove.push_back(iob_n->getPort(id_I)->name);
            iob_n->disconnectPort(id_I);

            if (ci.type.in(id_TLVDS_TBUF, id_ELVDS_TBUF)) {
                NetInfo *oen_net = iob_n->getPort(id_OEN);
                if (oen_net != nullptr) {
                    nets_to_remove.push_back(oen_net->name);
                }
                iob_n->disconnectPort(id_OEN);
                iob_p->disconnectPort(id_OEN);
                ci.movePortTo(id_OEN, iob_p, id_OEN);
            }
            iob_p->disconnectPort(id_I);
            ci.movePortTo(id_I, iob_p, id_I);
            return;
        }
        if (ci.type.in(id_TLVDS_IBUF, id_ELVDS_IBUF)) {
            nets_to_remove.push_back(ci.getPort(id_I)->name);
            ci.disconnectPort(id_I);
            nets_to_remove.push_back(ci.getPort(id_IB)->name);
            ci.disconnectPort(id_IB);
            iob_n->disconnectPort(id_O);
            iob_p->disconnectPort(id_O);
            ci.movePortTo(id_O, iob_p, id_O);
            return;
        }
        if (ci.type.in(id_TLVDS_IOBUF, id_ELVDS_IOBUF)) {
            nets_to_remove.push_back(ci.getPort(id_IO)->name);
            ci.disconnectPort(id_IO);
            nets_to_remove.push_back(ci.getPort(id_IOB)->name);
            ci.disconnectPort(id_IOB);
            nets_to_remove.push_back(iob_n->getPort(id_I)->name);
            iob_n->disconnectPort(id_I);
            iob_n->disconnectPort(id_OEN);

            iob_p->disconnectPort(id_OEN);
            ci.movePortTo(id_OEN, iob_p, id_OEN);
            iob_p->disconnectPort(id_I);
            ci.movePortTo(id_I, iob_p, id_I);
            iob_p->disconnectPort(id_O);
            ci.movePortTo(id_O, iob_p, id_O);
            return;
        }
    }

    void pack_diff_iobs(void)
    {
        log_info("Pack diff IOBs...\n");
        std::vector<IdString> cells_to_remove, nets_to_remove;

        for (auto &cell : ctx->cells) {
            CellInfo &ci = *cell.second;
            if (!is_diffio(&ci)) {
                continue;
            }
            if (!gwu.is_diff_io_supported(ci.type)) {
                log_error("%s is not supported\n", ci.type.c_str(ctx));
            }
            cells_to_remove.push_back(ci.name);
            auto pn_cells = get_pn_cells(ci);
            NPNR_ASSERT(pn_cells.first != nullptr && pn_cells.second != nullptr);

            mark_iobs_as_diff(ci, pn_cells);
            switch_diff_ports(ci, pn_cells, nets_to_remove);
        }

        for (auto cell : cells_to_remove) {
            ctx->cells.erase(cell);
        }
        for (auto net : nets_to_remove) {
            ctx->nets.erase(net);
        }
    }

    // ===================================
    // IO logic
    // ===================================
    // the functions of these two inputs are yet to be discovered, so we set as observed
    // in the exemplary images
    void set_daaj_nets(CellInfo &ci, BelId bel)
    {
        std::vector<IdString> pins = ctx->getBelPins(bel);
        if (std::find(pins.begin(), pins.end(), id_DAADJ0) != pins.end()) {
            ci.addInput(id_DAADJ0);
            ci.connectPort(id_DAADJ0, ctx->nets.at(ctx->id("$PACKER_GND")).get());
        }
        if (std::find(pins.begin(), pins.end(), id_DAADJ1) != pins.end()) {
            ci.addInput(id_DAADJ1);
            ci.connectPort(id_DAADJ1, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
        }
    }

    BelId get_iologico_bel(CellInfo *iob)
    {
        NPNR_ASSERT(iob->bel != BelId());
        Loc loc = ctx->getBelLocation(iob->bel);
        loc.z = loc.z - BelZ::IOBA_Z + BelZ::IOLOGICA_Z;
        BelId bel = ctx->getBelByLocation(loc);
        if (bel != BelId()) {
            if (ctx->getBelType(bel) == id_IOLOGICO) {
                return bel;
            }
        }
        return BelId();
    }

    BelId get_iologici_bel(CellInfo *iob)
    {
        NPNR_ASSERT(iob->bel != BelId());
        Loc loc = ctx->getBelLocation(iob->bel);
        loc.z = loc.z - BelZ::IOBA_Z + BelZ::IOLOGICA_Z + 2;
        BelId bel = ctx->getBelByLocation(loc);
        if (bel != BelId()) {
            if (ctx->getBelType(bel) == id_IOLOGICI) {
                return bel;
            }
        }
        return BelId();
    }

    void check_iologic_placement(CellInfo &ci, Loc iob_loc, int diff /* 1 - diff */)
    {
        if (ci.type.in(id_ODDR, id_ODDRC, id_IDDR, id_IDDRC, id_OSER4, id_IOLOGICI_EMPTY, id_IOLOGICO_EMPTY) || diff) {
            return;
        }
        BelId l_bel = ctx->getBelByLocation(Loc(iob_loc.x, iob_loc.y, BelZ::IOBA_Z + 1 - (iob_loc.z - BelZ::IOBA_Z)));
        if (!ctx->checkBelAvail(l_bel)) {
            log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci),
                      ctx->nameOfBel(l_bel), ctx->nameOf(ctx->getBoundBelCell(l_bel)));
        }
    }

    // While we require an exact match of the type, in the future the criteria
    // may be relaxed and there will be a comparison of the control networks
    // used.
    bool are_iologic_compatible(CellInfo *ci_0, CellInfo *ci_1)
    {
        switch (ci_0->type.hash()) {
        case ID_ODDR:
            return ci_1->type == id_IDDR;
        case ID_ODDRC:
            return ci_1->type == id_IDDRC;
        case ID_IDDR:
            return ci_1->type == id_ODDR;
        case ID_IDDRC:
            return ci_1->type == id_ODDRC;
        default:
            return false;
        }
        return false;
    }

    void pack_bi_output_iol(CellInfo &ci, std::vector<IdString> &nets_to_remove)
    {
        // These primitives have an additional pin to control the tri-state iob - Q1.
        IdString out_port = id_Q0;
        IdString tx_port = id_Q1;

        CellInfo *out_iob = net_only_drives(ctx, ci.ports.at(out_port).net, is_iob, id_I, true);
        NPNR_ASSERT(out_iob != nullptr && out_iob->bel != BelId());
        BelId iob_bel = out_iob->bel;

        BelId l_bel = get_iologico_bel(out_iob);
        // check compatible Input and Output iologic if any
        BelId in_l_bel = get_iologici_bel(out_iob);
        if (in_l_bel != BelId() && !ctx->checkBelAvail(in_l_bel)) {
            CellInfo *in_iologic_ci = ctx->getBoundBelCell(in_l_bel);
            if (!are_iologic_compatible(&ci, in_iologic_ci)) {
                log_error("IOLOGIC %s at %s cannot coexist with %s\n", ctx->nameOf(&ci), ctx->nameOfBel(iob_bel),
                          ctx->nameOf(in_iologic_ci));
            }
        }
        if (l_bel == BelId()) {
            log_error("Can't place IOLOGIC %s at %s\n", ctx->nameOf(&ci), ctx->nameOfBel(iob_bel));
        }
        // mark IOB as used by IOLOGIC
        out_iob->setParam(id_IOLOGIC_IOB, 1);
        check_iologic_placement(ci, ctx->getBelLocation(iob_bel), out_iob->params.count(id_DIFF_TYPE));

        if (!ctx->checkBelAvail(l_bel)) {
            log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci),
                      ctx->nameOfBel(l_bel), ctx->nameOf(ctx->getBoundBelCell(l_bel)));
        }
        ctx->bindBel(l_bel, &ci, PlaceStrength::STRENGTH_LOCKED);
        std::string out_mode;
        switch (ci.type.hash()) {
        case ID_ODDR:
        case ID_ODDRC:
            out_mode = "ODDRX1";
            break;
        case ID_OSER4:
            out_mode = "ODDRX2";
            break;
        case ID_OSER8:
            out_mode = "ODDRX4";
            break;
        }
        ci.setParam(ctx->id("OUTMODE"), out_mode);

        // disconnect Q output: it is wired internally
        nets_to_remove.push_back(ci.getPort(out_port)->name);
        out_iob->disconnectPort(id_I);
        ci.disconnectPort(out_port);
        set_daaj_nets(ci, iob_bel);

        Loc io_loc = ctx->getBelLocation(iob_bel);
        if (io_loc.y == ctx->getGridDimY() - 1) {
            config_bottom_row(*out_iob, io_loc, Bottom_io_POD::DDR);
        }

        // if Q1 is connected then disconnect it too
        if (gwu.port_used(&ci, tx_port)) {
            NPNR_ASSERT(out_iob == net_only_drives(ctx, ci.ports.at(tx_port).net, is_iob, id_OEN, true));
            nets_to_remove.push_back(ci.getPort(tx_port)->name);
            out_iob->disconnectPort(id_OEN);
            ci.disconnectPort(tx_port);
        }
        make_iob_nets(*out_iob);
    }

    void pack_single_output_iol(CellInfo &ci, std::vector<IdString> &nets_to_remove)
    {
        IdString out_port = id_Q;

        CellInfo *out_iob = net_only_drives(ctx, ci.ports.at(out_port).net, is_iob, id_I, true);
        NPNR_ASSERT(out_iob != nullptr && out_iob->bel != BelId());
        BelId iob_bel = out_iob->bel;

        BelId l_bel = get_iologico_bel(out_iob);
        if (l_bel == BelId()) {
            log_error("Can't place IOLOGIC %s at %s\n", ctx->nameOf(&ci), ctx->nameOfBel(iob_bel));
        }
        // mark IOB as used by IOLOGIC
        out_iob->setParam(id_IOLOGIC_IOB, 1);
        check_iologic_placement(ci, ctx->getBelLocation(iob_bel), out_iob->params.count(id_DIFF_TYPE));

        if (!ctx->checkBelAvail(l_bel)) {
            log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci),
                      ctx->nameOfBel(l_bel), ctx->nameOf(ctx->getBoundBelCell(l_bel)));
        }
        ctx->bindBel(l_bel, &ci, PlaceStrength::STRENGTH_LOCKED);
        std::string out_mode;
        switch (ci.type.hash()) {
        case ID_IOLOGICO_EMPTY:
            out_mode = "EMPTY";
            break;
        case ID_OVIDEO:
            out_mode = "VIDEORX";
            break;
        case ID_OSER10:
            out_mode = "ODDRX5";
            break;
        }
        ci.setParam(ctx->id("OUTMODE"), out_mode);
        // disconnect Q output: it is wired internally
        nets_to_remove.push_back(ci.getPort(out_port)->name);
        out_iob->disconnectPort(id_I);
        ci.disconnectPort(out_port);
        if (ci.type == id_IOLOGICO_EMPTY) {
            if (ci.attrs.count(id_HAS_REG) == 0) {
                ci.movePortTo(id_D, out_iob, id_I);
            }
            return;
        }
        set_daaj_nets(ci, iob_bel);

        Loc io_loc = ctx->getBelLocation(iob_bel);
        if (io_loc.y == ctx->getGridDimY() - 1) {
            config_bottom_row(*out_iob, io_loc, Bottom_io_POD::DDR);
        }
        make_iob_nets(*out_iob);
    }

    BelId get_aux_iologic_bel(const CellInfo &ci)
    {
        return ctx->getBelByLocation(gwu.get_pair_iologic_bel(ctx->getBelLocation(ci.bel)));
    }

    bool is_diff_io(BelId bel) { return ctx->getBoundBelCell(bel)->params.count(id_DIFF_TYPE) != 0; }

    CellInfo *create_aux_iologic_cell(CellInfo &ci, IdString mode, bool io16 = false, int idx = 0)
    {
        if (ci.type.in(id_ODDR, id_ODDRC, id_OSER4, id_IDDR, id_IDDRC, id_IDES4, id_IOLOGICI_EMPTY,
                       id_IOLOGICO_EMPTY)) {
            return nullptr;
        }
        IdString aux_name = gwu.create_aux_name(ci.name, idx);
        BelId bel = get_aux_iologic_bel(ci);
        BelId io_bel = gwu.get_io_bel_from_iologic(bel);
        if (!ctx->checkBelAvail(io_bel)) {
            if (!is_diff_io(io_bel)) {
                log_error("Can't place %s at %s because of a conflict with another IO %s\n", ctx->nameOf(&ci),
                          ctx->nameOfBel(bel), ctx->nameOf(ctx->getBoundBelCell(io_bel)));
            }
        }

        ctx->createCell(aux_name, id_IOLOGIC_DUMMY);
        CellInfo *aux = ctx->cells.at(aux_name).get();
        ci.copyPortTo(id_PCLK, aux, id_PCLK);
        ci.copyPortTo(id_RESET, aux, id_RESET);
        if (io16) {
            aux->setParam(mode, Property("DDRENABLE16"));
        } else {
            aux->setParam(mode, Property("DDRENABLE"));
        }
        aux->setAttr(ctx->id("IOLOGIC_TYPE"), Property("DUMMY"));
        aux->setAttr(ctx->id("MAIN_CELL"), Property(ci.name.str(ctx)));
        ctx->bindBel(bel, aux, PlaceStrength::STRENGTH_LOCKED);
        return aux;
    }

    void reconnect_ides_outs(CellInfo *ci)
    {
        IdString dest_ports[] = {id_Q9, id_Q8, id_Q7, id_Q6, id_Q5, id_Q4, id_Q3, id_Q2};
        switch (ci->type.hash()) {
        case ID_IDDR: /* fall-through*/
        case ID_IDDRC:
            ci->renamePort(id_Q1, id_Q9);
            ci->renamePort(id_Q0, id_Q8);
            break;
        case ID_IDES4:
            for (int i = 0; i < 4; ++i) {
                ci->renamePort(ctx->idf("Q%d", 3 - i), dest_ports[i]);
            }
            break;
        case ID_IVIDEO:
            for (int i = 0; i < 7; ++i) {
                ci->renamePort(ctx->idf("Q%d", 6 - i), dest_ports[i]);
            }
            break;
        case ID_IDES8:
            for (int i = 0; i < 8; ++i) {
                ci->renamePort(ctx->idf("Q%d", 7 - i), dest_ports[i]);
            }
            break;
        default:
            break;
        }
    }

    void pack_ides_iol(CellInfo &ci, std::vector<IdString> &nets_to_remove)
    {
        IdString in_port = id_D;

        CellInfo *in_iob = net_driven_by(ctx, ci.ports.at(in_port).net, is_iob, id_O);
        NPNR_ASSERT(in_iob != nullptr && in_iob->bel != BelId());
        BelId iob_bel = in_iob->bel;

        BelId l_bel = get_iologici_bel(in_iob);
        if (l_bel == BelId()) {
            log_error("Can't place IOLOGIC %s at %s\n", ctx->nameOf(&ci), ctx->nameOfBel(iob_bel));
        }
        // mark IOB as used by IOLOGIC
        in_iob->setParam(id_IOLOGIC_IOB, 1);
        check_iologic_placement(ci, ctx->getBelLocation(iob_bel), in_iob->params.count(id_DIFF_TYPE));

        if (!ctx->checkBelAvail(l_bel)) {
            log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci),
                      ctx->nameOfBel(l_bel), ctx->nameOf(ctx->getBoundBelCell(l_bel)));
        }
        ctx->bindBel(l_bel, &ci, PlaceStrength::STRENGTH_LOCKED);
        std::string in_mode;
        switch (ci.type.hash()) {
        case ID_IOLOGICI_EMPTY:
            in_mode = "EMPTY";
            break;
        case ID_IDDR:
        case ID_IDDRC:
            in_mode = "IDDRX1";
            break;
        case ID_IDES4:
            in_mode = "IDDRX2";
            break;
        case ID_IDES8:
            in_mode = "IDDRX4";
            break;
        case ID_IDES10:
            in_mode = "IDDRX5";
            break;
        case ID_IVIDEO:
            in_mode = "VIDEORX";
            break;
        }
        ci.setParam(ctx->id("INMODE"), in_mode);
        // disconnect D input: it is wired internally
        nets_to_remove.push_back(ci.getPort(in_port)->name);
        in_iob->disconnectPort(id_O);
        ci.disconnectPort(in_port);
        if (ci.type == id_IOLOGICI_EMPTY) {
            if (ci.attrs.count(id_HAS_REG) == 0) {
                ci.movePortTo(id_Q, in_iob, id_O);
            }
            return;
        }

        set_daaj_nets(ci, iob_bel);
        reconnect_ides_outs(&ci);

        make_iob_nets(*in_iob);
    }

    static bool is_ff(const Context *ctx, CellInfo *cell) { return is_dff(cell); }

    static bool incompatible_ffs(IdString type_a, IdString type_b)
    {
        return type_a != type_b &&
               ((type_a == id_DFFS && type_b != id_DFFR) || (type_a == id_DFFR && type_b != id_DFFS) ||
                (type_a == id_DFFSE && type_b != id_DFFRE) || (type_a == id_DFFRE && type_b != id_DFFSE) ||
                (type_a == id_DFFP && type_b != id_DFFC) || (type_a == id_DFFC && type_b != id_DFFP) ||
                (type_a == id_DFFPE && type_b != id_DFFCE) || (type_a == id_DFFCE && type_b != id_DFFPE) ||
                (type_a == id_DFFNS && type_b != id_DFFNR) || (type_a == id_DFFNR && type_b != id_DFFNS) ||
                (type_a == id_DFFNSE && type_b != id_DFFNRE) || (type_a == id_DFFNRE && type_b != id_DFFNSE) ||
                (type_a == id_DFFNP && type_b != id_DFFNC) || (type_a == id_DFFNC && type_b != id_DFFNP) ||
                (type_a == id_DFFNPE && type_b != id_DFFNCE) || (type_a == id_DFFNCE && type_b != id_DFFNPE) ||
                (type_a == id_DFF && type_b != id_DFF) || (type_a == id_DFFN && type_b != id_DFFN) ||
                (type_a == id_DFFE && type_b != id_DFFE) || (type_a == id_DFFNE && type_b != id_DFFNE));
    }

    void pack_io_regs()
    {
        log_info("Pack FFs into IO cells...\n");
        std::vector<IdString> cells_to_remove;
        std::vector<IdString> nets_to_remove;
        std::vector<std::unique_ptr<CellInfo>> new_cells;

        for (auto &cell : ctx->cells) {
            CellInfo &ci = *cell.second;
            if (!is_io(&ci)) {
                continue;
            }
            if (ci.attrs.count(id_NOIOBFF)) {
                if (ctx->debug) {
                    log_info(" NOIOBFF attribute at %s. Skipping FF placement.\n", ctx->nameOf(&ci));
                }
                continue;
            }

            // In the case of placing multiple registers in the IO it should be
            // noted that the CLK, ClockEnable and LocalSetReset nets must
            // match.
            const NetInfo *clk_net = nullptr;
            const NetInfo *ce_net = nullptr;
            const NetInfo *lsr_net = nullptr;
            IdString reg_type;

            // input reg in IO
            CellInfo *iologic_i = nullptr;
            if ((ci.type == id_IBUF && ctx->settings.count(id_IREG_IN_IOB)) ||
                (ci.type == id_IOBUF && ctx->settings.count(id_IOREG_IN_IOB))) {

                if (ci.getPort(id_O) == nullptr) {
                    continue;
                }
                // OBUF O -> D FF
                CellInfo *ff = net_only_drives(ctx, ci.ports.at(id_O).net, is_ff, id_D);
                if (ff == nullptr) {
                    if (ci.attrs.count(id_IOBFF)) {
                        log_warning("Port O of %s is not connected to FF.\n", ctx->nameOf(&ci));
                    }
                    continue;
                }
                if (ci.ports.at(id_O).net->users.entries() != 1) {
                    if (ci.attrs.count(id_IOBFF)) {
                        log_warning("Port O of %s is the driver of %s multi-sink network.\n", ctx->nameOf(&ci),
                                    ctx->nameOf(ci.ports.at(id_O).net));
                    }
                    continue;
                }
                BelId l_bel = get_iologici_bel(&ci);
                if (l_bel == BelId()) {
                    continue;
                }
                if (ctx->debug) {
                    log_info(" trying %s ff as Input Register of %s IO\n", ctx->nameOf(ff), ctx->nameOf(&ci));
                }

                clk_net = ff->getPort(id_CLK);
                ce_net = ff->getPort(id_CE);
                for (IdString port : {id_SET, id_RESET, id_PRESET, id_CLEAR}) {
                    lsr_net = ff->getPort(port);
                    if (lsr_net != nullptr) {
                        break;
                    }
                }
                reg_type = ff->type;

                // create IOLOGIC cell for flipflop
                IdString iologic_name = gwu.create_aux_name(ci.name, 0, "_iobff$");
                auto iologic_cell = gwu.create_cell(iologic_name, id_IOLOGICI_EMPTY);
                new_cells.push_back(std::move(iologic_cell));
                iologic_i = new_cells.back().get();

                // move ports
                for (auto &port : ff->ports) {
                    IdString port_name = port.first;
                    ff->movePortTo(port_name, iologic_i, port_name != id_Q ? port_name : id_Q4);
                }
                if (ctx->verbose) {
                    log_info("  place FF %s into IBUF %s, make iologic_i %s\n", ctx->nameOf(ff), ctx->nameOf(&ci),
                             ctx->nameOf(iologic_i));
                }
                iologic_i->setAttr(id_HAS_REG, 1);
                iologic_i->setAttr(id_IREG_TYPE, ff->type.str(ctx));
                cells_to_remove.push_back(ff->name);
            }

            // output reg in IO
            CellInfo *iologic_o = nullptr;
            if ((ci.type == id_OBUF && ctx->settings.count(id_OREG_IN_IOB)) ||
                (ci.type == id_IOBUF && ctx->settings.count(id_IOREG_IN_IOB))) {
                while (1) {
                    if (ci.getPort(id_I) == nullptr) {
                        break;
                    }
                    // OBUF I <- Q FF
                    CellInfo *ff = net_driven_by(ctx, ci.ports.at(id_I).net, is_ff, id_Q);
                    if (ff == nullptr) {
                        if (ci.attrs.count(id_IOBFF)) {
                            log_warning("Port I of %s is not connected to FF.\n", ctx->nameOf(&ci));
                        }
                    } else {
                        if (ci.ports.at(id_I).net->users.entries() != 1) {
                            if (ci.attrs.count(id_IOBFF)) {
                                log_warning("Port I of %s is not the only sink on the %s network.\n", ctx->nameOf(&ci),
                                            ctx->nameOf(ci.ports.at(id_I).net));
                            }
                            break;
                        }
                        BelId l_bel = get_iologico_bel(&ci);
                        if (l_bel == BelId()) {
                            break;
                        }

                        const NetInfo *this_clk_net = ff->getPort(id_CLK);
                        const NetInfo *this_ce_net = ff->getPort(id_CE);
                        const NetInfo *this_lsr_net;
                        for (IdString port : {id_SET, id_RESET, id_PRESET, id_CLEAR}) {
                            this_lsr_net = ff->getPort(port);
                            if (this_lsr_net != nullptr) {
                                break;
                            }
                        }
                        // The IOBUF may already have registers placed
                        if (ci.type == id_IOBUF) {
                            if (iologic_i != nullptr) {
                                if (incompatible_ffs(ff->type, reg_type)) {
                                    if (ci.attrs.count(id_IOBFF)) {
                                        log_warning("OREG type conflict:%s:%s vs %s IREG:%s\n", ctx->nameOf(ff),
                                                    ff->type.c_str(ctx), ctx->nameOf(&ci), reg_type.c_str(ctx));
                                    }
                                    break;
                                } else {
                                    if (clk_net != this_clk_net || ce_net != this_ce_net || lsr_net != this_lsr_net) {
                                        if (clk_net != this_clk_net) {
                                            if (ci.attrs.count(id_IOBFF)) {
                                                log_warning("Conflicting OREG CLK nets at %s:'%s' vs '%s'\n",
                                                            ctx->nameOf(&ci), ctx->nameOf(clk_net),
                                                            ctx->nameOf(this_clk_net));
                                            }
                                        }
                                        if (ce_net != this_ce_net) {
                                            if (ci.attrs.count(id_IOBFF)) {
                                                log_warning("Conflicting OREG CE nets at %s:'%s' vs '%s'\n",
                                                            ctx->nameOf(&ci), ctx->nameOf(ce_net),
                                                            ctx->nameOf(this_ce_net));
                                            }
                                        }
                                        if (lsr_net != this_lsr_net) {
                                            if (ci.attrs.count(id_IOBFF)) {
                                                log_warning("Conflicting OREG LSR nets at %s:'%s' vs '%s'\n",
                                                            ctx->nameOf(&ci), ctx->nameOf(lsr_net),
                                                            ctx->nameOf(this_lsr_net));
                                            }
                                        }
                                        break;
                                    }
                                }
                            } else {
                                clk_net = this_clk_net;
                                ce_net = this_ce_net;
                                lsr_net = this_lsr_net;
                                reg_type = ff->type;
                            }
                        }

                        // create IOLOGIC cell for flipflop
                        IdString iologic_name = gwu.create_aux_name(ci.name, 1, "_iobff$");
                        auto iologic_cell = gwu.create_cell(iologic_name, id_IOLOGICO_EMPTY);
                        new_cells.push_back(std::move(iologic_cell));
                        iologic_o = new_cells.back().get();

                        // move ports
                        for (auto &port : ff->ports) {
                            IdString port_name = port.first;
                            ff->movePortTo(port_name, iologic_o, port_name != id_D ? port_name : id_D0);
                        }
                        if (ctx->verbose) {
                            log_info("  place FF %s into OBUF %s, make iologic_o %s\n", ctx->nameOf(ff),
                                     ctx->nameOf(&ci), ctx->nameOf(iologic_o));
                        }
                        iologic_o->setAttr(id_HAS_REG, 1);
                        iologic_o->setAttr(id_OREG_TYPE, ff->type.str(ctx));
                        cells_to_remove.push_back(ff->name);
                    }
                    break;
                }
            }

            // output enable reg in IO
            if (ci.type == id_IOBUF && ctx->settings.count(id_IOREG_IN_IOB)) {
                while (1) {
                    if (ci.getPort(id_OEN) == nullptr) {
                        break;
                    }
                    // IOBUF OEN <- Q FF
                    CellInfo *ff = net_driven_by(ctx, ci.ports.at(id_OEN).net, is_ff, id_Q);
                    if (ff != nullptr) {
                        if (ci.ports.at(id_OEN).net->users.entries() != 1) {
                            if (ci.attrs.count(id_IOBFF)) {
                                log_warning("Port OEN of %s is not the only sink on the %s network.\n",
                                            ctx->nameOf(&ci), ctx->nameOf(ci.ports.at(id_OEN).net));
                            }
                            break;
                        }
                        BelId l_bel = get_iologico_bel(&ci);
                        if (l_bel == BelId()) {
                            break;
                        }
                        if (ctx->debug) {
                            log_info(" trying %s ff as Output Enable Register of %s IO\n", ctx->nameOf(ff),
                                     ctx->nameOf(&ci));
                        }

                        const NetInfo *this_clk_net = ff->getPort(id_CLK);
                        const NetInfo *this_ce_net = ff->getPort(id_CE);
                        const NetInfo *this_lsr_net;
                        for (IdString port : {id_SET, id_RESET, id_PRESET, id_CLEAR}) {
                            this_lsr_net = ff->getPort(port);
                            if (this_lsr_net != nullptr) {
                                break;
                            }
                        }

                        // The IOBUF may already have registers placed
                        if (iologic_i != nullptr || iologic_o != nullptr) {
                            if (iologic_o == nullptr) {
                                iologic_o = iologic_i;
                            }
                            if (incompatible_ffs(ff->type, reg_type)) {
                                if (ci.attrs.count(id_IOBFF)) {
                                    log_warning("TREG type conflict:%s:%s vs %s IREG/OREG:%s\n", ctx->nameOf(ff),
                                                ff->type.c_str(ctx), ctx->nameOf(&ci), reg_type.c_str(ctx));
                                }
                                break;
                            } else {
                                if (clk_net != this_clk_net || ce_net != this_ce_net || lsr_net != this_lsr_net) {
                                    if (clk_net != this_clk_net) {
                                        if (ci.attrs.count(id_IOBFF)) {
                                            log_warning("Conflicting TREG CLK nets at %s:'%s' vs '%s'\n",
                                                        ctx->nameOf(&ci), ctx->nameOf(clk_net),
                                                        ctx->nameOf(this_clk_net));
                                        }
                                    }
                                    if (ce_net != this_ce_net) {
                                        if (ci.attrs.count(id_IOBFF)) {
                                            log_warning("Conflicting TREG CE nets at %s:'%s' vs '%s'\n",
                                                        ctx->nameOf(&ci), ctx->nameOf(ce_net),
                                                        ctx->nameOf(this_ce_net));
                                        }
                                    }
                                    if (lsr_net != this_lsr_net) {
                                        if (ci.attrs.count(id_IOBFF)) {
                                            log_warning("Conflicting TREG LSR nets at %s:'%s' vs '%s'\n",
                                                        ctx->nameOf(&ci), ctx->nameOf(lsr_net),
                                                        ctx->nameOf(this_lsr_net));
                                        }
                                    }
                                    break;
                                }
                            }
                        }

                        if (iologic_o == nullptr) {
                            // create IOLOGIC cell for flipflop
                            IdString iologic_name = gwu.create_aux_name(ci.name, 2, "_iobff$");
                            auto iologic_cell = gwu.create_cell(iologic_name, id_IOLOGICO_EMPTY);
                            new_cells.push_back(std::move(iologic_cell));
                            iologic_o = new_cells.back().get();
                        }

                        // move ports
                        for (auto &port : ff->ports) {
                            IdString port_name = port.first;
                            if (port_name == id_Q) {
                                continue;
                            }
                            ff->movePortTo(port_name, iologic_o, port_name != id_D ? port_name : id_TX);
                        }

                        nets_to_remove.push_back(ci.getPort(id_OEN)->name);
                        ci.disconnectPort(id_OEN);
                        ff->disconnectPort(id_Q);

                        if (ctx->verbose) {
                            log_info("  place FF %s into IOBUF %s, make iologic_o %s\n", ctx->nameOf(ff),
                                     ctx->nameOf(&ci), ctx->nameOf(iologic_o));
                        }
                        iologic_o->setAttr(id_HAS_REG, 1);
                        iologic_o->setAttr(id_TREG_TYPE, ff->type.str(ctx));
                        cells_to_remove.push_back(ff->name);
                    }
                    break;
                }
            }
        }

        for (auto cell : cells_to_remove) {
            ctx->cells.erase(cell);
        }

        for (auto &ncell : new_cells) {
            ctx->cells[ncell->name] = std::move(ncell);
        }

        for (auto net : nets_to_remove) {
            ctx->nets.erase(net);
        }
    }

    void pack_iodelay()
    {
        log_info("Pack IODELAY...\n");
        std::vector<IdString> cells_to_remove;
        std::vector<IdString> nets_to_remove;
        std::vector<std::unique_ptr<CellInfo>> new_cells;

        for (auto &cell : ctx->cells) {
            CellInfo &ci = *cell.second;
            if (ci.type != id_IODELAY) {
                continue;
            }
            if (ctx->debug) {
                log_info("pack %s of type %s.\n", ctx->nameOf(&ci), ci.type.c_str(ctx));
            }
            // There is only one delay line in the IO block, which can be either
            // input or output.  Define which case we are dealing with.
            bool is_idelay = false;
            NetInfo *di_net = ci.ports.at(id_DI).net;
            NetInfo *do_net = ci.ports.at(id_DO).net;
            CellInfo *iob = net_driven_by(ctx, di_net, is_iob, id_O);
            if (iob != nullptr) {
                NPNR_ASSERT(iob->bel != BelId());
                if (di_net->users.entries() != 1) {
                    log_error("IODELAY %s should be the only sink in the %s network.\n", ctx->nameOf(&ci),
                              ctx->nameOf(di_net));
                }
                is_idelay = true;
            } else {
                iob = net_only_drives(ctx, do_net, is_iob, id_I, true);
                if (iob != nullptr) {
                    NPNR_ASSERT(iob->bel != BelId());
                } else {
                    log_error("IODELAY %s is not connected to the pin.\n", ctx->nameOf(&ci));
                }
            }

            BelId iob_bel = iob->bel;
            BelId l_bel = get_iologici_bel(iob);
            if (l_bel == BelId()) {
                log_error("Can't place IOLOGIC %s at %s\n", ctx->nameOf(&ci), ctx->nameOfBel(iob_bel));
            }

            // find IOLOGIC connected or create dummy one
            CellInfo *iologic = nullptr;
            Property attr;
            IdString dummy_iol_type;
            if (is_idelay) {
                attr = Property("IN");
                dummy_iol_type = id_IOLOGICI_EMPTY;
                for (auto &usr : do_net->users) {
                    if (is_iologici(usr.cell)) {
                        iologic = usr.cell;
                        if (iologic->attrs.count(id_IODELAY) != 0) {
                            log_error("Only one IODELAY allowed per IO block %s.\n", ctx->nameOfBel(iob->bel));
                        }
                        if (ctx->debug) {
                            log_info(" found IOLOGIC cell %s of type %s, use it.\n", ctx->nameOf(iologic),
                                     iologic->type.c_str(ctx));
                        }
                    }
                }
            } else {
                attr = Property("OUT");
                dummy_iol_type = id_IOLOGICO_EMPTY;
                if (is_iologico(di_net->driver.cell)) {
                    iologic = di_net->driver.cell;
                    if (iologic->attrs.count(id_IODELAY) != 0) {
                        log_error("Only one IODELAY allowed per IO block %s.\n", ctx->nameOfBel(iob->bel));
                    }
                    if (ctx->debug) {
                        log_info(" found IOLOGIC cell %s of type %s, use it.\n", ctx->nameOf(iologic),
                                 iologic->type.c_str(ctx));
                    }
                }
            }

            if (iologic == nullptr) {
                IdString iologic_name = gwu.create_aux_name(ci.name);
                if (ctx->debug) {
                    log_info(" create IOLOGIC cell %s.\n", iologic_name.c_str(ctx));
                }
                auto iologic_cell = gwu.create_cell(iologic_name, dummy_iol_type);
                new_cells.push_back(std::move(iologic_cell));
                iologic = new_cells.back().get();
                iologic->addInput(id_D);
                iologic->addOutput(id_Q);
                ci.movePortTo(id_DI, iologic, id_D);
                ci.movePortTo(id_DO, iologic, id_Q);
            } else {
                if (is_idelay) {
                    iob->disconnectPort(id_O);
                    ci.disconnectPort(id_I);
                    ci.movePortTo(id_DO, iob, id_O);
                } else {
                    IdString iol_out = di_net->driver.port;
                    ci.disconnectPort(id_DI);
                    iologic->disconnectPort(iol_out);
                    ci.movePortTo(id_DO, iologic, iol_out);
                }
                nets_to_remove.push_back(di_net->name);
            }

            ci.movePortTo(id_SDTAP, iologic, id_SDTAP);
            ci.movePortTo(id_SETN, iologic, id_SETN);
            ci.movePortTo(id_VALUE, iologic, id_VALUE);
            ci.movePortTo(id_DF, iologic, id_DF);

            if (ci.params.count(id_C_STATIC_DLY)) {
                iologic->setParam(id_C_STATIC_DLY, ci.params.at(id_C_STATIC_DLY));
            }
            iologic->setAttr(id_IODELAY, attr);
            cells_to_remove.push_back(ci.name);
        }
        for (auto cell : cells_to_remove) {
            ctx->cells.erase(cell);
        }

        for (auto &ncell : new_cells) {
            ctx->cells[ncell->name] = std::move(ncell);
        }

        for (auto net : nets_to_remove) {
            ctx->nets.erase(net);
        }
    }

    void pack_iem()
    {
        log_info("Pack Input Edge Monitors...\n");
        std::vector<IdString> cells_to_remove;
        std::vector<std::unique_ptr<CellInfo>> new_cells;

        for (auto &cell : ctx->cells) {
            CellInfo &ci = *cell.second;
            if (ci.type != id_IEM) {
                continue;
            }
            if (ctx->debug) {
                log_info("pack %s of type %s.\n", ctx->nameOf(&ci), ci.type.c_str(ctx));
            }
            // IEM is part of IOLOGIC but functions independently of the
            // presence/absence of other IOLOGIC components. Therefore, we use
            // the existing cell whenever possible.
            const NetInfo *d_net = ci.ports.at(id_D).net;
            CellInfo *in_iob = net_driven_by(ctx, d_net, is_iob, id_O);
            NPNR_ASSERT(in_iob != nullptr && in_iob->bel != BelId());
            BelId iob_bel = in_iob->bel;

            BelId l_bel = get_iologici_bel(in_iob);
            if (l_bel == BelId()) {
                log_error("Can't place IOLOGIC %s at %s\n", ctx->nameOf(&ci), ctx->nameOfBel(iob_bel));
            }
            CellInfo *iologic = nullptr;
            for (auto &usr : d_net->users) {
                if (is_iologici(usr.cell)) {
                    if (ctx->debug) {
                        log_info(" found IOLOGIC cell %s of type %s, use it.\n", ctx->nameOf(usr.cell),
                                 usr.cell->type.c_str(ctx));
                    }
                    iologic = usr.cell;
                    if (iologic->ports.count(id_CLK)) {
                        NPNR_ASSERT(iologic->ports.at(id_CLK).net == ci.ports.at(id_CLK).net);
                    } else {
                        if (iologic->ports.count(id_PCLK)) {
                            NPNR_ASSERT(iologic->ports.at(id_PCLK).net == ci.ports.at(id_CLK).net);
                        }
                        iologic->addInput(ctx->id("CLK"));
                    }
                    if (iologic->ports.count(id_RESET)) {
                        NPNR_ASSERT(iologic->ports.at(id_RESET).net == ci.ports.at(id_RESET).net);
                    } else {
                        iologic->addInput(ctx->id("RESET"));
                    }
                    break;
                }
            }
            if (iologic == nullptr) {
                IdString iologic_name = gwu.create_aux_name(ci.name);
                if (ctx->debug) {
                    log_info(" create IOLOGIC cell %s.\n", iologic_name.c_str(ctx));
                }
                auto iologic_cell = gwu.create_cell(iologic_name, id_IOLOGICI_EMPTY);
                new_cells.push_back(std::move(iologic_cell));
                iologic = new_cells.back().get();
                ci.copyPortTo(id_D, iologic, id_D);
                ci.copyPortTo(id_CLK, iologic, id_CLK);
                ci.copyPortTo(id_RESET, iologic, id_RESET);
            }
            ci.movePortTo(id_MCLK, iologic, id_MCLK);
            ci.movePortTo(id_LAG, iologic, id_LAG);
            ci.movePortTo(id_LEAD, iologic, id_LEAD);

            ci.disconnectPort(id_D);
            ci.disconnectPort(id_CLK);
            ci.disconnectPort(id_RESET);

            // WINSIZE attribute defines routing to ports WINSIZE0/1
            iologic->addInput(id_WINSIZE0);
            iologic->addInput(id_WINSIZE1);
            if (ci.params.count(id_WINSIZE) == 0) {
                ci.setParam(id_WINSIZE, Property("SMALL"));
            }

            NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
            NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();
            IdString winsize = ctx->id(ci.params.at(id_WINSIZE).as_string());
            switch (winsize.hash()) {
            case ID_SMALL:
                iologic->connectPort(id_WINSIZE0, vss_net);
                iologic->connectPort(id_WINSIZE1, vss_net);
                break;
            case ID_MIDSMALL:
                iologic->connectPort(id_WINSIZE0, vcc_net);
                iologic->connectPort(id_WINSIZE1, vss_net);
                break;
            case ID_MIDLARGE:
                iologic->connectPort(id_WINSIZE0, vss_net);
                iologic->connectPort(id_WINSIZE1, vcc_net);
                break;
            case ID_LARGE:
                iologic->connectPort(id_WINSIZE0, vcc_net);
                iologic->connectPort(id_WINSIZE1, vcc_net);
                break;
            default:
                log_error("%s has incorrect WINSIZE:%s\n", ctx->nameOf(&ci), ci.params.at(id_WINSIZE).c_str());
            }

            if (ci.params.count(id_GSREN) != 0) {
                if (iologic->params.count(id_GSREN) == 0) {
                    iologic->setParam(id_GSREN, ci.params.at(id_GSREN));
                } else {
                    if (ci.params.at(id_GSREN) != iologic->params.at(id_GSREN)) {
                        log_error("GSREN parameter values of %s and %s do not match.\n", ctx->nameOf(&ci),
                                  ctx->nameOf(iologic));
                    }
                }
            }
            if (ci.params.count(id_LSREN) != 0) {
                if (iologic->params.count(id_LSREN) == 0) {
                    iologic->setParam(id_LSREN, ci.params.at(id_LSREN));
                } else {
                    if (ci.params.at(id_LSREN) != iologic->params.at(id_LSREN)) {
                        log_error("LSREN parameter values of %s and %s do not match.\n", ctx->nameOf(&ci),
                                  ctx->nameOf(iologic));
                    }
                }
            }
            cells_to_remove.push_back(ci.name);
        }

        for (auto cell : cells_to_remove) {
            ctx->cells.erase(cell);
        }

        for (auto &ncell : new_cells) {
            ctx->cells[ncell->name] = std::move(ncell);
        }
    }

    void pack_iologic()
    {
        log_info("Pack IO logic...\n");
        std::vector<IdString> nets_to_remove;

        for (auto &cell : ctx->cells) {
            CellInfo &ci = *cell.second;
            if (!(is_iologici(&ci) || is_iologico(&ci))) {
                continue;
            }
            if (ctx->debug) {
                log_info("pack %s of type %s.\n", ctx->nameOf(&ci), ci.type.c_str(ctx));
            }
            if (ci.type.in(id_ODDR, id_ODDRC, id_OSER4, id_OSER8)) {
                pack_bi_output_iol(ci, nets_to_remove);
                create_aux_iologic_cell(ci, ctx->id("OUTMODE"));
                continue;
            }
            if (ci.type.in(id_OVIDEO, id_OSER10, id_IOLOGICO_EMPTY)) {
                pack_single_output_iol(ci, nets_to_remove);
                create_aux_iologic_cell(ci, ctx->id("OUTMODE"));
                continue;
            }
            if (ci.type.in(id_IDDR, id_IDDRC, id_IDES4, id_IDES8, id_IDES10, id_IVIDEO, id_IOLOGICI_EMPTY)) {
                pack_ides_iol(ci, nets_to_remove);
                create_aux_iologic_cell(ci, ctx->id("INMODE"));
                continue;
            }
        }

        for (auto net : nets_to_remove) {
            ctx->nets.erase(net);
        }
    }

    // ===================================
    // IDES16 / OSER16
    // ===================================
    void check_io16_placement(CellInfo &ci, Loc main_loc, Loc aux_off, int diff /* 1 - diff */)
    {
        if (main_loc.z != BelZ::IOBA_Z) {
            log_error("Can't place %s at %s because OSER16/IDES16 must be placed at A pin\n", ctx->nameOf(&ci),
                      ctx->nameOfBel(ctx->getBelByLocation(main_loc)));
        }

        int mod[][3] = {{0, 0, 1}, {1, 1, 0}, {1, 1, 1}};
        for (int i = diff; i < 3; ++i) {
            Loc aux_loc(main_loc.x + mod[i][0] * aux_off.x, main_loc.y + mod[i][1] * aux_off.y, main_loc.z + mod[i][2]);
            BelId l_bel = ctx->getBelByLocation(aux_loc);
            if (!ctx->checkBelAvail(l_bel)) {
                log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci),
                          ctx->nameOfBel(l_bel), ctx->nameOf(ctx->getBoundBelCell(l_bel)));
            }
        }
    }

    void pack_oser16(CellInfo &ci, std::vector<IdString> &nets_to_remove)
    {
        IdString out_port = id_Q;

        CellInfo *out_iob = net_only_drives(ctx, ci.ports.at(out_port).net, is_iob, id_I, true);
        NPNR_ASSERT(out_iob != nullptr && out_iob->bel != BelId());
        // mark IOB as used by IOLOGIC
        out_iob->setParam(id_IOLOGIC_IOB, 1);

        BelId iob_bel = out_iob->bel;

        Loc iob_loc = ctx->getBelLocation(iob_bel);
        Loc aux_offset = gwu.get_tile_io16_offs(iob_loc.x, iob_loc.y);

        if (aux_offset.x == 0 && aux_offset.y == 0) {
            log_error("OSER16 %s can not be placed at %s\n", ctx->nameOf(&ci), ctx->nameOfBel(iob_bel));
        }
        check_io16_placement(ci, iob_loc, aux_offset, out_iob->params.count(id_DIFF_TYPE));

        BelId main_bel = ctx->getBelByLocation(Loc(iob_loc.x, iob_loc.y, BelZ::OSER16_Z));
        ctx->bindBel(main_bel, &ci, PlaceStrength::STRENGTH_LOCKED);

        // disconnect Q output: it is wired internally
        nets_to_remove.push_back(ci.getPort(out_port)->name);
        out_iob->disconnectPort(id_I);
        ci.disconnectPort(out_port);

        // to simplify packaging, the parts of the OSER16 are presented as IOLOGIC cells
        // and one of these aux cells is declared as main
        IdString main_name = gwu.create_aux_name(ci.name);

        IdString aux_name = gwu.create_aux_name(ci.name, 1);
        ctx->createCell(aux_name, id_IOLOGIC_DUMMY);
        CellInfo *aux = ctx->cells.at(aux_name).get();

        aux->setAttr(ctx->id("MAIN_CELL"), Property(main_name.str(ctx)));
        aux->setParam(ctx->id("OUTMODE"), Property("ODDRX8"));
        aux->setParam(ctx->id("UPDATE"), Property("SAME"));
        aux->setAttr(ctx->id("IOLOGIC_TYPE"), Property("DUMMY"));
        ci.copyPortTo(id_PCLK, aux, id_PCLK);
        ci.copyPortTo(id_RESET, aux, id_RESET);
        ctx->bindBel(ctx->getBelByLocation(Loc(iob_loc.x, iob_loc.y, BelZ::IOLOGICA_Z)), aux,
                     PlaceStrength::STRENGTH_LOCKED);

        // make aux cell in the first cell
        aux = create_aux_iologic_cell(*aux, ctx->id("OUTMODE"), true, 2);
        aux->setAttr(ctx->id("MAIN_CELL"), Property(main_name.str(ctx)));
        aux->setParam(ctx->id("UPDATE"), Property("SAME"));

        // make cell in the next location
        ctx->createCell(main_name, id_IOLOGIC);
        aux = ctx->cells.at(main_name).get();

        aux->setAttr(ctx->id("MAIN_CELL"), Property(main_name.str(ctx)));
        aux->setParam(ctx->id("OUTMODE"), Property("DDRENABLE16"));
        aux->setParam(ctx->id("UPDATE"), Property("SAME"));
        aux->setAttr(ctx->id("IOLOGIC_TYPE"), Property("DUMMY"));
        ci.copyPortTo(id_PCLK, aux, id_PCLK);
        ci.copyPortTo(id_RESET, aux, id_RESET);
        ci.movePortTo(id_FCLK, aux, id_FCLK);
        ci.movePortTo(id_D12, aux, id_D0);
        ci.movePortTo(id_D13, aux, id_D1);
        ci.movePortTo(id_D14, aux, id_D2);
        ci.movePortTo(id_D15, aux, id_D3);
        Loc next_io16(iob_loc.x + aux_offset.x, iob_loc.y + aux_offset.y, BelZ::IOLOGICA_Z);
        ctx->bindBel(ctx->getBelByLocation(next_io16), aux, PlaceStrength::STRENGTH_LOCKED);

        Loc io_loc = ctx->getBelLocation(iob_bel);
        if (io_loc.y == ctx->getGridDimY() - 1) {
            config_bottom_row(*out_iob, io_loc, Bottom_io_POD::DDR);
        }
        make_iob_nets(*out_iob);
    }

    void pack_ides16(CellInfo &ci, std::vector<IdString> &nets_to_remove)
    {
        IdString in_port = id_D;

        CellInfo *in_iob = net_driven_by(ctx, ci.ports.at(in_port).net, is_iob, id_O);
        NPNR_ASSERT(in_iob != nullptr && in_iob->bel != BelId());
        // mark IOB as used by IOLOGIC
        in_iob->setParam(id_IOLOGIC_IOB, 1);

        BelId iob_bel = in_iob->bel;

        Loc iob_loc = ctx->getBelLocation(iob_bel);
        Loc aux_offset = gwu.get_tile_io16_offs(iob_loc.x, iob_loc.y);

        if (aux_offset.x == 0 && aux_offset.y == 0) {
            log_error("IDES16 %s can not be placed at %s\n", ctx->nameOf(&ci), ctx->nameOfBel(iob_bel));
        }
        check_io16_placement(ci, iob_loc, aux_offset, in_iob->params.count(id_DIFF_TYPE));

        BelId main_bel = ctx->getBelByLocation(Loc(iob_loc.x, iob_loc.y, BelZ::IDES16_Z));
        ctx->bindBel(main_bel, &ci, PlaceStrength::STRENGTH_LOCKED);

        // disconnect Q output: it is wired internally
        nets_to_remove.push_back(ci.getPort(in_port)->name);
        in_iob->disconnectPort(id_O);
        ci.disconnectPort(in_port);

        // to simplify packaging, the parts of the IDES16 are presented as IOLOGIC cells
        // and one of these aux cells is declared as main
        IdString main_name = gwu.create_aux_name(ci.name);

        IdString aux_name = gwu.create_aux_name(ci.name, 1);
        ctx->createCell(aux_name, id_IOLOGIC_DUMMY);
        CellInfo *aux = ctx->cells.at(aux_name).get();

        aux->setAttr(ctx->id("MAIN_CELL"), Property(main_name.str(ctx)));
        aux->setParam(ctx->id("INMODE"), Property("IDDRX8"));
        aux->setAttr(ctx->id("IOLOGIC_TYPE"), Property("DUMMY"));
        ci.copyPortTo(id_PCLK, aux, id_PCLK);
        ci.copyPortTo(id_RESET, aux, id_RESET);
        ctx->bindBel(ctx->getBelByLocation(Loc(iob_loc.x, iob_loc.y, BelZ::IOLOGICA_Z)), aux,
                     PlaceStrength::STRENGTH_LOCKED);

        // make aux cell in the first cell
        aux = create_aux_iologic_cell(*aux, ctx->id("INMODE"), true, 2);
        aux->setAttr(ctx->id("MAIN_CELL"), Property(main_name.str(ctx)));
        ci.copyPortTo(id_CALIB, aux, id_CALIB);

        // make cell in the next location
        ctx->createCell(main_name, id_IOLOGIC);
        aux = ctx->cells.at(main_name).get();

        aux->setAttr(ctx->id("MAIN_CELL"), Property(main_name.str(ctx)));
        aux->setParam(ctx->id("INMODE"), Property("DDRENABLE16"));
        aux->setAttr(ctx->id("IOLOGIC_TYPE"), Property("DUMMY"));
        ci.copyPortTo(id_PCLK, aux, id_PCLK);
        ci.copyPortTo(id_RESET, aux, id_RESET);
        ci.copyPortTo(id_CALIB, aux, id_CALIB);
        ci.movePortTo(id_FCLK, aux, id_FCLK);
        ci.movePortTo(id_Q0, aux, id_Q6);
        ci.movePortTo(id_Q1, aux, id_Q7);
        ci.movePortTo(id_Q2, aux, id_Q8);
        ci.movePortTo(id_Q3, aux, id_Q9);
        Loc next_io16(iob_loc.x + aux_offset.x, iob_loc.y + aux_offset.y, BelZ::IOLOGICA_Z);
        ctx->bindBel(ctx->getBelByLocation(next_io16), aux, PlaceStrength::STRENGTH_LOCKED);

        make_iob_nets(*in_iob);
    }

    void pack_io16(void)
    {
        std::vector<IdString> nets_to_remove;
        log_info("Pack DESER16 logic...\n");

        for (auto &cell : ctx->cells) {
            CellInfo &ci = *cell.second;
            if (ci.type == id_OSER16) {
                if (ctx->debug) {
                    log_info("pack %s of type %s.\n", ctx->nameOf(&ci), ci.type.c_str(ctx));
                }
                pack_oser16(ci, nets_to_remove);
                continue;
            }
            if (ci.type == id_IDES16) {
                if (ctx->debug) {
                    log_info("pack %s of type %s.\n", ctx->nameOf(&ci), ci.type.c_str(ctx));
                }
                pack_ides16(ci, nets_to_remove);
                continue;
            }
        }
        for (auto net : nets_to_remove) {
            ctx->nets.erase(net);
        }
    }

    // ===================================
    // Constant nets
    // ===================================
    void handle_constants(void)
    {
        log_info("Create constant nets...\n");
        const dict<IdString, Property> vcc_params;
        const dict<IdString, Property> gnd_params;
        h.replace_constants(CellTypePort(id_GOWIN_VCC, id_V), CellTypePort(id_GOWIN_GND, id_G), vcc_params, gnd_params);

        // disconnect the constant LUT inputs
        log_info("Modify LUTs...\n");
        for (IdString netname : {ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC")}) {
            auto net = ctx->nets.find(netname);
            if (net == ctx->nets.end()) {
                continue;
            }
            NetInfo *constnet = net->second.get();
            for (auto user : constnet->users) {
                CellInfo *uc = user.cell;
                if (is_lut(uc) && (user.port.str(ctx).at(0) == 'I')) {
                    if (ctx->debug) {
                        log_info("%s user %s/%s\n", ctx->nameOf(constnet), ctx->nameOf(uc), user.port.c_str(ctx));
                    }

                    auto it_param = uc->params.find(id_INIT);
                    if (it_param == uc->params.end())
                        log_error("No initialization for lut found.\n");

                    int64_t uc_init = it_param->second.intval;
                    int64_t mask = 0;
                    uint8_t amt = 0;

                    if (user.port == id_I0) {
                        mask = 0x5555;
                        amt = 1;
                    } else if (user.port == id_I1) {
                        mask = 0x3333;
                        amt = 2;
                    } else if (user.port == id_I2) {
                        mask = 0x0F0F;
                        amt = 4;
                    } else if (user.port == id_I3) {
                        mask = 0x00FF;
                        amt = 8;
                    } else {
                        log_error("Port number invalid.\n");
                    }

                    if ((constnet->name == ctx->id("$PACKER_GND"))) {
                        uc_init = (uc_init & mask) | ((uc_init & mask) << amt);
                    } else {
                        uc_init = (uc_init & (mask << amt)) | ((uc_init & (mask << amt)) >> amt);
                    }

                    size_t uc_init_len = it_param->second.to_string().length();
                    uc_init &= (1LL << uc_init_len) - 1;

                    if (ctx->verbose && it_param->second.intval != uc_init)
                        log_info("%s lut config modified from 0x%" PRIX64 " to 0x%" PRIX64 "\n", ctx->nameOf(uc),
                                 it_param->second.intval, uc_init);

                    it_param->second = Property(uc_init, uc_init_len);
                    uc->disconnectPort(user.port);
                }
            }
        }
    }

    // ===================================
    // Wideluts
    // ===================================
    void pack_wideluts(void)
    {
        log_info("Pack wide LUTs...\n");
        // children's offsets
        struct _children
        {
            IdString port;
            int dx, dz;
        } mux_inputs[4][2] = {{{id_I0, 1, -7}, {id_I1, 0, -7}},
                              {{id_I0, 0, 4}, {id_I1, 0, -4}},
                              {{id_I0, 0, 2}, {id_I1, 0, -2}},
                              {{id_I0, 0, -BelZ::MUX20_Z}, {id_I1, 0, 2 - BelZ::MUX20_Z}}};
        typedef std::function<void(CellInfo &, CellInfo *, int, int)> recurse_func_t;
        recurse_func_t make_cluster = [&, this](CellInfo &ci_root, CellInfo *ci_cursor, int dx, int dz) {
            _children *inputs;
            if (is_lut(ci_cursor)) {
                return;
            }
            switch (ci_cursor->type.hash()) {
            case ID_MUX2_LUT8:
                inputs = mux_inputs[0];
                break;
            case ID_MUX2_LUT7:
                inputs = mux_inputs[1];
                break;
            case ID_MUX2_LUT6:
                inputs = mux_inputs[2];
                break;
            case ID_MUX2_LUT5:
                inputs = mux_inputs[3];
                break;
            default:
                log_error("Bad MUX2 node:%s\n", ctx->nameOf(ci_cursor));
            }
            for (int i = 0; i < 2; ++i) {
                // input src
                NetInfo *in = ci_cursor->getPort(inputs[i].port);
                NPNR_ASSERT(in && in->driver.cell && in->driver.cell->cluster == ClusterId());
                int child_dx = dx + inputs[i].dx;
                int child_dz = dz + inputs[i].dz;
                ci_root.constr_children.push_back(in->driver.cell);
                in->driver.cell->cluster = ci_root.name;
                in->driver.cell->constr_abs_z = false;
                in->driver.cell->constr_x = child_dx;
                in->driver.cell->constr_y = 0;
                in->driver.cell->constr_z = child_dz;
                make_cluster(ci_root, in->driver.cell, child_dx, child_dz);
            }
        };

        // look for MUX2
        // MUX2_LUT8 create right away, collect others
        std::vector<IdString> muxes[3];
        int packed[4] = {0, 0, 0, 0};
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (ci.cluster != ClusterId()) {
                continue;
            }
            if (ci.type == id_MUX2_LUT8) {
                ci.cluster = ci.name;
                ci.constr_abs_z = false;
                make_cluster(ci, &ci, 0, 0);
                ++packed[0];
                continue;
            }
            if (ci.type.in(id_MUX2_LUT7, id_MUX2_LUT6, id_MUX2_LUT5)) {
                switch (ci.type.hash()) {
                case ID_MUX2_LUT7:
                    muxes[0].push_back(cell.first);
                    break;
                case ID_MUX2_LUT6:
                    muxes[1].push_back(cell.first);
                    break;
                default: // ID_MUX2_LUT5
                    muxes[2].push_back(cell.first);
                    break;
                }
            }
        }
        // create others
        for (int i = 0; i < 3; ++i) {
            for (IdString cell_name : muxes[i]) {
                auto &ci = *ctx->cells.at(cell_name);
                if (ci.cluster != ClusterId()) {
                    continue;
                }
                ci.cluster = ci.name;
                ci.constr_abs_z = false;
                make_cluster(ci, &ci, 0, 0);
                ++packed[i + 1];
            }
        }
        log_info("Packed MUX2_LUT8:%d, MUX2_LU7:%d, MUX2_LUT6:%d, MUX2_LUT5:%d\n", packed[0], packed[1], packed[2],
                 packed[3]);
    }

    // ===================================
    // ALU
    // ===================================
    // create ALU CIN block
    std::unique_ptr<CellInfo> alu_add_cin_block(Context *ctx, CellInfo *head, NetInfo *cin_net)
    {
        std::string name = head->name.str(ctx) + "_HEAD_ALULC";
        IdString name_id = ctx->id(name);

        NetInfo *cout_net = ctx->createNet(name_id);
        head->disconnectPort(id_CIN);
        head->connectPort(id_CIN, cout_net);

        auto cin_ci = std::make_unique<CellInfo>(ctx, name_id, id_ALU);
        cin_ci->addOutput(id_COUT);
        cin_ci->connectPort(id_COUT, cout_net);

        if (cin_net->name == ctx->id("$PACKER_GND")) {
            cin_ci->setParam(id_ALU_MODE, std::string("C2L"));
            cin_ci->addInput(id_I2);
            cin_ci->connectPort(id_I2, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
            return cin_ci;
        }
        if (cin_net->name == ctx->id("$PACKER_VCC")) {
            cin_ci->setParam(id_ALU_MODE, std::string("ONE2C"));
            cin_ci->addInput(id_I2);
            cin_ci->connectPort(id_I2, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
            return cin_ci;
        }
        // CIN from logic
        cin_ci->addInput(id_I0);
        cin_ci->connectPort(id_I0, ctx->nets.at(ctx->id("$PACKER_GND")).get());
        cin_ci->addInput(id_I1);
        cin_ci->addInput(id_I3);
        cin_ci->connectPort(id_I1, cin_net);
        cin_ci->connectPort(id_I3, cin_net);
        cin_ci->addInput(id_I2);
        cin_ci->connectPort(id_I2, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
        cin_ci->setParam(id_ALU_MODE, std::string("0")); // ADD
        return cin_ci;
    }

    // create ALU COUT block
    std::unique_ptr<CellInfo> alu_add_cout_block(Context *ctx, CellInfo *tail, NetInfo *cout_net)
    {
        std::string name = tail->name.str(ctx) + "_TAIL_ALULC";
        IdString name_id = ctx->id(name);

        NetInfo *cin_net = ctx->createNet(name_id);
        tail->disconnectPort(id_COUT);
        tail->connectPort(id_COUT, cin_net);

        auto cout_ci = std::make_unique<CellInfo>(ctx, name_id, id_ALU);
        cout_ci->addOutput(id_COUT); // may be needed for the ALU filler
        cout_ci->addInput(id_CIN);
        cout_ci->connectPort(id_CIN, cin_net);
        cout_ci->addOutput(id_SUM);
        cout_ci->connectPort(id_SUM, cout_net);
        cout_ci->addInput(id_I2);
        cout_ci->connectPort(id_I2, ctx->nets.at(ctx->id("$PACKER_VCC")).get());

        cout_ci->setParam(id_ALU_MODE, std::string("C2L"));
        return cout_ci;
    }

    // create ALU filler block
    std::unique_ptr<CellInfo> alu_add_dummy_block(Context *ctx, CellInfo *tail)
    {
        std::string name = tail->name.str(ctx) + "_DUMMY_ALULC";
        IdString name_id = ctx->id(name);

        auto dummy_ci = std::make_unique<CellInfo>(ctx, name_id, id_ALU);
        dummy_ci->setParam(id_ALU_MODE, std::string("C2L"));
        return dummy_ci;
    }

    // create ALU chain
    void pack_alus(void)
    {
        const CellTypePort cell_alu_cout = CellTypePort(id_ALU, id_COUT);
        const CellTypePort cell_alu_cin = CellTypePort(id_ALU, id_CIN);
        std::vector<std::unique_ptr<CellInfo>> new_cells;

        log_info("Pack ALUs...\n");
        for (auto &cell : ctx->cells) {
            auto ci = cell.second.get();
            if (ci->cluster != ClusterId()) {
                continue;
            }
            if (is_alu(ci)) {
                // The ALU head is when the input carry is not a dedicated wire from the previous ALU
                NetInfo *cin_net = ci->getPort(id_CIN);
                if (!cin_net || !cin_net->driver.cell) {
                    log_error("CIN disconnected at ALU:%s\n", ctx->nameOf(ci));
                }
                if (CellTypePort(cin_net->driver) != cell_alu_cout || cin_net->users.entries() > 1) {
                    if (ctx->debug) {
                        log_info("ALU head found %s. CIN net is %s\n", ctx->nameOf(ci), ctx->nameOf(cin_net));
                    }
                    // always prepend first ALU with carry generator block
                    // three cases: CIN == 0, CIN == 1 and CIN == ?
                    new_cells.push_back(std::move(alu_add_cin_block(ctx, ci, cin_net)));
                    CellInfo *cin_block_ci = new_cells.back().get();
                    // CIN block is the cluster root and is always placed in ALU0
                    // This is a possible place for further optimization
                    cin_block_ci->cluster = cin_block_ci->name;
                    cin_block_ci->constr_z = BelZ::ALU0_Z;
                    cin_block_ci->constr_abs_z = true;

                    int alu_chain_len = 1;
                    while (true) {
                        // add to cluster
                        if (ctx->debug) {
                            log_info("Add ALU to the chain (len:%d): %s\n", alu_chain_len, ctx->nameOf(ci));
                        }
                        cin_block_ci->constr_children.push_back(ci);
                        NPNR_ASSERT(ci->cluster == ClusterId());
                        ci->cluster = cin_block_ci->name;
                        ci->constr_abs_z = false;
                        ci->constr_x = alu_chain_len / 6;
                        ci->constr_y = 0;
                        ci->constr_z = alu_chain_len % 6;
                        // XXX mode 0 - ADD
                        if (ci->params.at(id_ALU_MODE).as_int64() == 0) {
                            ci->renamePort(id_I3, id_I2);
                            ci->renamePort(id_I0, id_I3);
                            ci->renamePort(id_I2, id_I0);
                        }
                        // XXX I2 is pin C which must be set to 1 for all ALU modes except MUL
                        // we use only mode 2 ADDSUB so create and connect this pin
                        ci->addInput(id_I2);
                        ci->connectPort(id_I2, ctx->nets.at(ctx->id("$PACKER_VCC")).get());

                        ++alu_chain_len;

                        // check for the chain end
                        NetInfo *cout_net = ci->getPort(id_COUT);
                        if (!cout_net || cout_net->users.empty()) {
                            break;
                        }
                        if (CellTypePort(*cout_net->users.begin()) != cell_alu_cin || cout_net->users.entries() > 1) {
                            new_cells.push_back(std::move(alu_add_cout_block(ctx, ci, cout_net)));
                            CellInfo *cout_block_ci = new_cells.back().get();
                            cin_block_ci->constr_children.push_back(cout_block_ci);
                            NPNR_ASSERT(cout_block_ci->cluster == ClusterId());
                            cout_block_ci->cluster = cin_block_ci->name;
                            cout_block_ci->constr_abs_z = false;
                            cout_block_ci->constr_x = alu_chain_len / 6;
                            cout_block_ci->constr_y = 0;
                            cout_block_ci->constr_z = alu_chain_len % 6;
                            if (ctx->debug) {
                                log_info("Add ALU carry out to the chain (len:%d): %s COUT-net: %s\n", alu_chain_len,
                                         ctx->nameOf(cout_block_ci), ctx->nameOf(cout_net));
                            }

                            ++alu_chain_len;

                            break;
                        }
                        ci = (*cout_net->users.begin()).cell;
                    }
                    // ALUs are always paired
                    if (alu_chain_len & 1) {
                        // create dummy cell
                        new_cells.push_back(std::move(alu_add_dummy_block(ctx, ci)));
                        CellInfo *dummy_block_ci = new_cells.back().get();
                        cin_block_ci->constr_children.push_back(dummy_block_ci);
                        NPNR_ASSERT(dummy_block_ci->cluster == ClusterId());
                        dummy_block_ci->cluster = cin_block_ci->name;
                        dummy_block_ci->constr_abs_z = false;
                        dummy_block_ci->constr_x = alu_chain_len / 6;
                        dummy_block_ci->constr_y = 0;
                        dummy_block_ci->constr_z = alu_chain_len % 6;
                        if (ctx->debug) {
                            log_info("Add ALU dummy cell to the chain (len:%d): %s\n", alu_chain_len,
                                     ctx->nameOf(dummy_block_ci));
                        }
                    }
                }
            }
        }
        for (auto &ncell : new_cells) {
            ctx->cells[ncell->name] = std::move(ncell);
        }
    }

    // ===================================
    // glue LUT and FF
    // ===================================
    void constrain_lutffs(void)
    {
        // Constrain directly connected LUTs and FFs together to use dedicated resources
        const pool<CellTypePort> lut_outs{{id_LUT1, id_F}, {id_LUT2, id_F}, {id_LUT3, id_F}, {id_LUT4, id_F}};
        const pool<CellTypePort> dff_ins{{id_DFF, id_D},  {id_DFFE, id_D},  {id_DFFN, id_D},  {id_DFFNE, id_D},
                                         {id_DFFS, id_D}, {id_DFFSE, id_D}, {id_DFFNS, id_D}, {id_DFFNSE, id_D},
                                         {id_DFFR, id_D}, {id_DFFRE, id_D}, {id_DFFNR, id_D}, {id_DFFNRE, id_D},
                                         {id_DFFP, id_D}, {id_DFFPE, id_D}, {id_DFFNP, id_D}, {id_DFFNPE, id_D},
                                         {id_DFFC, id_D}, {id_DFFCE, id_D}, {id_DFFNC, id_D}, {id_DFFNCE, id_D}};

        int lutffs = h.constrain_cell_pairs(lut_outs, dff_ins, 1, 1);
        log_info("Constrained %d LUTFF pairs.\n", lutffs);
    }

    // ===================================
    // SSRAM cluster
    // ===================================
    std::unique_ptr<CellInfo> ssram_make_lut(Context *ctx, CellInfo *ci, int index)
    {
        IdString name_id = ctx->idf("%s_LUT%d", ci->name.c_str(ctx), index);
        auto lut_ci = std::make_unique<CellInfo>(ctx, name_id, id_LUT4);
        if (index) {
            for (IdString port : {id_I0, id_I1, id_I2, id_I3}) {
                lut_ci->addInput(port);
            }
        }
        IdString init_name = ctx->idf("INIT_%d", index);
        if (ci->params.count(init_name)) {
            lut_ci->setParam(id_INIT, ci->params.at(init_name));
        } else {
            lut_ci->setParam(id_INIT, std::string("1111111111111111"));
        }
        return lut_ci;
    }

    void pack_ram16sdp4(void)
    {
        std::vector<std::unique_ptr<CellInfo>> new_cells;

        log_info("Pack RAMs...\n");
        for (auto &cell : ctx->cells) {
            auto ci = cell.second.get();
            if (ci->cluster != ClusterId()) {
                continue;
            }

            if (is_ssram(ci)) {
                // make cluster root
                ci->cluster = ci->name;
                ci->constr_abs_z = true;
                ci->constr_x = 0;
                ci->constr_y = 0;
                ci->constr_z = BelZ::RAMW_Z;

                ci->addInput(id_CE);
                ci->connectPort(id_CE, ctx->nets.at(ctx->id("$PACKER_VCC")).get());

                // RAD networks
                NetInfo *rad[4];
                for (int i = 0; i < 4; ++i) {
                    rad[i] = ci->getPort(ctx->idf("RAD[%d]", i));
                }

                // active LUTs
                int luts_num = 4;
                if (ci->type == id_RAM16SDP1) {
                    luts_num = 1;
                } else {
                    if (ci->type == id_RAM16SDP2) {
                        luts_num = 2;
                    }
                }

                // make actual storage cells
                for (int i = 0; i < 4; ++i) {
                    new_cells.push_back(std::move(ssram_make_lut(ctx, ci, i)));
                    CellInfo *lut_ci = new_cells.back().get();
                    ci->constr_children.push_back(lut_ci);
                    lut_ci->cluster = ci->name;
                    lut_ci->constr_abs_z = true;
                    lut_ci->constr_x = 0;
                    lut_ci->constr_y = 0;
                    lut_ci->constr_z = i * 2;
                    // inputs
                    // LUT0 is already connected when generating the base
                    if (i && i < luts_num) {
                        for (int j = 0; j < 4; ++j) {
                            lut_ci->connectPort(ctx->idf("I%d", j), rad[j]);
                        }
                    }
                }
            }
        }
        for (auto &ncell : new_cells) {
            ctx->cells[ncell->name] = std::move(ncell);
        }
    }

    // ===================================
    // Block RAM
    // ===================================
    void bsram_rename_ports(CellInfo *ci, int bit_width, char const *from, char const *to, int offset = 0)
    {
        int num = (bit_width == 9 || bit_width == 18 || bit_width == 36) ? 36 : 32;
        for (int i = 0, j = offset; i < num; ++i, ++j) {
            if (((i + 1) % 9) == 0 && (bit_width == 16 || bit_width == 32)) {
                ++j;
            }
            ci->renamePort(ctx->idf(from, i), ctx->idf(to, offset ? j % 36 : j));
        }
    }

    // We solve the BLKSEL problems that are observed on some chips by
    // connecting the BLKSEL ports to constant networks so that this BSRAM will
    // be selected, the actual selection is made by manipulating the Clock
    // Enable pin using a LUT-based decoder.
    void bsram_fix_blksel(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
    {
        // is BSRAM enabled
        NetInfo *ce_net = ci->getPort(id_CE);
        if (ce_net == nullptr || ce_net->name == ctx->id("$PACKER_GND")) {
            return;
        }

        // port name, BLK_SEL parameter for this port
        std::vector<std::pair<IdString, int>> dyn_blksel;

        int blk_sel_parameter = ci->params.at(id_BLK_SEL).as_int64();
        for (int i = 0; i < 3; ++i) {
            IdString pin_name = ctx->idf("BLKSEL[%d]", i);
            NetInfo *net = ci->getPort(pin_name);
            if (net == nullptr || net->name == ctx->id("$PACKER_GND") || net->name == ctx->id("$PACKER_VCC")) {
                continue;
            }
            dyn_blksel.push_back(std::make_pair(pin_name, (blk_sel_parameter >> i) & 1));
        }

        if (dyn_blksel.empty()) {
            return;
        }

        if (ctx->verbose) {
            log_info("  apply the BSRAM BLKSEL fix\n");
        }

        // Make a decoder
        auto lut_cell = gwu.create_cell(gwu.create_aux_name(ci->name, 0, "_blksel_lut$"), id_LUT4);
        CellInfo *lut = lut_cell.get();
        lut->addInput(id_I3);
        ci->movePortTo(id_CE, lut, id_I3);
        lut->addOutput(id_F);
        ci->connectPorts(id_CE, lut, id_F);

        NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
        NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();

        // Connected CE to I3 to make it easy to calculate the decoder
        int init = 0x100; // CE == 0 -->  F = 0
                          // CE == 1 -->  F = decoder result
        int idx = 0;
        for (auto &port : dyn_blksel) {
            IdString lut_input_name = ctx->idf("I%d", idx);
            ci->movePortTo(port.first, lut, lut_input_name);
            if (port.second) {
                init <<= (1 << idx);
                ci->connectPort(port.first, vcc_net);
            } else {
                ci->connectPort(port.first, vss_net);
            }
            ++idx;
        }
        lut->setParam(id_INIT, init);

        new_cells.push_back(std::move(lut_cell));
    }

    // Some chips cannot, for some reason, use internal BSRAM registers to
    // implement READ_MODE=1'b1 (pipeline) with a word width other than 32 or
    // 36 bits.
    // We work around this by adding an external DFF and using BSRAM
    // as READ_MODE=1'b0 (bypass).
    void bsram_fix_outreg(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
    {
        int bit_width = ci->params.at(id_BIT_WIDTH).as_int64();
        if (bit_width == 32 || bit_width == 36) {
            return;
        }
        int read_mode = ci->params.at(id_READ_MODE).as_int64();
        if (read_mode == 0) {
            return;
        }
        NetInfo *ce_net = ci->getPort(id_CE);
        NetInfo *oce_net = ci->getPort(id_OCE);
        if (ce_net == nullptr || oce_net == nullptr) {
            return;
        }
        if (ce_net->name == ctx->id("$PACKER_GND") || oce_net->name == ctx->id("$PACKER_GND")) {
            return;
        }

        if (ctx->verbose) {
            log_info("  apply the BSRAM OUTREG fix\n");
        }
        ci->setParam(id_READ_MODE, 0);
        ci->disconnectPort(id_OCE);
        ci->connectPort(id_OCE, ce_net);

        NetInfo *reset_net = ci->getPort(id_RESET);
        bool sync_reset = ci->params.at(id_RESET_MODE).as_string() == std::string("SYNC");
        IdString dff_type = sync_reset ? id_DFFRE : id_DFFCE;
        IdString reset_port = sync_reset ? id_RESET : id_CLEAR;

        for (int i = 0; i < bit_width; ++i) {
            IdString do_name = ctx->idf("DO[%d]", i);
            const NetInfo *net = ci->getPort(do_name);
            if (net != nullptr) {
                if (net->users.empty()) {
                    ci->disconnectPort(do_name);
                    continue;
                }

                // create DFF
                auto cache_dff_cell = gwu.create_cell(gwu.create_aux_name(ci->name, i, "_cache_dff$"), dff_type);
                CellInfo *cache_dff = cache_dff_cell.get();
                cache_dff->addInput(id_CE);
                cache_dff->connectPort(id_CE, oce_net);

                cache_dff->addInput(reset_port);
                cache_dff->connectPort(reset_port, reset_net);

                ci->copyPortTo(id_CLK, cache_dff, id_CLK);

                cache_dff->addOutput(id_Q);
                ci->movePortTo(do_name, cache_dff, id_Q);

                cache_dff->addInput(id_D);
                ci->connectPorts(do_name, cache_dff, id_D);

                new_cells.push_back(std::move(cache_dff_cell));
            }
        }
    }

    // Analysis of the images generated by the IDE showed that some components
    // are being added at the input and output of the BSRAM.  Two LUTs are
    // added on the WRE and CE inputs (strangely, OCE is not affected), a pair
    // of LUT-DFFs on each DO output, and one or two flipflops of different
    // types in the auxiliary network.
    // The semantics of these additions are unclear, but we can replicate this behavior.
    //  Fix BSRAM in single port mode.
    void bsram_fix_sp(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
    {
        int bit_width = ci->params.at(id_BIT_WIDTH).as_int64();

        if (ctx->verbose) {
            log_info("  apply the SP fix\n");
        }
        // create WRE LUT
        auto wre_lut_cell = gwu.create_cell(gwu.create_aux_name(ci->name, 0, "_wre_lut$"), id_LUT4);
        CellInfo *wre_lut = wre_lut_cell.get();
        wre_lut->setParam(id_INIT, 0x8888);
        ci->movePortTo(id_CE, wre_lut, id_I0);
        ci->movePortTo(id_WRE, wre_lut, id_I1);
        wre_lut->addOutput(id_F);
        ci->connectPorts(id_WRE, wre_lut, id_F);

        // create CE LUT
        auto ce_lut_cell = gwu.create_cell(gwu.create_aux_name(ci->name, 0, "_ce_lut$"), id_LUT4);
        CellInfo *ce_lut = ce_lut_cell.get();
        ce_lut->setParam(id_INIT, 0xeeee);
        wre_lut->copyPortTo(id_I0, ce_lut, id_I0);
        wre_lut->copyPortTo(id_I1, ce_lut, id_I1);
        ce_lut->addOutput(id_F);
        ci->connectPorts(id_CE, ce_lut, id_F);

        // create ce reg
        int write_mode = ci->params.at(id_WRITE_MODE).as_int64();
        IdString dff_type = write_mode ? id_DFF : id_DFFR;
        auto ce_pre_dff_cell = gwu.create_cell(gwu.create_aux_name(ci->name, 0, "_ce_pre_dff$"), dff_type);
        CellInfo *ce_pre_dff = ce_pre_dff_cell.get();
        ce_pre_dff->addInput(id_D);
        ce_lut->copyPortTo(id_I0, ce_pre_dff, id_D);
        ci->copyPortTo(id_CLK, ce_pre_dff, id_CLK);
        if (dff_type == id_DFFR) {
            wre_lut->copyPortTo(id_I1, ce_pre_dff, id_RESET);
        }
        ce_pre_dff->addOutput(id_Q);

        // new ce src with Q pin (used by output pins, not by BSRAM itself)
        CellInfo *new_ce_net_src = ce_pre_dff;

        // add delay register in pipeline mode
        int read_mode = ci->params.at(id_READ_MODE).as_int64();
        if (read_mode) {
            auto ce_pipe_dff_cell = gwu.create_cell(gwu.create_aux_name(ci->name, 0, "_ce_pipe_dff$"), id_DFF);
            new_cells.push_back(std::move(ce_pipe_dff_cell));
            CellInfo *ce_pipe_dff = new_cells.back().get();
            ce_pipe_dff->addInput(id_D);
            new_ce_net_src->connectPorts(id_Q, ce_pipe_dff, id_D);
            ci->copyPortTo(id_CLK, ce_pipe_dff, id_CLK);
            ce_pipe_dff->addOutput(id_Q);
            new_ce_net_src = ce_pipe_dff;
        }

        // used outputs of the BSRAM convert to cached
        for (int i = 0; i < bit_width; ++i) {
            IdString do_name = ctx->idf("DO[%d]", i);
            const NetInfo *net = ci->getPort(do_name);
            if (net != nullptr) {
                if (net->users.empty()) {
                    ci->disconnectPort(do_name);
                    continue;
                }
                // create cache lut
                auto cache_lut_cell = gwu.create_cell(gwu.create_aux_name(ci->name, i, "_cache_lut$"), id_LUT4);
                CellInfo *cache_lut = cache_lut_cell.get();
                cache_lut->setParam(id_INIT, 0xcaca);
                cache_lut->addInput(id_I0);
                cache_lut->addInput(id_I1);
                cache_lut->addInput(id_I2);
                ci->movePortTo(do_name, cache_lut, id_F);
                ci->connectPorts(do_name, cache_lut, id_I1);
                new_ce_net_src->connectPorts(id_Q, cache_lut, id_I2);

                // create cache DFF
                auto cache_dff_cell = gwu.create_cell(gwu.create_aux_name(ci->name, i, "_cache_dff$"), id_DFFE);
                CellInfo *cache_dff = cache_dff_cell.get();
                cache_dff->addInput(id_CE);
                cache_dff->addInput(id_D);
                ci->copyPortTo(id_CLK, cache_dff, id_CLK);
                new_ce_net_src->connectPorts(id_Q, cache_dff, id_CE);
                cache_lut->copyPortTo(id_I1, cache_dff, id_D);
                cache_dff->addOutput(id_Q);
                cache_dff->connectPorts(id_Q, cache_lut, id_I0);

                new_cells.push_back(std::move(cache_lut_cell));
                new_cells.push_back(std::move(cache_dff_cell));
            }
        }

        new_cells.push_back(std::move(wre_lut_cell));
        new_cells.push_back(std::move(ce_lut_cell));
        new_cells.push_back(std::move(ce_pre_dff_cell));
    }

    void pack_ROM(CellInfo *ci)
    {
        int default_bw = 32;
        // XXX use block 111
        ci->setParam(ctx->id("BLK_SEL"), Property(7, 32));
        if (ci->type == id_pROM) {
            ci->setAttr(id_BSRAM_SUBTYPE, Property(""));
        } else {
            ci->setAttr(id_BSRAM_SUBTYPE, Property("X9"));
            default_bw = 36;
        }

        NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
        NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();
        for (int i = 0; i < 3; ++i) {
            IdString port = ctx->idf("BLKSEL%d", i);
            ci->addInput(port);
            ci->connectPort(port, vcc_net);
            port = ctx->idf("BLKSELB%d", i);
            ci->addInput(port);
            ci->connectPort(port, vcc_net);
        }

        ci->addInput(id_WRE);
        ci->connectPort(id_WRE, vss_net);
        ci->addInput(id_WREB);
        ci->connectPort(id_WREB, vss_net);

        if (!ci->params.count(id_BIT_WIDTH)) {
            ci->setParam(id_BIT_WIDTH, Property(default_bw, 32));
        }

        int bit_width = ci->params.at(id_BIT_WIDTH).as_int64();
        if (bit_width == 32 || bit_width == 36) {
            ci->copyPortTo(id_CLK, ci, id_CLKB);
            ci->copyPortTo(id_CE, ci, id_CEB);
            ci->copyPortTo(id_OCE, ci, id_OCEB);
            ci->copyPortTo(id_RESET, ci, id_RESETB);

            for (int i = 0; i < 14; ++i) {
                ci->renamePort(ctx->idf("AD[%d]", i), ctx->idf("ADA%d", i));
                ci->copyPortTo(ctx->idf("ADA%d", i), ci, ctx->idf("ADB%d", i));
            }
            bsram_rename_ports(ci, bit_width, "DO[%d]", "DO%d");
        } else {
            // use port B
            ci->renamePort(id_CLK, id_CLKB);
            ci->renamePort(id_OCE, id_OCEB);
            ci->renamePort(id_CE, id_CEB);
            ci->renamePort(id_RESET, id_RESETB);

            ci->addInput(id_CEA);
            ci->connectPort(id_CEA, vss_net);
            for (int i = 0; i < 14; ++i) {
                ci->renamePort(ctx->idf("AD[%d]", i), ctx->idf("ADB%d", i));
            }
            bsram_rename_ports(ci, bit_width, "DO[%d]", "DO%d", 18);
        }
    }

    void pack_SDPB(CellInfo *ci)
    {
        int default_bw = 32;
        if (ci->type == id_SDPB) {
            ci->setAttr(id_BSRAM_SUBTYPE, Property(""));
        } else {
            ci->setAttr(id_BSRAM_SUBTYPE, Property("X9"));
            default_bw = 36;
        }

        NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
        NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();

        for (int i = 0; i < 14; ++i) {
            ci->renamePort(ctx->idf("ADA[%d]", i), ctx->idf("ADA%d", i));
            ci->renamePort(ctx->idf("ADB[%d]", i), ctx->idf("ADB%d", i));
        }

        for (int i = 0; i < 3; ++i) {
            ci->renamePort(ctx->idf("BLKSELA[%d]", i), ctx->idf("BLKSELA%d", i));
            ci->renamePort(ctx->idf("BLKSELB[%d]", i), ctx->idf("BLKSELB%d", i));
        }

        ci->copyPortTo(id_OCE, ci, id_OCEB);

        // Port A
        ci->addInput(id_WRE);
        ci->connectPort(id_WRE, vcc_net);

        if (!ci->params.count(id_BIT_WIDTH_0)) {
            ci->setParam(id_BIT_WIDTH_0, Property(default_bw, 32));
        }

        int bit_width = ci->params.at(id_BIT_WIDTH_0).as_int64();
        bsram_rename_ports(ci, bit_width, "DI[%d]", "DI%d");

        // Port B
        ci->addInput(id_WREB);
        if (!ci->params.count(id_BIT_WIDTH_1)) {
            ci->setParam(id_BIT_WIDTH_1, Property(default_bw, 32));
        }
        bit_width = ci->params.at(id_BIT_WIDTH_1).as_int64();
        if (bit_width == 32 || bit_width == 36) {
            ci->connectPort(id_WREB, vcc_net);
            bsram_rename_ports(ci, bit_width, "DO[%d]", "DO%d");
        } else {
            ci->connectPort(id_WREB, vss_net);
            bsram_rename_ports(ci, bit_width, "DO[%d]", "DO%d", 18);
        }
    }

    void pack_DPB(CellInfo *ci)
    {
        int default_bw = 16;
        if (ci->type == id_DPB) {
            ci->setAttr(id_BSRAM_SUBTYPE, Property(""));
        } else {
            ci->setAttr(id_BSRAM_SUBTYPE, Property("X9"));
            default_bw = 18;
        }

        for (int i = 0; i < 14; ++i) {
            ci->renamePort(ctx->idf("ADA[%d]", i), ctx->idf("ADA%d", i));
            ci->renamePort(ctx->idf("ADB[%d]", i), ctx->idf("ADB%d", i));
        }

        for (int i = 0; i < 3; ++i) {
            ci->renamePort(ctx->idf("BLKSELA[%d]", i), ctx->idf("BLKSELA%d", i));
            ci->renamePort(ctx->idf("BLKSELB[%d]", i), ctx->idf("BLKSELB%d", i));
        }

        if (!ci->params.count(id_BIT_WIDTH_0)) {
            ci->setParam(id_BIT_WIDTH_0, Property(default_bw, 32));
        }
        int bit_width = ci->params.at(id_BIT_WIDTH_0).as_int64();
        bsram_rename_ports(ci, bit_width, "DIA[%d]", "DIA%d");
        bsram_rename_ports(ci, bit_width, "DOA[%d]", "DOA%d");

        if (!ci->params.count(id_BIT_WIDTH_1)) {
            ci->setParam(id_BIT_WIDTH_1, Property(default_bw, 32));
        }
        bit_width = ci->params.at(id_BIT_WIDTH_1).as_int64();
        bsram_rename_ports(ci, bit_width, "DIB[%d]", "DIB%d");
        bsram_rename_ports(ci, bit_width, "DOB[%d]", "DOB%d");
    }

    void divide_sp(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
    {
        int bw = ci->params.at(id_BIT_WIDTH).as_int64();
        NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
        NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();

        IdString cell_type = id_SP;
        IdString name = ctx->idf("%s_AUX", ctx->nameOf(ci));

        auto sp_cell = gwu.create_cell(name, cell_type);
        CellInfo *sp = sp_cell.get();

        ci->copyPortTo(id_CLK, sp, id_CLK);
        ci->copyPortTo(id_OCE, sp, id_OCE);
        ci->copyPortTo(id_CE, sp, id_CE);
        ci->copyPortTo(id_RESET, sp, id_RESET);
        ci->copyPortTo(id_WRE, sp, id_WRE);

        // XXX Separate "byte enable" port
        ci->movePortTo(ctx->id("AD[2]"), sp, ctx->id("AD0"));
        ci->movePortTo(ctx->id("AD[3]"), sp, ctx->id("AD1"));
        ci->connectPort(ctx->id("AD[2]"), vss_net);
        ci->connectPort(ctx->id("AD[3]"), vss_net);

        sp->addInput(ctx->id("AD2"));
        sp->connectPort(ctx->id("AD2"), vss_net);
        sp->addInput(ctx->id("AD3"));
        sp->connectPort(ctx->id("AD3"), vss_net);

        ci->disconnectPort(ctx->id("AD[4]"));
        ci->connectPort(ctx->id("AD[4]"), vss_net);
        sp->addInput(ctx->id("AD4"));
        sp->connectPort(ctx->id("AD4"), vcc_net);

        ci->copyPortBusTo(id_AD, 5, true, sp, id_AD, 5, false, 14 - 5 + 1);

        sp->params = ci->params;
        sp->setAttr(id_BSRAM_SUBTYPE, ci->attrs.at(id_BSRAM_SUBTYPE));

        if (bw == 32) {
            ci->setParam(id_BIT_WIDTH, Property(16, 32));
            sp->setParam(id_BIT_WIDTH, Property(16, 32));
            ci->movePortBusTo(id_DI, 16, true, sp, id_DI, 0, false, 16);
            ci->movePortBusTo(id_DO, 16, true, sp, id_DO, 0, false, 16);
        } else {
            ci->setParam(id_BIT_WIDTH, Property(18, 32));
            sp->setParam(id_BIT_WIDTH, Property(18, 32));
            ci->movePortBusTo(id_DI, 18, true, sp, id_DI, 0, false, 18);
            ci->movePortBusTo(id_DO, 18, true, sp, id_DO, 0, false, 18);
        }
        ci->copyPortBusTo(ctx->id("BLKSEL"), 0, true, sp, ctx->id("BLKSEL"), 0, false, 3);

        new_cells.push_back(std::move(sp_cell));
    }

    void pack_SP(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
    {
        int default_bw = 32;
        if (ci->type == id_SP) {
            ci->setAttr(id_BSRAM_SUBTYPE, Property(""));
        } else {
            ci->setAttr(id_BSRAM_SUBTYPE, Property("X9"));
            default_bw = 36;
        }
        if (!ci->params.count(id_BIT_WIDTH)) {
            ci->setParam(id_BIT_WIDTH, Property(default_bw, 32));
        }

        int bit_width = ci->params.at(id_BIT_WIDTH).as_int64();

        // XXX strange WRE<->CE relations
        // Gowin IDE adds two LUTs to the WRE and CE signals. The logic is
        // unclear, but without them effects occur. Perhaps this is a
        // correction of some BSRAM defects.
        if (gwu.need_SP_fix()) {
            bsram_fix_sp(ci, new_cells);
        }

        // Some chips have faulty output registers
        if (gwu.need_BSRAM_OUTREG_fix()) {
            bsram_fix_outreg(ci, new_cells);
        }

        // Some chips have problems with BLKSEL ports
        if (gwu.need_BLKSEL_fix()) {
            bsram_fix_blksel(ci, new_cells);
        }

        // XXX UG285-1.3.6_E Gowin BSRAM & SSRAM User Guide:
        // For GW1N-9/GW1NR-9/GW1NS-4 series, 32/36-bit SP/SPX9 is divided into two
        // SP/SPX9s, which occupy two BSRAMs.
        // So divide it here
        if ((bit_width == 32 || bit_width == 36) && !gwu.has_SP32()) {
            divide_sp(ci, new_cells);
            bit_width = ci->params.at(id_BIT_WIDTH).as_int64();
        }

        NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
        for (int i = 0; i < 3; ++i) {
            ci->renamePort(ctx->idf("BLKSEL[%d]", i), ctx->idf("BLKSEL%d", i));
            if (bit_width == 32 || bit_width == 36) {
                ci->copyPortTo(ctx->idf("BLKSEL%d", i), ci, ctx->idf("BLKSELB%d", i));
            }
        }

        for (int i = 0; i < 14; ++i) {
            ci->renamePort(ctx->idf("AD[%d]", i), ctx->idf("AD%d", i));
            if (bit_width == 32 || bit_width == 36) {
                ci->copyPortTo(ctx->idf("AD%d", i), ci, ctx->idf("ADB%d", i));
            }
        }
        if (bit_width == 32 || bit_width == 36) {
            ci->copyPortTo(id_CLK, ci, id_CLKB);
            ci->copyPortTo(id_OCE, ci, id_OCEB);
            ci->copyPortTo(id_CE, ci, id_CEB);
            ci->copyPortTo(id_RESET, ci, id_RESETB);
            ci->copyPortTo(id_WRE, ci, id_WREB);
            ci->disconnectPort(ctx->id("ADB4"));
            ci->connectPort(ctx->id("ADB4"), vcc_net);
        }
        bsram_rename_ports(ci, bit_width, "DI[%d]", "DI%d");
        bsram_rename_ports(ci, bit_width, "DO[%d]", "DO%d");
    }

    void pack_bsram(void)
    {
        std::vector<std::unique_ptr<CellInfo>> new_cells;
        log_info("Pack BSRAMs...\n");

        for (auto &cell : ctx->cells) {
            auto ci = cell.second.get();
            if (is_bsram(ci)) {
                if (ctx->verbose) {
                    log_info(" pack %s\n", ci->type.c_str(ctx));
                }
                switch (ci->type.hash()) {
                case ID_pROMX9: /* fallthrough */
                case ID_pROM:
                    pack_ROM(ci);
                    ci->type = id_ROM;
                    break;
                case ID_SDPX9B: /* fallthrough */
                case ID_SDPB:
                    pack_SDPB(ci);
                    ci->type = id_SDP;
                    break;
                case ID_DPX9B: /* fallthrough */
                case ID_DPB:
                    pack_DPB(ci);
                    ci->type = id_DP;
                    break;
                case ID_SPX9: /* fallthrough */
                case ID_SP:
                    pack_SP(ci, new_cells);
                    ci->type = id_SP;
                    break;
                default:
                    log_error("Unsupported BSRAM type '%s'\n", ci->type.c_str(ctx));
                }
            }
        }

        for (auto &cell : new_cells) {
            ctx->cells[cell->name] = std::move(cell);
        }
    }

    // ===================================
    // DSP
    // ===================================
    void pass_net_type(CellInfo *ci, IdString port)
    {
        const NetInfo *net = ci->getPort(port);
        std::string connected_net = "NET";
        if (net != nullptr) {
            if (net->name == ctx->id("$PACKER_VCC")) {
                connected_net = "VCC";
            } else if (net->name == ctx->id("$PACKER_GND")) {
                connected_net = "GND";
            }
            ci->setAttr(ctx->idf("NET_%s", port.c_str(ctx)), connected_net);
        } else {
            ci->setAttr(ctx->idf("NET_%s", port.c_str(ctx)), std::string(""));
        }
    }

    void pack_dsp(void)
    {
        std::vector<std::unique_ptr<CellInfo>> new_cells;
        log_info("Pack DSP...\n");

        std::vector<CellInfo *> dsp_heads;

        for (auto &cell : ctx->cells) {
            auto ci = cell.second.get();
            if (is_dsp(ci)) {
                if (ctx->verbose) {
                    log_info(" pack %s %s\n", ci->type.c_str(ctx), ctx->nameOf(ci));
                }
                switch (ci->type.hash()) {
                case ID_PADD9: {
                    pass_net_type(ci, id_ASEL);
                    for (int i = 0; i < 9; ++i) {
                        ci->renamePort(ctx->idf("A[%d]", i), ctx->idf("A%d", i));
                        ci->renamePort(ctx->idf("B[%d]", i), ctx->idf("B%d", i));
                    }
                    for (int i = 0; i < 9; ++i) {
                        ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                    }

                    // ADD_SUB wire
                    IdString add_sub_net = ctx->id("$PACKER_GND");
                    if (ci->params.count(ctx->id("ADD_SUB"))) {
                        if (ci->params.at(ctx->id("ADD_SUB")).as_int64() == 1) {
                            add_sub_net = ctx->id("$PACKER_VCC");
                        }
                    }
                    ci->addInput(ctx->id("ADDSUB"));
                    ci->connectPort(ctx->id("ADDSUB"), ctx->nets.at(add_sub_net).get());

                    // PADD does not have outputs to the outside of the DSP -
                    // it is always connected to the inputs of the multiplier;
                    // to emulate a separate PADD primitive, we use
                    // multiplication by input C, equal to 1. We can switch the
                    // multiplier to multiplication mode by C in gowin_pack,
                    // but we will have to generate the value 1 at input C
                    // here.
                    ci->addInput(ctx->id("C0"));
                    ci->connectPort(ctx->id("C0"), ctx->nets.at(ctx->id("$PACKER_VCC")).get());
                    for (int i = 1; i < 9; ++i) {
                        ci->addInput(ctx->idf("C%d", i));
                        ci->connectPort(ctx->idf("C%d", i), ctx->nets.at(ctx->id("$PACKER_GND")).get());
                    }
                    // mark mult9x9 as used by making cluster
                    ci->cluster = ci->name;
                    ci->constr_abs_z = false;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    ci->constr_y = 0;

                    IdString mult_name = gwu.create_aux_name(ci->name);
                    std::unique_ptr<CellInfo> mult_cell = gwu.create_cell(mult_name, id_DUMMY_CELL);
                    new_cells.push_back(std::move(mult_cell));
                    CellInfo *mult_ci = new_cells.back().get();

                    mult_ci->cluster = ci->name;
                    mult_ci->constr_x = 0;
                    mult_ci->constr_y = 0;
                    mult_ci->constr_z = gwu.get_dsp_mult_from_padd(0);

                    // DSP head?
                    if (gwu.dsp_bus_src(ci, "SI", 9) == nullptr && gwu.dsp_bus_dst(ci, "SBO", 9) == nullptr) {
                        for (int i = 0; i < 9; ++i) {
                            ci->disconnectPort(ctx->idf("SI[%d]", i));
                            ci->disconnectPort(ctx->idf("SBO[%d]", i));
                        }
                        dsp_heads.push_back(ci);
                        if (ctx->verbose) {
                            log_info(" found a DSP head: %s\n", ctx->nameOf(ci));
                        }
                    }
                } break;
                case ID_PADD18: {
                    pass_net_type(ci, id_ASEL);
                    for (int i = 0; i < 18; ++i) {
                        ci->renamePort(ctx->idf("A[%d]", i), ctx->idf("A%d", i));
                        ci->renamePort(ctx->idf("B[%d]", i), ctx->idf("B%d", i));
                    }
                    for (int i = 0; i < 18; ++i) {
                        ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                    }

                    // ADD_SUB wire
                    IdString add_sub_net = ctx->id("$PACKER_GND");
                    if (ci->params.count(ctx->id("ADD_SUB"))) {
                        if (ci->params.at(ctx->id("ADD_SUB")).as_int64() == 1) {
                            add_sub_net = ctx->id("$PACKER_VCC");
                        }
                    }
                    ci->addInput(ctx->id("ADDSUB"));
                    ci->connectPort(ctx->id("ADDSUB"), ctx->nets.at(add_sub_net).get());

                    // XXX form C as 1
                    ci->addInput(ctx->id("C0"));
                    ci->connectPort(ctx->id("C0"), ctx->nets.at(ctx->id("$PACKER_VCC")).get());
                    for (int i = 1; i < 18; ++i) {
                        ci->addInput(ctx->idf("C%d", i));
                        ci->connectPort(ctx->idf("C%d", i), ctx->nets.at(ctx->id("$PACKER_GND")).get());
                    }
                    //
                    // add padd9s and mult9s as a children
                    ci->cluster = ci->name;
                    ci->constr_abs_z = false;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    ci->constr_z = 0;
                    ci->constr_children.clear();

                    for (int i = 0; i < 2; ++i) {
                        IdString padd_name = gwu.create_aux_name(ci->name, i * 2);
                        std::unique_ptr<CellInfo> padd_cell = gwu.create_cell(padd_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(padd_cell));
                        CellInfo *padd_ci = new_cells.back().get();

                        padd_ci->cluster = ci->name;
                        padd_ci->constr_abs_z = false;
                        padd_ci->constr_x = 0;
                        padd_ci->constr_y = 0;
                        padd_ci->constr_z = BelZ::PADD9_0_0_Z - BelZ::PADD18_0_0_Z + i;

                        IdString mult_name = gwu.create_aux_name(ci->name, i * 2 + 1);
                        std::unique_ptr<CellInfo> mult_cell = gwu.create_cell(mult_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(mult_cell));
                        CellInfo *mult_ci = new_cells.back().get();

                        mult_ci->cluster = ci->name;
                        mult_ci->constr_abs_z = false;
                        mult_ci->constr_x = 0;
                        mult_ci->constr_y = 0;
                        mult_ci->constr_z = BelZ::MULT9X9_0_0_Z - BelZ::PADD18_0_0_Z + i;
                    }
                    // DSP head?
                    if (gwu.dsp_bus_src(ci, "SI", 18) == nullptr && gwu.dsp_bus_dst(ci, "SBO", 18) == nullptr) {
                        for (int i = 0; i < 18; ++i) {
                            ci->disconnectPort(ctx->idf("SI[%d]", i));
                            ci->disconnectPort(ctx->idf("SBO[%d]", i));
                        }
                        dsp_heads.push_back(ci);
                        if (ctx->verbose) {
                            log_info(" found a DSP head: %s\n", ctx->nameOf(ci));
                        }
                    }
                } break;
                case ID_MULT9X9: {
                    pass_net_type(ci, id_ASEL);
                    pass_net_type(ci, id_BSEL);
                    for (int i = 0; i < 9; ++i) {
                        ci->renamePort(ctx->idf("A[%d]", i), ctx->idf("A%d", i));
                        ci->renamePort(ctx->idf("B[%d]", i), ctx->idf("B%d", i));
                    }
                    for (int i = 0; i < 18; ++i) {
                        ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                    }
                    // add padd9 as a child
                    ci->cluster = ci->name;
                    ci->constr_abs_z = false;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    ci->constr_z = 0;
                    ci->constr_children.clear();

                    IdString padd_name = gwu.create_aux_name(ci->name);
                    std::unique_ptr<CellInfo> padd_cell = gwu.create_cell(padd_name, id_DUMMY_CELL);
                    new_cells.push_back(std::move(padd_cell));
                    CellInfo *padd_ci = new_cells.back().get();

                    padd_ci->cluster = ci->name;
                    padd_ci->constr_abs_z = false;
                    padd_ci->constr_x = 0;
                    padd_ci->constr_y = 0;
                    padd_ci->constr_z = BelZ::PADD9_0_0_Z - BelZ::MULT9X9_0_0_Z;

                    // DSP head?
                    if (gwu.dsp_bus_src(ci, "SIA", 9) == nullptr && gwu.dsp_bus_src(ci, "SIB", 9) == nullptr) {
                        for (int i = 0; i < 9; ++i) {
                            ci->disconnectPort(ctx->idf("SIA[%d]", i));
                            ci->disconnectPort(ctx->idf("SIB[%d]", i));
                        }
                        dsp_heads.push_back(ci);
                        if (ctx->verbose) {
                            log_info(" found a DSP head: %s\n", ctx->nameOf(ci));
                        }
                    }
                } break;
                case ID_MULT18X18: {
                    pass_net_type(ci, id_ASEL);
                    pass_net_type(ci, id_BSEL);
                    for (int i = 0; i < 18; ++i) {
                        ci->renamePort(ctx->idf("A[%d]", i), ctx->idf("A%d", i));
                        ci->renamePort(ctx->idf("B[%d]", i), ctx->idf("B%d", i));
                    }
                    for (int i = 0; i < 36; ++i) {
                        ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                    }
                    // add padd9s and mult9s as a children
                    ci->cluster = ci->name;
                    ci->constr_abs_z = false;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    ci->constr_z = 0;
                    ci->constr_children.clear();

                    for (int i = 0; i < 2; ++i) {
                        IdString padd_name = gwu.create_aux_name(ci->name, i * 2);
                        std::unique_ptr<CellInfo> padd_cell = gwu.create_cell(padd_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(padd_cell));
                        CellInfo *padd_ci = new_cells.back().get();

                        padd_ci->cluster = ci->name;
                        padd_ci->constr_abs_z = false;
                        padd_ci->constr_x = 0;
                        padd_ci->constr_y = 0;
                        padd_ci->constr_z = BelZ::PADD9_0_0_Z - BelZ::MULT18X18_0_0_Z + i;

                        IdString mult_name = gwu.create_aux_name(ci->name, i * 2 + 1);
                        std::unique_ptr<CellInfo> mult_cell = gwu.create_cell(mult_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(mult_cell));
                        CellInfo *mult_ci = new_cells.back().get();

                        mult_ci->cluster = ci->name;
                        mult_ci->constr_abs_z = false;
                        mult_ci->constr_x = 0;
                        mult_ci->constr_y = 0;
                        mult_ci->constr_z = BelZ::MULT9X9_0_0_Z - BelZ::MULT18X18_0_0_Z + i;
                    }

                    // DSP head?
                    if (gwu.dsp_bus_src(ci, "SIA", 18) == nullptr && gwu.dsp_bus_src(ci, "SIB", 18) == nullptr) {
                        for (int i = 0; i < 18; ++i) {
                            ci->disconnectPort(ctx->idf("SIA[%d]", i));
                            ci->disconnectPort(ctx->idf("SIB[%d]", i));
                        }
                        dsp_heads.push_back(ci);
                        if (ctx->verbose) {
                            log_info(" found a DSP head: %s\n", ctx->nameOf(ci));
                        }
                    }
                } break;
                case ID_ALU54D: {
                    pass_net_type(ci, id_ACCLOAD);
                    for (int i = 0; i < 54; ++i) {
                        ci->renamePort(ctx->idf("A[%d]", i), ctx->idf("A%d", i));
                        ci->renamePort(ctx->idf("B[%d]", i), ctx->idf("B%d", i));
                    }
                    // ACCLOAD - It looks like these wires are always connected to each other.
                    ci->cell_bel_pins.at(id_ACCLOAD).clear();
                    ci->cell_bel_pins.at(id_ACCLOAD).push_back(id_ACCLOAD0);
                    ci->cell_bel_pins.at(id_ACCLOAD).push_back(id_ACCLOAD1);

                    for (int i = 0; i < 54; ++i) {
                        ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                    }
                    // add padd9s and mult9s as a children
                    ci->cluster = ci->name;
                    ci->constr_abs_z = false;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    ci->constr_z = 0;
                    ci->constr_children.clear();

                    for (int i = 0; i < 4; ++i) {
                        IdString padd_name = gwu.create_aux_name(ci->name, i * 2);
                        std::unique_ptr<CellInfo> padd_cell = gwu.create_cell(padd_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(padd_cell));
                        CellInfo *padd_ci = new_cells.back().get();

                        padd_ci->cluster = ci->name;
                        padd_ci->constr_abs_z = false;
                        padd_ci->constr_x = 0;
                        padd_ci->constr_y = 0;
                        padd_ci->constr_z = BelZ::PADD9_0_0_Z - BelZ::ALU54D_0_Z + 4 * (i / 2) + (i % 2);

                        IdString mult_name = gwu.create_aux_name(ci->name, i * 2 + 1);
                        std::unique_ptr<CellInfo> mult_cell = gwu.create_cell(mult_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(mult_cell));
                        CellInfo *mult_ci = new_cells.back().get();

                        mult_ci->cluster = ci->name;
                        mult_ci->constr_abs_z = false;
                        mult_ci->constr_x = 0;
                        mult_ci->constr_y = 0;
                        mult_ci->constr_z = BelZ::MULT9X9_0_0_Z - BelZ::ALU54D_0_Z + 4 * (i / 2) + (i % 2);
                    }

                    // DSP head?
                    if (gwu.dsp_bus_src(ci, "CASI", 55) == nullptr) {
                        for (int i = 0; i < 55; ++i) {
                            ci->disconnectPort(ctx->idf("CASI[%d]", i));
                        }
                        dsp_heads.push_back(ci);
                        if (ctx->verbose) {
                            log_info(" found a DSP head: %s\n", ctx->nameOf(ci));
                        }
                    }
                } break;
                case ID_MULTALU18X18: {
                    // Ports C and D conflict so we need to know the operating mode here.
                    if (ci->params.count(id_MULTALU18X18_MODE) == 0) {
                        ci->setParam(id_MULTALU18X18_MODE, 0);
                    }
                    int multalu18x18_mode = ci->params.at(id_MULTALU18X18_MODE).as_int64();
                    if (multalu18x18_mode < 0 || multalu18x18_mode > 2) {
                        log_error("%s MULTALU18X18_MODE is not in {0, 1, 2}.\n", ctx->nameOf(ci));
                    }
                    NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();

                    for (int i = 0; i < 54; ++i) {
                        if (i < 18) {
                            if (multalu18x18_mode != 2) {
                                ci->renamePort(ctx->idf("A[%d]", i), ctx->idf("A%d1", i));
                                ci->renamePort(ctx->idf("B[%d]", i), ctx->idf("B%d1", i));
                            } else {
                                ci->renamePort(ctx->idf("A[%d]", i), ctx->idf("A%d0", i));
                                ci->renamePort(ctx->idf("B[%d]", i), ctx->idf("B%d0", i));
                            }
                        }
                        switch (multalu18x18_mode) {
                        case 0:
                            ci->renamePort(ctx->idf("C[%d]", i), ctx->idf("C%d", i));
                            ci->disconnectPort(ctx->idf("D[%d]", i));
                            break;
                        case 1:
                            ci->disconnectPort(ctx->idf("C[%d]", i));
                            ci->disconnectPort(ctx->idf("D[%d]", i));
                            break;
                        case 2:
                            ci->disconnectPort(ctx->idf("C[%d]", i));
                            ci->renamePort(ctx->idf("D[%d]", i), ctx->idf("D%d", i));
                            break;
                        default:
                            break;
                        }
                    }
                    if (multalu18x18_mode != 2) {
                        ci->renamePort(id_ASIGN, id_ASIGN1);
                        ci->renamePort(id_BSIGN, id_BSIGN1);
                        ci->addInput(id_ASIGN0);
                        ci->addInput(id_BSIGN0);
                        ci->connectPort(id_ASIGN0, vss_net);
                        ci->connectPort(id_BSIGN0, vss_net);
                        ci->disconnectPort(id_DSIGN);
                    } else { // BSIGN0 and DSIGN are the same wire
                        ci->renamePort(id_ASIGN, id_ASIGN0);
                        ci->addInput(id_ASIGN1);
                        ci->connectPort(id_ASIGN1, vss_net);
                        ci->renamePort(id_BSIGN, id_BSIGN0);
                    }

                    // ACCLOAD - It looks like these wires are always connected to each other.
                    pass_net_type(ci, id_ACCLOAD);
                    ci->cell_bel_pins.at(id_ACCLOAD).clear();
                    ci->cell_bel_pins.at(id_ACCLOAD).push_back(id_ACCLOAD0);
                    ci->cell_bel_pins.at(id_ACCLOAD).push_back(id_ACCLOAD1);

                    for (int i = 0; i < 54; ++i) {
                        ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                    }

                    // add padd9s and mult9s as a children
                    ci->cluster = ci->name;
                    ci->constr_abs_z = false;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    ci->constr_z = 0;
                    ci->constr_children.clear();

                    for (int i = 0; i < 2; ++i) {
                        IdString padd_name = gwu.create_aux_name(ci->name, i * 2);
                        std::unique_ptr<CellInfo> padd_cell = gwu.create_cell(padd_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(padd_cell));
                        CellInfo *padd_ci = new_cells.back().get();

                        padd_ci->cluster = ci->name;
                        padd_ci->constr_abs_z = false;
                        padd_ci->constr_x = 0;
                        padd_ci->constr_y = 0;
                        padd_ci->constr_z = BelZ::PADD9_0_0_Z - BelZ::MULTALU18X18_0_Z + i;

                        IdString mult_name = gwu.create_aux_name(ci->name, i * 2 + 1);
                        std::unique_ptr<CellInfo> mult_cell = gwu.create_cell(mult_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(mult_cell));
                        CellInfo *mult_ci = new_cells.back().get();

                        mult_ci->cluster = ci->name;
                        mult_ci->constr_abs_z = false;
                        mult_ci->constr_x = 0;
                        mult_ci->constr_y = 0;
                        mult_ci->constr_z = BelZ::MULT9X9_0_0_Z - BelZ::MULTALU18X18_0_Z + i;
                    }

                    // DSP head?
                    if (gwu.dsp_bus_src(ci, "CASI", 55) == nullptr) {
                        for (int i = 0; i < 55; ++i) {
                            ci->disconnectPort(ctx->idf("CASI[%d]", i));
                        }
                        dsp_heads.push_back(ci);
                        if (ctx->verbose) {
                            log_info(" found a DSP head: %s\n", ctx->nameOf(ci));
                        }
                    }
                } break;
                case ID_MULTALU36X18: {
                    if (ci->params.count(id_MULTALU18X18_MODE) == 0) {
                        ci->setParam(id_MULTALU18X18_MODE, 0);
                    }
                    int multalu36x18_mode = ci->params.at(id_MULTALU36X18_MODE).as_int64();
                    if (multalu36x18_mode < 0 || multalu36x18_mode > 2) {
                        log_error("%s MULTALU36X18_MODE is not in {0, 1, 2}.\n", ctx->nameOf(ci));
                    }
                    NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();

                    for (int i = 0; i < 36; ++i) {
                        if (i < 18) {
                            ci->cell_bel_pins.at(ctx->idf("A[%d]", i)).clear();
                            ci->cell_bel_pins.at(ctx->idf("A[%d]", i)).push_back(ctx->idf("A%d0", i));
                            ci->cell_bel_pins.at(ctx->idf("A[%d]", i)).push_back(ctx->idf("A%d1", i));
                        }
                        ci->renamePort(ctx->idf("B[%d]", i), ctx->idf("B%d", i));
                    }
                    for (int i = 0; i < 54; ++i) {
                        switch (multalu36x18_mode) {
                        case 0:
                            ci->renamePort(ctx->idf("C[%d]", i), ctx->idf("C%d", i));
                            break;
                        case 1: /* fallthrough */
                        case 2:
                            ci->disconnectPort(ctx->idf("C[%d]", i));
                            break;
                        default:
                            break;
                        }
                    }

                    // both A have sign bit
                    // only MSB part of B has sign bit
                    ci->cell_bel_pins.at(id_ASIGN).clear();
                    ci->cell_bel_pins.at(id_ASIGN).push_back(id_ASIGN0);
                    ci->cell_bel_pins.at(id_ASIGN).push_back(id_ASIGN1);
                    ci->renamePort(id_BSIGN, id_BSIGN1);
                    ci->addInput(id_BSIGN0);
                    ci->connectPort(id_BSIGN0, vss_net);

                    pass_net_type(ci, id_ACCLOAD);
                    if (multalu36x18_mode == 1) {
                        if (ci->attrs.at(id_NET_ACCLOAD).as_string() == "GND" ||
                            ci->attrs.at(id_NET_ACCLOAD).as_string() == "VCC") {
                            ci->disconnectPort(id_ACCLOAD);
                        } else {
                            ci->addInput(id_ALUSEL4);
                            ci->addInput(id_ALUSEL6);
                            ci->cell_bel_pins.at(id_ACCLOAD).clear();
                            ci->cell_bel_pins.at(id_ACCLOAD).push_back(id_ALUSEL4);
                            ci->cell_bel_pins.at(id_ACCLOAD).push_back(id_ALUSEL6);
                        }
                    } else {
                        ci->disconnectPort(id_ACCLOAD);
                    }

                    for (int i = 0; i < 54; ++i) {
                        ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                    }

                    // add padd9s and mult9s as a children
                    ci->cluster = ci->name;
                    ci->constr_abs_z = false;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    ci->constr_z = 0;
                    ci->constr_children.clear();

                    for (int i = 0; i < 2; ++i) {
                        IdString padd_name = gwu.create_aux_name(ci->name, i * 2);
                        std::unique_ptr<CellInfo> padd_cell = gwu.create_cell(padd_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(padd_cell));
                        CellInfo *padd_ci = new_cells.back().get();

                        padd_ci->cluster = ci->name;
                        padd_ci->constr_abs_z = false;
                        padd_ci->constr_x = 0;
                        padd_ci->constr_y = 0;
                        padd_ci->constr_z = BelZ::PADD9_0_0_Z - BelZ::MULTALU36X18_0_Z + i;

                        IdString mult_name = gwu.create_aux_name(ci->name, i * 2 + 1);
                        std::unique_ptr<CellInfo> mult_cell = gwu.create_cell(mult_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(mult_cell));
                        CellInfo *mult_ci = new_cells.back().get();

                        mult_ci->cluster = ci->name;
                        mult_ci->constr_abs_z = false;
                        mult_ci->constr_x = 0;
                        mult_ci->constr_y = 0;
                        mult_ci->constr_z = BelZ::MULT9X9_0_0_Z - BelZ::MULTALU36X18_0_Z + i;
                    }

                    // DSP head?
                    if (gwu.dsp_bus_src(ci, "CASI", 55) == nullptr) {
                        for (int i = 0; i < 55; ++i) {
                            ci->disconnectPort(ctx->idf("CASI[%d]", i));
                        }
                        dsp_heads.push_back(ci);
                        if (ctx->verbose) {
                            log_info(" found a DSP head: %s\n", ctx->nameOf(ci));
                        }
                    }
                } break;
                case ID_MULTADDALU18X18: {
                    if (ci->params.count(id_MULTADDALU18X18_MODE) == 0) {
                        ci->setParam(id_MULTADDALU18X18_MODE, 0);
                    }
                    int multaddalu18x18_mode = ci->params.at(id_MULTADDALU18X18_MODE).as_int64();
                    if (multaddalu18x18_mode < 0 || multaddalu18x18_mode > 2) {
                        log_error("%s MULTADDALU18X18_MODE is not in {0, 1, 2}.\n", ctx->nameOf(ci));
                    }
                    for (int i = 0; i < 54; ++i) {
                        if (i < 18) {
                            ci->renamePort(ctx->idf("A0[%d]", i), ctx->idf("A%d0", i));
                            ci->renamePort(ctx->idf("B0[%d]", i), ctx->idf("B%d0", i));
                            ci->renamePort(ctx->idf("A1[%d]", i), ctx->idf("A%d1", i));
                            ci->renamePort(ctx->idf("B1[%d]", i), ctx->idf("B%d1", i));
                        }
                        if (multaddalu18x18_mode == 0) {
                            ci->renamePort(ctx->idf("C[%d]", i), ctx->idf("C%d", i));
                        } else {
                            ci->disconnectPort(ctx->idf("C[%d]", i));
                        }
                    }
                    for (int i = 0; i < 2; ++i) {
                        ci->renamePort(ctx->idf("ASIGN[%d]", i), ctx->idf("ASIGN%d", i));
                        ci->renamePort(ctx->idf("BSIGN[%d]", i), ctx->idf("BSIGN%d", i));
                        ci->renamePort(ctx->idf("ASEL[%d]", i), ctx->idf("ASEL%d", i));
                        ci->renamePort(ctx->idf("BSEL[%d]", i), ctx->idf("BSEL%d", i));
                    }

                    pass_net_type(ci, id_ASEL0);
                    pass_net_type(ci, id_ASEL1);
                    pass_net_type(ci, id_BSEL0);
                    pass_net_type(ci, id_BSEL1);
                    pass_net_type(ci, id_ACCLOAD);
                    if (multaddalu18x18_mode == 1) {
                        if (ci->attrs.at(id_NET_ACCLOAD).as_string() == "GND" ||
                            ci->attrs.at(id_NET_ACCLOAD).as_string() == "VCC") {
                            ci->disconnectPort(id_ACCLOAD);
                        } else {
                            ci->addInput(id_ALUSEL4);
                            ci->addInput(id_ALUSEL6);
                            ci->cell_bel_pins.at(id_ACCLOAD).clear();
                            ci->cell_bel_pins.at(id_ACCLOAD).push_back(id_ALUSEL4);
                            ci->cell_bel_pins.at(id_ACCLOAD).push_back(id_ALUSEL6);
                        }
                    } else {
                        ci->disconnectPort(id_ACCLOAD);
                    }

                    for (int i = 0; i < 54; ++i) {
                        ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                    }

                    // add padd9s and mult9s as a children
                    ci->cluster = ci->name;
                    ci->constr_abs_z = false;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    ci->constr_z = 0;
                    ci->constr_children.clear();

                    for (int i = 0; i < 2; ++i) {
                        IdString padd_name = gwu.create_aux_name(ci->name, i * 2);
                        std::unique_ptr<CellInfo> padd_cell = gwu.create_cell(padd_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(padd_cell));
                        CellInfo *padd_ci = new_cells.back().get();

                        padd_ci->cluster = ci->name;
                        padd_ci->constr_abs_z = false;
                        padd_ci->constr_x = 0;
                        padd_ci->constr_y = 0;
                        padd_ci->constr_z = BelZ::PADD9_0_0_Z - BelZ::MULTADDALU18X18_0_Z + i;

                        IdString mult_name = gwu.create_aux_name(ci->name, i * 2 + 1);
                        std::unique_ptr<CellInfo> mult_cell = gwu.create_cell(mult_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(mult_cell));
                        CellInfo *mult_ci = new_cells.back().get();

                        mult_ci->cluster = ci->name;
                        mult_ci->constr_abs_z = false;
                        mult_ci->constr_x = 0;
                        mult_ci->constr_y = 0;
                        mult_ci->constr_z = BelZ::MULT9X9_0_0_Z - BelZ::MULTADDALU18X18_0_Z + i;
                    }

                    // DSP head? This primitive has the ability to form chains using both SO[AB] -> SI[AB] and
                    // CASO->CASI
                    bool cas_head = false;
                    if (gwu.dsp_bus_src(ci, "CASI", 55) == nullptr) {
                        for (int i = 0; i < 55; ++i) {
                            ci->disconnectPort(ctx->idf("CASI[%d]", i));
                        }
                        cas_head = true;
                    }
                    bool so_head = false;
                    if (gwu.dsp_bus_src(ci, "SIA", 18) == nullptr && gwu.dsp_bus_src(ci, "SIB", 18) == nullptr) {
                        for (int i = 0; i < 18; ++i) {
                            ci->disconnectPort(ctx->idf("SIA[%d]", i));
                            ci->disconnectPort(ctx->idf("SIB[%d]", i));
                        }
                        so_head = true;
                    }
                    if (cas_head && so_head) {
                        dsp_heads.push_back(ci);
                        if (ctx->verbose) {
                            log_info(" found a DSP head: %s\n", ctx->nameOf(ci));
                        }
                    }
                } break;
                case ID_MULT36X36: {
                    for (int i = 0; i < 36; ++i) {
                        ci->cell_bel_pins.at(ctx->idf("A[%d]", i)).clear();
                        ci->cell_bel_pins.at(ctx->idf("A[%d]", i)).push_back(ctx->idf("A%d0", i));
                        ci->cell_bel_pins.at(ctx->idf("A[%d]", i)).push_back(ctx->idf("A%d1", i));
                        ci->cell_bel_pins.at(ctx->idf("B[%d]", i)).clear();
                        ci->cell_bel_pins.at(ctx->idf("B[%d]", i)).push_back(ctx->idf("B%d0", i));
                        ci->cell_bel_pins.at(ctx->idf("B[%d]", i)).push_back(ctx->idf("B%d1", i));
                    }
                    // only MSB sign bits
                    ci->cell_bel_pins.at(id_ASIGN).clear();
                    ci->cell_bel_pins.at(id_ASIGN).push_back(id_ASIGN0);
                    ci->cell_bel_pins.at(id_ASIGN).push_back(id_ASIGN1);
                    ci->cell_bel_pins.at(id_BSIGN).clear();
                    ci->cell_bel_pins.at(id_BSIGN).push_back(id_BSIGN0);
                    ci->cell_bel_pins.at(id_BSIGN).push_back(id_BSIGN1);

                    // LSB sign bits = 0
                    NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();
                    ci->addInput(id_ZERO_SIGN);
                    ci->cell_bel_pins[id_ZERO_SIGN].push_back(id_ZERO_ASIGN0);
                    ci->cell_bel_pins.at(id_ZERO_SIGN).push_back(id_ZERO_BSIGN0);
                    ci->cell_bel_pins.at(id_ZERO_SIGN).push_back(id_ZERO_BSIGN1);
                    ci->cell_bel_pins.at(id_ZERO_SIGN).push_back(id_ZERO_ASIGN1);
                    ci->connectPort(id_ZERO_SIGN, vss_net);

                    for (int i = 0; i < 72; ++i) {
                        ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                    }

                    // add padd9s and mult9s as a children
                    ci->cluster = ci->name;
                    ci->constr_abs_z = false;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    ci->constr_z = 0;
                    ci->constr_children.clear();

                    for (int i = 0; i < 8; ++i) {
                        IdString padd_name = gwu.create_aux_name(ci->name, i * 2);
                        std::unique_ptr<CellInfo> padd_cell = gwu.create_cell(padd_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(padd_cell));
                        CellInfo *padd_ci = new_cells.back().get();

                        static int padd_z[] = {BelZ::PADD9_0_0_Z, BelZ::PADD9_0_2_Z, BelZ::PADD9_1_0_Z,
                                               BelZ::PADD9_1_2_Z};
                        padd_ci->cluster = ci->name;
                        padd_ci->constr_abs_z = false;
                        padd_ci->constr_x = 0;
                        padd_ci->constr_y = 0;
                        padd_ci->constr_z = padd_z[i / 2] - BelZ::MULT36X36_Z + i % 2;

                        IdString mult_name = gwu.create_aux_name(ci->name, i * 2 + 1);
                        std::unique_ptr<CellInfo> mult_cell = gwu.create_cell(mult_name, id_DUMMY_CELL);
                        new_cells.push_back(std::move(mult_cell));
                        CellInfo *mult_ci = new_cells.back().get();

                        static int mult_z[] = {BelZ::MULT9X9_0_0_Z, BelZ::MULT9X9_0_2_Z, BelZ::MULT9X9_1_0_Z,
                                               BelZ::MULT9X9_1_2_Z};
                        mult_ci->cluster = ci->name;
                        mult_ci->constr_abs_z = false;
                        mult_ci->constr_x = 0;
                        mult_ci->constr_y = 0;
                        mult_ci->constr_z = mult_z[i / 2] - BelZ::MULT36X36_Z + i % 2;
                    }
                } break;
                default:
                    log_error("Unsupported DSP type '%s'\n", ci->type.c_str(ctx));
                }
            }
        }

        // add new cells
        for (auto &cell : new_cells) {
            if (cell->cluster != ClusterId()) {
                IdString cluster_root = cell->cluster;
                IdString cell_name = cell->name;
                ctx->cells[cell_name] = std::move(cell);
                ctx->cells.at(cluster_root).get()->constr_children.push_back(ctx->cells.at(cell_name).get());
            } else {
                ctx->cells[cell->name] = std::move(cell);
            }
        }

        // DSP chains
        for (CellInfo *head : dsp_heads) {
            if (ctx->verbose) {
                log_info("Process a DSP head: %s\n", ctx->nameOf(head));
            }
            switch (head->type.hash()) {
            case ID_PADD9: /* fallthrough */
            case ID_PADD18: {
                int wire_num = 9;
                if (head->type == id_PADD18) {
                    wire_num = 18;
                }

                CellInfo *cur_dsp = head;
                while (1) {
                    CellInfo *next_dsp_a = gwu.dsp_bus_dst(cur_dsp, "SO", wire_num);
                    CellInfo *next_dsp_b = gwu.dsp_bus_src(cur_dsp, "SBI", wire_num);
                    if (next_dsp_a != nullptr && next_dsp_b != nullptr && next_dsp_a != next_dsp_b) {
                        log_error("%s is the next for two different DSPs (%s and %s) in the chain.",
                                  ctx->nameOf(cur_dsp), ctx->nameOf(next_dsp_a), ctx->nameOf(next_dsp_b));
                    }
                    if (next_dsp_a == nullptr && next_dsp_b == nullptr) {
                        // End of chain
                        cur_dsp->setAttr(id_LAST_IN_CHAIN, 1);
                        for (int i = 0; i < wire_num; ++i) {
                            cur_dsp->disconnectPort(ctx->idf("SO[%d]", i));
                            cur_dsp->disconnectPort(ctx->idf("SBI[%d]", i));
                        }
                        break;
                    }

                    next_dsp_a = next_dsp_a != nullptr ? next_dsp_a : next_dsp_b;
                    for (int i = 0; i < wire_num; ++i) {
                        cur_dsp->disconnectPort(ctx->idf("SO[%d]", i));
                        cur_dsp->disconnectPort(ctx->idf("SBI[%d]", i));
                        next_dsp_a->disconnectPort(ctx->idf("SI[%d]", i));
                        next_dsp_a->disconnectPort(ctx->idf("SBO[%d]", i));
                    }
                    cur_dsp = next_dsp_a;
                    if (ctx->verbose) {
                        log_info("  add %s to the chain.\n", ctx->nameOf(cur_dsp));
                    }
                    if (head->cluster == ClusterId()) {
                        head->cluster = head->name;
                    }
                    cur_dsp->cluster = head->name;
                    head->constr_children.push_back(cur_dsp);
                    for (auto child : cur_dsp->constr_children) {
                        child->cluster = head->name;
                        head->constr_children.push_back(child);
                    }
                    cur_dsp->constr_children.clear();
                }
            } break;
            case ID_MULT9X9: /* fallthrough */
            case ID_MULT18X18: {
                int wire_num = 9;
                if (head->type == id_MULT18X18) {
                    wire_num = 18;
                }

                CellInfo *cur_dsp = head;
                while (1) {
                    CellInfo *next_dsp_a = gwu.dsp_bus_dst(cur_dsp, "SOA", wire_num);
                    CellInfo *next_dsp_b = gwu.dsp_bus_dst(cur_dsp, "SOB", wire_num);
                    if (next_dsp_a != nullptr && next_dsp_b != nullptr && next_dsp_a != next_dsp_b) {
                        log_error("%s is the source for two different DSPs (%s and %s) in the chain.",
                                  ctx->nameOf(cur_dsp), ctx->nameOf(next_dsp_a), ctx->nameOf(next_dsp_b));
                    }
                    if (next_dsp_a == nullptr && next_dsp_b == nullptr) {
                        // End of chain
                        for (int i = 0; i < wire_num; ++i) {
                            cur_dsp->disconnectPort(ctx->idf("SOA[%d]", i));
                            cur_dsp->disconnectPort(ctx->idf("SOB[%d]", i));
                        }
                        break;
                    }

                    next_dsp_a = next_dsp_a != nullptr ? next_dsp_a : next_dsp_b;
                    for (int i = 0; i < wire_num; ++i) {
                        cur_dsp->disconnectPort(ctx->idf("SOA[%d]", i));
                        cur_dsp->disconnectPort(ctx->idf("SOB[%d]", i));
                        next_dsp_a->disconnectPort(ctx->idf("SIA[%d]", i));
                        next_dsp_a->disconnectPort(ctx->idf("SIB[%d]", i));
                    }
                    cur_dsp = next_dsp_a;
                    if (ctx->verbose) {
                        log_info("  add %s to the chain.\n", ctx->nameOf(cur_dsp));
                    }
                    if (head->cluster == ClusterId()) {
                        head->cluster = head->name;
                    }
                    cur_dsp->cluster = head->name;
                    head->constr_children.push_back(cur_dsp);
                    for (auto child : cur_dsp->constr_children) {
                        child->cluster = head->name;
                        head->constr_children.push_back(child);
                    }
                    cur_dsp->constr_children.clear();
                }
            } break;
            case ID_MULTALU18X18: /* fallthrough */
            case ID_MULTALU36X18: /* fallthrough */
            case ID_ALU54D: {
                int wire_num = 55;
                CellInfo *cur_dsp = head;
                while (1) {
                    CellInfo *next_dsp_a = gwu.dsp_bus_dst(cur_dsp, "CASO", wire_num);
                    if (next_dsp_a == nullptr) {
                        // End of chain
                        for (int i = 0; i < wire_num; ++i) {
                            cur_dsp->disconnectPort(ctx->idf("CASO[%d]", i));
                        }
                        break;
                    }
                    for (int i = 0; i < wire_num; ++i) {
                        cur_dsp->disconnectPort(ctx->idf("CASO[%d]", i));
                        next_dsp_a->disconnectPort(ctx->idf("CASI[%d]", i));
                    }
                    cur_dsp->setAttr(id_USE_CASCADE_OUT, 1);
                    cur_dsp = next_dsp_a;
                    cur_dsp->setAttr(id_USE_CASCADE_IN, 1);
                    if (ctx->verbose) {
                        log_info("  add %s to the chain.\n", ctx->nameOf(cur_dsp));
                    }
                    if (head->cluster == ClusterId()) {
                        head->cluster = head->name;
                    }
                    cur_dsp->cluster = head->name;
                    head->constr_children.push_back(cur_dsp);
                    for (auto child : cur_dsp->constr_children) {
                        child->cluster = head->name;
                        head->constr_children.push_back(child);
                    }
                    cur_dsp->constr_children.clear();
                }
            } break;
            case ID_MULTADDALU18X18: {
                // This primitive has the ability to form chains using both SO[AB] -> SI[AB] and CASO->CASI
                CellInfo *cur_dsp = head;
                while (1) {
                    bool end_of_cas_chain = false;
                    int wire_num = 55;
                    CellInfo *next_dsp_a = gwu.dsp_bus_dst(cur_dsp, "CASO", wire_num);
                    if (next_dsp_a == nullptr) {
                        // End of CASO chain
                        for (int i = 0; i < wire_num; ++i) {
                            cur_dsp->disconnectPort(ctx->idf("CASO[%d]", i));
                        }
                        end_of_cas_chain = true;
                    } else {
                        for (int i = 0; i < wire_num; ++i) {
                            cur_dsp->disconnectPort(ctx->idf("CASO[%d]", i));
                            next_dsp_a->disconnectPort(ctx->idf("CASI[%d]", i));
                        }
                    }

                    bool end_of_so_chain = false;
                    wire_num = 18;
                    CellInfo *next_so_dsp_a = gwu.dsp_bus_dst(cur_dsp, "SOA", wire_num);
                    CellInfo *next_so_dsp_b = gwu.dsp_bus_dst(cur_dsp, "SOB", wire_num);
                    if (next_so_dsp_a != nullptr && next_so_dsp_b != nullptr && next_so_dsp_a != next_so_dsp_b) {
                        log_error("%s is the source for two different DSPs (%s and %s) in the chain.",
                                  ctx->nameOf(cur_dsp), ctx->nameOf(next_so_dsp_a), ctx->nameOf(next_so_dsp_b));
                    }
                    if (next_so_dsp_a == nullptr && next_so_dsp_b == nullptr) {
                        // End of SO chain
                        for (int i = 0; i < wire_num; ++i) {
                            cur_dsp->disconnectPort(ctx->idf("SOA[%d]", i));
                            cur_dsp->disconnectPort(ctx->idf("SOB[%d]", i));
                        }
                        end_of_so_chain = true;
                    } else {
                        next_so_dsp_a = next_so_dsp_a != nullptr ? next_so_dsp_a : next_so_dsp_b;
                        for (int i = 0; i < wire_num; ++i) {
                            cur_dsp->disconnectPort(ctx->idf("SOA[%d]", i));
                            cur_dsp->disconnectPort(ctx->idf("SOB[%d]", i));
                            next_so_dsp_a->disconnectPort(ctx->idf("SIA[%d]", i));
                            next_so_dsp_a->disconnectPort(ctx->idf("SIB[%d]", i));
                        }
                    }
                    if (end_of_cas_chain && end_of_so_chain) {
                        break;
                    }

                    // to next
                    if (!end_of_cas_chain) {
                        cur_dsp->setAttr(id_USE_CASCADE_OUT, 1);
                    }
                    cur_dsp = next_dsp_a != nullptr ? next_dsp_a : next_so_dsp_a;
                    if (!end_of_cas_chain) {
                        cur_dsp->setAttr(id_USE_CASCADE_IN, 1);
                    }
                    if (ctx->verbose) {
                        log_info("  add %s to the chain. End of the SO chain:%d, end of the CAS chain:%d\n",
                                 ctx->nameOf(cur_dsp), end_of_so_chain, end_of_cas_chain);
                    }
                    if (head->cluster == ClusterId()) {
                        head->cluster = head->name;
                    }
                    cur_dsp->cluster = head->name;
                    head->constr_children.push_back(cur_dsp);
                    for (auto child : cur_dsp->constr_children) {
                        child->cluster = head->name;
                        head->constr_children.push_back(child);
                    }
                    cur_dsp->constr_children.clear();
                }
            } break;
            }
        }
    }

    // ===================================
    // Global set/reset
    // ===================================
    void pack_gsr(void)
    {
        log_info("Pack GSR...\n");

        bool user_gsr = false;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;

            if (ci.type == id_GSR) {
                user_gsr = true;
                break;
            }
        }
        if (!user_gsr) {
            // make default GSR
            auto gsr_cell = std::make_unique<CellInfo>(ctx, id_GSR, id_GSR);
            gsr_cell->addInput(id_GSRI);
            gsr_cell->connectPort(id_GSRI, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
            ctx->cells[gsr_cell->name] = std::move(gsr_cell);
        }
        if (ctx->verbose) {
            if (user_gsr) {
                log_info("Have user GSR\n");
            } else {
                log_info("No user GSR. Make one.\n");
            }
        }
    }

    // ===================================
    // Global power regulator
    // ===================================
    void pack_bandgap(void)
    {
        if (!gwu.has_BANDGAP()) {
            return;
        }
        log_info("Pack BANDGAP...\n");

        bool user_bandgap = false;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;

            if (ci.type == id_BANDGAP) {
                user_bandgap = true;
                break;
            }
        }
        if (!user_bandgap) {
            // make default BANDGAP
            auto bandgap_cell = std::make_unique<CellInfo>(ctx, id_BANDGAP, id_BANDGAP);
            bandgap_cell->addInput(id_BGEN);
            bandgap_cell->connectPort(id_BGEN, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
            ctx->cells[bandgap_cell->name] = std::move(bandgap_cell);
        }
        if (ctx->verbose) {
            if (user_bandgap) {
                log_info("Have user BANDGAP\n");
            } else {
                log_info("No user BANDGAP. Make one.\n");
            }
        }
    }

    // ===================================
    // Replace INV with LUT
    // ===================================
    void pack_inv(void)
    {
        log_info("Pack INV...\n");

        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;

            if (ci.type == id_INV) {
                ci.type = id_LUT4;
                ci.renamePort(id_O, id_F);
                ci.renamePort(id_I, id_I3); // use D - it's simple for INIT
                ci.params[id_INIT] = Property(0x00ff);
            }
        }
    }

    // ===================================
    // PLL
    // ===================================
    void pack_pll(void)
    {
        log_info("Pack PLL...\n");

        pool<BelId> used_pll_bels;

        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;

            if (ci.type.in(id_rPLL, id_PLLVR)) {
                // pin renaming for compatibility
                for (int i = 0; i < 6; ++i) {
                    ci.renamePort(ctx->idf("FBDSEL[%d]", i), ctx->idf("FBDSEL%d", i));
                    ci.renamePort(ctx->idf("IDSEL[%d]", i), ctx->idf("IDSEL%d", i));
                    ci.renamePort(ctx->idf("ODSEL[%d]", i), ctx->idf("ODSEL%d", i));
                    if (i < 4) {
                        ci.renamePort(ctx->idf("PSDA[%d]", i), ctx->idf("PSDA%d", i));
                        ci.renamePort(ctx->idf("DUTYDA[%d]", i), ctx->idf("DUTYDA%d", i));
                        ci.renamePort(ctx->idf("FDLY[%d]", i), ctx->idf("FDLY%d", i));
                    }
                }
                // If CLKIN is connected to a special pin, then it makes sense
                // to try to place the PLL so that it uses a direct connection
                // to this pin.
                if (ci.bel == BelId()) {
                    NetInfo *ni = ci.getPort(id_CLKIN);
                    if (ni && ni->driver.cell && ni->driver.cell->bel != BelId()) {
                        BelId pll_bel = gwu.get_pll_bel(ni->driver.cell->bel, id_CLKIN_T);
                        if (ctx->debug) {
                            log_info("PLL clkin driver:%s at %s, PLL bel:%s\n", ctx->nameOf(ni->driver.cell),
                                     ctx->getBelName(ni->driver.cell->bel).str(ctx).c_str(),
                                     pll_bel != BelId() ? ctx->getBelName(pll_bel).str(ctx).c_str() : "NULL");
                        }
                        if (pll_bel != BelId() && used_pll_bels.count(pll_bel) == 0) {
                            used_pll_bels.insert(pll_bel);
                            ctx->bindBel(pll_bel, &ci, PlaceStrength::STRENGTH_LOCKED);
                            ci.disconnectPort(id_CLKIN);
                            ci.setParam(id_INSEL, std::string("CLKIN0"));
                        }
                    }
                }
            }
        }
    }

    // ===================================
    // HCLK -- CLKDIV and CLKDIV2 for now
    // ===================================
    void pack_hclk(void)
    {
        log_info("Pack HCLK cells...\n");

        for (auto &cell : ctx->cells) {
            auto ci = cell.second.get();
            if (ci->type != id_CLKDIV)
                continue;
            NetInfo *hclk_in = ci->getPort(id_HCLKIN);
            if (hclk_in) {
                CellInfo *this_driver = hclk_in->driver.cell;
                if (this_driver && this_driver->type == id_CLKDIV2) {
                    NetInfo *out = this_driver->getPort(id_CLKOUT);
                    if (out->users.entries() > 1) {
                        // We could do as the IDE does sometimes and replicate the CLKDIV2 cell
                        // as many times as we need. For now, we keep things simple
                        log_error("CLKDIV2 that drives CLKDIV should drive no other cells\n");
                    }
                    ci->cluster = ci->name;
                    this_driver->cluster = ci->name;
                    ci->constr_children.push_back(this_driver);
                    this_driver->constr_x = 0;
                    this_driver->constr_y = 0;
                    this_driver->constr_z = BelZ::CLKDIV2_0_Z - BelZ::CLKDIV_0_Z;
                    this_driver->constr_abs_z = false;
                }
            }
        }
    }

    // =========================================
    // Create entry points to the clock system
    // =========================================
    void pack_buffered_nets()
    {
        log_info("Pack buffered nets...\n");

        for (auto &net : ctx->nets) {
            auto &ni = *net.second;
            if (ni.driver.cell == nullptr || ni.attrs.count(id_CLOCK) == 0 || ni.users.empty()) {
                continue;
            }

            // make new BUF cell single user for the net driver
            IdString buf_name = ctx->idf("%s_BUFG", net.first.c_str(ctx));
            ctx->createCell(buf_name, id_BUFG);
            CellInfo *buf_ci = ctx->cells.at(buf_name).get();
            buf_ci->addInput(id_I);
            // move driver
            CellInfo *driver_cell = ni.driver.cell;
            IdString driver_port = ni.driver.port;

            driver_cell->movePortTo(driver_port, buf_ci, id_O);
            buf_ci->connectPorts(id_I, driver_cell, driver_port);
        }
    }

    // =========================================
    // Create DQCEs
    // =========================================
    void pack_dqce()
    {
        log_info("Pack DQCE cells...\n");

        // At the placement stage, nothing can be said definitively about DQCE,
        // so we make user cells virtual but allocate all available bels by
        // creating and placing cells - we will use some of them after, and
        // delete the rest.
        // We do this here because the decision about which physical DQCEs to
        // use is made during routing, but some of the information (let’s say
        // mapping cell pins -> bel pins) is filled in before routing.
        bool grab_bels = false;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (ci.type == id_DQCE) {
                ci.pseudo_cell = std::make_unique<RegionPlug>(Loc(0, 0, 0));
                grab_bels = true;
            }
        }
        if (grab_bels) {
            for (int i = 0; i < 32; ++i) {
                BelId dqce_bel = gwu.get_dqce_bel(ctx->idf("SPINE%d", i));
                if (dqce_bel != BelId()) {
                    IdString dqce_name = ctx->idf("$PACKER_DQCE_SPINE%d", i);
                    CellInfo *dqce = ctx->createCell(dqce_name, id_DQCE);
                    dqce->addInput(id_CE);
                    ctx->bindBel(dqce_bel, dqce, STRENGTH_LOCKED);
                }
            }
        }
    }

    // =========================================
    // Create DCSs
    // =========================================
    void pack_dcs()
    {
        log_info("Pack DCS cells...\n");

        // At the placement stage, nothing can be said definitively about DCS,
        // so we make user cells virtual but allocate all available bels by
        // creating and placing cells - we will use some of them after, and
        // delete the rest.
        // We do this here because the decision about which physical DCEs to
        // use is made during routing, but some of the information (let’s say
        // mapping cell pins -> bel pins) is filled in before routing.
        bool grab_bels = false;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (ci.type == id_DCS) {
                ci.pseudo_cell = std::make_unique<RegionPlug>(Loc(0, 0, 0));
                grab_bels = true;
            }
        }
        if (grab_bels) {
            for (int i = 0; i < 8; ++i) {
                BelId dcs_bel = gwu.get_dcs_bel(ctx->idf("P%d%dA", 1 + (i % 4), 6 + (i >> 2)));
                if (dcs_bel != BelId()) {
                    IdString dcs_name = ctx->idf("$PACKER_DCS_SPINE%d", 8 * (i % 4) + 6 + (i >> 2));
                    CellInfo *dcs = ctx->createCell(dcs_name, id_DCS);
                    dcs->addInput(id_SELFORCE);
                    for (int j = 0; j < 4; ++j) {
                        dcs->addInput(ctx->idf("CLK%d", j));
                        dcs->addInput(ctx->idf("CLKSEL%d", j));
                    }
                    dcs->addOutput(id_CLKOUT);
                    ctx->bindBel(dcs_bel, dcs, STRENGTH_LOCKED);
                }
            }
        }
    }

    // =========================================
    // Create DHCENs
    // =========================================
    void pack_dhcens()
    {
        // Allocate all available dhcen bels; we will find out which of them
        // will actually be used during the routing process.
        bool grab_bels = false;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (ci.type == id_DHCEN) {
                ci.pseudo_cell = std::make_unique<RegionPlug>(Loc(0, 0, 0));
                grab_bels = true;
            }
        }
        if (grab_bels) {
            // sane message if new primitives are used with old bases
            auto buckets = ctx->getBelBuckets();
            NPNR_ASSERT_MSG(std::find(buckets.begin(), buckets.end(), id_DHCEN) != buckets.end(),
                            "There are no DHCEN bels to use.");
            int i = 0;
            for (auto &bel : ctx->getBelsInBucket(ctx->getBelBucketForCellType(id_DHCEN))) {
                IdString dhcen_name = ctx->idf("$PACKER_DHCEN_%d", ++i);
                CellInfo *dhcen = ctx->createCell(dhcen_name, id_DHCEN);
                dhcen->addInput(id_CE);
                ctx->bindBel(bel, dhcen, STRENGTH_LOCKED);
            }
        }
    }

    // =========================================
    // Enable UserFlash
    // =========================================
    void pack_userflash(bool have_emcu)
    {
        log_info("Pack UserFlash cells...\n");
        std::vector<std::unique_ptr<CellInfo>> new_cells;

        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (!is_userflash(&ci)) {
                continue;
            }

            if (ci.type.in(id_FLASH96K, id_FLASH256K, id_FLASH608K)) {
                // enable
                ci.addInput(id_INUSEN);
                ci.connectPort(id_INUSEN, ctx->nets.at(ctx->id("$PACKER_GND")).get());
            }
            // rename ports
            for (int i = 0; i < 32; ++i) {
                ci.renamePort(ctx->idf("DIN[%d]", i), ctx->idf("DIN%d", i));
                ci.renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
            }
            if (ci.type.in(id_FLASH96K)) {
                for (int i = 0; i < 6; ++i) {
                    ci.renamePort(ctx->idf("RA[%d]", i), ctx->idf("RA%d", i));
                    ci.renamePort(ctx->idf("CA[%d]", i), ctx->idf("CA%d", i));
                    ci.renamePort(ctx->idf("PA[%d]", i), ctx->idf("PA%d", i));
                }
                for (int i = 0; i < 2; ++i) {
                    ci.renamePort(ctx->idf("MODE[%d]", i), ctx->idf("MODE%d", i));
                    ci.renamePort(ctx->idf("SEQ[%d]", i), ctx->idf("SEQ%d", i));
                    ci.renamePort(ctx->idf("RMODE[%d]", i), ctx->idf("RMODE%d", i));
                    ci.renamePort(ctx->idf("WMODE[%d]", i), ctx->idf("WMODE%d", i));
                    ci.renamePort(ctx->idf("RBYTESEL[%d]", i), ctx->idf("RBYTESEL%d", i));
                    ci.renamePort(ctx->idf("WBYTESEL[%d]", i), ctx->idf("WBYTESEL%d", i));
                }
            } else {
                for (int i = 0; i < 9; ++i) {
                    ci.renamePort(ctx->idf("XADR[%d]", i), ctx->idf("XADR%d", i));
                }
                for (int i = 0; i < 6; ++i) {
                    ci.renamePort(ctx->idf("YADR[%d]", i), ctx->idf("YADR%d", i));
                }
            }

            if (have_emcu) {
                continue;
            }

            // add invertor
            int lut_idx = 0;
            auto add_inv = [&](IdString port, PortType port_type) {
                if (!gwu.port_used(&ci, port)) {
                    return;
                }

                std::unique_ptr<CellInfo> lut_cell =
                        gwu.create_cell(gwu.create_aux_name(ci.name, lut_idx, "_lut$"), id_LUT4);
                new_cells.push_back(std::move(lut_cell));
                CellInfo *lut = new_cells.back().get();
                lut->addInput(id_I0);
                lut->addOutput(id_F);
                lut->setParam(id_INIT, 0x5555);
                ++lut_idx;

                if (port_type == PORT_IN) {
                    ci.movePortTo(port, lut, id_I0);
                    lut->connectPorts(id_F, &ci, port);
                } else {
                    ci.movePortTo(port, lut, id_F);
                    ci.connectPorts(port, lut, id_I0);
                }
            };
            for (auto pin : ci.ports) {
                if (pin.second.type == PORT_OUT) {
                    add_inv(pin.first, PORT_OUT);
                } else {
                    if (pin.first == id_INUSEN) {
                        continue;
                    }
                    if (ci.type == id_FLASH608K && pin.first.in(id_XADR0, id_XADR1, id_XADR2, id_XADR3, id_XADR4,
                                                                id_XADR5, id_XADR6, id_XADR7, id_XADR8)) {
                        continue;
                    }
                    add_inv(pin.first, PORT_IN);
                }
            }
        }
        for (auto &ncell : new_cells) {
            ctx->cells[ncell->name] = std::move(ncell);
        }
    }

    // =========================================
    // Create EMCU
    // =========================================
    void pack_emcu_and_flash()
    {
        log_info("Pack EMCU and UserFlash cells...\n");
        std::vector<std::unique_ptr<CellInfo>> new_cells;

        bool have_emcu = false;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (!is_emcu(&ci)) {
                continue;
            }
            have_emcu = true;

            // rename ports
            for (int i = 0; i < 2; ++i) {
                ci.renamePort(ctx->idf("TARGFLASH0HTRANS[%d]", i), ctx->idf("TARGFLASH0HTRANS%d", i));
                ci.renamePort(ctx->idf("TARGEXP0HTRANS[%d]", i), ctx->idf("TARGEXP0HTRANS%d", i));
                ci.renamePort(ctx->idf("TARGEXP0MEMATTR[%d]", i), ctx->idf("TARGEXP0MEMATTR%d", i));
                // ins
                ci.renamePort(ctx->idf("INITEXP0HTRANS[%d]", i), ctx->idf("INITEXP0HTRANS%d", i));
                ci.renamePort(ctx->idf("INITEXP0MEMATTR[%d]", i), ctx->idf("INITEXP0MEMATTR%d", i));
            }
            for (int i = 0; i < 3; ++i) {
                ci.renamePort(ctx->idf("TARGEXP0HSIZE[%d]", i), ctx->idf("TARGEXP0HSIZE%d", i));
                ci.renamePort(ctx->idf("TARGEXP0HBURST[%d]", i), ctx->idf("TARGEXP0HBURST%d", i));
                ci.renamePort(ctx->idf("APBTARGEXP2PPROT[%d]", i), ctx->idf("APBTARGEXP2PPROT%d", i));
                // ins
                ci.renamePort(ctx->idf("TARGEXP0HRUSER[%d]", i), ctx->idf("TARGEXP0HRUSER%d", i));
                ci.renamePort(ctx->idf("INITEXP0HSIZE[%d]", i), ctx->idf("INITEXP0HSIZE%d", i));
                ci.renamePort(ctx->idf("INITEXP0HBURST[%d]", i), ctx->idf("INITEXP0HBURST%d", i));
            }
            for (int i = 0; i < 4; ++i) {
                ci.renamePort(ctx->idf("SRAM0WREN[%d]", i), ctx->idf("SRAM0WREN%d", i));
                ci.renamePort(ctx->idf("TARGEXP0HPROT[%d]", i), ctx->idf("TARGEXP0HPROT%d", i));
                ci.renamePort(ctx->idf("TARGEXP0HMASTER[%d]", i), ctx->idf("TARGEXP0HMASTER%d", i));
                ci.renamePort(ctx->idf("APBTARGEXP2PSTRB[%d]", i), ctx->idf("APBTARGEXP2PSTRB%d", i));
                ci.renamePort(ctx->idf("TPIUTRACEDATA[%d]", i), ctx->idf("TPIUTRACEDATA%d", i));
                // ins
                ci.renamePort(ctx->idf("INITEXP0HPROT[%d]", i), ctx->idf("INITEXP0HPROT%d", i));
                ci.renamePort(ctx->idf("INITEXP0HMASTER[%d]", i), ctx->idf("INITEXP0HMASTER%d", i));
                ci.renamePort(ctx->idf("INITEXP0HWUSER[%d]", i), ctx->idf("INITEXP0HWUSER%d", i));
            }
            for (int i = 0; i < 16; ++i) {
                if (i < 13) {
                    if (i < 12) {
                        if (i < 5) {
                            ci.renamePort(ctx->idf("GPINT[%d]", i), ctx->idf("GPINT%d", i));
                        }
                        ci.renamePort(ctx->idf("APBTARGEXP2PADDR[%d]", i), ctx->idf("APBTARGEXP2PADDR%d", i));
                    }
                    ci.renamePort(ctx->idf("SRAM0ADDR[%d]", i), ctx->idf("SRAM0ADDR%d", i));
                }
                ci.renamePort(ctx->idf("IOEXPOUTPUTO[%d]", i), ctx->idf("IOEXPOUTPUTO%d", i));
                ci.renamePort(ctx->idf("IOEXPOUTPUTENO[%d]", i), ctx->idf("IOEXPOUTPUTENO%d", i));
                // ins
                ci.renamePort(ctx->idf("IOEXPINPUTI[%d]", i), ctx->idf("IOEXPINPUTI%d", i));
            }
            for (int i = 0; i < 32; ++i) {
                if (i < 29) {
                    ci.renamePort(ctx->idf("TARGFLASH0HADDR[%d]", i), ctx->idf("TARGFLASH0HADDR%d", i));
                }
                ci.renamePort(ctx->idf("SRAM0WDATA[%d]", i), ctx->idf("SRAM0WDATA%d", i));
                ci.renamePort(ctx->idf("TARGEXP0HADDR[%d]", i), ctx->idf("TARGEXP0HADDR%d", i));
                ci.renamePort(ctx->idf("TARGEXP0HWDATA[%d]", i), ctx->idf("TARGEXP0HWDATA%d", i));
                ci.renamePort(ctx->idf("INITEXP0HRDATA[%d]", i), ctx->idf("INITEXP0HRDATA%d", i));
                ci.renamePort(ctx->idf("APBTARGEXP2PWDATA[%d]", i), ctx->idf("APBTARGEXP2PWDATA%d", i));
                // ins
                ci.renamePort(ctx->idf("SRAM0RDATA[%d]", i), ctx->idf("SRAM0RDATA%d", i));
                ci.renamePort(ctx->idf("TARGEXP0HRDATA[%d]", i), ctx->idf("TARGEXP0HRDATA%d", i));
                ci.renamePort(ctx->idf("INITEXP0HADDR[%d]", i), ctx->idf("INITEXP0HADDR%d", i));
                ci.renamePort(ctx->idf("INITEXP0HWDATA[%d]", i), ctx->idf("INITEXP0HWDATA%d", i));
                ci.renamePort(ctx->idf("APBTARGEXP2PRDATA[%d]", i), ctx->idf("APBTARGEXP2PRDATA%d", i));
            }
            // The flash data bus is connected directly to the CPU so just disconnect these networks
            // also other non-switched networks
            ci.disconnectPort(ctx->id("DAPNTDOEN"));
            ci.disconnectPort(ctx->id("DAPNTRST"));
            ci.disconnectPort(ctx->id("DAPTDO"));
            ci.disconnectPort(ctx->id("DAPTDI"));
            ci.disconnectPort(ctx->id("TARGFLASH0HREADYMUX"));
            ci.disconnectPort(ctx->id("TARGEXP0HAUSER"));
            ci.disconnectPort(ctx->id("TARGFLASH0EXRESP"));
            ci.disconnectPort(ctx->id("PORESETN"));
            ci.disconnectPort(ctx->id("SYSRESETN"));
            ci.disconnectPort(ctx->id("DAPSWDITMS"));
            ci.disconnectPort(ctx->id("DAPSWCLKTCK"));
            ci.disconnectPort(ctx->id("TPIUTRACECLK"));
            for (int i = 0; i < 32; ++i) {
                if (i < 4) {
                    if (i < 3) {
                        ci.disconnectPort(ctx->idf("TARGFLASH0HSIZE[%d]", i));
                        ci.disconnectPort(ctx->idf("TARGFLASH0HBURST[%d]", i));
                        ci.disconnectPort(ctx->idf("TARGFLASH0HRUSER[%d]", i));
                        ci.disconnectPort(ctx->idf("INITEXP0HRUSER[%d]", i));
                    }
                    // ci.disconnectPort(ctx->idf("TARGFLASH0HPROT[%d]", i));
                    ci.disconnectPort(ctx->idf("TARGEXP0HWUSER[%d]", i));
                    ci.disconnectPort(ctx->idf("MTXREMAP[%d]", i));
                }
                // ins
                ci.disconnectPort(ctx->idf("TARGFLASH0HRDATA[%d]", i));
            }
        }
        pack_userflash(have_emcu);
    }

    void run(void)
    {
        handle_constants();
        pack_iobs();
        ctx->check();

        pack_diff_iobs();
        ctx->check();

        pack_io_regs();
        ctx->check();

        pack_iodelay();
        ctx->check();

        pack_iem();
        ctx->check();

        pack_iologic();
        ctx->check();

        pack_io16();
        ctx->check();

        pack_gsr();
        ctx->check();

        pack_hclk();
        ctx->check();

        pack_bandgap();
        ctx->check();

        pack_wideluts();
        ctx->check();

        pack_alus();
        ctx->check();

        constrain_lutffs();
        ctx->check();

        pack_pll();
        ctx->check();

        pack_ram16sdp4();
        ctx->check();

        pack_bsram();
        ctx->check();

        pack_dsp();
        ctx->check();

        pack_inv();
        ctx->check();

        pack_buffered_nets();
        ctx->check();

        pack_emcu_and_flash();
        ctx->check();

        pack_dhcens();
        ctx->check();

        pack_dqce();
        ctx->check();

        pack_dcs();
        ctx->check();

        ctx->fixupHierarchy();
        ctx->check();
    }
};
} // namespace

void gowin_pack(Context *ctx)
{
    GowinPacker packer(ctx);
    packer.run();
}

NEXTPNR_NAMESPACE_END

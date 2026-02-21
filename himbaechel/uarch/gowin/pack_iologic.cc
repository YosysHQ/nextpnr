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

// ===================================
// IO logic
// ===================================
// the functions of these two inputs are yet to be discovered, so we set as observed
// in the exemplary images
void GowinPacker::set_daaj_nets(CellInfo &ci, BelId bel)
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

BelId GowinPacker::get_iologico_bel(CellInfo *iob)
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

BelId GowinPacker::get_iologici_bel(CellInfo *iob)
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

void GowinPacker::check_iologic_placement(CellInfo &ci, Loc iob_loc, int diff /* 1 - diff */)
{
    if (ci.type.in(id_ODDR, id_ODDRC, id_IDDR, id_IDDRC, id_OSER4, id_IOLOGICI_EMPTY, id_IOLOGICO_EMPTY) || diff) {
        return;
    }
    BelId l_bel = ctx->getBelByLocation(Loc(iob_loc.x, iob_loc.y, BelZ::IOBA_Z + 1 - (iob_loc.z - BelZ::IOBA_Z)));
    if (!ctx->checkBelAvail(l_bel)) {
        log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci), ctx->nameOfBel(l_bel),
                  ctx->nameOf(ctx->getBoundBelCell(l_bel)));
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

void GowinPacker::pack_bi_output_iol(CellInfo &ci, std::vector<IdString> &nets_to_remove)
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
    check_iologic_placement(ci, ctx->getBelLocation(iob_bel),
                            out_iob->params.count(id_DIFF_TYPE) || out_iob->params.count(id_MIPI_OBUF));

    if (!ctx->checkBelAvail(l_bel)) {
        log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci), ctx->nameOfBel(l_bel),
                  ctx->nameOf(ctx->getBoundBelCell(l_bel)));
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
    } else { // disconnect TXx ports, ignore these nets
        switch (ci.type.hash()) {
        case ID_OSER8:
            ci.disconnectPort(id_TX3);
            ci.disconnectPort(id_TX2); /* fall-through */
        case ID_OSER4:
            ci.disconnectPort(id_TX1);
            ci.disconnectPort(id_TX0);
            break;
        case ID_ODDR:  /* fall-through */
        case ID_ODDRC: /* fall-through */
            ci.disconnectPort(id_TX);
            break;
        }
    }
    make_iob_nets(*out_iob);
}

void GowinPacker::pack_single_output_iol(CellInfo &ci, std::vector<IdString> &nets_to_remove)
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
    check_iologic_placement(ci, ctx->getBelLocation(iob_bel),
                            out_iob->params.count(id_DIFF_TYPE) || out_iob->params.count(id_MIPI_OBUF));

    if (!ctx->checkBelAvail(l_bel)) {
        log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci), ctx->nameOfBel(l_bel),
                  ctx->nameOf(ctx->getBoundBelCell(l_bel)));
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

BelId GowinPacker::get_aux_iologic_bel(const CellInfo &ci)
{
    return ctx->getBelByLocation(gwu.get_pair_iologic_bel(ctx->getBelLocation(ci.bel)));
}

bool GowinPacker::is_diff_io(BelId bel) { return ctx->getBoundBelCell(bel)->params.count(id_DIFF_TYPE) != 0; }
bool GowinPacker::is_mipi_io(BelId bel)
{
    return ctx->getBoundBelCell(bel)->params.count(id_MIPI_IBUF) ||
           ctx->getBoundBelCell(bel)->params.count(id_MIPI_OBUF);
}

CellInfo *GowinPacker::create_aux_iologic_cell(CellInfo &ci, IdString mode, bool io16, int idx)
{
    if (ci.type.in(id_ODDR, id_ODDRC, id_OSER4, id_IDDR, id_IDDRC, id_IDES4, id_IOLOGICI_EMPTY, id_IOLOGICO_EMPTY)) {
        return nullptr;
    }
    IdString aux_name = gwu.create_aux_name(ci.name, idx);
    BelId bel = get_aux_iologic_bel(ci);
    BelId io_bel = gwu.get_io_bel_from_iologic(bel);
    if (!ctx->checkBelAvail(io_bel)) {
        if (!(is_diff_io(io_bel) || is_mipi_io(io_bel))) {
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

void GowinPacker::reconnect_ides_outs(CellInfo *ci)
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

void GowinPacker::pack_ides_iol(CellInfo &ci, std::vector<IdString> &nets_to_remove)
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
    check_iologic_placement(ci, ctx->getBelLocation(iob_bel),
                            in_iob->params.count(id_DIFF_TYPE) || in_iob->params.count(id_MIPI_IBUF));

    if (!ctx->checkBelAvail(l_bel)) {
        log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci), ctx->nameOfBel(l_bel),
                  ctx->nameOf(ctx->getBoundBelCell(l_bel)));
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

void GowinPacker::pack_iem(void)
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

void GowinPacker::pack_iodelay(void)
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

void GowinPacker::pack_iologic(void)
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
void GowinPacker::check_io16_placement(CellInfo &ci, Loc main_loc, Loc aux_off, int diff /* 1 - diff */)
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

void GowinPacker::pack_oser16(CellInfo &ci, std::vector<IdString> &nets_to_remove)
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

void GowinPacker::pack_ides16(CellInfo &ci, std::vector<IdString> &nets_to_remove)
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

void GowinPacker::pack_io16(void)
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
NEXTPNR_NAMESPACE_END

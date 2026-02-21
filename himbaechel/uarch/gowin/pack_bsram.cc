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
// Block RAM
// ===================================
void GowinPacker::bsram_rename_ports(CellInfo *ci, int bit_width, char const *from, char const *to, int offset)
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
void GowinPacker::bsram_fix_blksel(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
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
void GowinPacker::bsram_fix_outreg(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
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
void GowinPacker::bsram_fix_sp(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
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

void GowinPacker::pack_ROM(CellInfo *ci)
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

void GowinPacker::divide_sdp(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    if (ctx->verbose) {
        log_info("  divide SDP\n");
    }

    int bw = ci->params.at(id_BIT_WIDTH_0).as_int64();
    NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
    NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();

    IdString cell_type = bw == 32 ? id_SDPB : id_SDPX9B;
    IdString name = ctx->idf("%s_AUX", ctx->nameOf(ci));

    auto sdp_cell = gwu.create_cell(name, cell_type);
    CellInfo *sdp = sdp_cell.get();
    sdp->setAttr(id_AUX, 1);

    int new_bw = bw / 2;
    ci->setParam(id_BIT_WIDTH_0, new_bw);
    ci->setParam(id_BIT_WIDTH_1, new_bw);
    sdp->params = ci->params;
    sdp->setParam(id_BIT_WIDTH_0, new_bw);
    sdp->setParam(id_BIT_WIDTH_1, new_bw);

    // copy control ports
    ci->copyPortBusTo(ctx->id("BLKSELA"), 0, true, sdp, ctx->id("BLKSELA"), 0, true, 3);
    ci->copyPortBusTo(ctx->id("BLKSELB"), 0, true, sdp, ctx->id("BLKSELB"), 0, true, 3);
    ci->copyPortTo(id_CEA, sdp, id_CEA);
    ci->copyPortTo(id_CEB, sdp, id_CEB);
    ci->copyPortTo(id_CLKA, sdp, id_CLKA);
    ci->copyPortTo(id_CLKB, sdp, id_CLKB);
    ci->copyPortTo(id_OCE, sdp, id_OCE);
    ci->copyPortTo(id_RESET, sdp, id_RESET);

    //  Separate port A
    ci->movePortTo(ctx->id("ADA[2]"), sdp, ctx->id("ADA[0]"));
    ci->movePortTo(ctx->id("ADA[3]"), sdp, ctx->id("ADA[1]"));

    ci->addInput(ctx->id("ADA[2]"));
    ci->addInput(ctx->id("ADA[3]"));
    ci->connectPort(ctx->id("ADA[2]"), vss_net);
    ci->connectPort(ctx->id("ADA[3]"), vss_net);

    sdp->addInput(ctx->id("ADA[2]"));
    sdp->addInput(ctx->id("ADA[3]"));
    sdp->connectPort(ctx->id("ADA[2]"), vss_net);
    sdp->connectPort(ctx->id("ADA[3]"), vss_net);

    ci->disconnectPort(ctx->id("ADA[4]"));
    ci->connectPort(ctx->id("ADA[4]"), vss_net);
    sdp->addInput(ctx->id("ADA[4]"));
    sdp->connectPort(ctx->id("ADA[4]"), vcc_net);

    ci->copyPortBusTo(id_ADA, 5, true, sdp, id_ADA, 5, true, 9);

    //  Separate port B
    for (int i = 0; i < 4; ++i) {
        IdString port = ctx->idf("ADB[%d]", i);
        ci->disconnectPort(port);
        ci->connectPort(port, vss_net);
        ci->copyPortTo(port, sdp, port);
    }

    ci->disconnectPort(ctx->id("ADB[4]"));
    ci->connectPort(ctx->id("ADB[4]"), vss_net);
    sdp->addInput(ctx->id("ADB[4]"));
    sdp->connectPort(ctx->id("ADB[4]"), vcc_net);

    ci->copyPortBusTo(id_ADB, 5, true, sdp, id_ADB, 5, true, 9);

    ci->movePortBusTo(id_DI, new_bw, true, sdp, id_DI, 0, true, new_bw);
    ci->movePortBusTo(id_DO, new_bw, true, sdp, id_DO, 0, true, new_bw);

    new_cells.push_back(std::move(sdp_cell));
}

void GowinPacker::pack_SDPB(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    int default_bw = 32;
    if (ci->type == id_SDPB) {
        ci->setAttr(id_BSRAM_SUBTYPE, Property(""));
    } else {
        ci->setAttr(id_BSRAM_SUBTYPE, Property("X9"));
        default_bw = 36;
    }

    if (!ci->params.count(id_BIT_WIDTH_0)) {
        ci->setParam(id_BIT_WIDTH_0, Property(default_bw, 32));
    }
    if (!ci->params.count(id_BIT_WIDTH_1)) {
        ci->setParam(id_BIT_WIDTH_1, Property(default_bw, 32));
    }

    int bit_width = ci->params.at(id_BIT_WIDTH_0).as_int64();

    if ((bit_width == 32 || bit_width == 36) && gwu.need_SDP_fix()) {
        int bit_width_b = ci->params.at(id_BIT_WIDTH_1).as_int64();
        if (bit_width == bit_width_b) {
            divide_sdp(ci, new_cells);
        } else {
            log_error("The fix for SDP when ports A and B have different bit widths has not yet been implemented. "
                      "Cell: '%s'\n",
                      ci->type.c_str(ctx));
        }
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

    // If misconnected RESET
    if (gwu.need_BSRAM_RESET_fix()) {
        ci->renamePort(id_RESET, id_RESETB);
    }

    // Port A
    ci->addInput(id_WREA);
    ci->connectPort(id_WREA, vcc_net);

    // Port B
    ci->addInput(id_WREB);
    bit_width = ci->params.at(id_BIT_WIDTH_1).as_int64();

    if (bit_width == 32 || bit_width == 36) {
        ci->connectPort(id_WREB, vcc_net);
        bsram_rename_ports(ci, bit_width, "DO[%d]", "DO%d");
    } else {
        ci->connectPort(id_WREB, vss_net);
        bsram_rename_ports(ci, bit_width, "DO[%d]", "DO%d", 18);
    }
    bsram_rename_ports(ci, bit_width, "DI[%d]", "DI%d");
}

void GowinPacker::pack_DPB(CellInfo *ci)
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

void GowinPacker::divide_sp(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    if (ctx->verbose) {
        log_info("  divide SP\n");
    }

    int bw = ci->params.at(id_BIT_WIDTH).as_int64();
    NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
    NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();

    IdString cell_type = bw == 32 ? id_SP : id_SPX9;
    IdString name = ctx->idf("%s_AUX", ctx->nameOf(ci));

    auto sp_cell = gwu.create_cell(name, cell_type);
    CellInfo *sp = sp_cell.get();
    sp->setAttr(id_AUX, 1);

    ci->copyPortTo(id_CLK, sp, id_CLK);
    ci->copyPortTo(id_OCE, sp, id_OCE);
    ci->copyPortTo(id_CE, sp, id_CE);
    ci->copyPortTo(id_RESET, sp, id_RESET);
    ci->copyPortTo(id_WRE, sp, id_WRE);

    // XXX Separate "byte enable" port
    ci->movePortTo(ctx->id("AD[2]"), sp, ctx->id("AD[0]"));
    ci->movePortTo(ctx->id("AD[3]"), sp, ctx->id("AD[1]"));
    ci->connectPort(ctx->id("AD[2]"), vss_net);
    ci->connectPort(ctx->id("AD[3]"), vss_net);

    sp->addInput(ctx->id("AD[2]"));
    sp->connectPort(ctx->id("AD[2]"), vss_net);
    sp->addInput(ctx->id("AD[3]"));
    sp->connectPort(ctx->id("AD[3]"), vss_net);

    ci->disconnectPort(ctx->id("AD[4]"));
    ci->connectPort(ctx->id("AD[4]"), vss_net);
    sp->addInput(ctx->id("AD[4]"));
    sp->connectPort(ctx->id("AD[4]"), vcc_net);

    ci->copyPortBusTo(id_AD, 5, true, sp, id_AD, 5, true, 9);

    sp->params = ci->params;

    bw /= 2;
    ci->setParam(id_BIT_WIDTH, Property(bw, 32));
    sp->setParam(id_BIT_WIDTH, Property(bw, 32));
    ci->movePortBusTo(id_DI, bw, true, sp, id_DI, 0, true, bw);
    ci->movePortBusTo(id_DO, bw, true, sp, id_DO, 0, true, bw);

    ci->copyPortBusTo(ctx->id("BLKSEL"), 0, true, sp, ctx->id("BLKSEL"), 0, true, 3);

    new_cells.push_back(std::move(sp_cell));
}

void GowinPacker::pack_SP(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells)
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

    if (!ci->attrs.count(id_AUX)) {
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
    NetInfo *gnd_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();
    for (int i = 0; i < 3; ++i) {
        ci->renamePort(ctx->idf("BLKSEL[%d]", i), ctx->idf("BLKSEL%d", i));
        if (bit_width == 32 || bit_width == 36) {
            ci->copyPortTo(ctx->idf("BLKSEL%d", i), ci, ctx->idf("BLKSELB%d", i));
        }
    }

    for (int i = 0; i < 14; ++i) {
        ci->renamePort(ctx->idf("AD[%d]", i), ctx->idf("AD%d", i));
        if (bit_width == 32 || bit_width == 36) {
            // Since we are dividing 32/36 bits into two parts between
            // ports A and B, the ‘Byte Enables’ require special
            // separation.
            if (i < 4) {
                if (i > 1) {
                    ci->movePortTo(ctx->idf("AD%d", i), ci, ctx->idf("ADB%d", i - 2));
                    ci->connectPort(ctx->idf("AD%d", i), gnd_net);
                    ci->addInput(ctx->idf("ADB%d", i));
                    ci->connectPort(ctx->idf("ADB%d", i), gnd_net);
                }
            } else {
                ci->copyPortTo(ctx->idf("AD%d", i), ci, ctx->idf("ADB%d", i));
            }
        }
    }
    if (bit_width == 32 || bit_width == 36) {
        ci->copyPortTo(id_CLK, ci, id_CLKB);
        ci->copyPortTo(id_OCE, ci, id_OCEB);
        ci->copyPortTo(id_CE, ci, id_CEB);
        ci->copyPortTo(id_RESET, ci, id_RESETB);
        ci->copyPortTo(id_WRE, ci, id_WREB);
        ci->disconnectPort(ctx->id("AD4"));
        ci->connectPort(ctx->id("AD4"), gnd_net);
        ci->disconnectPort(ctx->id("ADB4"));
        ci->connectPort(ctx->id("ADB4"), vcc_net);
    }
    bsram_rename_ports(ci, bit_width, "DI[%d]", "DI%d");
    bsram_rename_ports(ci, bit_width, "DO[%d]", "DO%d");
}

void GowinPacker::pack_bsram(void)
{
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    log_info("Pack BSRAMs...\n");

    auto do_bsram = [&](CellInfo *ci) {
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
            pack_SDPB(ci, new_cells);
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
    };

    for (auto &cell : ctx->cells) {
        auto ci = cell.second.get();
        if (is_bsram(ci)) {
            do_bsram(ci);
        }
    }

    // Process new cells. New cells should not generate more.
    for (auto &cell : new_cells) {
        auto ci = cell.get();
        if (is_bsram(ci)) {
            do_bsram(ci);
        }
        ctx->cells[cell->name] = std::move(cell);
    }
}
NEXTPNR_NAMESPACE_END

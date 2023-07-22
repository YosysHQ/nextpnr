#include "log.h"
#include "nextpnr.h"

#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_helpers.h"

#include "gowin.h"
#include "pack.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct GowinPacker
{
    Context *ctx;
    HimbaechelHelpers h;

    GowinPacker(Context *ctx) : ctx(ctx) { h.init(ctx); }

    // ===================================
    // IO
    // ===================================
    void pack_iobs(void)
    {
        log_info("Pack IOBs...\n");
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
                CellTypePort(id_IBUF, id_I),
                CellTypePort(id_OBUF, id_O),
        };
        std::vector<IdString> to_remove;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (!ci.type.in(ctx->id("$nextpnr_ibuf"), ctx->id("$nextpnr_obuf"), ctx->id("$nextpnr_iobuf")))
                continue;
            NetInfo *i = ci.getPort(ctx->id("I"));
            if (i && i->driver.cell) {
                if (!top_ports.count(CellTypePort(i->driver)))
                    log_error("Top-level port '%s' driven by illegal port %s.%s\n", ctx->nameOf(&ci),
                              ctx->nameOf(i->driver.cell), ctx->nameOf(i->driver.port));
                for (const auto &attr : ci.attrs) {
                    i->driver.cell->attrs[attr.first] = attr.second;
                }
            }
            NetInfo *o = ci.getPort(ctx->id("O"));
            if (o) {
                for (auto &usr : o->users) {
                    if (!top_ports.count(CellTypePort(usr)))
                        log_error("Top-level port '%s' driving illegal port %s.%s\n", ctx->nameOf(&ci),
                                  ctx->nameOf(usr.cell), ctx->nameOf(usr.port));
                    for (const auto &attr : ci.attrs) {
                        usr.cell->attrs[attr.first] = attr.second;
                    }
                }
            }
            ci.disconnectPort(ctx->id("I"));
            ci.disconnectPort(ctx->id("O"));
            to_remove.push_back(ci.name);
        }
        for (IdString cell_name : to_remove)
            ctx->cells.erase(cell_name);
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
                if (ctx->debug)
                    log_info("%s user %s/%s\n", ctx->nameOf(constnet), ctx->nameOf(uc), user.port.c_str(ctx));

                if (is_lut(uc) && (user.port.str(ctx).at(0) == 'I')) {
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
                        log_info("%s lut config modified from 0x%lX to 0x%lX\n", ctx->nameOf(uc),
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
                NPNR_ASSERT(in && in->driver.cell);
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
            cin_ci->params[id_ALU_MODE] = std::string("C2L");
            cin_ci->addInput(id_I2);
            cin_ci->connectPort(id_I2, ctx->nets[ctx->id("$PACKER_VCC")].get());
            return cin_ci;
        }
        if (cin_net->name == ctx->id("$PACKER_VCC")) {
            cin_ci->params[id_ALU_MODE] = std::string("ONE2C");
            cin_ci->addInput(id_I2);
            cin_ci->connectPort(id_I2, ctx->nets[ctx->id("$PACKER_VCC")].get());
            return cin_ci;
        }
        // CIN from logic
        cin_ci->addInput(id_I1);
        cin_ci->addInput(id_I3);
        cin_ci->addInput(id_I2);
        cin_ci->connectPort(id_I1, cin_net);
        cin_ci->connectPort(id_I3, cin_net);
        cin_ci->connectPort(id_I2, ctx->nets[ctx->id("$PACKER_VCC")].get());
        cin_ci->params[id_ALU_MODE] = std::string("0"); // ADD
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

        cout_ci->params[id_ALU_MODE] = std::string("C2L");
        return cout_ci;
    }

    // create ALU filler block
    std::unique_ptr<CellInfo> alu_add_dummy_block(Context *ctx, CellInfo *tail)
    {
        std::string name = tail->name.str(ctx) + "_DUMMY_ALULC";
        IdString name_id = ctx->id(name);

        auto dummy_ci = std::make_unique<CellInfo>(ctx, name_id, id_ALU);
        dummy_ci->params[id_ALU_MODE] = std::string("C2L");
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
                        ci->cluster = cin_block_ci->name;
                        ci->constr_abs_z = false;
                        ci->constr_x = alu_chain_len / 6;
                        ci->constr_y = 0;
                        ci->constr_z = alu_chain_len % 6;
                        // XXX I2 is pin C which must be set to 1 for all ALU modes except MUL
                        // we use only mode 2 ADDSUB so create and connect this pin
                        ci->addInput(id_I2);
                        ci->connectPort(id_I2, ctx->nets[ctx->id("$PACKER_VCC")].get());

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
                            cout_block_ci->cluster = cin_block_ci->name;
                            cout_block_ci->constr_abs_z = false;
                            cout_block_ci->constr_x = alu_chain_len / 6;
                            cout_block_ci->constr_y = 0;
                            cout_block_ci->constr_z = alu_chain_len % 6;
                            if (ctx->debug) {
                                log_info("Add ALU carry out to the chain (len:%d): %s\n", alu_chain_len,
                                         ctx->nameOf(cout_block_ci));
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

        int lutffs = h.constrain_cell_pairs(lut_outs, dff_ins, 1);
        log_info("Constrained %d LUTFF pairs.\n", lutffs);
    }

    // ===================================
    // SSRAM cluster
    // ===================================
    // create ALU filler block
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
            lut_ci->params[id_INIT] = ci->params.at(init_name);
        } else {
            lut_ci->params[id_INIT] = std::string("1111111111111111");
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
                ci->connectPort(id_CE, ctx->nets[ctx->id("$PACKER_VCC")].get());

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
    // Global set/reset
    // ===================================
    void pack_gsr(void)
    {
        log_info("Packing GSR..\n");

        bool user_gsr = false;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;

            if (ci.type == id_GSR) {
                user_gsr = true;
                break;
            }
        }
        if (!user_gsr) {
            bool have_gsr_bel = false;
            auto bels = ctx->getBels();
            for (auto bid : bels) {
                if (ctx->getBelType(bid) == id_GSR) {
                    have_gsr_bel = true;
                    break;
                }
            }
            if (have_gsr_bel) {
                // make default GSR
                auto gsr_cell = std::make_unique<CellInfo>(ctx, id_GSR, id_GSR);
                gsr_cell->addInput(id_GSRI);
                gsr_cell->connectPort(id_GSRI, ctx->nets[ctx->id("$PACKER_VCC")].get());
                ctx->cells[gsr_cell->name] = std::move(gsr_cell);
            } else {
                log_info("No GSR in the chip base\n");
            }
        }
    }

    // ===================================
    // PLL
    // ===================================
    void pack_pll(void)
    {
        log_info("Packing PLL..\n");

        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;

            if (ci.type == id_rPLL) {
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
            }
        }
    }

    void run(void)
    {
        pack_iobs();
        handle_constants();
        pack_gsr();
        pack_wideluts();
        pack_alus();
        constrain_lutffs();
        pack_pll();
        pack_ram16sdp4();
    }
};
} // namespace

void gowin_pack(Context *ctx)
{
    GowinPacker packer(ctx);
    packer.run();
}

NEXTPNR_NAMESPACE_END

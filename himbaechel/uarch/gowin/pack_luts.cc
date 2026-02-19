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
// Constant nets
// ===================================
void GowinPacker::handle_constants(void)
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
        constnet->constant_value = (constnet->name == ctx->id("$PACKER_GND")) ? id_VSS : id_VCC;
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
void GowinPacker::pack_wideluts(void)
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
std::unique_ptr<CellInfo> GowinPacker::alu_add_cin_block(Context *ctx, CellInfo *head, NetInfo *cin_net,
                                                         bool cin_is_vcc, bool cin_is_gnd)
{
    std::string name = head->name.str(ctx) + "_HEAD_ALULC";
    IdString name_id = ctx->id(name);

    NetInfo *cout_net = ctx->createNet(name_id);
    head->disconnectPort(id_CIN);
    head->connectPort(id_CIN, cout_net);

    auto cin_ci = std::make_unique<CellInfo>(ctx, name_id, id_ALU);
    cin_ci->addOutput(id_COUT);
    cin_ci->connectPort(id_COUT, cout_net);

    if (cin_is_gnd) {
        cin_ci->setParam(id_ALU_MODE, std::string("C2L"));
        cin_ci->addInput(id_I2);
        cin_ci->connectPort(id_I2, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
        return cin_ci;
    }
    if (cin_is_vcc) {
        cin_ci->setParam(id_ALU_MODE, std::string("ONE2C"));
        cin_ci->addInput(id_I2);
        cin_ci->connectPort(id_I2, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
        return cin_ci;
    }
    // CIN from logic
    cin_ci->addInput(id_I2);
    cin_ci->connectPort(id_I2, ctx->nets.at(ctx->id("$PACKER_VCC")).get());
    cin_ci->addInput(id_I0);
    cin_ci->connectPort(id_I0, cin_net);
    cin_ci->setParam(id_RAW_ALU_LUT, 0x505a); // 0101_0000_0101_1010 -> ignore I1 and I3, out carry = I0
    cin_ci->setParam(id_CIN_NETTYPE, Property("LOGIC"));
    return cin_ci;
}

// create ALU COUT block
std::unique_ptr<CellInfo> GowinPacker::alu_add_cout_block(Context *ctx, CellInfo *tail, NetInfo *cout_net)
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
std::unique_ptr<CellInfo> GowinPacker::alu_add_dummy_block(Context *ctx, CellInfo *tail)
{
    std::string name = tail->name.str(ctx) + "_DUMMY_ALULC";
    IdString name_id = ctx->id(name);

    auto dummy_ci = std::make_unique<CellInfo>(ctx, name_id, id_ALU);
    dummy_ci->setParam(id_ALU_MODE, std::string("C2L"));
    return dummy_ci;
}

// optimize ALU wiring
// A very simple ALU optimization: once we detect that one of the inputs is
// a constant, we modify the main LUT that describes the ALU function so
// that this primitive input is ignored, and then disconnect it from the
// network, freeing up the PIP.
// For example (unrealistic, since a real ALU LUT has a larger size and
// service bits in the middle, etc.), the addition function of A and B when
// A = 1 is converted from the general case (A isn't a constant and B isn't a
// constant) to a special case:
// 0110 -> 0011
void GowinPacker::optimize_alu_lut(CellInfo *ci, int mode)
{
    auto uni_shift = [&](unsigned int val, int amount) {
        if (amount < 0) {
            return val >> -amount;
        }
        return val << amount;
    };

    IdString vcc_net_name = ctx->id("$PACKER_VCC");
    IdString gnd_net_name = ctx->id("$PACKER_GND");
    bool optimized = false;
    switch (mode) {
    case 2: {
        // ALU LUT for mode 2 is 0110_0000_1001_1010 for all chips
        // We will change this feature if the next
        // unreleased Gowin chip series changes this
        // representation.
        // If ADDSUB dynamically switches between + and -,
        // optimization is not possible.
        int possible_carry = 0b1100U;
        IdString inp_net_name = ci->getPort(id_I3)->name;
        if (inp_net_name != vcc_net_name && inp_net_name != gnd_net_name) {
            break;
        }
        if (inp_net_name == gnd_net_name) {
            possible_carry = 0b0011U;
        }
        unsigned int alu_lut = 0b0110000010011010U;
        for (int i = 0; i < 3; ++i) {
            if (i == 2) {
                break;
            }
            IdString inp_name = ctx->idf("I%d", i);
            inp_net_name = ci->getPort(inp_name)->name;
            if (inp_net_name == vcc_net_name || inp_net_name == gnd_net_name) {
                ci->disconnectPort(inp_name);
                optimized = true;

                // fix the carry
                if (i == 0) {
                    if (inp_net_name == vcc_net_name) {
                        alu_lut |= 0xfU;
                    } else {
                        alu_lut &= ~0xfU;
                        alu_lut |= possible_carry;
                    }
                }

                // We rearrange bits to account for constant networks
                int bit_n = 4;
                int copy_dist = 1 << i;
                if (inp_net_name == vcc_net_name) {
                    bit_n += copy_dist;
                    copy_dist = -copy_dist;
                }
                for (int j = 0; j < 4; ++j) {
                    alu_lut &= ~(1 << (bit_n + copy_dist));
                    alu_lut |= uni_shift(alu_lut & (1 << bit_n), copy_dist);
                    switch (i) {
                    case 0: // skip the service bits
                        bit_n += j == 1 ? 5 : 1;
                        break;
                    case 1: // skip the service bits
                        bit_n += j == 1 ? 6 : 0;
                        break;
                    default:
                        break;
                    }
                    ++bit_n;
                }
            }
        }
        if (optimized) {
            ci->setParam(id_RAW_ALU_LUT, alu_lut);
        }
    } break;
    default:
        break;
    }
}

// create ALU chain
void GowinPacker::pack_alus(void)
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

                bool cin_is_vcc = cin_net->name == ctx->id("$PACKER_VCC");
                bool cin_is_gnd = cin_net->name == ctx->id("$PACKER_GND");
                bool cin_is_logic = !cin_is_vcc && !cin_is_gnd;
                CellInfo *cin_block_ci;
                int alu_chain_len;

                // According to the documentation, GW5A can use CIN from
                // logic using the input MUX, but in practice this has not
                // yet been achieved. We are leaving the old mechanism in
                // place for this case.
                if ((!gwu.has_CIN_MUX()) || cin_is_logic) {
                    // prepend first ALU with carry generator block
                    // three cases: CIN == 0, CIN == 1 and CIN == ?
                    new_cells.push_back(alu_add_cin_block(ctx, ci, cin_net, cin_is_vcc, cin_is_gnd));
                    cin_block_ci = new_cells.back().get();
                    // CIN block is the cluster root and is always placed in ALU0
                    alu_chain_len = 1;
                } else {
                    cin_block_ci = ci;
                    ci->disconnectPort(id_CIN);
                    if (cin_is_vcc) {
                        ci->setParam(id_CIN_NETTYPE, Property("VCC"));
                    } else {
                        ci->setParam(id_CIN_NETTYPE, Property("GND"));
                    }
                    alu_chain_len = 0;
                }
                cin_block_ci->cluster = cin_block_ci->name;
                cin_block_ci->constr_z = BelZ::ALU0_Z;
                cin_block_ci->constr_abs_z = true;

                while (true) {
                    if (ci != cin_block_ci) {
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
                    }
                    // optimize only MODE=2 for now
                    if (ci->params.at(id_ALU_MODE).as_int64() == 2) {
                        optimize_alu_lut(ci, 2);
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
                        new_cells.push_back(alu_add_cout_block(ctx, ci, cout_net));
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
                    new_cells.push_back(alu_add_dummy_block(ctx, ci));
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
    new_cells.clear();
    // The placer doesn't know "a priori" that LUTs and ALUs conflict. So create blocker LUTs to make this explicit and reduce wasted legalisation effort
    for (auto &cell : ctx->cells) {
        auto ci = cell.second.get();
        if (ci->cluster == ClusterId()) {
            continue;
        }
        if (is_alu(ci)) {
            auto cell = std::make_unique<CellInfo>(ctx, ctx->idf("%s_BLOCKER_LUT", ctx->nameOf(ci)), id_BLOCKER_LUT);
            cell->cluster = ci->cluster;
            ctx->cells.at(cell->cluster)->constr_children.push_back(cell.get());
            cell->constr_abs_z = true;
            cell->constr_x = ci->constr_x;
            cell->constr_y = ci->constr_y;
            cell->constr_z = 2 * (ci->constr_z - (ci->constr_abs_z ? BelZ::ALU0_Z : 0));
            new_cells.emplace_back(std::move(cell));
        }
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

// ===================================
// glue LUT and FF
// ===================================
void GowinPacker::constrain_lutffs(void)
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
std::unique_ptr<CellInfo> GowinPacker::ssram_make_lut(Context *ctx, CellInfo *ci, int index)
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

void GowinPacker::pack_ssram(void)
{
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    std::vector<IdString> cells_to_remove;

    log_info("Pack SSRAMs...\n");
    for (auto &cell : ctx->cells) {
        auto ci = cell.second.get();
        if (ci->cluster != ClusterId()) {
            continue;
        }

        if (is_ssram(ci)) {
            if (ci->type == id_ROM16) {
                new_cells.push_back(ssram_make_lut(ctx, ci, 0));
                CellInfo *lut_ci = new_cells.back().get();
                // inputs
                ci->movePortBusTo(id_AD, 0, true, lut_ci, id_I, 0, false, 4);
                // output
                ci->movePortTo(id_DO, lut_ci, id_F);

                cells_to_remove.push_back(ci->name);
                continue;
            }
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
                new_cells.push_back(ssram_make_lut(ctx, ci, i));
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
    for (auto cell : cells_to_remove) {
        ctx->cells.erase(cell);
    }
}

NEXTPNR_NAMESPACE_END

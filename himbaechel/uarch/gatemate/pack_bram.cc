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

#include "design_utils.h"
#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

inline bool is_bram_40k(const BaseCtx *ctx, const CellInfo *cell) { return cell->type.in(id_CC_BRAM_40K); }

uint8_t GateMatePacker::ram_ctrl_signal(CellInfo *cell, IdString port, bool alt)
{
    NetInfo *net = cell->getPort(port);
    if (net) {
        if (net->name == ctx->id("$PACKER_GND")) {
            cell->disconnectPort(port);
            return 0b00000011;
        } else if (net->name == ctx->id("$PACKER_VCC")) {
            cell->disconnectPort(port);
            return 0b00010011;
        } else {
            return alt ? 0b00000100 : 0b00000000;
        }
    }
    return 0b00000011;
}

uint8_t GateMatePacker::ram_clk_signal(CellInfo *cell, IdString port)
{
    NetInfo *clk_net = cell->getPort(port);
    if (!global_signals.count(clk_net)) {
        return 0b00000000;
    } else {
        int index = global_signals[clk_net];
        uint8_t val = 0;
        switch (index) {
        case 0:
            val = 0b00100011;
            break;
        case 1:
            val = 0b00110011;
            break;
        case 2:
            val = 0b00000011;
            break;
        case 3:
            val = 0b00010011;
            break;
        }
        return val;
    }
}

int width_to_config(int width)
{
    switch (width) {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return 2;
    case 3 ... 5:
        return 3;
    case 6 ... 10:
        return 4;
    case 11 ... 20:
        return 5;
    case 21 ... 40:
        return 6;
    case 41 ... 80:
        return 7;
    default:
        log_error("Unsupported width '%d'.\n", width);
    }
}

static void rename_or_move(CellInfo *main, CellInfo *other, IdString port, IdString other_port)
{
    if (main == other)
        main->renamePort(port, other_port);
    else
        main->movePortTo(port, other, other_port);
}

void GateMatePacker::pack_ram_cell(CellInfo &ci, CellInfo *cell, int num, bool is_split)
{
    // Port Widths
    int a_rd_width = int_or_default(cell->params, id_A_RD_WIDTH, 0);
    int b_rd_width = int_or_default(cell->params, id_B_RD_WIDTH, 0);
    int a_wr_width = int_or_default(cell->params, id_A_WR_WIDTH, 0);
    int b_wr_width = int_or_default(cell->params, id_B_WR_WIDTH, 0);

    std::string a_wr_mode_str = str_or_default(cell->params, id_A_WR_MODE, "NO_CHANGE");
    if (a_wr_mode_str != "NO_CHANGE" && a_wr_mode_str != "WRITE_THROUGH")
        log_error("Unknown A_WR_MODE parameter value '%s' for cell %s.\n", a_wr_mode_str.c_str(),
                  cell->name.c_str(ctx));
    int a_wr_mode = a_wr_mode_str == "NO_CHANGE" ? 0 : 1;
    std::string b_wr_mode_str = str_or_default(cell->params, id_B_WR_MODE, "NO_CHANGE");
    if (b_wr_mode_str != "NO_CHANGE" && b_wr_mode_str != "WRITE_THROUGH")
        log_error("Unknown B_WR_MODE parameter value '%s' for cell %s.\n", b_wr_mode_str.c_str(),
                  cell->name.c_str(ctx));
    int b_wr_mode = b_wr_mode_str == "NO_CHANGE" ? 0 : 1;

    // Inverting Control Pins
    int a_clk_inv = int_or_default(cell->params, id_A_CLK_INV, 0);
    int b_clk_inv = int_or_default(cell->params, id_B_CLK_INV, 0);
    int a_en_inv = int_or_default(cell->params, id_A_EN_INV, 0);
    int b_en_inv = int_or_default(cell->params, id_B_EN_INV, 0);
    int a_we_inv = int_or_default(cell->params, id_A_WE_INV, 0);
    int b_we_inv = int_or_default(cell->params, id_B_WE_INV, 0);

    // Output Register
    int a_do_reg = int_or_default(cell->params, id_A_DO_REG, 0);
    int b_do_reg = int_or_default(cell->params, id_B_DO_REG, 0);

    uint8_t cfg_a = ram_clk_signal(cell, id_A_CLK);
    uint8_t cfg_b = ram_clk_signal(cell, id_B_CLK);
    uint8_t a_inv = a_clk_inv << 2 | a_we_inv << 1 | a_en_inv;
    uint8_t b_inv = b_clk_inv << 2 | b_we_inv << 1 | b_en_inv;
    uint8_t a_en = ram_ctrl_signal(cell, id_A_EN, false);
    uint8_t b_en = ram_ctrl_signal(cell, id_B_EN, false);
    uint8_t a_we = ram_ctrl_signal(cell, id_A_WE, false);
    uint8_t b_we = ram_ctrl_signal(cell, id_B_WE, false);

    if (num) {
        ci.params[id_RAM_cfg_forward_a1_clk] = Property(cfg_a, 8);
        ci.params[id_RAM_cfg_forward_b1_clk] = Property(cfg_b, 8);

        ci.params[id_RAM_cfg_forward_a1_en] = Property(a_en, 8);
        ci.params[id_RAM_cfg_forward_b1_en] = Property(b_en, 8);

        ci.params[id_RAM_cfg_forward_a1_we] = Property(a_we, 8);
        ci.params[id_RAM_cfg_forward_b1_we] = Property(b_we, 8);

        ci.params[id_RAM_cfg_input_config_a1] = Property(width_to_config(a_wr_width), 3);
        ci.params[id_RAM_cfg_input_config_b1] = Property(width_to_config(b_wr_width), 3);
        ci.params[id_RAM_cfg_output_config_a1] = Property(width_to_config(a_rd_width), 3);
        ci.params[id_RAM_cfg_output_config_b1] = Property(width_to_config(b_rd_width), 3);

        ci.params[id_RAM_cfg_a1_writemode] = Property(a_wr_mode, 1);
        ci.params[id_RAM_cfg_b1_writemode] = Property(b_wr_mode, 1);

        ci.params[id_RAM_cfg_a1_set_outputreg] = Property(a_do_reg, 1);
        ci.params[id_RAM_cfg_b1_set_outputreg] = Property(b_do_reg, 1);

        ci.params[id_RAM_cfg_inversion_a1] = Property(a_inv, 3);
        ci.params[id_RAM_cfg_inversion_b1] = Property(b_inv, 3);
    } else {
        ci.params[id_RAM_cfg_forward_a0_clk] = Property(cfg_a, 8);
        if (!is_split)
            ci.params[id_RAM_cfg_forward_a1_clk] = Property(cfg_a, 8);

        ci.params[id_RAM_cfg_forward_b0_clk] = Property(cfg_b, 8);
        if (!is_split)
            ci.params[id_RAM_cfg_forward_b1_clk] = Property(cfg_b, 8);

        ci.params[id_RAM_cfg_forward_a0_en] = Property(a_en, 8);
        ci.params[id_RAM_cfg_forward_b0_en] = Property(b_en, 8);

        ci.params[id_RAM_cfg_forward_a0_we] = Property(a_we, 8);
        ci.params[id_RAM_cfg_forward_b0_we] = Property(b_we, 8);

        ci.params[id_RAM_cfg_input_config_a0] = Property(width_to_config(a_wr_width), 3);
        ci.params[id_RAM_cfg_input_config_b0] = Property(width_to_config(b_wr_width), 3);
        ci.params[id_RAM_cfg_output_config_a0] = Property(width_to_config(a_rd_width), 3);
        ci.params[id_RAM_cfg_output_config_b0] = Property(width_to_config(b_rd_width), 3);

        ci.params[id_RAM_cfg_a0_writemode] = Property(a_wr_mode, 1);
        ci.params[id_RAM_cfg_b0_writemode] = Property(b_wr_mode, 1);

        ci.params[id_RAM_cfg_a0_set_outputreg] = Property(a_do_reg, 1);
        ci.params[id_RAM_cfg_b0_set_outputreg] = Property(b_do_reg, 1);

        ci.params[id_RAM_cfg_inversion_a0] = Property(a_inv, 3);
        ci.params[id_RAM_cfg_inversion_b0] = Property(b_inv, 3);
    }

    int index = (num == 0) ? 0 : 2;
    rename_or_move(cell, &ci, id_A_CLK, ctx->idf("CLKA[%d]", index));
    rename_or_move(cell, &ci, id_B_CLK, ctx->idf("CLKB[%d]", index));
    rename_or_move(cell, &ci, id_A_EN, ctx->idf("ENA[%d]", index));
    rename_or_move(cell, &ci, id_B_EN, ctx->idf("ENB[%d]", index));
    rename_or_move(cell, &ci, id_A_WE, ctx->idf("GLWEA[%d]", index));
    rename_or_move(cell, &ci, id_B_WE, ctx->idf("GLWEB[%d]", index));
    int items = is_split ? 20 : 40;
    for (int i = 0; i < items; i++) {
        rename_or_move(cell, &ci, ctx->idf("A_BM[%d]", i), ctx->idf("WEA[%d]", i + num * 20));
        rename_or_move(cell, &ci, ctx->idf("B_BM[%d]", i), ctx->idf("WEB[%d]", i + num * 20));
    }

    for (int i = 0; i < 16; i++) {
        rename_or_move(cell, &ci, ctx->idf("A_ADDR[%d]", i), ctx->idf("ADDRA%d[%d]", num, i));
        rename_or_move(cell, &ci, ctx->idf("B_ADDR[%d]", i), ctx->idf("ADDRB%d[%d]", num, i));
    }

    for (int i = 0; i < items; i++) {
        rename_or_move(cell, &ci, ctx->idf("A_DI[%d]", i), ctx->idf("DIA[%d]", i + num * 20));
        rename_or_move(cell, &ci, ctx->idf("A_DO[%d]", i), ctx->idf("DOA[%d]", i + num * 20));
        rename_or_move(cell, &ci, ctx->idf("B_DI[%d]", i), ctx->idf("DIB[%d]", i + num * 20));
        rename_or_move(cell, &ci, ctx->idf("B_DO[%d]", i), ctx->idf("DOB[%d]", i + num * 20));
    }
}

void GateMatePacker::pack_ram()
{
    std::vector<std::pair<CellInfo *, CellInfo *>> rams;
    std::vector<std::pair<CellInfo *, CellInfo *>> rams_merged[2];
    std::map<CellInfo *, CellInfo *> ram_cascade;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_BRAM_20K, id_CC_BRAM_40K, id_CC_FIFO_40K))
            continue;
        int split = ci.type.in(id_CC_BRAM_20K) ? 1 : 0;
        std::string ram_mode_str = str_or_default(ci.params, id_RAM_MODE, "SDP");
        if (ram_mode_str != "SDP" && ram_mode_str != "TDP")
            log_error("Unknown RAM_MODE parameter value '%s' for cell %s.\n", ram_mode_str.c_str(), ci.name.c_str(ctx));
        int ram_mode = ram_mode_str == "SDP" ? 1 : 0;
        std::string cas = str_or_default(ci.params, id_CAS, "NONE");
        if (cas != "NONE" && !ci.type.in(id_CC_BRAM_40K))
            log_error("Cascade feature only supported for CC_BRAM_40K.\n");
        if (split) {
            bool added = false;
            if (!rams_merged[ram_mode].empty()) {
                auto &last = rams_merged[ram_mode].back();
                if (last.second == nullptr) {
                    last.second = &ci;
                    packed_cells.insert(ci.name);
                    added = true;
                }
            }
            if (!added)
                rams_merged[ram_mode].push_back(std::make_pair(&ci, nullptr));
        } else {
            CellInfo *upper = nullptr;
            CellInfo *lower = nullptr;
            if (cas != "NONE" && ram_mode_str != "TDP")
                log_error("Cascade feature only supported in TDP mode.\n");
            int a_rd_width = int_or_default(ci.params, id_A_WIDTH, 0);
            int b_wr_width = int_or_default(ci.params, id_B_WIDTH, 0);
            if (cas != "NONE" && (a_rd_width > 1 || b_wr_width > 1))
                log_error("Cascade feature only supported in 1 bit data width mode.\n");
            if (cas == "UPPER") {
                if (!net_driven_by(ctx, ci.getPort(id_A_CI), is_bram_40k, id_A_CO))
                    log_error("Port A_CI of '%s' must be driven by other CC_BRAM_40K.", ci.name.c_str(ctx));
                if (!net_driven_by(ctx, ci.getPort(id_B_CI), is_bram_40k, id_B_CO))
                    log_error("Port B_CI of '%s' must be driven by other CC_BRAM_40K.", ci.name.c_str(ctx));
                upper = &ci;
                lower = ci.getPort(id_A_CI)->driver.cell;
            } else if (cas == "LOWER") {
                if (!net_only_drives(ctx, ci.getPort(id_A_CO), is_bram_40k, id_A_CI, true))
                    log_error("Port A_CO of '%s' must be driving one other CC_BRAM_40K.", ci.name.c_str(ctx));
                if (!net_only_drives(ctx, ci.getPort(id_B_CO), is_bram_40k, id_B_CI, true))
                    log_error("Port B_CO of '%s' must be driving one other CC_BRAM_40K.", ci.name.c_str(ctx));
                upper = (*ci.getPort(id_A_CO)->users.begin()).cell;
                lower = &ci;
            }
            if (ram_cascade.count(lower) && ram_cascade[lower] != upper)
                log_error("RAM cell '%s' already cascaded to different RAM block.\n", ci.name.c_str(ctx));
            ram_cascade[lower] = upper;

            rams.push_back(std::make_pair(&ci, nullptr));
        }
    }
    rams.insert(rams.end(), rams_merged[0].begin(), rams_merged[0].end());
    rams.insert(rams.end(), rams_merged[1].begin(), rams_merged[1].end());

    for (auto item : rams) {
        CellInfo &ci = *item.first;
        int split = ci.type.in(id_CC_BRAM_20K) ? 1 : 0;
        bool is_fifo = ci.type.in(id_CC_FIFO_40K);

        ci.type = id_RAM;
        ci.cluster = ci.name;

        // Location format: D(0..N-1)X(0..3)Y(0..7) or UNPLACED
        std::string loc = str_or_default(ci.params, id_LOC, "UNPLACED");
        std::string cas = str_or_default(ci.params, id_CAS, "NONE");

        int cascade = 0;
        // Concepts of UPPER and LOWER are different in documentation
        if (cas == "NONE") {
            cascade = 0;
        } else if (cas == "UPPER") {
            cascade = 2;
            ci.disconnectPort(id_A_CI);
            ci.disconnectPort(id_B_CI);
        } else if (cas == "LOWER") {
            cascade = 1;
            ci.disconnectPort(id_A_CO);
            ci.disconnectPort(id_B_CO);
            if (!ram_cascade.count(&ci))
                log_error("Unable to find cascaded RAM for '%s'.\n", ci.name.c_str(ctx));
            CellInfo *upper = ram_cascade[&ci];
            ci.cluster = upper->name;
            upper->constr_children.push_back(&ci);
            ci.constr_abs_z = false;
            ci.constr_y = -16;
            ci.constr_z = 0;
        } else {
            log_error("Unknown CAS parameter value '%s' for cell %s.\n", cas.c_str(), ci.name.c_str(ctx));
        }

        // RAM and Write Modes
        std::string ram_mode_str = str_or_default(ci.params, id_RAM_MODE, "SDP");
        if (ram_mode_str != "SDP" && ram_mode_str != "TDP")
            log_error("Unknown RAM_MODE parameter value '%s' for cell %s.\n", ram_mode_str.c_str(), ci.name.c_str(ctx));
        int ram_mode = ram_mode_str == "SDP" ? 1 : 0;

        // Error Checking and Correction
        int a_ecc_en = int_or_default(ci.params, id_A_ECC_EN, 0);
        int b_ecc_en = int_or_default(ci.params, id_B_ECC_EN, 0);

        ci.params[id_RAM_cfg_forward_a_addr] = Property(0b00000000, 8);
        ci.params[id_RAM_cfg_forward_b_addr] = Property(0b00000000, 8);

        ci.params[id_RAM_cfg_sram_mode] = Property(ram_mode << 1 | split, 2);

        pack_ram_cell(ci, item.first, 0, split);
        if (item.second) {
            pack_ram_cell(ci, item.second, 1, split);
        }
        if (split) {
            for (int i = 63; i >= 0; i--) {
                std::vector<bool> orig_first =
                        item.first->params.at(ctx->idf("INIT_%02X", i)).extract(0, 320).as_bits();
                std::vector<bool> orig_second;
                if (item.second)
                    orig_second = item.second->params.at(ctx->idf("INIT_%02X", i)).extract(0, 320).as_bits();
                std::string init[2];

                for (int j = 0; j < 2; j++) {
                    for (int k = 0; k < 4; k++) {
                        for (int l = 0; l < 40; l++) {
                            if (item.second)
                                init[j].push_back(orig_second.at(319 - (l + k * 40 + j * 160)) ? '1' : '0');
                            else
                                init[j].push_back('0');
                        }
                        for (int l = 0; l < 40; l++) {
                            init[j].push_back(orig_first.at(319 - (l + k * 40 + j * 160)) ? '1' : '0');
                        }
                    }
                }
                ci.params[ctx->idf("INIT_%02X", i * 2 + 1)] = Property::from_string(init[0]);
                ci.params[ctx->idf("INIT_%02X", i * 2 + 0)] = Property::from_string(init[1]);
            }
        }

        if (is_fifo) {
            int a_rd_width = int_or_default(ci.params, id_A_WIDTH, 0);
            int b_wr_width = int_or_default(ci.params, id_B_WIDTH, 0);
            if (a_rd_width != b_wr_width)
                log_error("The FIFO configuration of A_WIDTH and B_WIDTH must be equal.\n");

            if (a_rd_width != 80 && ram_mode == 1)
                log_error("FIFO SDP is ony supported in 80 bit mode.\n");

            ci.params[id_RAM_cfg_input_config_b0] = Property(width_to_config(b_wr_width), 3);
            ci.params[id_RAM_cfg_output_config_a0] = Property(width_to_config(a_rd_width), 3);

            std::string fifo_mode_str = str_or_default(ci.params, id_FIFO_MODE, "SYNC");
            if (fifo_mode_str != "SYNC" && fifo_mode_str != "ASYNC")
                log_error("Unknown FIFO_MODE parameter value '%s' for cell %s.\n", fifo_mode_str.c_str(),
                          ci.name.c_str(ctx));
            int fifo_mode = fifo_mode_str == "SYNC" ? 1 : 0;

            if (fifo_mode)
                ci.params[id_RAM_cfg_fifo_sync_enable] = Property(0b1, 1);
            else
                ci.params[id_RAM_cfg_fifo_async_enable] = Property(0b1, 1);

            // TODO: Handle dynamic almost empty/full
            int dyn_stat_select = int_or_default(ci.params, id_DYN_STAT_SELECT, 0);
            if (dyn_stat_select != 0 && dyn_stat_select != 1)
                log_error("DYN_STAT_SELECT must be 0 or 1.\n");
            if (dyn_stat_select != 0 && ram_mode == 1)
                log_error("Dynamic FIFO offset configuration is not supported in SDP mode.\n");
            ci.params[id_RAM_cfg_dyn_stat_select] = Property(dyn_stat_select << 1, 2);
            ci.params[id_RAM_cfg_almost_empty_offset] =
                    Property(int_or_default(ci.params, id_F_ALMOST_EMPTY_OFFSET, 0), 15);
            ci.params[id_RAM_cfg_almost_full_offset] =
                    Property(int_or_default(ci.params, id_F_ALMOST_FULL_OFFSET, 0), 15);
        }

        ci.params[id_RAM_cfg_ecc_enable] = Property(b_ecc_en << 1 | a_ecc_en, 2);
        ci.params[id_RAM_cfg_sram_delay] = Property(0b000101, 6); // Always set to default
        // id_RAM_cfg_datbm_sel
        ci.params[id_RAM_cfg_cascade_enable] = Property(cascade, 2);

        for (int i = 0; i < 40; i++) {
            move_ram_o(&ci, ctx->idf("WEA[%d]", i));
            move_ram_o(&ci, ctx->idf("WEB[%d]", i));
        }

        for (int i = 0; i < 16; i++) {
            move_ram_o(&ci, ctx->idf("ADDRA0[%d]", i));
            move_ram_o(&ci, ctx->idf("ADDRB0[%d]", i));
            move_ram_o(&ci, ctx->idf("ADDRA1[%d]", i));
            move_ram_o(&ci, ctx->idf("ADDRB1[%d]", i));
        }

        for (int i = 0; i < 40; i++) {
            move_ram_io(&ci, ctx->idf("DOA[%d]", i), ctx->idf("DIA[%d]", i));
            move_ram_io(&ci, ctx->idf("DOB[%d]", i), ctx->idf("DIB[%d]", i));
        }
        for (int i = 0; i < 4; i++) {
            move_ram_o(&ci, ctx->idf("CLKA[%d]", i));
            move_ram_o(&ci, ctx->idf("CLKB[%d]", i));
            move_ram_o(&ci, ctx->idf("ENA[%d]", i));
            move_ram_o(&ci, ctx->idf("ENB[%d]", i));
            move_ram_o(&ci, ctx->idf("GLWEA[%d]", i));
            move_ram_o(&ci, ctx->idf("GLWEB[%d]", i));
        }

        if (is_fifo) {
            for (int i = 0; i < 15; i++) {
                ci.disconnectPort(ctx->idf("F_ALMOST_EMPTY_OFFSET[%d]", i));
                ci.disconnectPort(ctx->idf("F_ALMOST_FULL_OFFSET[%d]", i));
            }
            ci.renamePort(id_F_EMPTY, ctx->id("F_EMPTY[0]"));
            move_ram_i(&ci, ctx->id("F_EMPTY[0]"));
            ci.renamePort(id_F_FULL, ctx->id("F_FULL[0]"));
            move_ram_i(&ci, ctx->id("F_FULL[0]"));
            ci.renamePort(id_F_ALMOST_FULL, ctx->id("F_AL_FULL[0]"));
            move_ram_i(&ci, ctx->id("F_AL_FULL[0]"));
            ci.renamePort(id_F_ALMOST_EMPTY, ctx->id("F_AL_EMPTY[0]"));
            move_ram_i(&ci, ctx->id("F_AL_EMPTY[0]"));

            ci.renamePort(id_F_WR_ERROR, ctx->id("FWR_ERR[0]"));
            move_ram_i(&ci, ctx->id("FWR_ERR[0]"));
            ci.renamePort(id_F_RD_ERROR, ctx->id("FRD_ERR[0]"));
            move_ram_i(&ci, ctx->id("FRD_ERR[0]"));

            ci.renamePort(id_F_RST_N, ctx->id("F_RSTN"));
            move_ram_o(&ci, ctx->id("F_RSTN"));
        }
    }
    flush_cells();
}

NEXTPNR_NAMESPACE_END

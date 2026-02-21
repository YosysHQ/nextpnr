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
// DSP
// ===================================
void GowinPacker::pass_net_type(CellInfo *ci, IdString port)
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

void GowinPacker::pack_dsp(void)
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
            case ID_MULT12X12: {
                for (int i = 0; i < 2; ++i) {
                    ci->renamePort(ctx->idf("CLK[%d]", i), ctx->idf("CLK%d", i));
                    ci->renamePort(ctx->idf("CE[%d]", i), ctx->idf("CE%d", i));
                    ci->renamePort(ctx->idf("RESET[%d]", i), ctx->idf("RESET%d", i));
                }
                for (int i = 0; i < 12; ++i) {
                    ci->renamePort(ctx->idf("A[%d]", i), ctx->idf("A%d", i));
                    ci->renamePort(ctx->idf("B[%d]", i), ctx->idf("B%d", i));
                }
                for (int i = 0; i < 24; ++i) {
                    ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
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
            case ID_MULTADDALU12X12: {
                for (int i = 0; i < 2; ++i) {
                    ci->renamePort(ctx->idf("CLK[%d]", i), ctx->idf("CLK%d", i));
                    ci->renamePort(ctx->idf("CE[%d]", i), ctx->idf("CE%d", i));
                    ci->renamePort(ctx->idf("RESET[%d]", i), ctx->idf("RESET%d", i));
                    ci->renamePort(ctx->idf("ADDSUB[%d]", i), ctx->idf("ADDSUB%d", i));
                }
                for (int i = 0; i < 12; ++i) {
                    ci->renamePort(ctx->idf("A0[%d]", i), ctx->idf("A0%d", i));
                    ci->renamePort(ctx->idf("B0[%d]", i), ctx->idf("B0%d", i));
                    ci->renamePort(ctx->idf("A1[%d]", i), ctx->idf("A1%d", i));
                    ci->renamePort(ctx->idf("B1[%d]", i), ctx->idf("B1%d", i));
                }
                pass_net_type(ci, id_ACCSEL);
                ci->cell_bel_pins.at(id_ACCSEL).clear();
                ci->cell_bel_pins.at(id_ACCSEL).push_back(id_ACCSEL0);
                ci->cell_bel_pins.at(id_ACCSEL).push_back(id_ACCSEL1);

                for (int i = 0; i < 48; ++i) {
                    ci->renamePort(ctx->idf("DOUT[%d]", i), ctx->idf("DOUT%d", i));
                }

                // mark 2 mult12x12 as parts of the cluster to prevent
                // other multipliers from being placed there
                ci->cluster = ci->name;
                ci->constr_abs_z = false;
                ci->constr_x = 0;
                ci->constr_y = 0;
                ci->constr_z = 0;
                ci->constr_children.clear();

                for (int i = 0; i < 2; ++i) {
                    IdString mult12x12_name = gwu.create_aux_name(ci->name, i * 2);
                    std::unique_ptr<CellInfo> mult12x12_cell = gwu.create_cell(mult12x12_name, id_DUMMY_CELL);
                    new_cells.push_back(std::move(mult12x12_cell));
                    CellInfo *mult12x12_ci = new_cells.back().get();

                    mult12x12_ci->cluster = ci->name;
                    mult12x12_ci->constr_abs_z = false;
                    mult12x12_ci->constr_x = 0;
                    mult12x12_ci->constr_y = 0;
                    mult12x12_ci->constr_z = BelZ::MULT12X12_0_Z - BelZ::MULTADDALU12X12_Z + i;
                }

                // DSP head?
                if (gwu.dsp_bus_src(ci, "CASI", 48) == nullptr) {
                    for (int i = 0; i < 48; ++i) {
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

                    static int padd_z[] = {BelZ::PADD9_0_0_Z, BelZ::PADD9_0_2_Z, BelZ::PADD9_1_0_Z, BelZ::PADD9_1_2_Z};
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

    auto make_CAS_chain = [&](CellInfo *head, int wire_num) {
        CellInfo *cur_dsp = head;
        while (1) {
            CellInfo *next_dsp = gwu.dsp_bus_dst(cur_dsp, "CASO", wire_num);
            if (next_dsp == nullptr) {
                // End of chain
                for (int i = 0; i < wire_num; ++i) {
                    cur_dsp->disconnectPort(ctx->idf("CASO[%d]", i));
                }
                break;
            }
            for (int i = 0; i < wire_num; ++i) {
                cur_dsp->disconnectPort(ctx->idf("CASO[%d]", i));
                next_dsp->disconnectPort(ctx->idf("CASI[%d]", i));
            }
            cur_dsp->setAttr(id_USE_CASCADE_OUT, 1);
            cur_dsp = next_dsp;
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
    };

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
                    log_error("%s is the next for two different DSPs (%s and %s) in the chain.", ctx->nameOf(cur_dsp),
                              ctx->nameOf(next_dsp_a), ctx->nameOf(next_dsp_b));
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
                    log_error("%s is the source for two different DSPs (%s and %s) in the chain.", ctx->nameOf(cur_dsp),
                              ctx->nameOf(next_dsp_a), ctx->nameOf(next_dsp_b));
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
            make_CAS_chain(head, 55);
        } break;
        case ID_MULTADDALU12X12: {
            make_CAS_chain(head, 48);
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
                    log_error("%s is the source for two different DSPs (%s and %s) in the chain.", ctx->nameOf(cur_dsp),
                              ctx->nameOf(next_so_dsp_a), ctx->nameOf(next_so_dsp_b));
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
NEXTPNR_NAMESPACE_END

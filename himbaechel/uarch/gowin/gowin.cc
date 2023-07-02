#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "gowin.h"

NEXTPNR_NAMESPACE_BEGIN

void GowinImpl::init(Context *ctx)
{
    h.init(ctx);
    HimbaechelAPI::init(ctx);
    // These fields go in the header of the output JSON file and can help
    // gowin_pack support different architectures
    ctx->settings[ctx->id("packer.arch")] = std::string("himbaechel/gowin");
    // XXX it would be nice to write chip/base name in the header as well,
    // but maybe that will come up when there is clarity with
    // Arch::archArgsToId
}

void GowinImpl::prePlace() { assign_cell_info(); }

void GowinImpl::pack()
{
    // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
    const pool<CellTypePort> top_ports{
            CellTypePort(id_IBUF, id_I),
            CellTypePort(id_OBUF, id_O),
    };
    h.remove_nextpnr_iobs(top_ports);
    // Replace constants with LUTs
    const dict<IdString, Property> vcc_params;
    const dict<IdString, Property> gnd_params;
    h.replace_constants(CellTypePort(id_GOWIN_VCC, id_V), CellTypePort(id_GOWIN_GND, id_G), vcc_params, gnd_params);

    // disconnect the constant LUT inputs
    mod_lut_inputs();

    pack_wideluts();

    // Constrain directly connected LUTs and FFs together to use dedicated resources
    auto lut_outs = pool<CellTypePort>{{id_LUT1, id_F}, {id_LUT2, id_F}, {id_LUT3, id_F}, {id_LUT4, id_F}};
    auto dff_ins = pool<CellTypePort>{{id_DFF, id_D},  {id_DFFE, id_D},  {id_DFFN, id_D},  {id_DFFNE, id_D},
                                      {id_DFFS, id_D}, {id_DFFSE, id_D}, {id_DFFNS, id_D}, {id_DFFNSE, id_D},
                                      {id_DFFR, id_D}, {id_DFFRE, id_D}, {id_DFFNR, id_D}, {id_DFFNRE, id_D},
                                      {id_DFFP, id_D}, {id_DFFPE, id_D}, {id_DFFNP, id_D}, {id_DFFNPE, id_D},
                                      {id_DFFC, id_D}, {id_DFFCE, id_D}, {id_DFFNC, id_D}, {id_DFFNCE, id_D}};

    int lutffs = h.constrain_cell_pairs(lut_outs, dff_ins, 1);
    log_info("Constrained %d LUTFF pairs.\n", lutffs);
}

bool GowinImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    Loc l = ctx->getBelLocation(bel);
    if (ctx->getBelType(bel).in(id_LUT4, id_DFF)) {
        return slice_valid(l.x, l.y, l.z / 2);
    } else {
        return true;
    }
}

// Bel bucket functions
IdString GowinImpl::getBelBucketForCellType(IdString cell_type) const
{
    if (cell_type.in(id_IBUF, id_OBUF)) {
        return id_IOB;
    }
    if (type_is_lut(cell_type)) {
        return id_LUT4;
    }
    if (type_is_dff(cell_type)) {
        return id_DFF;
    }
    if (cell_type == id_GOWIN_GND) {
        return id_GND;
    }
    if (cell_type == id_GOWIN_VCC) {
        return id_VCC;
    }
    return cell_type;
}

bool GowinImpl::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type == id_IOB) {
        return cell_type.in(id_IBUF, id_OBUF);
    }
    if (bel_type == id_LUT4) {
        return type_is_lut(cell_type);
    }
    if (bel_type == id_DFF) {
        return type_is_dff(cell_type);
    }
    if (bel_type == id_GND) {
        return cell_type == id_GOWIN_GND;
    }
    if (bel_type == id_VCC) {
        return cell_type == id_GOWIN_VCC;
    }
    return (bel_type == cell_type);
}

void GowinImpl::assign_cell_info()
{
    fast_cell_info.resize(ctx->cells.size());
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        auto &fc = fast_cell_info.at(ci->flat_index);
        if (is_lut(ci)) {
            fc.lut_f = ci->getPort(id_F);
        } else if (is_dff(ci)) {
            fc.ff_d = ci->getPort(id_D);
            fc.ff_clk = ci->getPort(id_CLK);
            fc.ff_ce = ci->getPort(id_CE);
            for (IdString port : {id_SET, id_RESET, id_PRESET, id_CLEAR}) {
                fc.ff_lsr = ci->getPort(port);
                if (fc.ff_lsr != nullptr) {
                    break;
                }
            }
        }
    }
}

bool GowinImpl::slice_valid(int x, int y, int z) const
{
    const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2)));
    const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2 + 1)));
    if (!ff) {
        return true; // always valid if only LUT used
    }
    const auto &ff_data = fast_cell_info.at(ff->flat_index);
    if (lut) {
        const auto &lut_data = fast_cell_info.at(lut->flat_index);
        if (ff_data.ff_d != lut_data.lut_f)
            return false;
    }
    int adj_z = (1 - (z & 1) * 2 + z) * 2 + 1;
    const CellInfo *adj_ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_z)));
    if (adj_ff == nullptr) {
        return true;
    }
    // DFFs must be same type or compatible
    if (ff->type != adj_ff->type &&
        ((ff->type.in(id_DFFS) && !adj_ff->type.in(id_DFFR)) || (ff->type.in(id_DFFR) && !adj_ff->type.in(id_DFFS)) ||
         (ff->type.in(id_DFFSE) && !adj_ff->type.in(id_DFFRE)) ||
         (ff->type.in(id_DFFRE) && !adj_ff->type.in(id_DFFSE)) || (ff->type.in(id_DFFP) && !adj_ff->type.in(id_DFFC)) ||
         (ff->type.in(id_DFFC) && !adj_ff->type.in(id_DFFP)) || (ff->type.in(id_DFFPE) && !adj_ff->type.in(id_DFFCE)) ||
         (ff->type.in(id_DFFCE) && !adj_ff->type.in(id_DFFPE)) ||
         (ff->type.in(id_DFFNS) && !adj_ff->type.in(id_DFFNR)) ||
         (ff->type.in(id_DFFNR) && !adj_ff->type.in(id_DFFNS)) ||
         (ff->type.in(id_DFFNSE) && !adj_ff->type.in(id_DFFNRE)) ||
         (ff->type.in(id_DFFNRE) && !adj_ff->type.in(id_DFFNSE)) ||
         (ff->type.in(id_DFFNP) && !adj_ff->type.in(id_DFFNC)) ||
         (ff->type.in(id_DFFNC) && !adj_ff->type.in(id_DFFNP)) ||
         (ff->type.in(id_DFFNPE) && !adj_ff->type.in(id_DFFNCE)) ||
         (ff->type.in(id_DFFNCE) && !adj_ff->type.in(id_DFFNPE)))) {
        return false;
    }

    // CE, LSR and CLK must match
    const auto &adj_ff_data = fast_cell_info.at(adj_ff->flat_index);
    if (adj_ff_data.ff_lsr == ff_data.ff_lsr) {
        return true;
    }
    //
    return false;
}

// modify LUTs with constant inputs
void GowinImpl::mod_lut_inputs(void)
{
    for (IdString netname : {ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC")}) {
        auto net = ctx->nets.find(netname);
        if (net == ctx->nets.end()) {
            continue;
        }
        NetInfo *constnet = net->second.get();
        for (auto user : constnet->users) {
            CellInfo *uc = user.cell;
            if (ctx->verbose)
                log_info("%s user %s\n", ctx->nameOf(constnet), ctx->nameOf(uc));

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

                if (ctx->verbose)
                    log_info("%s lut config modified from 0x%lX to 0x%lX\n", ctx->nameOf(uc), it_param->second.intval,
                             uc_init);

                it_param->second = Property(uc_init, uc_init_len);
                uc->disconnectPort(user.port);
            }
        }
    }
}

// make cluster from LUTs and MUXes
void GowinImpl::pack_wideluts(void)
{
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
            ci.constr_abs_z = 0;
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
            ci.constr_abs_z = 0;
            make_cluster(ci, &ci, 0, 0);
            ++packed[i + 1];
        }
    }
    log_info("Packed MUX2_LUT8:%d, MUX2_LU7:%d, MUX2_LUT6:%d, MUX2_LUT5:%d\n", packed[0], packed[1], packed[2],
             packed[3]);
}

NEXTPNR_NAMESPACE_END

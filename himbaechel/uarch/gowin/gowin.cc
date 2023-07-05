#include "himbaechel_api.h"
#include "himbaechel_helpers.h"
#include "log.h"
#include "nextpnr.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"

#include "gowin.h"
#include "pack.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct GowinImpl : HimbaechelAPI
{

    ~GowinImpl(){};
    void init_constids(Arch *arch) override { init_uarch_constids(arch); }
    void init(Context *ctx) override;

    void prePlace() override;
    void postPlace() override;
    void pack() override;

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override;

    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override;

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override;

  private:
    HimbaechelHelpers h;

    // Validity checking
    struct GowinCellInfo
    {
        const NetInfo *lut_f = nullptr;
        const NetInfo *ff_d = nullptr, *ff_ce = nullptr, *ff_clk = nullptr, *ff_lsr = nullptr;
        const NetInfo *alu_sum = nullptr;
    };
    std::vector<GowinCellInfo> fast_cell_info;
    void assign_cell_info();

    // bel placement validation
    bool slice_valid(int x, int y, int z) const;
};

struct GowinArch : HimbaechelArch
{
    GowinArch() : HimbaechelArch("gowin"){};
    std::unique_ptr<HimbaechelAPI> create(const dict<std::string, std::string> &args)
    {
        return std::make_unique<GowinImpl>();
    }
} gowinrArch;

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

void GowinImpl::pack() { gowin_pack(ctx); }

bool GowinImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    Loc l = ctx->getBelLocation(bel);
    if (!ctx->getBoundBelCell(bel)) {
        return true;
    }
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type.in(id_LUT4, id_DFF)) {
        return slice_valid(l.x, l.y, l.z / 2);
    } else {
        if (bel_type == id_ALU) {
            return slice_valid(l.x, l.y, l.z - BelZ::ALU0_Z);
        }
    }
    return true;
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
            continue;
        }
        if (is_dff(ci)) {
            fc.ff_d = ci->getPort(id_D);
            fc.ff_clk = ci->getPort(id_CLK);
            fc.ff_ce = ci->getPort(id_CE);
            for (IdString port : {id_SET, id_RESET, id_PRESET, id_CLEAR}) {
                fc.ff_lsr = ci->getPort(port);
                if (fc.ff_lsr != nullptr) {
                    break;
                }
            }
            continue;
        }
        if (is_alu(ci)) {
            fc.alu_sum = ci->getPort(id_SUM);
            continue;
        }
    }
}

// placement validation
bool GowinImpl::slice_valid(int x, int y, int z) const
{
    const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2)));
    const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2 + 1)));
    const CellInfo *alu = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z + BelZ::ALU0_Z)));

    if (alu && lut) {
        return false;
    }

    // check for ALU/LUT in the adjacent cell
    int adj_lut_z = (1 - (z & 1) * 2 + z) * 2;
    int adj_alu_z = adj_lut_z / 2 + BelZ::ALU0_Z;
    const CellInfo *adj_lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_lut_z)));
    const CellInfo *adj_ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_lut_z + 1)));
    const CellInfo *adj_alu = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_alu_z)));

    if ((alu && (adj_lut || (adj_ff && !adj_alu))) || ((lut || (ff && !alu)) && adj_alu)) {
        return false;
    }

    // if there is DFF it must be connected to this LUT or ALU
    if (ff) {
        const auto &ff_data = fast_cell_info.at(ff->flat_index);
        if (lut) {
            const auto &lut_data = fast_cell_info.at(lut->flat_index);
            if (ff_data.ff_d != lut_data.lut_f) {
                return false;
            }
        }
        if (alu) {
            const auto &alu_data = fast_cell_info.at(alu->flat_index);
            if (ff_data.ff_d != alu_data.alu_sum) {
                return false;
            }
        }
        if (adj_ff) {
            // DFFs must be same type or compatible
            if (ff->type != adj_ff->type && ((ff->type.in(id_DFFS) && !adj_ff->type.in(id_DFFR)) ||
                                             (ff->type.in(id_DFFR) && !adj_ff->type.in(id_DFFS)) ||
                                             (ff->type.in(id_DFFSE) && !adj_ff->type.in(id_DFFRE)) ||
                                             (ff->type.in(id_DFFRE) && !adj_ff->type.in(id_DFFSE)) ||
                                             (ff->type.in(id_DFFP) && !adj_ff->type.in(id_DFFC)) ||
                                             (ff->type.in(id_DFFC) && !adj_ff->type.in(id_DFFP)) ||
                                             (ff->type.in(id_DFFPE) && !adj_ff->type.in(id_DFFCE)) ||
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
            if (adj_ff_data.ff_lsr != ff_data.ff_lsr) {
                return false;
            }
            if (adj_ff_data.ff_clk != ff_data.ff_clk) {
                return false;
            }
            if (adj_ff_data.ff_ce != ff_data.ff_ce) {
                return false;
            }
        }
    }
    return true;
}

void GowinImpl::postPlace()
{
    if (ctx->debug) {
        log_info("================== Final Placement ===================\n");
        for (auto &cell : ctx->cells) {
            auto ci = cell.second.get();
            IdStringList bel = ctx->getBelName(ci->bel);
            log_info("%s -> %s\n", ctx->nameOf(ci), bel.str(ctx).c_str());
        }
    }
}
} // namespace

NEXTPNR_NAMESPACE_END

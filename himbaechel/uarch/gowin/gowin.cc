#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "himbaechel_helpers.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct GowinImpl : HimbaechelAPI
{

    ~GowinImpl(){};
    void init_constids(Arch *arch) override { init_uarch_constids(arch); }
    void init(Context *ctx) override
    {
        h.init(ctx);
        HimbaechelAPI::init(ctx);
    }

    void prePlace() override {
		ctx->cells.at(ctx->id("leds_OBUF_O"))->setAttr(ctx->id("BEL"), std::string("X46Y14/IOBA"));
		ctx->cells.at(ctx->id("leds_OBUF_O_1"))->setAttr(ctx->id("BEL"), std::string("X0Y15/IOBB"));
		ctx->cells.at(ctx->id("leds_OBUF_O_2"))->setAttr(ctx->id("BEL"), std::string("X0Y20/IOBB"));
		ctx->cells.at(ctx->id("leds_OBUF_O_3"))->setAttr(ctx->id("BEL"), std::string("X0Y21/IOBB"));
		ctx->cells.at(ctx->id("leds_OBUF_O_4"))->setAttr(ctx->id("BEL"), std::string("X0Y24/IOBB"));
		ctx->cells.at(ctx->id("leds_OBUF_O_5"))->setAttr(ctx->id("BEL"), std::string("X0Y25/IOBB"));
		ctx->cells.at(ctx->id("rst_IBUF_I"))->setAttr(ctx->id("BEL"), std::string("X0Y4/IOBA"));
		assign_cell_info();
	}

    void pack() override
    {
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
                CellTypePort(id_IBUF, id_I),
                CellTypePort(id_OBUF, id_O),
        };
        h.remove_nextpnr_iobs(top_ports);
        // Replace constants with LUTs
        const dict<IdString, Property> vcc_params = {{id_INIT, Property(0xFFFF, 16)}};
        const dict<IdString, Property> gnd_params = {{id_INIT, Property(0x0000, 16)}};
        h.replace_constants(CellTypePort(id_LUT4, id_F), CellTypePort(id_LUT4, id_F), vcc_params, gnd_params);
        // Constrain directly connected LUTs and FFs together to use dedicated resources
        int lutffs = h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT4, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1);
        lutffs += h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT3, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1);
        lutffs += h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT2, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1);
        lutffs += h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT1, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1);
        lutffs += h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT4, id_F}}, pool<CellTypePort>{{id_DFFR, id_D}}, 1);
        lutffs += h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT3, id_F}}, pool<CellTypePort>{{id_DFFR, id_D}}, 1);
        lutffs += h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT2, id_F}}, pool<CellTypePort>{{id_DFFR, id_D}}, 1);
        lutffs += h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT1, id_F}}, pool<CellTypePort>{{id_DFFR, id_D}}, 1);
        log_info("Constrained %d LUTFF pairs.\n", lutffs);
    }

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override
    {
        Loc l = ctx->getBelLocation(bel);
        if (ctx->getBelType(bel).in(id_LUT4, id_DFF)) {
            return slice_valid(l.x, l.y, l.z / 2);
        } else {
            return true;
        }
    }

    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override
    {
        if (cell_type.in(id_IBUF, id_OBUF)) {
            return id_IOB;
		}
		if (cell_type.in(id_LUT1, id_LUT2, id_LUT3, id_LUT4)) {
			return id_LUT4;
		}
        return cell_type;
    }

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        IdString bel_type = ctx->getBelType(bel);
        if (bel_type == id_IOB) {
            return cell_type.in(id_IBUF, id_OBUF);
		}
		if (bel_type == id_LUT4) {
			return cell_type.in(id_LUT1, id_LUT2, id_LUT3, id_LUT4);
		}
		if (bel_type == id_DFF) {
			return cell_type.in(id_DFF, id_DFFR);
		}
        return (bel_type == cell_type);
    }

  private:
    HimbaechelHelpers h;

    // Validity checking
    struct GowinCellInfo
    {
        const NetInfo *lut_f = nullptr, *ff_d = nullptr;
    };
    std::vector<GowinCellInfo> fast_cell_info;
    void assign_cell_info()
    {
        fast_cell_info.resize(ctx->cells.size());
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            auto &fc = fast_cell_info.at(ci->flat_index);
            if (ci->type.in(id_LUT1, id_LUT2, id_LUT3, id_LUT4)) {
                fc.lut_f = ci->getPort(id_F);
            } else if (ci->type.in(id_DFF, id_DFFR)) {
                fc.ff_d = ci->getPort(id_D);
            }
        }
    }
    bool slice_valid(int x, int y, int z) const
    {
        const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2)));
        const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2 + 1)));
        if (!lut || !ff)
            return true; // always valid if only LUT or FF used
        const auto &lut_data = fast_cell_info.at(lut->flat_index);
        const auto &ff_data = fast_cell_info.at(ff->flat_index);
        if (ff_data.ff_d == lut_data.lut_f)
            return true;
        return false;
    }
};

struct GowinArch : HimbaechelArch
{
    GowinArch() : HimbaechelArch("gowin"){};
    std::unique_ptr<HimbaechelAPI> create(const dict<std::string, std::string> &args)
    {
        return std::make_unique<GowinImpl>();
    }
} exampleArch;
} // namespace

NEXTPNR_NAMESPACE_END

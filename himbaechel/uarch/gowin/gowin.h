#ifndef GOWIN_H
#define GOWIN_H

#include "himbaechel_api.h"
#include "himbaechel_helpers.h"
#include "nextpnr.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct GowinImpl : HimbaechelAPI
{

    ~GowinImpl(){};
    void init_constids(Arch *arch) override { init_uarch_constids(arch); }
    void init(Context *ctx) override;

    void prePlace() override;
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
    };
    std::vector<GowinCellInfo> fast_cell_info;
    void assign_cell_info();
    bool slice_valid(int x, int y, int z) const;

    // modify LUTs with constant inputs
    void mod_lut_inputs(void);

    // Return true if a cell is a LUT
    inline bool type_is_lut(IdString cell_type) const { return cell_type.in(id_LUT1, id_LUT2, id_LUT3, id_LUT4); }
    inline bool is_lut(const CellInfo *cell) const { return type_is_lut(cell->type); }
    // Return true if a cell is a DFF
    inline bool type_is_dff(IdString cell_type) const
    {
        return cell_type.in(id_DFF, id_DFFE, id_DFFN, id_DFFNE, id_DFFS, id_DFFSE, id_DFFNS, id_DFFNSE, id_DFFR,
                            id_DFFRE, id_DFFNR, id_DFFNRE, id_DFFP, id_DFFPE, id_DFFNP, id_DFFNPE, id_DFFC, id_DFFCE,
                            id_DFFNC, id_DFFNCE);
    }
    inline bool is_dff(const CellInfo *cell) const { return type_is_dff(cell->type); }
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
#endif

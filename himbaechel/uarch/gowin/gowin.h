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
        const NetInfo *lut_f = nullptr, *ff_d = nullptr;
    };
    std::vector<GowinCellInfo> fast_cell_info;
    void assign_cell_info();
    bool slice_valid(int x, int y, int z) const;

	// modify LUTs with constant inputs
	void mod_lut_inputs(void);

	// Return true if a cell is a LUT
	bool is_lut(const BaseCtx *ctx, const CellInfo *cell) const;
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


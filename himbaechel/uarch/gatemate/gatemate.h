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

#ifndef HIMBAECHEL_GATEMATE_H
#define HIMBAECHEL_GATEMATE_H

#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "himbaechel_helpers.h"

NEXTPNR_NAMESPACE_BEGIN

// Constant zero.
struct ZeroDriver
{
    ZeroDriver() : lower{nullptr}, upper{nullptr} {}
    ZeroDriver(CellInfo *lower, CellInfo *upper, IdString name);

    CellInfo *lower;
    CellInfo *upper;
};

// Propagate A0 through OUT1 and A1 through OUT2; zero COUTX and POUTX.
struct APassThroughCell
{
    APassThroughCell(CellInfo *lower, CellInfo *upper, IdString name);

    void clean_up(Context *ctx);

    CellInfo *lower;
    CellInfo *upper;
    bool inverted;
};

// Propagate B0 through POUTY1 and B1 through COUTY1
struct BPassThroughCell
{
    BPassThroughCell() : lower{nullptr}, upper{nullptr} {}
    BPassThroughCell(CellInfo *lower, CellInfo *upper, IdString name);

    void clean_up(Context *ctx);

    CellInfo *lower;
    CellInfo *upper;
};

// TODO: Micko points out this is an L2T4 CPE_HALF
struct CarryGenCell
{
    CarryGenCell() : lower{nullptr}, upper{nullptr} {}
    CarryGenCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x);

    CellInfo *lower;
    CellInfo *upper;
};

// This prepares B bits for multiplication.
struct MultfabCell
{
    MultfabCell() : lower{nullptr}, upper{nullptr} {}
    MultfabCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x);

    CellInfo *lower;
    CellInfo *upper;
};

// CITE: CPE_ges_f-routing-1.pdf
struct FRoutingCell
{
    FRoutingCell() : lower{nullptr}, upper{nullptr} {}
    FRoutingCell(CellInfo *lower, CellInfo *upper, IdString name, bool is_even_x);

    CellInfo *lower;
    CellInfo *upper;
};

// Multiply two bits of A with two bits of B.
//
// CITE: CPE_MULT.pdf
struct MultCell
{
    MultCell() : lower{nullptr}, upper{nullptr} {}
    MultCell(CellInfo *lower, CellInfo *upper, IdString name);

    CellInfo *lower;
    CellInfo *upper;
};

struct MultMsbCell
{
    MultMsbCell() : lower{nullptr}, upper{nullptr} {}
    MultMsbCell(CellInfo *lower, CellInfo *upper, IdString name);

    CellInfo *lower;
    CellInfo *upper;
};

// CITE: CPE_ges_MSB-routing.pdf
struct MsbRoutingCell
{
    MsbRoutingCell() : lower{nullptr}, upper{nullptr} {}
    MsbRoutingCell(CellInfo *lower, CellInfo *upper, IdString name);

    CellInfo *lower;
    CellInfo *upper;
};

struct MultiplierColumn
{
    BPassThroughCell b_passthru;
    CarryGenCell carry;
    MultfabCell multfab;
    FRoutingCell f_route;
    std::vector<MultCell> mults;
    MultMsbCell mult_msb;
    MsbRoutingCell msb_route;
};

// A GateMate multiplier is made up of columns of 2x2 multipliers.
struct Multiplier
{
    ZeroDriver zero;
    std::vector<APassThroughCell> a_passthrus;
    std::vector<MultiplierColumn> cols;

    size_t cpe_count() const
    {
        auto count = 1 /* (zero driver) */ + a_passthrus.size();
        for (const auto &col : cols) {
            count += 4 /* (b_passthru, carry, multfab, f_route) */ + col.mults.size() + 2 /* (mult_msb, msb_route* */;
        }
        return count;
    }
};

struct GateMateImpl : HimbaechelAPI
{
    ~GateMateImpl();
    void init_database(Arch *arch) override;

    void init(Context *ctx) override;

    void pack() override;

    void prePlace() override;
    void postPlace() override;
    void preRoute() override;
    void postRoute() override;

    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override;
    delay_t estimateDelay(WireId src, WireId dst) const override;

    void drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc) override;

    bool getClusterPlacement(ClusterId cluster, BelId root_bel,
                             std::vector<std::pair<CellInfo *, BelId>> &placement) const override;

    IdString getBelBucketForCellType(IdString cell_type) const override;
    bool isValidBelForCellType(IdString cell_type, BelId bel) const override;
    BelBucketId getBelBucketForBel(BelId bel) const override;

    Loc getRelativeConstraint(Loc &root_loc, IdString id) const;

    void configurePlacerHeap(PlacerHeapCfg &cfg) override;

    bool isPipInverting(PipId pip) const override;

    const GateMateTileExtraDataPOD *tile_extra_data(int tile) const;

    std::set<IdString> available_pads;
    std::map<BelId, const PadInfoPOD *> bel_to_pad;
    pool<IdString> ddr_nets;
    dict<std::pair<IdString, int>, Loc> locations;
    int dies;
    int preferred_die;
    std::vector<Multiplier> multipliers;

  private:
    bool getChildPlacement(const BaseClusterInfo *cluster, Loc root_loc,
                           std::vector<std::pair<CellInfo *, BelId>> &placement) const;

    void write_bitstream(const std::string &device, const std::string &filename);

    void parse_ccf(const std::string &filename);

    void assign_cell_info();
    void rename_param(CellInfo *cell, IdString name, IdString new_name, int width);
    void route_clock();
    void route_mults();

    const GateMateBelExtraDataPOD *bel_extra_data(BelId bel) const;

    struct GateMateCellInfo
    {
        // slice info
        const NetInfo *ff_en = nullptr, *ff_clk = nullptr, *ff_sr = nullptr;
        int ff_config = 0;
        int signal_used = -1;
        bool dff_used = false;
    };
    std::vector<GateMateCellInfo> fast_cell_info;
    std::map<BelId, std::map<IdString, const GateMateBelPinConstraintPOD *>> pin_to_constr;
};

NEXTPNR_NAMESPACE_END
#endif

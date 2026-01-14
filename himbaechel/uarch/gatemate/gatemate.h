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

enum MultiDieStrategy
{
    CLOCK_MIRROR,
    REUSE_CLK1,
    FULL_USE,
};

struct GateMateImpl : HimbaechelAPI
{
    ~GateMateImpl();
    po::options_description getUArchOptions() override;
    void init_database(Arch *arch) override;

    void init(Context *ctx) override;

    void pack() override;

    void prePlace() override;
    void postPlace() override;
    void preRoute() override;
    void postRoute() override;

    BoundingBox getRouteBoundingBox(WireId src, WireId dst) const override;
    void expandBoundingBox(BoundingBox &bb) const override;
    bool checkPipAvail(PipId pip) const override;
    bool checkPipAvailForNet(PipId pip, const NetInfo *net) const override { return checkPipAvail(pip); };

    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override;
    delay_t estimateDelay(WireId src, WireId dst) const override;
    delay_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const override;
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const override;
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override;
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override;

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
    const GateMateBelExtraDataPOD *bel_extra_data(BelId bel) const;
    const GateMatePipExtraDataPOD *pip_extra_data(PipId pip) const;

    int get_dff_config(CellInfo *dff) const;
    int get_ram_config(CellInfo *ram) const;

    std::set<IdString> available_pads;
    std::map<BelId, const PadInfoPOD *> bel_to_pad;
    pool<IdString> ddr_nets;
    dict<std::pair<IdString, int>, Loc> locations;
    int dies;
    int preferred_die;
    pool<IdString> multiplier_a_passthru_lowers;
    pool<IdString> multiplier_a_passthru_uppers;
    pool<IdString> multiplier_zero_drivers;
    std::vector<bool> used_cpes;
    std::vector<uint32_t> pip_data;
    std::vector<uint32_t> pip_mask;
    int fpga_mode;
    int timing_mode;
    std::map<const NetInfo *, int> global_signals;
    dict<std::pair<IdString, int>, NetInfo *> global_mapping;
    dict<std::pair<IdString, int>, IdString> global_clk_mapping;
    std::vector<CellInfo *> clkin;
    std::vector<CellInfo *> glbout;
    std::vector<CellInfo *> pll;
    pool<IdString> ignore;
    MultiDieStrategy strategy;
    dict<int, IdString> index_to_die;
    dict<IdString, int> die_to_index;

  private:
    bool getChildPlacement(const BaseClusterInfo *cluster, Loc root_loc,
                           std::vector<std::pair<CellInfo *, BelId>> &placement) const;

    void write_bitstream(const std::string &device, const std::string &filename);

    void parse_ccf(const std::string &filename);

    void assign_cell_info();
    void route_clock();
    void route_mult();
    void reassign_bridges(NetInfo *net, const dict<WireId, PipMap> &net_wires, WireId wire,
                          dict<WireId, IdString> &wire_to_net, int &num);
    void reassign_cplines(NetInfo *net, const dict<WireId, PipMap> &net_wires, WireId wire,
                          dict<WireId, IdString> &wire_to_net, int &num);
    void repack();

    bool get_delay_from_tmg_db(IdString id, DelayQuad &delay) const;
    void get_setuphold_from_tmg_db(IdString id_setup, IdString id_hold, DelayPair &setup, DelayPair &hold) const;
    void get_setuphold_from_tmg_db(IdString id_setuphold, DelayPair &setup, DelayPair &hold) const;

    struct GateMateCellInfo
    {
        // slice info
        const NetInfo *ff_en = nullptr, *ff_clk = nullptr, *ff_sr = nullptr;
        int config = 0;
        int signal_used = -1;
        bool used = false;
    };
    std::vector<GateMateCellInfo> fast_cell_info;
    std::map<BelId, std::map<IdString, const GateMateBelPinConstraintPOD *>> pin_to_constr;
    std::map<IdString, const GateMateTimingExtraDataPOD *> timing;
    dict<IdString, int> ram_signal_clk;
    IdString forced_die;
    bool use_cp_for_clk;
};

NEXTPNR_NAMESPACE_END
#endif

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  gatecat <gatecat@ds0.me>
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

#ifndef HIMBAECHEL_XILINX_H
#define HIMBAECHEL_XILINX_H

#include "extra_data.h"
#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "himbaechel_helpers.h"

NEXTPNR_NAMESPACE_BEGIN

struct XilinxCellTags
{
    union
    {
        struct
        {
            bool is_memory, is_srl;
            int input_count, output_count;
            int memory_group;
            bool only_drives_carry;
            NetInfo *input_sigs[6], *output_sigs[2];
            NetInfo *address_msb[3];
            NetInfo *di1_net, *di2_net, *wclk;
        } lut;
        struct
        {
            bool is_latch, is_clkinv, is_srinv, ffsync;
            bool is_paired;
            NetInfo *clk, *sr, *ce, *d;
        } ff;
        struct
        {
            NetInfo *out_sigs[8], *cout_sigs[8], *x_sigs[8];
        } carry;
        struct
        {
            NetInfo *sel, *out;
        } mux;
    };
};

struct SiteIndex
{
    SiteIndex() : tile(-1), site(-1){};
    SiteIndex(int32_t tile, int32_t site) : tile(tile), site(site){};

    int32_t tile;
    int32_t site;
    bool operator==(const SiteIndex &other) const { return tile == other.tile && site == other.site; }
    bool operator!=(const SiteIndex &other) const { return tile != other.tile || site != other.site; }
    unsigned hash() const { return mkhash(tile, site); }
};

struct XilinxImpl : HimbaechelAPI
{

    struct LogicTileStatus
    {
        // z -> cell
        CellInfo *cells[128];

        // Eight-tile valid and dirty status
        struct EigthTileStatus
        {
            mutable bool valid = true, dirty = true;
        } eights[8];
        struct HalfTileStatus
        {
            mutable bool valid = true, dirty = true;
        } halfs[8];
    };

    struct BRAMTileStatus
    {
        CellInfo *cells[12] = {nullptr};
    };

    struct TileStatus
    {
        std::unique_ptr<LogicTileStatus> lts;
        std::unique_ptr<BRAMTileStatus> bts;
        std::vector<int> site_variant;
    };

    ~XilinxImpl();
    void init_database(Arch *arch) override;

    void init(Context *ctx) override;

    // Bels
    void notifyBelChange(BelId bel, CellInfo *cell) override;
    void update_logic_bel(BelId bel, CellInfo *cell);
    void update_bram_bel(BelId bel, CellInfo *cell);

    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override;
    bool xc7_logic_tile_valid(IdString tileType, const LogicTileStatus &lts) const;

    // Pips
    bool is_pip_unavail(PipId pip) const;
    bool checkPipAvail(PipId pip) const override { return !is_pip_unavail(pip); }
    bool checkPipAvailForNet(PipId pip, const NetInfo *net) const override { return !is_pip_unavail(pip); }

    // Flow management
    void parse_xdc(const std::string &filename);
    void pack() override;
    void prePlace() override;
    void preRoute() override;
    void postPlace() override;
    void postRoute() override;
    void write_fasm(const std::string &filename);

    void configurePlacerHeap(PlacerHeapCfg &cfg) override;

    void fixup_placement();
    void fixup_routing();
    void route_clocks();

    // Misc utility functions
    const XlnxTileInstExtraDataPOD *tile_extra_data(int tile) const;
    IdString bel_tile_type(BelId bel) const;
    bool is_logic_tile(BelId bel) const;
    bool is_bram_tile(BelId bel) const;

    SiteIndex get_bel_site(BelId bel) const;
    Loc rel_site_loc(SiteIndex site) const;
    IdString get_site_name(SiteIndex site) const;
    IdString bel_name_in_site(BelId bel) const;
    IdStringList get_site_bel_name(BelId bel) const;
    BelId get_site_bel(SiteIndex site, IdString bel_name) const;

    int hclk_for_iob(BelId pad) const;
    int hclk_for_ioi(int tile) const;

    std::string tile_name(int tile) const;

    std::vector<XilinxCellTags> cell_tags;
    const XilinxCellTags *get_tags(const CellInfo *cell) const
    {
        return cell ? &cell_tags.at(cell->flat_index) : nullptr;
    }

    std::vector<TileStatus> tile_status;

    // Improved delay predictions where sites are located far from their associated interconnect
    dict<WireId, Loc> source_locs, sink_locs;
    bool is_general_routing(WireId wire) const;
    void find_source_sink_locs();

    delay_t estimateDelay(WireId src, WireId dst) const override;
    BoundingBox getRouteBoundingBox(WireId src, WireId dst) const override;

  private:
    HimbaechelHelpers h;
    void assign_cell_tags();
};

NEXTPNR_NAMESPACE_END
#endif

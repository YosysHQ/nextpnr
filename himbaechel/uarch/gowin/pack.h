#ifndef GOWIN_PACK_H
#define GOWIN_PACK_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

void gowin_pack(Context *ctx);

struct GowinPacker
{
    Context *ctx;
    HimbaechelHelpers h;
    GowinUtils gwu;

    GowinPacker(Context *ctx) : ctx(ctx)
    {
        h.init(ctx);
        gwu.init(ctx);
    }

    // IO
    void pack_iobs(void);
    void pack_i3c(void);
    void pack_mipi(void);
    void pack_diff_iobs(void);
    void pack_io_regs(void);
    void pack_iodelay(void);
    void pack_iologic(void);

    // 16 SERDES
    void pack_io16(void);

    // LUTs
    void pack_wideluts(void);
    void pack_alus(void);
    void pack_ssram(void);
    void pack_inv(void);

    // BSRAM
    void pack_bsram(void);

    // DSP
    void pack_dsp(void);

    // PLL
    void pack_pll(void);

    // Clocks
    void pack_hclk(void);
    void pack_dlldly(void);
    void pack_buffered_nets(void);
    void pack_dqce(void);
    void pack_dcs(void);
    void pack_dhcens(void);

    // ADC
    void pack_adc(void);

    // Misc
    void pack_iem(void);
    void handle_constants(void);
    void pack_gsr(void);
    void pack_pincfg(void);
    void pack_bandgap(void);
    void pack_userflash(bool have_emcu);
    void pack_emcu_and_flash(void);

    void run(void);

  private:
    // IO
    void make_iob_nets(CellInfo &iob);
    void config_simple_io(CellInfo &ci);
    void config_bottom_row(CellInfo &ci, Loc loc, uint8_t cnd = Bottom_io_POD::NORMAL);
    void trim_nextpnr_iobs(void);
    BelId bind_io(CellInfo &ci);
    std::pair<CellInfo *, CellInfo *> get_pn_cells(const CellInfo &ci);
    void mark_iobs_as_diff(CellInfo &ci, std::pair<CellInfo *, CellInfo *> &pn_cells);
    void switch_diff_ports(CellInfo &ci, std::pair<CellInfo *, CellInfo *> &pn_cells,
                           std::vector<IdString> &nets_to_remove);

    static bool is_iob(const Context *ctx, CellInfo *cell) { return is_io(cell); }

    // IOLOGIC
    void set_daaj_nets(CellInfo &ci, BelId bel);
    BelId get_iologico_bel(CellInfo *iob);
    BelId get_iologici_bel(CellInfo *iob);
    void check_iologic_placement(CellInfo &ci, Loc iob_loc, int diff);
    void pack_bi_output_iol(CellInfo &ci, std::vector<IdString> &nets_to_remove);
    void pack_single_output_iol(CellInfo &ci, std::vector<IdString> &nets_to_remove);
    BelId get_aux_iologic_bel(const CellInfo &ci);
    bool is_diff_io(BelId bel);
    bool is_mipi_io(BelId bel);
    CellInfo *create_aux_iologic_cell(CellInfo &ci, IdString mode, bool io16 = false, int idx = 0);
    void reconnect_ides_outs(CellInfo *ci);
    void pack_ides_iol(CellInfo &ci, std::vector<IdString> &nets_to_remove);

    // 16 SERDES
    void check_io16_placement(CellInfo &ci, Loc main_loc, Loc aux_off, int diff);
    void pack_oser16(CellInfo &ci, std::vector<IdString> &nets_to_remove);
    void pack_ides16(CellInfo &ci, std::vector<IdString> &nets_to_remove);

    // LUTs
    std::unique_ptr<CellInfo> alu_add_cin_block(Context *ctx, CellInfo *head, NetInfo *cin_net, bool cin_is_vcc,
                                                bool cin_is_gnd);
    std::unique_ptr<CellInfo> alu_add_cout_block(Context *ctx, CellInfo *tail, NetInfo *cout_net);
    std::unique_ptr<CellInfo> alu_add_dummy_block(Context *ctx, CellInfo *tail);
    void optimize_alu_lut(CellInfo *ci, int mode);
    void constrain_lutffs(void);
    std::unique_ptr<CellInfo> ssram_make_lut(Context *ctx, CellInfo *ci, int index);

    // BSRAM
    void bsram_rename_ports(CellInfo *ci, int bit_width, char const *from, char const *to, int offset = 0);
    void bsram_fix_blksel(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells);
    void bsram_fix_outreg(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells);
    void bsram_fix_sp(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells);
    void pack_ROM(CellInfo *ci);
    void divide_sdp(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells);
    void pack_SDPB(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells);
    void pack_DPB(CellInfo *ci);
    void divide_sp(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells);
    void pack_SP(CellInfo *ci, std::vector<std::unique_ptr<CellInfo>> &new_cells);

    // DSP
    void pass_net_type(CellInfo *ci, IdString port);
};

NEXTPNR_NAMESPACE_END

#endif

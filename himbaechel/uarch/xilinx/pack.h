/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019-23  Myrtle Shah <gatecat@ds0.me>
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

#include <algorithm>
#include <boost/optional.hpp>
#include <iterator>
#include <queue>
#include <unordered_set>
#include "chain_utils.h"
#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "pins.h"
#include "xilinx.h"

#ifndef HB_XILINX_PACK_H
#define HB_XILINX_PACK_H

NEXTPNR_NAMESPACE_BEGIN

struct XilinxPacker
{
    Context *ctx;
    XilinxImpl *uarch;

    XilinxPacker(Context *ctx, XilinxImpl *uarch) : ctx(ctx), uarch(uarch){};

    // Generic cell transformation
    // Given cell name map and port map
    // If port name is not found in port map; it will be copied as-is but stripping []
    struct XFormRule
    {
        IdString new_type;
        dict<IdString, IdString> port_xform;
        dict<IdString, std::vector<IdString>> port_multixform;
        dict<IdString, IdString> param_xform;
        std::vector<std::pair<IdString, std::string>> set_attrs;
        std::vector<std::pair<IdString, Property>> set_params;
    };

    // Distributed RAM control set
    struct DRAMControlSet
    {
        std::vector<NetInfo *> wa;
        NetInfo *wclk, *we;
        bool wclk_inv;
        IdString memtype;

        bool operator==(const DRAMControlSet &other) const
        {
            return wa == other.wa && wclk == other.wclk && we == other.we && wclk_inv == other.wclk_inv &&
                   memtype == other.memtype;
        }
        bool operator!=(const DRAMControlSet &other) const
        {
            return wa != other.wa || wclk != other.wclk || we != other.we || wclk_inv != other.wclk_inv ||
                   memtype != other.memtype;
        }
        unsigned int hash() const
        {
            unsigned seed = 0;
            seed = mkhash(seed, wa.size());
            for (auto abit : wa)
                seed = mkhash(seed, (abit == nullptr ? IdString() : abit->name).hash());
            seed = mkhash(seed, (wclk == nullptr ? IdString() : wclk->name).hash());
            seed = mkhash(seed, (we == nullptr ? IdString() : we->name).hash());
            seed = mkhash(seed, wclk_inv);
            seed = mkhash(seed, memtype.hash());
            return seed;
        }
    };

    struct DRAMType
    {
        int abits;
        int dbits;
        int rports;
    };

    struct CarryGroup
    {
        std::vector<CellInfo *> muxcys;
        std::vector<CellInfo *> xorcys;
    };

    pool<IdString> packed_cells;

    // General helper functions
    void flush_cells();

    void xform_cell(const dict<IdString, XFormRule> &rules, CellInfo *ci);
    void generic_xform(const dict<IdString, XFormRule> &rules, bool print_summary = false);

    CellInfo *feed_through_lut(NetInfo *net, const std::vector<PortRef> &feed_users);
    CellInfo *feed_through_muxf(NetInfo *net, IdString type, const std::vector<PortRef> &feed_users);

    IdString int_name(IdString base, const std::string &postfix, bool is_hierarchy = true);
    NetInfo *create_internal_net(IdString base, const std::string &postfix, bool is_hierarchy = true);
    void rename_net(IdString old, IdString newname);

    void tie_port(CellInfo *ci, const std::string &port, bool value, bool inv = false);

    // LUTs & FFs
    void pack_inverters();
    void pack_luts();
    void pack_ffs();
    void pack_lutffs();

    bool is_constrained(const CellInfo *cell);
    void pack_muxfs();
    void finalise_muxfs();
    void legalise_muxf_tree(CellInfo *curr, std::vector<CellInfo *> &mux_roots);
    void constrain_muxf_tree(CellInfo *curr, CellInfo *base, int zoffset);
    void create_muxf_tree(CellInfo *base, const std::string &name_base, const std::vector<NetInfo *> &data,
                          const std::vector<NetInfo *> &select, NetInfo *out, int zoffset);

    void pack_srls();

    void split_carry4s();

    // DistRAM
    dict<IdString, XFormRule> dram_rules, dram32_6_rules, dram32_5_rules;
    CellInfo *create_dram_lut(const std::string &name, CellInfo *base, const DRAMControlSet &ctrlset,
                              std::vector<NetInfo *> address, NetInfo *di, NetInfo *dout, int z);
    CellInfo *create_dram32_lut(const std::string &name, CellInfo *base, const DRAMControlSet &ctrlset,
                                std::vector<NetInfo *> address, NetInfo *di, NetInfo *dout, bool o5, int z);
    void pack_dram();

    // Constant pins
    dict<IdString, dict<IdString, bool>> tied_pins;
    dict<IdString, pool<IdString>> invertible_pins;
    void pack_constants();

    // IO
    dict<IdString, pool<IdString>> toplevel_ports;
    NetInfo *invert_net(NetInfo *toinv);
    CellInfo *insert_obuf(IdString name, IdString type, NetInfo *i, NetInfo *o, NetInfo *tri = nullptr);
    CellInfo *insert_outinv(IdString name, NetInfo *i, NetInfo *o);
    std::pair<CellInfo *, PortRef> insert_pad_and_buf(CellInfo *npnr_io);
    CellInfo *create_iobuf(CellInfo *npnr_io, IdString &top_port);

    // Clocking
    BelId find_bel_with_short_route(WireId source, IdString beltype, IdString belpin);
    void try_preplace(CellInfo *cell, IdString port);
    void preplace_unique(CellInfo *cell);

    // Cell creating
    CellInfo *create_cell(IdString type, IdString name);
    CellInfo *create_lut(const std::string &name, const std::vector<NetInfo *> &inputs, NetInfo *output,
                         const Property &init);

    int autoidx = 0;
};

struct XC7Packer : public XilinxPacker
{
    XC7Packer(Context *ctx, XilinxImpl *uarch) : XilinxPacker(ctx, uarch){};

    // Carries
    bool has_illegal_fanout(NetInfo *carry);
    void pack_carries();

    // IO
    CellInfo *insert_ibuf(IdString name, IdString type, NetInfo *i, NetInfo *o);
    CellInfo *insert_diffibuf(IdString name, IdString type, const std::array<NetInfo *, 2> &i, NetInfo *o);

    void decompose_iob(CellInfo *xil_iob, bool is_hr, const std::string &iostandard);
    void pack_io();

    // IOLOGIC
    dict<IdString, XFormRule> hp_iol_rules, hd_iol_rules, ioctrl_rules;
    void fold_inverter(CellInfo *cell, std::string port);
    std::string get_ologic_site(const std::string &io_bel);
    std::string get_ilogic_site(const std::string &io_bel);
    std::string get_ioctrl_site(const std::string &io_bel);
    std::string get_odelay_site(const std::string &io_bel);
    std::string get_idelay_site(const std::string &io_bel);
    // Call before packing constants
    void prepare_iologic();

    void pack_iologic();
    void pack_idelayctrl();

    // Clocking
    void prepare_clocking();
    void pack_plls();
    void pack_gbs();
    void pack_clocking();

    // BRAM
    void pack_bram();

    // DSP
    void pack_dsps();

  private:
    void walk_dsp(CellInfo *root, CellInfo *ci, int constr_z);
    void check_valid_pad(CellInfo *ci, std::string type);
};

NEXTPNR_NAMESPACE_END
#endif

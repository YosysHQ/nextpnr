/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-22  gatecat <gatecat@ds0.me>
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
#ifndef VALIDITY_CHECKING_H
#define VALIDITY_CHECKING_H

#include "fab_cfg.h"
#include "fab_defs.h"
#include "sso_array.h"

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// The validity checking engine for the fabulous configurable CLB

// data that we tag onto cells for fast lookup, so we aren't doing slow hash map accesses in the inner-loop-critical
// validity checking code
struct ControlSig
{
    ControlSig() : net(), invert(false){};
    ControlSig(IdString net, bool invert) : net(net), invert(invert){};
    IdString net;
    bool invert;
    bool operator==(const ControlSig &other) const { return net == other.net && invert == other.invert; }
    bool operator!=(const ControlSig &other) const { return net != other.net || invert != other.invert; }
};

struct CellTags
{
    struct
    {
        SSOArray<IdString, MAX_LUTK> lut_inputs; // for checking fracturable LUTs
        bool carry_used = false;
        const NetInfo *lut_out = nullptr;
        // ...
    } comb; // data for LUTs, or the LUT part of combined LUT+FF cells
    struct
    {
        ControlSig clk, sr, en;
        bool ff_used = false;
        bool async = false;
        bool latch = false;
        const NetInfo *d = nullptr, *q = nullptr;
    } ff; // data for FFs, or the FF part of combined LUT+FF cells
};

// map between cell and tags, using the flat_index that viaduct defines for this purpose
struct CellTagger
{
    std::vector<CellTags> data;
    const CellTags &get(const CellInfo *ci) const { return data.at(ci->flat_index); }
    void assign_for(const Context *ctx, const FabricConfig &cfg, const CellInfo *ci);
};

// we need to add some extra data to CLB bels to track what they do, so we can update CLBState accordingly
struct BelFlags
{
    enum BlockType : uint8_t
    {
        BLOCK_OTHER,
        BLOCK_CLB,
        // ...
    } block = BlockType::BLOCK_OTHER;
    enum FuncType : uint8_t
    {
        FUNC_LC_COMB,
        FUNC_FF,
        FUNC_MUX,
        FUNC_OTHER,
    } func;
    uint8_t index;
    // ...
};

// state of a CLB, for fast bel->cell lookup
// TODO: add valid/dirty tracking for incremental validity re-checking, important once we have bigger/more complex CLBs
// (cf. xilinx/intel arches in nextpnr)
struct CLBState
{
    explicit CLBState(const LogicConfig &cfg);
    // In combined-LC mode (LC bel contains LUT and FF), this indexes the entire LC bel to cell. In separate mode, this
    // indexes the combinational part (LUT or LUT+carry only).
    std::unique_ptr<CellInfo *[]> lc_comb;
    // In split-LC mode only, this maps FF bel (in CLB) index to cell
    std::unique_ptr<CellInfo *[]> ff;
    // If there is (a) separate mux bel(s), map them to cells
    std::unique_ptr<CellInfo *[]> mux;
    bool check_validity(const LogicConfig &cfg, const CellTagger &cell_data);
};

struct BlockTracker
{
    Context *ctx;
    const FabricConfig &cfg;
    std::vector<BelFlags> bel_data;

    BlockTracker(Context *ctx, const FabricConfig &cfg) : ctx(ctx), cfg(cfg){};
    void set_bel_type(BelId bel, BelFlags::BlockType block, BelFlags::FuncType func, uint8_t index);
    void update_bel(BelId bel, CellInfo *old_cell, CellInfo *new_cell);
    struct TileData
    {
        std::unique_ptr<CLBState> clb;
        // ...
    };
    std::vector<std::vector<TileData>> tiles;
    bool check_validity(BelId bel, const FabricConfig &cfg, const CellTagger &cell_data);
};

struct PseudoPipTags
{
    BelId bel;
    enum PPType : uint16_t
    {
        NONE,
        LUT_CONST,
        LUT_PERM,
    } type;
    uint16_t data;
    PseudoPipTags(PPType type = NONE, BelId bel = BelId(), uint16_t data = 0x0) : bel(bel), type(type), data(data) {}
};

NEXTPNR_NAMESPACE_END

#endif

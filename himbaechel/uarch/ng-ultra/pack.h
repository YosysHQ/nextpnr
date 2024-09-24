/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  NG-Ultra Architecture Implementation
 *
 *  Copyright (C) 2024  YosysHQ GmbH
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
#include "ng_ultra.h"

#ifndef NG_ULTRA_PACK_H
#define NG_ULTRA_PACK_H

NEXTPNR_NAMESPACE_BEGIN

struct NgUltraPacker
{
    NgUltraPacker(Context *ctx, NgUltraImpl *uarch) : ctx(ctx), uarch(uarch) { h.init(ctx); };

    void remove_not_used();
    // Constants
    void pack_constants();
    void remove_constants();

    // LUTs & FFs
    void update_lut_init();
    void update_dffs();
    void pack_xluts();
    void pack_lut_multi_dffs();
    void pack_dff_chains();
    void pack_lut_dffs();
    void pack_dffs();
    void pack_cys();
    void pack_rfs();
    void pack_cdcs();
    void pack_fifos();

    void pack_rams();
    void pack_dsps();

    // IO
    void pack_iobs();
    void pack_ioms();

    void pack_gcks();
    void pack_plls();
    void pack_wfgs();
    void pre_place();

    void insert_ioms();
    void insert_wfbs();

    // Post placement
    void duplicate_gck();
    void insert_bypass_gck();
    void insert_csc();

TESTABLE_PRIVATE:
    void set_lut_input_if_constant(CellInfo *cell, IdString input);
    void lut_to_fe(CellInfo *lut, CellInfo *fe, bool no_dff, Property lut_table);
    void dff_to_fe(CellInfo *dff, CellInfo *fe, bool pass_thru_lut);
    void dff_rewrite(CellInfo *cell);
    void ddfr_rewrite(CellInfo *cell);

    void exchange_if_constant(CellInfo *cell, IdString input1, IdString input2);
    void pack_cy_input_and_output(CellInfo *cy, IdString cluster, IdString in_port, IdString out_port, int placer, int &lut_only, int &lut_and_ff, int &dff_only);

    void pack_xrf_input_and_output(CellInfo *cy, IdString cluster, IdString in_port, IdString out_port, ClusterPlacement placement, int &lut_only, int &lut_and_ff, int &dff_only);

    void connect_gnd_if_unconnected(CellInfo *cell, IdString input, bool warn);
    void disconnect_if_gnd(CellInfo *cell, IdString input);

    void insert_wfb(CellInfo *cell, IdString port);

    void mandatory_param(CellInfo *cell, IdString param);
    void disconnect_unused(CellInfo *cell, IdString port);
    void bind_attr_loc(CellInfo *cell, dict<IdString, Property> *attrs);
    BelId get_available_gck(int lobe, NetInfo *si1, NetInfo *si2);
    BelId getCSC(Loc l, int row);
    // General helper functions
    void flush_cells();

    IdString assign_wfg(IdString ckg, IdString ckg2, CellInfo *cell);
    void dsp_same_driver(IdString port, CellInfo *cell, CellInfo **target);
    void dsp_same_sink(IdString port, CellInfo *cell, CellInfo **target);

    int make_init_with_const_input(int init, int input, bool value);

    int memory_width(int config, bool ecc);
    int memory_addr_bits(int config,bool ecc);

    void constrain_location(CellInfo *cell);
    void extract_lowskew_signals(CellInfo *cell, dict<IdString,dict<IdString,std::vector<PortRef>>> &lowskew_signals);
    // Cell creating
    std::unique_ptr<CellInfo> create_cell(IdString type, IdString name);
    CellInfo *create_cell_ptr(IdString type, IdString name);

    Context *ctx;
    NgUltraImpl *uarch;

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    pool<IdString> global_lowskew;

    HimbaechelHelpers h;
};

NEXTPNR_NAMESPACE_END
#endif

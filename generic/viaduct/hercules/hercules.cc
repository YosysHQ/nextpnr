#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "viaduct_api.h"
#include "viaduct_helpers.h"

#define GEN_INIT_CONSTIDS
#define VIADUCT_CONSTIDS "viaduct/hercules/constids.inc"
#include "viaduct_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

struct LpInfo {
    std::array<WireId, 5> byp;
    WireId c_in;
    std::array<WireId, 2> down_i;
    WireId dx_fb;
    std::array<WireId, 3> f2;
    std::array<WireId, 3> f1;
    std::array<WireId, 3> f0;
    WireId fy;
    std::array<WireId, 2> up_i;

    WireId c_out;
    // down_o is the same as qx;
    std::array<WireId, 2> dx;
    WireId dx40;
    WireId dy;
    // fx is the same as dx[1]
    std::array<WireId, 2> qx;
    // up_o is the same as qx;
};

struct LbufInfo {
    WireId a_sr;
    WireId mclk_b;
    WireId sclk;
    std::array<WireId, 2> sh;
};

struct LeInfo {
    LbufInfo lbuf;
    std::array<LpInfo, 4> lp;
};

struct PlbInfo {
    LeInfo le;
    std::array<WireId, 6> tn0_o;
    std::array<WireId, 6> te0_o;
    std::array<WireId, 6> ts0_o;
    std::array<WireId, 6> tw0_o;
    std::array<WireId, 4> mn0_o;
    std::array<WireId, 4> me0_o;
    std::array<WireId, 4> ms0_o;
    std::array<WireId, 4> mw0_o;
    std::array<WireId, 9> on0_o;
    std::array<WireId, 7> oe0_o;
    std::array<WireId, 9> os0_o;
    std::array<WireId, 7> ow0_o;

    std::array<WireId, 3> n_xy_o;
    WireId ne_xy_o;
    std::array<WireId, 2> en_xy_o;
    std::array<WireId, 3> e_xy_o;
    WireId es_xy_o;
    std::array<WireId, 2> se_xy_o;
    std::array<WireId, 3> s_xy_o;
    WireId sw_xy_o;
    std::array<WireId, 2> ws_xy_o;
    std::array<WireId, 3> w_xy_o;
    WireId wn_xy_o;
    std::array<WireId, 2> nw_xy_o;

    std::array<WireId, 6> cclk;
    std::array<WireId, 4> clk_xbar;
};

struct Hercules : ViaductAPI {
    void init(Context *const ctx) override {
        init_uarch_constids(ctx); // Set up the string-interning pool.
        ViaductAPI::init(ctx);
        h.init(ctx);
        log_info("Setting up FPGA...\n");
        create_bels();
        log_info("Setting up FPGA...done\n");
    }

    void pack() override {
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
                CellTypePort(id_M7S_DGPIO, id_pad),
        };
        h.remove_nextpnr_iobs(top_ports);
        // Replace constants with LUTs
        const dict<IdString, Property> vcc_params = {{id_config_data, Property(0xFFFF, 16)}};
        const dict<IdString, Property> gnd_params = {{id_config_data, Property(0x0000, 16)}};
        h.replace_constants(CellTypePort(id_LUT4, id_f3), CellTypePort(id_LUT4, id_f3), vcc_params, gnd_params);
        rename_regs();
    }

    bool isBelLocationValid(BelId bel) const override {
        const Loc loc = ctx->getBelLocation(bel);

        const bool at_x_edge = (loc.x == 0) || (loc.x == COLUMNS - 1);
        const bool at_y_edge = (loc.y == 0) || (loc.y == ROWS - 1);

        if (at_y_edge) {
            // TODO: I/O buffer validation.
            return true;
        } else if (!at_x_edge && !at_y_edge) {
            // TODO: LP validation.
            return lp_is_valid(loc.x, loc.y, loc.z / LP_BELS);
        }

        return true;
    }

private:
    void rename_regs() {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_REG)
                continue;
            ci->type = id_REGS;
        }
    }

    BelId addBel(const int x, const int y, const int z, const IdString id) {
        return ctx->addBel(h.xy_id(x, y, IdStringList::concat(id, ctx->id(stringf("%d", z)))), id, Loc(x, y, z), /* gb = */ false, /* hidden = */ false);
    }

    WireId addWire(const int x, const int y, const IdStringList name, const IdString type) {
        return ctx->addWire(h.xy_id(x, y, name), type, x, y);
    }

    WireId addWireAsBelInput(const int x, const int y, const IdString id, const BelId bel) {
        WireId wire = ctx->addWire(IdStringList::concat(ctx->getBelName(bel), id), id, x, y);
        ctx->addBelInput(bel, id, wire);
        return wire;
    }

    WireId addWireAsBelOutput(const int x, const int y, const IdString id, const BelId bel) {
        WireId wire = ctx->addWire(IdStringList::concat(ctx->getBelName(bel), id), id, x, y);
        ctx->addBelOutput(bel, id, wire);
        return wire;
    }

    void add_cfgmux(const Loc loc, const WireId dst, const WireId src) {
        NPNR_ASSERT(src != WireId());
        NPNR_ASSERT(dst != WireId());
        IdStringList name = IdStringList::concat(ctx->getWireName(dst), ctx->getWireName(src));
        ctx->addPip(name, id_PIP, src, dst, 0.05, loc);
    }

    template<typename... Rest>
    void add_cfgmux(const Loc loc, const WireId dst, const WireId src, Rest... rest) {
        NPNR_ASSERT(src != WireId());
        NPNR_ASSERT(dst != WireId());
        IdStringList name = IdStringList::concat(ctx->getWireName(dst), ctx->getWireName(src));
        ctx->addPip(name, id_PIP, src, dst, 0.05, loc);
        add_cfgmux(loc, dst, rest...);
    }

    void create_iopad(const int x, const int y) {
        // Is there another IO pad combined?
        // note to self: xbar_tile_[ew]cap_io
        for (int z = 0; z < 4; z++) {
            const BelId bel = addBel(x, y, z, id_M7S_DGPIO);
            if (x == 0) {
                ctx->addBelInput(bel, id_od_d, plbs[x + 1][y].tw0_o[z]);
                ctx->addBelInput(bel, id_oen, plbs[x + 1][y].mw0_o[3]);
            } else {
                ctx->addBelInput(bel, id_od_d, plbs[x - 1][y].te0_o[z]);
                ctx->addBelInput(bel, id_oen, plbs[x - 1][y].me0_o[3]);
            }
            plbs[x][y].te0_o[z] = plbs[x][y].tw0_o[z] = addWireAsBelOutput(x, y, id_id_q, bel);
            const WireId pad = addWire(x, y, IdStringList::concat(ctx->getBelName(bel), id_pad), id_pad);
            ctx->addBelInout(bel, id_pad, pad);
        }
    }

    void create_logic_parcel(const int x, const int y, const int lp_idx, LeInfo& le) {
        LpInfo& lp = le.lp[lp_idx];

        const BelId lut0 = addBel(x, y, LP_BELS*lp_idx + 0, id_LUT4);
        ctx->addBelInput(lut0, id_f0, lp.f0[0]);
        ctx->addBelInput(lut0, id_f1, lp.f1[0]);
        const WireId l0_f2 = addWireAsBelInput(x, y, id_f2, lut0);
        const WireId l0_f3 = addWireAsBelInput(x, y, id_f3, lut0);
        ctx->addBelOutput(lut0, id_dx, lp.dx[0]);

        const BelId lut40 = addBel(x, y, LP_BELS*lp_idx + 1, id_LUT4C);
        const WireId l40_f0 = addWireAsBelInput(x, y, id_f0, lut40);
        const WireId l40_f1 = addWireAsBelInput(x, y, id_f1, lut40);
        const WireId l40_f2 = addWireAsBelInput(x, y, id_f2, lut40);
        const WireId l40_f3 = addWireAsBelInput(x, y, id_f3, lut40);
        const WireId l40_ca = addWireAsBelInput(x, y, id_ca, lut40);
        ctx->addBelInput(lut40, id_ci, lp.c_in);
        ctx->addBelOutput(lut40, id_co, lp.c_out);
        ctx->addBelOutput(lut40, id_dx, lp.dx40);
        const WireId l40_s = addWireAsBelOutput(x, y, id_s, lut40);

        const BelId lut41 = addBel(x, y, LP_BELS*lp_idx + 2, id_LUT4);
        ctx->addBelInput(lut41, id_f0, lp.f0[2]);
        ctx->addBelInput(lut41, id_f1, lp.f1[2]);
        ctx->addBelInput(lut41, id_f2, lp.f2[2]);
        const WireId l41_f3 = addWireAsBelInput(x, y, id_f3, lut41);
        const WireId l41_dx = addWireAsBelOutput(x, y, id_dx, lut41);

        const BelId reg0 = addBel(x, y, LP_BELS*lp_idx + 3, id_REGS);
        ctx->addBelInput(reg0, id_a_sr, le.lbuf.a_sr);
        const WireId r0_di = addWireAsBelInput(x, y, id_di, reg0);
        ctx->addBelInput(reg0, id_down_i, lp.down_i[0]);
        ctx->addBelInput(reg0, id_mclk_b, le.lbuf.mclk_b);
        ctx->addBelInput(reg0, id_sclk, le.lbuf.sclk);
        ctx->addBelInput(reg0, id_shift, le.lbuf.sh[0]);
        ctx->addBelInput(reg0, id_up_i, lp.up_i[0]);
        ctx->addBelOutput(reg0, id_qx, lp.qx[0]);

        const BelId reg1 = addBel(x, y, LP_BELS*lp_idx + 4, id_REGS);
        ctx->addBelInput(reg1, id_a_sr, le.lbuf.a_sr);
        const WireId r1_di = addWireAsBelInput(x, y, id_di, reg1);
        ctx->addBelInput(reg1, id_down_i, lp.down_i[1]);
        ctx->addBelInput(reg1, id_mclk_b, le.lbuf.mclk_b);
        ctx->addBelInput(reg1, id_sclk, le.lbuf.sclk);
        ctx->addBelInput(reg1, id_shift, le.lbuf.sh[1]);
        ctx->addBelInput(reg1, id_up_i, lp.up_i[1]);
        ctx->addBelOutput(reg1, id_qx, lp.qx[1]);

        const BelId mux_dx4_0 = addBel(x, y, LP_BELS*lp_idx + 5, id_mux_dx4);
        ctx->addBelInput(mux_dx4_0, id_in0, lp.dx40);
        ctx->addBelInput(mux_dx4_0, id_in1, l41_dx);
        ctx->addBelInput(mux_dx4_0, id_sel, lp.byp[2]);
        ctx->addBelOutput(mux_dx4_0, id_out, lp.dx[1]);

        const BelId mux_dx4_1 = addBel(x, y, LP_BELS*lp_idx + 6, id_mux_dx4);
        ctx->addBelInput(mux_dx4_1, id_in0, l41_dx);
        ctx->addBelInput(mux_dx4_1, id_in1, lp.dx40);
        ctx->addBelInput(mux_dx4_1, id_sel, lp.byp[2]);
        const WireId dx41_out = addWireAsBelOutput(x, y, id_out, mux_dx4_1);

        add_cfgmux(Loc(x, y, 0), l0_f2, lp.f2[0], lp.qx[0]);
        add_cfgmux(Loc(x, y, 0), l0_f3, lp.byp[0], lp.dx_fb);

        add_cfgmux(Loc(x, y, 0), l40_f0, lp.f0[1], lp.dx[1]);
        add_cfgmux(Loc(x, y, 0), l40_f1, lp.f1[1], l41_dx);
        add_cfgmux(Loc(x, y, 0), l40_f2, lp.f2[1], lp.qx[1], lp.fy);
        add_cfgmux(Loc(x, y, 0), l40_f3, lp.byp[1], lp.f2[2]);
        add_cfgmux(Loc(x, y, 0), l40_ca, lp.byp[3], l40_f2, lp.dx[0]); // Technically input 0 should be GND, and this should go through an inverter.
        WireId mux_sc = addWire(x, y, IdStringList::concat(ctx->getBelName(lut40), id_mux_sc), id_mux_sc);
        add_cfgmux(Loc(x, y, 0), mux_sc, lp.c_out, l40_s);
        add_cfgmux(Loc(x, y, 0), lp.dy, dx41_out, mux_sc);

        add_cfgmux(Loc(x, y, 0), l41_f3, lp.byp[2], lp.f2[1], lp.qx[1]);

        add_cfgmux(Loc(x, y, 0), r0_di, l40_s, lp.byp[3], lp.dx[0], lp.dx[1]);

        add_cfgmux(Loc(x, y, 0), r1_di, l40_s, lp.byp[4], lp.dx[0], dx41_out);

        add_cfgmux(Loc(x, y, 0), lp.dx[1], lp.dx40, l41_dx);
        add_cfgmux(Loc(x, y, 0), dx41_out, l41_dx, lp.dx40);
    }

    void create_logic_element(const int x, const int y, LeInfo& info) {
        // Create an unconnected wire for the initial carry-in if necessary.
        if (y > 1)
            info.lp[0].c_in = plbs[x][y-1].le.lp[3].c_out;
        else
            info.lp[0].c_in = addWire(x, y, IdStringList::concat(id_DUMMY, id_C_OUT), id_C_OUT);

        // Same for dx_fb
        if (y > 1)
            info.lp[0].dx_fb = plbs[x][y-1].le.lp[3].dx[1];
        else
            info.lp[0].dx_fb = addWire(x, y, IdStringList::concat(id_DUMMY, id_DX_FB), id_DX_FB);

        // Setup LBUF wires.
        info.lbuf.a_sr = addWire(x, y, IdStringList(id_a_sr), id_a_sr);
        info.lbuf.mclk_b = addWire(x, y, IdStringList(id_mclk_b), id_mclk_b);
        info.lbuf.sclk = addWire(x, y, IdStringList(id_sclk), id_sclk);
        info.lbuf.sh[0] = addWire(x, y, IdStringList::concat(id_shift, ctx->id(stringf("0"))), id_shift);
        info.lbuf.sh[1] = addWire(x, y, IdStringList::concat(id_shift, ctx->id(stringf("1"))), id_shift);

        /* lbuf bel I guess? */
        const WireId lbuf_en{};
        add_cfgmux(Loc(x, y, 0), info.lbuf.a_sr, plbs[x][y].cclk[0], plbs[x][y].cclk[1], plbs[x][y].cclk[2], plbs[x][y].cclk[3], plbs[x][y].cclk[4], plbs[x][y].cclk[5] /*, rc[1]? */);
        //add_cfgmux(Loc(x, y, 0), lbuf_en, plbs[x][y].cclk[0], plbs[x][y].cclk[1], plbs[x][y].cclk[2], plbs[x][y].cclk[3], plbs[x][y].cclk[4], plbs[x][y].cclk[5] /*, rc[0]? */);

        // Setup LP wires.
        for (int lp_idx = 0; lp_idx < 4; lp_idx++) {
            IdString lp_id = ctx->id(stringf("LP%d", lp_idx));
            LpInfo& lp = info.lp[lp_idx];

            // LP inputs.
            for (int byp = 0; byp < 5; byp++)
                lp.byp[byp] = addWire(x, y, IdStringList::concat(lp_id, ctx->id(stringf("BYP[%d]", byp))), id_BYP);

            for (int fx = 0; fx < 3; fx++) {
                lp.f0[fx] = addWire(x, y, IdStringList::concat(lp_id, ctx->id(stringf("F0[%d]", fx))), id_f0);
                lp.f1[fx] = addWire(x, y, IdStringList::concat(lp_id, ctx->id(stringf("F1[%d]", fx))), id_f1);
                lp.f2[fx] = addWire(x, y, IdStringList::concat(lp_id, ctx->id(stringf("F2[%d]", fx))), id_f2);
            }

            if (x > 1)
                lp.fy = plbs[x-1][y].le.lp[lp_idx].dy;
            else
                lp.fy = addWire(x, y, IdStringList::concat(lp_id, id_DUMMY), id_DY);

            // LP outputs.
            info.lp[lp_idx].c_out = addWire(x, y, IdStringList::concat(lp_id, id_C_OUT), id_C_OUT);

            for (int dx = 0; dx < 2; dx++) {
                lp.dx[dx] = addWire(x, y, IdStringList::concat(lp_id, ctx->id(stringf("DX[%d]", dx))), id_dx);
                lp.qx[dx] = addWire(x, y, IdStringList::concat(lp_id, ctx->id(stringf("QX[%d]", dx))), id_qx);
            }

            info.lp[lp_idx].dx40 = addWire(x, y, IdStringList::concat(lp_id, id_DX40), id_DX40);
            info.lp[lp_idx].dy = addWire(x, y, IdStringList::concat(lp_id, id_DY), id_DY);
        }

        for (int lp_idx = 0; lp_idx < 4; lp_idx++) {
            if (lp_idx != 0) {
                info.lp[lp_idx].c_in = info.lp[lp_idx-1].c_out;
                info.lp[lp_idx].down_i[0] = info.lp[lp_idx-1].qx[0];
                info.lp[lp_idx].down_i[1] = info.lp[lp_idx-1].qx[1];
                info.lp[lp_idx].dx_fb = info.lp[lp_idx-1].dx[1];
            }

            if (lp_idx != 3) {
                info.lp[lp_idx].up_i[0] = info.lp[lp_idx+1].qx[0];
                info.lp[lp_idx].up_i[1] = info.lp[lp_idx+1].qx[1];
            }

            create_logic_parcel(x, y, lp_idx, info);
        }
    }

    void create_input_crossbars(const int x_coord, const int y_coord, PlbInfo& plb)
    {
        const auto& lp = plb.le.lp;

        const WireId i[4][4][16] = {
            {
                { plb.te0_o[0], plb.oe0_o[0], get(x_coord+2, y_coord).te0_o[0], get(x_coord+1, y_coord).e_xy_o[0], get(x_coord+1, y_coord).te0_o[0], get(x_coord+4, y_coord).oe0_o[0], get(x_coord-1, y_coord).w_xy_o[1], plb.e_xy_o[0], get(x_coord+2, y_coord).te0_o[1], plb.te0_o[1], plb.oe0_o[1], plb.me0_o[0], get(x_coord+1, y_coord).ne_xy_o, get(x_coord+1, y_coord).te0_o[1], get(x_coord+4, y_coord).oe0_o[1], get(x_coord+1, y_coord).me0_o[0], },
                { plb.ts0_o[0], plb.os0_o[0], get(x_coord+1, y_coord+1).ne_xy_o, lp[3].byp[2], get(x_coord, y_coord+1).tn0_o[0], get(x_coord, y_coord+4).on0_o[1], get(x_coord, y_coord+2).tn0_o[0], get(x_coord, y_coord+1).tn0_o[1], lp[2].byp[2], get(x_coord, y_coord+1).n_xy_o[0], plb.on0_o[0], plb.mn0_o[0], get(x_coord-1, y_coord).ws_xy_o[0], plb.s_xy_o[0], get(x_coord, y_coord-4).os0_o[1], get(x_coord, y_coord-1).ms0_o[0], },
                { get(x_coord, y_coord+1).mn0_o[0], get(x_coord, y_coord+4).on0_o[0], get(x_coord-1, y_coord).nw_xy_o[0], lp[2].byp[0], plb.ms0_o[0], plb.os0_o[1], get(x_coord-1, y_coord+1).nw_xy_o[0], lp[1].byp[2], get(x_coord+1, y_coord).es_xy_o, get(x_coord, y_coord-2).ts0_o[0], get(x_coord, y_coord-4).os0_o[0], get(x_coord, y_coord-1).ts0_o[0], get(x_coord, y_coord+2).tn0_o[1], plb.tn0_o[1], plb.on0_o[1], plb.tn0_o[0], },
                { get(x_coord-1, y_coord).mw0_o[0], get(x_coord-4, y_coord).ow0_o[0], lp[0].dy, lp[0].dx[0], plb.mw0_o[0], plb.ow0_o[0], lp[0].qx[0], lp[2].qx[0], lp[2].dx[0], get(x_coord+1, y_coord+1).en_xy_o[0], get(x_coord-4, y_coord).ow0_o[1], get(x_coord-1, y_coord).tw0_o[0], lp[0].byp[1], get(x_coord-2, y_coord).tw0_o[0], plb.ow0_o[1], plb.tw0_o[0], },
            },
            {
                { plb.te0_o[2], plb.oe0_o[2], get(x_coord+2, y_coord).te0_o[2], get(x_coord+1, y_coord).e_xy_o[1], get(x_coord+1, y_coord).te0_o[2], get(x_coord+4, y_coord).oe0_o[2], get(x_coord-1, y_coord+1).wn_xy_o, lp[0].byp[2], lp[2].byp[3], get(x_coord+1, y_coord+1).en_xy_o[1], plb.oe0_o[3], plb.me0_o[1], lp[2].dx[1], get(x_coord-1, y_coord+1).nw_xy_o[1], get(x_coord+4, y_coord).oe0_o[3], get(x_coord+1, y_coord).me0_o[1], },
                { plb.ts0_o[1], plb.os0_o[2], plb.ts0_o[2], get(x_coord, y_coord-2).ts0_o[2], get(x_coord, y_coord+1).tn0_o[2], get(x_coord, y_coord+4).on0_o[3], get(x_coord, y_coord+2).tn0_o[2], lp[1].dy, lp[2].byp[1], plb.s_xy_o[1], plb.on0_o[2], plb.mn0_o[1], get(x_coord-1, y_coord).ws_xy_o[1], get(x_coord, y_coord+1).n_xy_o[1], get(x_coord, y_coord-4).os0_o[3], get(x_coord, y_coord-1).ms0_o[1], },
                { get(x_coord, y_coord+1).mn0_o[1], get(x_coord, y_coord+4).on0_o[2], plb.n_xy_o[0], lp[0].dx[1], plb.ms0_o[1], plb.os0_o[3], lp[0].byp[4], get(x_coord, y_coord-1).s_xy_o[0], get(x_coord, y_coord-1).ts0_o[2], get(x_coord, y_coord-2).ts0_o[1], get(x_coord, y_coord-4).os0_o[2], get(x_coord, y_coord-1).ts0_o[1], lp[0].qx[1], lp[2].qx[1], plb.on0_o[3], plb.tn0_o[2], },
                { get(x_coord-1, y_coord).mw0_o[1], get(x_coord-4, y_coord).ow0_o[2], get(x_coord-1, y_coord).tw0_o[2], lp[3].byp[3], plb.mw0_o[1], plb.ow0_o[2], plb.tw0_o[2], get(x_coord-2, y_coord).tw0_o[2], get(x_coord-1, y_coord).nw_xy_o[1], plb.w_xy_o[0], get(x_coord-4, y_coord).ow0_o[3], get(x_coord-1, y_coord).tw0_o[1], get(x_coord-1, y_coord).w_xy_o[0], get(x_coord-2, y_coord).tw0_o[1], plb.ow0_o[3], plb.tw0_o[1], },
            },
            {
                { plb.te0_o[3], plb.oe0_o[4], get(x_coord+2, y_coord).te0_o[3], get(x_coord+1, y_coord).e_xy_o[2], get(x_coord+1, y_coord).te0_o[3], get(x_coord+4, y_coord).oe0_o[4], get(x_coord+1, y_coord).se_xy_o[0], plb.e_xy_o[2], get(x_coord+2, y_coord).te0_o[4], plb.te0_o[4], plb.oe0_o[5], plb.me0_o[2], lp[3].dx[0], get(x_coord+1, y_coord).te0_o[4], get(x_coord+4, y_coord).oe0_o[5], get(x_coord+1, y_coord).me0_o[2], },
                { plb.ts0_o[3], plb.os0_o[4], lp[1].qx[0], lp[3].qx[0], get(x_coord, y_coord+1).tn0_o[3], get(x_coord, y_coord+4).on0_o[5], get(x_coord, y_coord+2).tn0_o[3], get(x_coord, y_coord+1).tn0_o[4], lp[2].byp[4], get(x_coord, y_coord+1).n_xy_o[2], plb.on0_o[4], plb.mn0_o[2], lp[1].byp[3], plb.s_xy_o[2], get(x_coord, y_coord-4).os0_o[5], get(x_coord, y_coord-1).ms0_o[2], },
                { get(x_coord, y_coord+1).mn0_o[2], get(x_coord, y_coord+4).on0_o[4], get(x_coord+1, y_coord).en_xy_o[0], get(x_coord, y_coord-1).s_xy_o[1], plb.ms0_o[2], plb.os0_o[5], plb.n_xy_o[1], lp[1].dx[0], lp[2].dy, get(x_coord, y_coord-2).ts0_o[3], get(x_coord, y_coord-4).os0_o[4], get(x_coord, y_coord-1).ts0_o[3], get(x_coord, y_coord+2).tn0_o[4], plb.tn0_o[4], plb.on0_o[5], plb.tn0_o[3], },
                { get(x_coord-1, y_coord).mw0_o[2], get(x_coord-4, y_coord).ow0_o[4], get(x_coord+1, y_coord-1).es_xy_o, get(x_coord+1, y_coord-1).se_xy_o[0], plb.mw0_o[2], plb.ow0_o[4], get(x_coord-1, y_coord-1).ws_xy_o[0], lp[1].byp[0], lp[3].byp[4], plb.w_xy_o[1], get(x_coord-4, y_coord).ow0_o[5], get(x_coord-1, y_coord).tw0_o[3], lp[0].byp[3], get(x_coord-2, y_coord).tw0_o[3], plb.ow0_o[5], plb.tw0_o[3], },
            },
            {
                { plb.te0_o[5], plb.oe0_o[6], get(x_coord+2, y_coord).te0_o[5], get(x_coord+1, y_coord-1).se_xy_o[1], get(x_coord+1, y_coord).te0_o[5], get(x_coord+4, y_coord).oe0_o[6], lp[1].byp[4], get(x_coord+1, y_coord).se_xy_o[1], lp[1].qx[1], lp[3].qx[1], plb.on0_o[8], plb.me0_o[3], plb.e_xy_o[1], lp[3].dy, get(x_coord, y_coord+4).on0_o[8], get(x_coord+1, y_coord).me0_o[3], },
                { plb.ts0_o[4], plb.os0_o[6], plb.ts0_o[5], get(x_coord, y_coord-2).ts0_o[5], get(x_coord, y_coord+1).tn0_o[5], get(x_coord, y_coord+4).on0_o[7], get(x_coord, y_coord+2).tn0_o[5], lp[0].byp[0], lp[3].byp[0], get(x_coord+1, y_coord).en_xy_o[1], plb.on0_o[6], plb.mn0_o[3], get(x_coord-1, y_coord-1).sw_xy_o, lp[1].byp[1], get(x_coord, y_coord-4).os0_o[7], get(x_coord, y_coord-1).ms0_o[3], },
                { get(x_coord, y_coord+1).mn0_o[3], get(x_coord, y_coord+4).on0_o[6], plb.n_xy_o[2], lp[1].dx[1], plb.ms0_o[3], plb.os0_o[7], get(x_coord-1, y_coord-1).ws_xy_o[1], get(x_coord, y_coord-1).s_xy_o[2], get(x_coord, y_coord-1).ts0_o[5], get(x_coord, y_coord-2).ts0_o[4], get(x_coord, y_coord-4).os0_o[6], get(x_coord, y_coord-1).ts0_o[4], lp[3].dx[1], get(x_coord-1, y_coord).wn_xy_o, plb.on0_o[7], plb.tn0_o[5], },
                { get(x_coord-1, y_coord).mw0_o[3], get(x_coord-4, y_coord).ow0_o[6], get(x_coord-1, y_coord).tw0_o[5], lp[3].byp[1], plb.mw0_o[3], plb.ow0_o[6], plb.tw0_o[5], get(x_coord-2, y_coord).tw0_o[5], get(x_coord-1, y_coord).sw_xy_o, plb.w_xy_o[2], get(x_coord, y_coord-4).os0_o[8], get(x_coord-1, y_coord).tw0_o[4], get(x_coord-1, y_coord).w_xy_o[2], get(x_coord-2, y_coord).tw0_o[4], plb.os0_o[8], plb.tw0_o[4], },
            },
        };

        const WireId z[4][4][8] = {
            {
                { plb.on0_o[0], plb.on0_o[1], lp[3].f2[0], lp[1].f0[2], lp[0].byp[0], plb.tn0_o[1], lp[0].f2[2], lp[0].f0[0], },
                { plb.mn0_o[0], plb.tn0_o[0], lp[3].byp[1], plb.te0_o[1], plb.mw0_o[0], plb.te0_o[0], plb.ow0_o[1], plb.oe0_o[1], },
                { lp[2].f0[2], lp[2].byp[2], plb.ts0_o[0], plb.ms0_o[0], plb.ow0_o[0], plb.oe0_o[0], plb.tw0_o[0], plb.me0_o[0], },
                { lp[3].f1[1], lp[2].f1[0], plb.os0_o[0], plb.os0_o[1], lp[2].f2[1], lp[0].f1[1], lp[0].byp[4], lp[1].byp[3], },
            },
            {
                { plb.on0_o[2], plb.on0_o[3], lp[2].f1[2], lp[2].f0[0], lp[3].byp[2], lp[3].f0[0], lp[1].f2[0], lp[0].f0[1], },
                { plb.mn0_o[1], plb.tn0_o[2], plb.ts0_o[2], lp[2].byp[3], plb.mw0_o[1], plb.te0_o[2], plb.ow0_o[3], plb.oe0_o[3], },
                { plb.tw0_o[2], lp[0].byp[1], plb.ts0_o[1], plb.ms0_o[1], plb.ow0_o[2], plb.oe0_o[2], plb.tw0_o[1], plb.me0_o[1], },
                { lp[3].f2[1], lp[2].f1[1], plb.os0_o[2], plb.os0_o[3], lp[1].f2[1], lp[0].f1[2], lp[1].byp[0], lp[1].byp[4], },
            },
            {
                { plb.on0_o[4], plb.on0_o[5], lp[2].f2[2], lp[2].f0[1], lp[2].byp[4], plb.tn0_o[4], lp[0].f2[0], lp[0].f0[2], },
                { plb.mn0_o[2], plb.tn0_o[3], lp[3].f0[1], plb.te0_o[4], plb.mw0_o[2], plb.te0_o[3], plb.ow0_o[5], plb.oe0_o[5], },
                { lp[3].byp[3], lp[0].byp[2], plb.ts0_o[3], plb.ms0_o[2], plb.ow0_o[4], plb.oe0_o[4], plb.tw0_o[3], plb.me0_o[2], },
                { lp[3].f1[2], lp[1].f1[1], plb.os0_o[4], plb.os0_o[5], lp[1].f2[2], lp[1].f1[0], lp[1].byp[1], lp[2].byp[0], },
            },
            {
                { plb.on0_o[6], plb.on0_o[7], lp[3].f1[0], lp[1].f0[1], lp[3].byp[0], lp[0].byp[3], lp[0].f2[1], lp[1].f0[0], },
                { plb.mn0_o[3], plb.tn0_o[5], plb.ts0_o[5], lp[3].f0[2], plb.mw0_o[3], plb.te0_o[5], plb.on0_o[8], plb.os0_o[8], },
                { plb.tw0_o[5], lp[3].byp[4], plb.ts0_o[4], plb.ms0_o[3], plb.ow0_o[6], plb.oe0_o[6], plb.tw0_o[4], plb.me0_o[3], },
                { lp[3].f2[2], lp[1].f1[2], plb.os0_o[6], plb.os0_o[7], lp[2].f2[0], lp[0].f1[0], lp[1].byp[2], lp[2].byp[1], },
            },
        };

        const int swizzle[8] = {0, 1, 4, 5, 2, 3, 6, 7};

        // Far, får får får? Nej, får får inte får, får får lamm.
        for (int ixbar = 0; ixbar < 4; ixbar++) {
            WireId x[4][8][4];

            for (int a = 0; a < 4; a++)
                for (int b = 0; b < 8; b++)
                    for (int c = 0; c < 4; c++) {
                        IdString ixbar_id = ctx->id(stringf("IXBAR%d", ixbar));
                        IdStringList a_id = IdStringList::concat(ixbar_id, ctx->id(stringf("IX%d", a)));
                        IdStringList b_id = IdStringList::concat(a_id, ctx->id(stringf("IX%d", b)));
                        IdStringList id = IdStringList::concat(b_id, ctx->id(stringf("SC_MUX_%d", c)));
                        x[a][b][c] = addWire(x_coord, y_coord, id, id_IXIX);
                    }

            // stage 1: the "ixix": select 32 groups of 4 signals from the i inputs.
            for (int ixN = 0; ixN < 4; ixN++)
                for (int ixM = 0; ixM < 8; ixM++)
                    for (int SC_mux_C = 0; SC_mux_C < 4; SC_mux_C++)
                        for (int a = 0; a < 4; a++)
                            add_cfgmux(Loc(x_coord, y_coord, 32*ixN+4*ixM+SC_mux_C), x[ixN][ixM][SC_mux_C], i[ixbar][a][ixN*4+SC_mux_C]);

            // some ixixes can select from the clocks, as well.
            if (ixbar < 2) {
                for (int ixB = 6; ixB < 8; ixB++) {
                    add_cfgmux(Loc(x_coord, y_coord, 0), x[ixbar][ixB][0], plb.clk_xbar[2*ixbar+0]);
                    add_cfgmux(Loc(x_coord, y_coord, 0), x[ixbar][ixB][3], plb.clk_xbar[2*ixbar+1]);
                }
            }

            // stage 2: the "xzzx": select 16 signals from 4 groups of 4 signals from the ixix.
            for (int xzN = 0; xzN < 8; xzN++) {
                for (int zxM = 0; zxM < 4; zxM++)
                    for (const auto x : x)
                        for (const auto x : x[swizzle[xzN]])
                            add_cfgmux(Loc(x_coord, y_coord, 4*xzN+zxM), z[ixbar][zxM][xzN], x);
            }
        }
    }

    void create_output_crossbar(const int x, const int y, PlbInfo& plb)
    {
        const WireId xy[24] = {
            plb.s_xy_o[0],
            plb.ws_xy_o[0],
            plb.e_xy_o[0],
            plb.es_xy_o,
            plb.w_xy_o[0],
            plb.se_xy_o[0],
            plb.s_xy_o[1],
            plb.ws_xy_o[1],
            plb.n_xy_o[0],
            plb.sw_xy_o,
            plb.s_xy_o[2],
            plb.se_xy_o[1],
            plb.w_xy_o[1],
            plb.nw_xy_o[0],
            plb.e_xy_o[2],
            plb.ne_xy_o,
            plb.n_xy_o[1],
            plb.en_xy_o[0],
            plb.e_xy_o[1],
            plb.nw_xy_o[1],
            plb.w_xy_o[2],
            plb.wn_xy_o,
            plb.n_xy_o[2],
            plb.en_xy_o[1],
        };

        for (int n = 0; n < 24; n++)
            add_cfgmux(Loc(x, y, n), xy[n],
                plb.le.lp[0].dx[0],
                plb.le.lp[0].qx[0],
                plb.le.lp[0].dx[1],
                plb.le.lp[0].qx[1],
                plb.le.lp[0].dy,
                plb.le.lp[1].dx[0],
                plb.le.lp[1].qx[0],
                plb.le.lp[1].dx[1],
                plb.le.lp[1].qx[1],
                plb.le.lp[1].dy,
                plb.le.lp[2].dx[0],
                plb.le.lp[2].qx[0],
                plb.le.lp[2].dx[1],
                plb.le.lp[2].qx[1],
                plb.le.lp[2].dy,
                plb.le.lp[3].dx[0],
                plb.le.lp[3].qx[0],
                plb.le.lp[3].dx[1],
                plb.le.lp[3].qx[1],
                plb.le.lp[3].dy,
                plb.le.lp[3].byp[4],
                plb.le.lp[2].byp[4],
                plb.le.lp[1].byp[4],
                plb.le.lp[0].byp[4]
            );
    }

    void create_programmable_logic_block(const int x, const int y) {
        create_logic_element(x, y, plbs[x][y].le);
        create_input_crossbars(x, y, plbs[x][y]);
        create_output_crossbar(x, y, plbs[x][y]);
    }

    void create_bels() {
        for (int x = 0; x < COLUMNS; x++) {
            for (int y = 0; y < ROWS; y++) {
                plbs[x][y] = setup_plb(x, y);
            }
        }

        for (int x = 1; x < COLUMNS - 1; x++) {
            for (int y = 0; y < ROWS; y++) {
                const bool at_y_edge = (y == 0) || (y == ROWS - 1);
                if (at_y_edge) {
                    create_iopad(x, y);
                }
            }
        }

        for (int x = 0; x < COLUMNS; x++) {
            const bool at_x_edge = (x == 0) || (x == COLUMNS - 1);
            for (int y = 0; y < ROWS; y++) {
                const bool at_y_edge = (y == 0) || (y == ROWS - 1);
                if (!at_x_edge && !at_y_edge) {
                    create_programmable_logic_block(x, y);
                }
            }
        }
    }

    PlbInfo setup_plb(const int x, const int y) {
        PlbInfo plb{};
        for (int triple = 0; triple < 6; triple++) {
            IdString id = ctx->id(stringf("%d", triple));
            plb.tn0_o[triple] = addWire(x, y, IdStringList::concat(id_TN, id), id_TN);
            plb.te0_o[triple] = addWire(x, y, IdStringList::concat(id_TE, id), id_TE);
            plb.ts0_o[triple] = addWire(x, y, IdStringList::concat(id_TS, id), id_TS);
            plb.tw0_o[triple] = addWire(x, y, IdStringList::concat(id_TW, id), id_TW);
            plb.cclk[triple] = addWire(x, y, IdStringList::concat(id_CCLK, id), id_CCLK);
        }

        for (int mono = 0; mono < 4; mono++) {
            IdString id = ctx->id(stringf("%d", mono));
            plb.mn0_o[mono] = addWire(x, y, IdStringList::concat(id_MN, id), id_MN);
            plb.me0_o[mono] = addWire(x, y, IdStringList::concat(id_ME, id), id_ME);
            plb.ms0_o[mono] = addWire(x, y, IdStringList::concat(id_MS, id), id_MS);
            plb.mw0_o[mono] = addWire(x, y, IdStringList::concat(id_MW, id), id_MW);
            plb.clk_xbar[mono] = addWire(x, y, IdStringList::concat(id_CLK_XBAR, id), id_CLK_XBAR);
        }

        for (int octal = 0; octal < 9; octal++) {
            IdString id = ctx->id(stringf("%d", octal));
            plb.on0_o[octal] = addWire(x, y, IdStringList::concat(id_ON, id), id_ON);
            plb.os0_o[octal] = addWire(x, y, IdStringList::concat(id_OS, id), id_OS);
            if (octal < 7) {
                plb.oe0_o[octal] = addWire(x, y, IdStringList::concat(id_OE, id), id_OE);
                plb.ow0_o[octal] = addWire(x, y, IdStringList::concat(id_OW, id), id_OW);
            }
        }

        for (int xy = 0; xy < 3; xy++) {
            IdString id = ctx->id(stringf("%d", xy));
            plb.n_xy_o[xy] = addWire(x, y, IdStringList::concat(id_XYN, id), id_XYN);
            plb.e_xy_o[xy] = addWire(x, y, IdStringList::concat(id_XYE, id), id_XYE);
            plb.s_xy_o[xy] = addWire(x, y, IdStringList::concat(id_XYS, id), id_XYS);
            plb.w_xy_o[xy] = addWire(x, y, IdStringList::concat(id_XYW, id), id_XYW);
        }

        for (int xy = 0; xy < 2; xy++) {
            IdString id = ctx->id(stringf("%d", xy));
            plb.en_xy_o[xy] = addWire(x, y, IdStringList::concat(id_XYEN, id), id_XYEN);
            plb.se_xy_o[xy] = addWire(x, y, IdStringList::concat(id_XYSE, id), id_XYSE);
            plb.ws_xy_o[xy] = addWire(x, y, IdStringList::concat(id_XYWS, id), id_XYWS);
            plb.nw_xy_o[xy] = addWire(x, y, IdStringList::concat(id_XYNW, id), id_XYNW);
        }

        IdString id = ctx->id("0");
        plb.ne_xy_o = addWire(x, y, IdStringList::concat(id_XYNE, id), id_XYNE);
        plb.es_xy_o = addWire(x, y, IdStringList::concat(id_XYES, id), id_XYES);
        plb.sw_xy_o = addWire(x, y, IdStringList::concat(id_XYSW, id), id_XYSW);
        plb.wn_xy_o = addWire(x, y, IdStringList::concat(id_XYWN, id), id_XYWN);

        return plb;
    }

    PlbInfo get(int x, int y) const {
        if (x < 0)
            x = -x;
        if (x >= COLUMNS)
            x = COLUMNS - (x - COLUMNS);
        if (y < 0)
            y = -y;
        if (y >= ROWS)
            y = ROWS - (y - ROWS);
        return plbs[x][y];
    }

    bool lp_is_valid(int x, int y, int lp_idx) const {
        const CellInfo *lut0 = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, LP_BELS*lp_idx + 0)));
        const CellInfo *lut40 = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, LP_BELS*lp_idx + 1)));
        const CellInfo *lut41 = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, LP_BELS*lp_idx + 2)));
        const CellInfo *reg0 = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, LP_BELS*lp_idx + 3)));
        const CellInfo *reg1 = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, LP_BELS*lp_idx + 4)));
        const CellInfo *mux_dx4_0 = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, LP_BELS*lp_idx + 5)));
        const CellInfo *mux_dx4_1 = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, LP_BELS*lp_idx + 6)));

        // TODO: mux_dx4

        if (!lut0 && !lut40 && !lut41) {
            // assume routing via byp[34]
            return true;
        }

        /*if (reg0) {
            if ()
        }*/

        // common signals for reg0/reg1
        if (reg0 && reg1) {
            if (reg0->getPort(id_a_sr) != reg1->getPort(id_a_sr))
                return false;
            if (reg0->getPort(id_mclk_b) != reg1->getPort(id_mclk_b))
                return false;
            if (reg0->getPort(id_sclk) != reg1->getPort(id_sclk))
                return false;
        }

        return true;
    }

private:
    ViaductHelpers h;

    static const int COLUMNS = 28;
    static const int ROWS = 50;
    static const int LP_BELS = 7;

    std::array<std::array<PlbInfo, ROWS>, COLUMNS> plbs;
};

struct HerculesArch : ViaductArch {
    HerculesArch() : ViaductArch("hercules") {}
    std::unique_ptr<ViaductAPI> create(const dict<std::string, std::string> &args) {
        return std::make_unique<Hercules>();
    }
} hercules;
} // namespace

NEXTPNR_NAMESPACE_END

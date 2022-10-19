/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2022  gatecat <gatecat@ds0.me>
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

#include "placer_star.h"
#include <Eigen/Core>
#include <Eigen/IterativeLinearSolvers>
#include <boost/optional.hpp>
#include <chrono>
#include <deque>
#include <fstream>
#include <numeric>
#include <queue>
#include <tuple>
#include "array2d.h"
#include "fast_bels.h"
#include "log.h"
#include "nextpnr.h"
#include "parallel_refine.h"
#include "place_common.h"
#include "placer1.h"
#include "scope_lock.h"
#include "timing.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

enum class Axis
{
    X,
    Y
};

struct RealLoc
{
    RealLoc() : x(0), y(0){};
    RealLoc(double x, double y) : x(x), y(y){};
    double x, y;
    RealLoc &operator+=(const RealLoc &other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }
    RealLoc &operator/=(double factor)
    {
        x /= factor;
        y /= factor;
        return *this;
    }
    RealLoc operator/(double factor) const { return RealLoc(x / factor, y / factor); }
    // to simplify axis-generic code
    double &at(Axis axis) { return (axis == Axis::Y) ? y : x; }
    const double &at(Axis axis) const { return (axis == Axis::Y) ? y : x; }
};

struct PlacerBucket
{
    BelBucketId bucket;
    int total_bels = 0;
    array2d<int> loc_bels;
};

struct PlacerCell
{
    CellInfo *ci;
    bool fixed = false, global = false;
    int xi, yi; // legalised grid x,y position
    int bucket;
    RealLoc r;
    int macro_idx = -1;
    Loc macro_offset;
};

struct PlacerMacro
{
    ClusterId cluster;
    bool fixed = false;
    std::vector<int> area;  // area by bucket
    std::vector<int> cells; // subcells
    int root = -1;
};

struct PlacerNet
{
    NetInfo *ni;
    RealLoc centroid;

    RealLoc pos_sum, delta_sum, pcost;
    double tmg_critsqsum;
    RealLoc tmg_deltasum, tmg_critsqpossum, tcost;
};

class StarPlacer
{
    Context *ctx;
    PlacerStarCfg cfg;

    std::vector<PlacerBucket> buckets;
    std::vector<PlacerCell> cells;
    std::vector<PlacerMacro> macros;
    std::vector<PlacerNet> nets;

    idict<BelBucketId> bucket2idx;
    idict<ClusterId> cluster2idx;

    FastBels fast_bels;
    TimingAnalyser tmg;

    int width, height;

    void init_buckets()
    {
        for (auto bel : ctx->getBels()) {
            BelBucketId bucket = ctx->getBelBucketForBel(bel);
            int idx = bucket2idx(bucket);
            if (idx >= int(buckets.size())) {
                buckets.emplace_back();
                buckets.back().bucket = bucket;
                buckets.back().loc_bels.reset(width, height);
            }
            Loc l = ctx->getBelLocation(bel);
            auto &b = buckets.at(idx);
            b.total_bels++;
            b.loc_bels.at(l.x, l.y)++;
        }
    }
    void init_cells()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            // init our own cells araay
            ci->udata = cells.size();
            cells.emplace_back();
            auto &data = cells.back();
            data.ci = ci;
            // process legacy-ish bel attributes
            if (ci->attrs.count(ctx->id("BEL")) && ci->bel == BelId()) {
                std::string loc_name = ci->attrs.at(ctx->id("BEL")).as_string();
                BelId bel = ctx->getBelByNameStr(loc_name);
                if (bel == BelId()) {
                    log_error("No Bel named \'%s\' located for "
                              "this chip (processing BEL attribute on \'%s\')\n",
                              loc_name.c_str(), ci->name.c_str(ctx));
                }

                if (!ctx->isValidBelForCellType(ci->type, bel)) {
                    IdString bel_type = ctx->getBelType(bel);
                    log_error("Bel \'%s\' of type \'%s\' does not match cell "
                              "\'%s\' of type \'%s\'\n",
                              loc_name.c_str(), bel_type.c_str(ctx), ci->name.c_str(ctx), ci->type.c_str(ctx));
                }
                auto bound_cell = ctx->getBoundBelCell(bel);
                if (bound_cell) {
                    log_error("Cell \'%s\' cannot be bound to bel \'%s\' since it is already bound to cell \'%s\'\n",
                              ci->name.c_str(ctx), loc_name.c_str(), bound_cell->name.c_str(ctx));
                }
                ctx->bindBel(bel, ci, STRENGTH_USER);
            }
            // already constrained
            if (ci->bel != BelId()) {
                Loc l = ctx->getBelLocation(ci->bel);
                data.r.x = data.xi = l.x;
                data.r.y = data.yi = l.y;
                data.fixed = true;
            }
            data.bucket = bucket2idx.at(ctx->getBelBucketForCellType(ci->type));
        }
    }
    void init_nets()
    {
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            ni->udata = nets.size();
            nets.emplace_back();
            nets.back().ni = ni;
        }
    }
    void init_macros()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            auto &cell_data = cells.at(ci->udata);
            ClusterId cluster = ci->cluster;
            if (cluster == ClusterId())
                continue;
            int idx = cluster2idx(cluster);
            if (idx >= int(macros.size())) {
                macros.emplace_back();
                macros.back().cluster = cluster;
                macros.back().area.resize(buckets.size());
            }
            auto &macro_data = macros.at(idx);
            cell_data.macro_idx = idx;
            cell_data.macro_offset = ctx->getClusterOffset(ci);
            macro_data.cells.push_back(ci->udata);
            macro_data.area.at(bucket2idx.at(ctx->getBelBucketForCellType(ci->type))) += 1;
            if (cell_data.fixed)
                macro_data.fixed = true;
            if (ci == ctx->getClusterRootCell(cluster))
                macro_data.root = ci->udata;
        }
    }
    void place_initial()
    {
        // TODO: randomly place unconstrained IO (let's ignore this case for now)
        // pin propagation pre-placement
        dict<IdString, RealLoc> fwd_loc, bwd_loc;
        TopoSort<IdString> order;
        for (auto &c : cells) {
            if (c.fixed) {
                fwd_loc[c.ci->name] = RealLoc(c.xi, c.yi);
                bwd_loc[c.ci->name] = RealLoc(c.xi, c.yi);
            }
            order.node(c.ci->name);
            for (auto &port : c.ci->ports) {
                if (port.second.type == PORT_IN && port.second.net != nullptr &&
                    port.second.net->driver.cell != nullptr)
                    order.edge(port.second.net->driver.cell->name, c.ci->name);
            }
        }
        RealLoc centroid(ctx->getGridDimX() / 2.0, ctx->getGridDimY() / 2.0);
        order.sort();
        // forward propagate
        for (auto cell_name : order.sorted) {
            if (fwd_loc.count(cell_name))
                continue; // already locked
            RealLoc s{};
            int count = 0;
            CellInfo *ci = ctx->cells.at(cell_name).get();
            for (auto &port : ci->ports) {
                if (port.second.type == PORT_IN && port.second.net != nullptr &&
                    port.second.net->driver.cell != nullptr) {
                    IdString drv = port.second.net->driver.cell->name;
                    if (fwd_loc.count(drv)) {
                        s += fwd_loc.at(drv);
                        ++count;
                    }
                }
            }
            if (count > 0)
                fwd_loc[cell_name] = s / count;
        }
        // backward propagate
        std::reverse(order.sorted.begin(), order.sorted.end());
        for (auto cell_name : order.sorted) {
            if (bwd_loc.count(cell_name))
                continue; // already locked
            RealLoc s;
            int count = 0;
            CellInfo *ci = ctx->cells.at(cell_name).get();
            for (auto &port : ci->ports) {
                if (port.second.type == PORT_OUT && port.second.net != nullptr) {
                    for (auto &usr : port.second.net->users) {
                        if (!bwd_loc.count(usr.cell->name))
                            continue;
                        s += bwd_loc.at(usr.cell->name);
                        ++count;
                    }
                }
            }
            if (count > 0)
                bwd_loc[cell_name] = s / count;
        }
        // merge forward and backward results
        for (auto &c : cells) {
            if (c.fixed)
                continue;
            int count = 0;
            RealLoc s;
            if (fwd_loc.count(c.ci->name)) {
                s += fwd_loc.at(c.ci->name);
                ++count;
            }
            if (bwd_loc.count(c.ci->name)) {
                s += bwd_loc.at(c.ci->name);
                ++count;
            }
            if (count == 0) {
                // fall back to centroid if neither fwd or bwd found any connectivity
                c.r = centroid;
            } else {
                c.r = s / count;
            }
            c.xi = int(c.r.x + 0.5);
            c.yi = int(c.r.y + 0.5);
        }
    }
    void update_nets()
    {
        // TODO: the incremental update as described in the paper
        for (auto &n : nets) {
            int count = 0;
            // wirelength cost
            n.pos_sum = RealLoc(0, 0);
            n.delta_sum = RealLoc(0, 0);
            n.centroid = RealLoc(0, 0);
            n.pcost = RealLoc(1, 1);
            if (n.ni->driver.cell) {
                const auto &drv = cells.at(n.ni->driver.cell->udata);
                n.pos_sum += drv.r;
                ++count;
            }
            for (const auto &user : n.ni->users) {
                const auto &u = cells.at(user.cell->udata);
                n.pos_sum += u.r;
                ++count;
            }
            if (count > 0) {
                n.centroid = n.pos_sum / count;
                if (n.ni->driver.cell) {
                    const auto &drv = cells.at(n.ni->driver.cell->udata);
                    n.delta_sum += RealLoc(std::pow(drv.r.x - n.centroid.x, 2), std::pow(drv.r.y - n.centroid.y, 2));
                }
                for (const auto &user : n.ni->users) {
                    const auto &u = cells.at(user.cell->udata);
                    n.delta_sum += RealLoc(std::pow(u.r.x - n.centroid.x, 2), std::pow(u.r.y - n.centroid.y, 2));
                }
                for (auto axis : {Axis::X, Axis::Y}) {
                    n.pcost.at(axis) = std::sqrt(1.0 + n.delta_sum.at(axis));
                }
            }
            // timing cost
            n.tcost = RealLoc(0, 0);
            n.tmg_deltasum = RealLoc(0, 0);
            n.tmg_critsqsum = 0;
            n.tmg_critsqpossum = RealLoc(0, 0);
            if (n.ni->driver.cell) {
                const auto &drv = cells.at(n.ni->driver.cell->udata);
                for (auto axis : {Axis::X, Axis::Y}) {
                    for (auto &user : n.ni->users) {
                        double crit = tmg.get_criticality(CellPortKey(user));
                        const auto &u = cells.at(user.cell->udata);
                        n.tmg_deltasum.at(axis) += std::pow((u.r.at(axis) - drv.r.at(axis)) * crit, 2);
                        if (axis == Axis::X)
                            n.tmg_critsqsum += std::pow(crit, 2);
                        n.tmg_critsqpossum.at(axis) += std::pow(crit, 2) * u.r.at(axis);
                    }
                    n.tcost.at(axis) = std::sqrt(1 + n.tmg_deltasum.at(axis));
                }
            }
        }
    }

    void update_net_cost(int net, const CellInfo *cell, Axis axis, double old_loc, double new_loc)
    {
        // TODO: incr updates
    }

    const double lambda = 0.5; // TODO

    void calculate_cell(const CellInfo *cell, Axis axis, double &w, double &g)
    {
        for (const auto &p : cell->ports) {
            const PortInfo &pi = p.second;
            if (!pi.net || !pi.net->driver.cell)
                continue;
            const auto &net_data = nets.at(pi.net->udata);
            // wirelength part
            w += lambda / net_data.pcost.at(axis);
            g += (lambda * net_data.centroid.at(axis)) / net_data.pcost.at(axis);
            // timing part
            if (pi.type == PORT_OUT) {
                w += ((1 - lambda) / net_data.tcost.at(axis)) * net_data.tmg_critsqsum;
                g += ((1 - lambda) / net_data.tcost.at(axis)) * net_data.tmg_critsqpossum.at(axis);
            } else if (pi.type == PORT_IN) {
                auto crit = tmg.get_criticality(CellPortKey(cell->name, pi.name));
                w += ((1 - lambda) / net_data.tcost.at(axis)) * std::pow(crit, 2);
                g += ((1 - lambda) / net_data.tcost.at(axis)) * std::pow(crit, 2) *
                     cells.at(pi.net->driver.cell->udata).r.at(axis);
            }
        }
    }

    void do_solve(Axis axis)
    {
        const double omega = 0.75;
        for (auto &m : macros) {
            // moveable macros
            if (m.fixed)
                continue;
            double w = 0, g = 0;
            for (auto cell_idx : m.cells) {
                calculate_cell(cells.at(cell_idx).ci, axis, w, g);
            }
            double pos = g / w;
            // update all macro consituents with newly calculated position
            for (auto cell_idx : m.cells) {
                auto &c = cells.at(cell_idx);
                c.r.at(axis) = omega * c.r.at(axis) +
                               (1 - omega) * (pos + (axis == Axis::Y ? c.macro_offset.y : c.macro_offset.x));
                (axis == Axis::Y ? c.yi : c.xi) =
                        std::min(std::max(int(c.r.at(axis) + 0.5), 0), axis == Axis::Y ? height - 1 : width - 1);
            }
        }
        for (auto &c : cells) {
            // moveable cells not part of a macro
            if (c.fixed || c.macro_idx != -1)
                continue;
            double w = 0, g = 0;
            calculate_cell(c.ci, axis, w, g);
            c.r.at(axis) = omega * c.r.at(axis) + (1 - omega) * (g / w);
            (axis == Axis::Y ? c.yi : c.xi) =
                    std::min(std::max(int(c.r.at(axis) + 0.5), 0), axis == Axis::Y ? height - 1 : width - 1);
        }
    }

    struct SpreaderBin
    {
        // for each bel type
        std::vector<int> available;
        std::vector<int> used;
        pool<int> cell_idxs;
        bool spreaded = false;
    };

    array2d<SpreaderBin> bins;

    // todo make adjustable
    const float beta = 0.7;

    bool is_overused(int available, int used)
    {
        if (available < 4)
            return (used > available);
        else
            return (used > (beta * available));
    }

    bool bin_overused(const SpreaderBin &bin)
    {
        for (int i = 0; i < int(bin.available.size()); i++) {
            if (is_overused(bin.available.at(i), bin.used.at(i)))
                return true;
        }
        return false;
    }

    void init_spread()
    {
        // TODO: different binning ratios other than 1:1
        bins.reset(width, height);
        for (auto entry : bins) {
            auto &b = entry.value;
            b.available.resize(buckets.size());
            for (int i = 0; i < int(buckets.size()); i++) {
                b.available.at(i) = buckets.at(i).loc_bels.at(entry.x, entry.y);
            }
            b.used.resize(buckets.size());
            std::fill(b.used.begin(), b.used.end(), 0);
            b.cell_idxs.clear();
            b.spreaded = false;
        }
        for (int i = 0; i < int(cells.size()); i++) {
            auto &cell = cells.at(i);
            auto &b = bins.at(cell.xi, cell.yi);
            ++b.used.at(cell.bucket);
            b.cell_idxs.insert(i);
        }
    }

    struct ExpandedBin
    {
        int x0, y0, x1, y1;
        std::vector<int> used;
        std::vector<int> available;
        pool<int> cell_idxs;
    };

    ExpandedBin expand_bin(int xc, int yc)
    {
        ExpandedBin exp;
        exp.x0 = exp.x1 = xc;
        exp.y0 = exp.y1 = yc;
        exp.used.resize(buckets.size());
        exp.available.resize(buckets.size());
        auto add_loc = [&](int x, int y) {
            const auto &b = bins.at(x, y);
            for (int i = 0; i < int(buckets.size()); i++) {
                exp.used.at(i) += b.used.at(i);
                exp.available.at(i) += b.available.at(i);
            }
            for (auto c : b.cell_idxs)
                exp.cell_idxs.insert(c);
        };
        auto any_overused = [&](bool strict = false) {
            for (int i = 0; i < int(buckets.size()); i++) {
                if (strict) {
                    if (exp.available.at(i) < exp.used.at(i))
                        return true;
                } else {
                    if (is_overused(exp.available.at(i), exp.used.at(i)))
                        return true;
                }
            }
            return false;
        };
        enum
        {
            NORTH,
            EAST,
            SOUTH,
            WEST
        } dir = NORTH;
        add_loc(xc, yc);
        while (any_overused()) {
            if (exp.x0 == 0 && exp.y0 == 0 && exp.x1 == (width - 1) && exp.y1 == (height - 1)) {
                // no more expansion possible
                if (any_overused(true))
                    log_error("expanding failed, probably too much utilisation!\n");
                else
                    break;
            }

            switch (dir) {
            case NORTH:
                if (exp.y0 > 0) {
                    exp.y0--;
                    for (int x = exp.x0; x <= exp.x1; x++)
                        add_loc(x, exp.y0);
                }
                dir = EAST;
                break;
            case EAST:
                if (exp.x1 < (width - 1)) {
                    exp.x1++;
                    for (int y = exp.y0; y <= exp.y1; y++)
                        add_loc(exp.x1, y);
                }
                dir = SOUTH;
                break;
            case SOUTH:
                if (exp.y1 < (height - 1)) {
                    exp.y1++;
                    for (int x = exp.x0; x <= exp.x1; x++)
                        add_loc(x, exp.y1);
                }
                dir = WEST;
                break;
            case WEST:
                if (exp.x0 > 0) {
                    exp.x0--;
                    for (int y = exp.y0; y <= exp.y1; y++)
                        add_loc(exp.x0, y);
                }
                dir = NORTH;
                break;
            }
        }
        if (ctx->debug)
            log_info("    expanded (%d, %d) -> (%d, %d) (%d, %d)\n", xc, yc, exp.x0, exp.y0, exp.x1, exp.y1);
        return exp;
    }

    struct CellPartition
    {
        int x0, x1, y0, y1;
        std::vector<int> cells;
    };

    void update_cell_bin(int cell, int xn, int yn)
    {
        xn = std::max(0, std::min(width - 1, xn));
        yn = std::max(0, std::min(height - 1, yn));

        auto &cell_data = cells.at(cell);
        auto &old_bin = bins.at(cell_data.xi, cell_data.yi);
        auto &new_bin = bins.at(xn, yn);
        old_bin.used.at(cell_data.bucket)--;
        old_bin.cell_idxs.erase(cell);
        new_bin.used.at(cell_data.bucket)++;
        new_bin.cell_idxs.insert(cell);
        cell_data.xi = xn;
        cell_data.yi = yn;
    }

    void spread_cell_or_macro(int cell, int xn, int yn)
    {
        auto &cell_data = cells.at(cell);
        if (cell_data.macro_idx == -1) {
            update_cell_bin(cell, xn, yn);
        } else {
            auto &macro_data = macros.at(cell_data.macro_idx);
            NPNR_ASSERT(macro_data.root == cell); // we should only be moving the root...
            int xm = xn - cell_data.macro_offset.x;
            int ym = yn - cell_data.macro_offset.y;
            for (int sub_cell : macro_data.cells) {
                auto &sc_data = cells.at(sub_cell);
                update_cell_bin(sub_cell, xm + sc_data.macro_offset.x, ym + sc_data.macro_offset.y);
            }
        }
    }

    // compute the area of a cell for bipart purposes
    int bipart_get_cell_area(int cell)
    {
        auto &cell_data = cells.at(cell);
        if (cell_data.macro_idx == -1) {
            // not a macro
            return 1; // TODO: actually we might not always want this to be true, e.g. around large vs small fracturable
                      // LUTs
        } else {
            // should be a macro root only during bipart
            auto &macro_data = macros.at(cell_data.macro_idx);
            NPNR_ASSERT(macro_data.root == cell);
            return macro_data.area.at(cell_data.bucket);
        }
    }

    void bipartition_worker(CellPartition &init, CellPartition &a, CellPartition &b, int bucket, Axis axis)
    {
        // TODO: currently bipartitioning doesn't take into account heterogeneous macros like LUT+FF, LUT+carry, etc
        // is there a good way of accounting for these, maybe we should actually bipartition for all buckets at once and
        // not per bucket? sort cells by solver bucket
        std::stable_sort(init.cells.begin(), init.cells.end(),
                         [&](int ca, int cb) -> bool { return cells.at(ca).r.at(axis) < cells.at(cb).r.at(axis); });
        int total_cell_area = 0;
        for (int cell : init.cells) {
            total_cell_area += bipart_get_cell_area(cell);
        }
        // on-axis bel bounding box
        int bel_left = (axis == Axis::Y) ? init.y0 : init.x0;
        int bel_right = (axis == Axis::Y) ? init.y1 : init.x1;
        // off-axis bel bounding box ('perpendicular')
        int pe0 = (axis == Axis::Y) ? init.x0 : init.y0;
        int pe1 = (axis == Axis::Y) ? init.x1 : init.y1;

        auto get_bin = [&](int o, int p) -> const SpreaderBin & {
            return bins.at(axis == Axis::Y ? p : o, axis == Axis::Y ? o : p);
        };
        auto slither_bels = [&](int o) -> int {
            int sum = 0;
            for (int p = pe0; p <= pe1; p++) {
                sum += get_bin(o, p).available.at(bucket);
            }
            return sum;
        };
        // trim bounding box if we have no bels
        while (bel_left < bel_right && (slither_bels(bel_left) == 0)) {
            bel_left++;
        }
        while (bel_right > bel_left && (slither_bels(bel_right) == 0)) {
            bel_right--;
        }
        // find the bel half-way point
        int bel_pivot = bel_left;
        int total_bels = 0;
        int a_bels = slither_bels(bel_pivot); // bels on the lower side of the pivot
        for (int b = bel_left; b <= bel_right; b++)
            total_bels += slither_bels(b);
        for (bel_pivot = bel_left; bel_pivot < bel_right; bel_pivot++) {
            int next_bels = slither_bels(bel_pivot + 1);
            if ((a_bels + next_bels) > (total_bels / 2))
                break;
            a_bels += next_bels;
        }
        int b_bels = total_bels - a_bels;
        double bel_ratio = (total_bels == 0) ? 0 : (a_bels / double(a_bels + b_bels));
        // find the point at which to cut cells to match the ratio between bel halves as best as possible
        int accum_cell_area = 0;
        int best_cell_pivot = 0;
        if (a_bels == 0) {
            best_cell_pivot = -1;
        } else if (b_bels == 0) {
            best_cell_pivot = int(init.cells.size());
            // b-partition is empty
        } else {
            double best_ratio_delta = 3;
            for (int i = 0; i < int(init.cells.size()); i++) {
                accum_cell_area += bipart_get_cell_area(init.cells.at(i));
                double cell_ratio = double(accum_cell_area) / double(total_cell_area);
                double ratio_delta = std::abs(cell_ratio - bel_ratio);
                if (ratio_delta < best_ratio_delta) {
                    best_cell_pivot = i;
                    best_ratio_delta = ratio_delta;
                }
            }
        }
        if (ctx->debug) {
            log_info("    axis=%c cut=(%d, %d, %d) cells=%d/%d bels=%d:%d\n", (axis == Axis::Y) ? 'Y' : 'X', bel_left,
                     bel_pivot, bel_right, best_cell_pivot, int(init.cells.size()), a_bels, b_bels);
        }
        // create the output partitions
        a.x0 = (axis == Axis::X) ? bel_left : init.x0;
        a.y0 = (axis == Axis::Y) ? bel_left : init.y0;
        a.x1 = (axis == Axis::X) ? bel_pivot : init.x1;
        a.y1 = (axis == Axis::Y) ? bel_pivot : init.y1;
        a.cells.clear();
        b.x0 = (axis == Axis::X) ? (bel_pivot + 1) : init.x0;
        b.y0 = (axis == Axis::Y) ? (bel_pivot + 1) : init.y0;
        b.x1 = (axis == Axis::X) ? bel_right : init.x1;
        b.y1 = (axis == Axis::Y) ? bel_right : init.y1;
        b.cells.clear();
        for (int i = 0; i < int(init.cells.size()); i++) {
            if (i <= best_cell_pivot)
                a.cells.push_back(init.cells.at(i));
            else
                b.cells.push_back(init.cells.at(i));
        }
    }

    void bipartiton_place(CellPartition &part)
    {
        // 'place' cells inside a partition, once it's been bipartitioned to a 1x1 tile
        // nothing to do
        if (part.cells.empty())
            return;
        NPNR_ASSERT(part.x0 == part.x1);
        NPNR_ASSERT(part.y0 == part.y1);
        for (int c : part.cells)
            spread_cell_or_macro(c, part.x0, part.y0);
    }

    void bipartition(const ExpandedBin &bin, int bucket)
    {
        // TODO better way of storing the partitions
        std::queue<CellPartition> part_queue;
        CellPartition init;
        init.x0 = bin.x0;
        init.y0 = bin.y0;
        init.x1 = bin.x1;
        init.y1 = bin.y1;
        for (int c : bin.cell_idxs) {
            auto &cell_data = cells.at(c);
            if (cell_data.fixed)
                continue;
            if (cell_data.bucket != bucket)
                continue;
            if (cell_data.macro_idx != -1 && macros.at(cell_data.macro_idx).root != c)
                continue;
            init.cells.push_back(c);
        }
        if (ctx->debug) {
            log_info("    running bipartition in (%d, %d) -> (%d, %d); %d cells\n", init.x0, init.y0, init.x1, init.y1,
                     int(init.cells.size()));
        }
        part_queue.push(init);
        while (!part_queue.empty()) {
            CellPartition front = std::move(part_queue.front());
            part_queue.pop();
            if (front.x0 == front.x1 && front.y0 == front.y1) {
                bipartiton_place(front);
            } else if (front.x0 <= front.x1 && front.y0 <= front.y1) {
                CellPartition a, b;
                bipartition_worker(front, a, b, bucket,
                                   ((front.x1 - front.x0) > (front.y1 - front.y0)) ? Axis::X : Axis::Y);
                if (!a.cells.empty())
                    part_queue.push(a);
                if (!b.cells.empty())
                    part_queue.push(b);
            } else {
                // negative-size null partition
                NPNR_ASSERT(front.cells.empty());
            }
        }
    }

    void do_spread()
    {
        init_spread();
        // find overused bins
        std::vector<Loc> overused;
        for (auto bin : bins) {
            if (bin_overused(bin.value))
                overused.push_back(Loc(bin.x, bin.y, 0));
        }
        if (ctx->debug)
            log_info("    %d overused bins\n", int(overused.size()));
        // sort by most populous bins first
        std::stable_sort(overused.begin(), overused.end(), [&](const Loc &a, const Loc &b) -> bool {
            const auto &a_bin = bins.at(a.x, a.y), &b_bin = bins.at(b.x, b.y);
            return a_bin.cell_idxs.size() > b_bin.cell_idxs.size();
        });
        // resolve starting from the most populous
        for (auto loc : overused) {
            auto &bin = bins.at(loc);
            // overuse might actually have been resolved by a previous expand and spread operation
            if (bin.spreaded)
                continue;
            ExpandedBin exp = expand_bin(loc.x, loc.y);
            for (int i = 0; i < int(exp.used.size()); i++) {
                if (exp.used.at(i) == 0)
                    continue; // no bels in this bucket used
                bipartition(exp, i);
            }
            for (int y = exp.y0; y <= exp.y1; y++) {
                for (int x = exp.x0; x <= exp.x1; x++) {
                    bins.at(x, y).spreaded = true;
                }
            }
        }
    }

    int total_hpwl()
    {
        int wl = 0;
        for (const auto &n : ctx->nets) {
            const NetInfo *ni = n.second.get();
            if (ni->driver.cell == nullptr)
                continue;
            auto &drv_data = cells.at(ni->driver.cell->udata);
            int x0 = drv_data.xi, x1 = drv_data.xi;
            int y0 = drv_data.yi, y1 = drv_data.yi;
            for (auto &usr : ni->users) {
                auto &usr_data = cells.at(usr.cell->udata);
                x0 = std::min(x0, usr_data.xi);
                x1 = std::max(x1, usr_data.xi);
                y0 = std::min(y0, usr_data.yi);
                y1 = std::max(y1, usr_data.yi);
            }
            wl += (y1 - y0) + (x1 - x0);
        }
        return wl;
    }

    void update_real_locs()
    {
        for (auto &cell : cells) {
            cell.r.x = cell.xi;
            cell.r.y = cell.yi;
        }
    }

    // Strict placement legalisation, performed after the initial spreading
    // currently taken more or less as-is from HeAP
    void legalise_placement_strict(bool require_validity = false)
    {

        // Unbind all cells placed in this solution
        for (auto &cell : cells) {
            if (cell.fixed)
                continue;
            if (cell.ci->bel != BelId())
                ctx->unbindBel(cell.ci->bel);
        }

        // At the moment we don't follow the full HeAP algorithm using cuts for legalisation, instead using
        // the simple greedy largest-macro-first approach.
        std::priority_queue<std::pair<int, IdString>> remaining;
        for (int i = 0; i < int(cells.size()); i++) {
            auto &cell = cells.at(i);
            if (cell.macro_idx == -1) {
                remaining.emplace(1, cell.ci->name);
            } else {
                auto &macro = macros.at(cell.macro_idx);
                if (macro.root == i)
                    remaining.emplace(macro.cells.size(), cell.ci->name);
            }
        }
        int ripup_radius = 2;
        int total_iters = 0;
        int total_iters_noreset = 0;
        while (!remaining.empty()) {
            auto top = remaining.top();
            remaining.pop();

            CellInfo *ci = ctx->cells.at(top.second).get();
            auto &cell_data = cells.at(ci->udata);
            // Was now placed, ignore
            if (ci->bel != BelId())
                continue;
            // log_info("   Legalising %s (%s)\n", top.second.c_str(ctx), ci->type.c_str(ctx));
            FastBels::FastBelsData *fb;
            fast_bels.getBelsForCellType(ci->type, &fb);
            int radius = 0;
            int iter = 0;
            int iter_at_radius = 0;
            bool placed = false;
            BelId bestBel;
            int best_inp_len = std::numeric_limits<int>::max();

            total_iters++;
            total_iters_noreset++;
            if (total_iters > int(cells.size())) {
                total_iters = 0;
                ripup_radius = std::max(std::max(width - 1, height - 1), ripup_radius * 2);
            }

            if (total_iters_noreset > std::max(5000, 8 * int(ctx->cells.size()))) {
                log_error("Unable to find legal placement for all cells, design is probably at utilisation limit.\n");
            }

            while (!placed) {

                // Set a conservative timeout
                if (iter > std::max(10000, 3 * int(ctx->cells.size())))
                    log_error("Unable to find legal placement for cell '%s', check constraints and utilisation.\n",
                              ctx->nameOf(ci));

                // Determine a search radius around the solver location (which increases over time) that is clamped to
                // the region constraint for the cell (if applicable)
                int rx = radius, ry = radius;

                // Pick a random X and Y location within our search radius
                int nx = ctx->rng(2 * rx + 1) + std::max(cell_data.xi - rx, 0);
                int ny = ctx->rng(2 * ry + 1) + std::max(cell_data.yi - ry, 0);

                iter++;
                iter_at_radius++;
                if (iter >= (10 * (radius + 1))) {
                    // No luck yet, increase radius
                    radius = std::min(std::max(width - 1, height - 1), radius + 1);
                    while (radius < std::max(width - 1, height - 1)) {
                        // Keep increasing the radius until it will actually increase the number of cells we are
                        // checking (e.g. BRAM and DSP will not be in all cols/rows), so we don't waste effort
                        for (int x = std::max(0, cell_data.xi - radius);
                             x <= std::min(width - 1, cell_data.xi + radius); x++) {
                            if (x >= int(fb->size()))
                                break;
                            for (int y = std::max(0, cell_data.yi - radius);
                                 y <= std::min(height - 1, cell_data.yi + radius); y++) {
                                if (y >= int(fb->at(x).size()))
                                    break;
                                if (fb->at(x).at(y).size() > 0)
                                    goto notempty;
                            }
                        }
                        radius = std::min(std::max(width - 1, height - 1), radius + 1);
                    }
                notempty:
                    iter_at_radius = 0;
                    iter = 0;
                }
                // If our randomly chosen cooridnate is out of bounds; or points to a tile with no relevant bels; ignore
                // it
                if (nx < 0 || nx >= width)
                    continue;
                if (ny < 0 || ny >= height)
                    continue;

                if (nx >= int(fb->size()))
                    continue;
                if (ny >= int(fb->at(nx).size()))
                    continue;
                if (fb->at(nx).at(ny).empty())
                    continue;

                // The number of attempts to find a location to try
                int need_to_explore = 2 * radius;

                // If we have found at least one legal location; and made enough attempts; assume it's good enough and
                // finish
                if (iter_at_radius >= need_to_explore && bestBel != BelId()) {
                    CellInfo *bound = ctx->getBoundBelCell(bestBel);
                    if (bound != nullptr) {
                        ctx->unbindBel(bound->bel);
                        remaining.emplace(1, bound->name);
                    }
                    ctx->bindBel(bestBel, ci, STRENGTH_WEAK);
                    placed = true;
                    Loc loc = ctx->getBelLocation(bestBel);
                    cells.at(ci->udata).xi = loc.x;
                    cells.at(ci->udata).yi = loc.y;
                    break;
                }

                if (ci->cluster == ClusterId()) {
                    // The case where we have no relative constraints
                    for (auto sz : fb->at(nx).at(ny)) {
                        // Look through all bels in this tile; checking region constraint if applicable
                        if (!ci->testRegion(sz))
                            continue;
                        // Prefer available bels; unless we are dealing with a wide radius (e.g. difficult control sets)
                        // or occasionally trigger a tiebreaker
                        if (ctx->checkBelAvail(sz) || (radius > ripup_radius || ctx->rng(20000) < 10)) {
                            CellInfo *bound = ctx->getBoundBelCell(sz);
                            if (bound != nullptr) {
                                // Only rip up cells without constraints
                                if (bound->cluster != ClusterId())
                                    continue;
                                ctx->unbindBel(bound->bel);
                            }
                            // Provisionally bind the bel
                            ctx->bindBel(sz, ci, STRENGTH_WEAK);
                            if (require_validity && !ctx->isBelLocationValid(sz)) {
                                // New location is not legal; unbind the cell (and rebind the cell we ripped up if
                                // applicable)
                                ctx->unbindBel(sz);
                                if (bound != nullptr)
                                    ctx->bindBel(sz, bound, STRENGTH_WEAK);
                            } else if (iter_at_radius < need_to_explore) {
                                // It's legal, but we haven't tried enough locations yet
                                ctx->unbindBel(sz);
                                if (bound != nullptr)
                                    ctx->bindBel(sz, bound, STRENGTH_WEAK);
                                int input_len = 0;
                                // Compute a fast input wirelength metric at this bel; and save if better than our last
                                // try
                                for (auto &port : ci->ports) {
                                    auto &p = port.second;
                                    if (p.type != PORT_IN || p.net == nullptr || p.net->driver.cell == nullptr)
                                        continue;
                                    CellInfo *drv = p.net->driver.cell;
                                    auto &drv_data = cells.at(drv->udata);
                                    if (drv_data.global)
                                        continue;
                                    input_len += std::abs(drv_data.xi - nx) + std::abs(drv_data.yi - ny);
                                }
                                if (input_len < best_inp_len) {
                                    best_inp_len = input_len;
                                    bestBel = sz;
                                }
                                break;
                            } else {
                                // It's legal, and we've tried enough. Finish.
                                if (bound != nullptr)
                                    remaining.emplace(1, bound->name);
                                Loc loc = ctx->getBelLocation(sz);
                                cells.at(ci->udata).xi = loc.x;
                                cells.at(ci->udata).yi = loc.y;
                                placed = true;
                                break;
                            }
                        }
                    }
                } else {
                    // We do have relative constraints
                    for (auto sz : fb->at(nx).at(ny)) {
                        // List of cells and their destination
                        std::vector<std::pair<CellInfo *, BelId>> targets;
                        // List of bels we placed things at; and the cell that was there before if applicable
                        std::vector<std::pair<BelId, CellInfo *>> swaps_made;

                        if (!ctx->getClusterPlacement(ci->cluster, sz, targets))
                            continue;

                        for (auto &target : targets) {
                            // Check it satisfies the region constraint if applicable
                            if (!target.first->testRegion(target.second))
                                goto fail;
                            CellInfo *bound = ctx->getBoundBelCell(target.second);
                            // Chains cannot overlap; so if we have to ripup a cell make sure it isn't part of a chain
                            if (bound != nullptr)
                                if (bound->cluster != ClusterId() || bound->belStrength > STRENGTH_WEAK)
                                    goto fail;
                        }
                        // Actually perform the move; keeping track of the moves we make so we can revert them if needed
                        for (auto &target : targets) {
                            CellInfo *bound = ctx->getBoundBelCell(target.second);
                            if (bound != nullptr)
                                ctx->unbindBel(target.second);
                            ctx->bindBel(target.second, target.first, STRENGTH_STRONG);
                            swaps_made.emplace_back(target.second, bound);
                        }
                        // Check that the move we have made is legal
                        for (auto &sm : swaps_made) {
                            if (!ctx->isBelLocationValid(sm.first))
                                goto fail;
                        }

                        if (false) {
                        fail:
                            // If the move turned out to be illegal; revert all the moves we made
                            for (auto &swap : swaps_made) {
                                ctx->unbindBel(swap.first);
                                if (swap.second != nullptr)
                                    ctx->bindBel(swap.first, swap.second, STRENGTH_WEAK);
                            }
                            continue;
                        }
                        for (auto &target : targets) {
                            Loc loc = ctx->getBelLocation(target.second);
                            cells.at(target.first->udata).xi = loc.x;
                            cells.at(target.first->udata).yi = loc.y;
                            // log_info("%s %d %d %d\n", target.first->name.c_str(ctx), loc.x, loc.y, loc.z);
                        }
                        for (auto &swap : swaps_made) {
                            // Where we have ripped up cells; add them to the queue
                            if (swap.second != nullptr)
                                remaining.emplace(1, swap.second->name);
                        }

                        placed = true;
                        break;
                    }
                }
            }
        }
    }

    void do_iter(int m)
    {
        for (int i = 0; i < m; i++) {
            update_nets();
            do_solve(Axis::X);
            do_solve(Axis::Y);
        }
        log_info("   post solver HPWL=%d\n", total_hpwl());
        do_spread();
        log_info("   post spread HPWL=%d\n", total_hpwl());
        legalise_placement_strict(true);
        update_real_locs();
        tmg.run();
        if (ctx->verbose)
            tmg.print_fmax();
        // TODO: legalise...
    }

    void do_placement()
    {
        int m = int(sqrt(cells.size()));
        int iter = 1;
        while (m > 1) {
            do_iter(m);
            log_info("at iteration %d, HPWL=%d\n", iter, total_hpwl());
            ++iter;
            m = m * 0.7;
        }
    }

  public:
    StarPlacer(Context *ctx, PlacerStarCfg cfg) : ctx(ctx), cfg(cfg), fast_bels(ctx, true, 8), tmg(ctx)
    {
        width = ctx->getGridDimX();
        height = ctx->getGridDimY();
    };
    void place()
    {
        log_info("Running Star placer...\n");
        init_buckets();
        init_cells();
        init_macros();
        init_nets();
        place_initial();
        tmg.setup();
        tmg.run();
        log_info("after IO propagation: HPWL=%d\n", total_hpwl());
        do_placement();
        Placer1Cfg refine_cfg(ctx);
        refine_cfg.timingWeight = 0.95;
        placer1_refine(ctx, refine_cfg);
    }
};
}; // namespace

bool placer_star(Context *ctx, PlacerStarCfg cfg)
{
    StarPlacer(ctx, cfg).place();
    return true;
}

PlacerStarCfg::PlacerStarCfg(Context *ctx)
{
    timing_driven = ctx->setting<bool>("timing_driven");

    hpwl_scale_x = 1;
    hpwl_scale_y = 1;
}

NEXTPNR_NAMESPACE_END

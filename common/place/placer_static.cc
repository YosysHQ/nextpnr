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

#include "placer_static.h"
#include "static_util.h"

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

#include "fftsg.h"

#ifndef NPNR_DISABLE_THREADS
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#endif

NEXTPNR_NAMESPACE_BEGIN

using namespace StaticUtil;

namespace {

struct PlacerGroup
{
    int total_bels = 0;
    double concrete_area = 0;
    double dark_area = 0;
    double total_area = 0;
    array2d<float> loc_area;

    float overlap = 0;

    array2d<double> conc_density; // excludes fillers and dark nodes
    array2d<double> density;
    // FFT related data (TODO: should these be per group?)
    FFTArray density_fft;
    FFTArray electro_phi;
    FFTArray electro_fx, electro_fy;
};

// Could be an actual concrete netlist cell; or just a spacer
struct MoveCell
{
    StaticRect rect;
    // TODO: multiple contiguous vectors is probably faster than an array of structs, but also messier
    RealPair pos, ref_pos, last_pos, last_ref_pos;
    RealPair ref_wl_grad, wl_grad, last_wl_grad;
    RealPair ref_dens_grad, dens_grad, last_dens_grad;
    RealPair ref_total_grad, total_grad, last_total_grad;
    int32_t pin_count;
    int16_t group;
    int16_t bx, by; // bins
    bool is_fixed : 1;
    bool is_spacer : 1;
    bool is_dark : 1;
};

// Extra data for cells that aren't spacers
struct ConcreteCell
{
    CellInfo *base_cell;
    // When cells are macros; we split them up into chunks
    // based on dx/dy location
    int32_t macro_idx = -1;
    int16_t chunk_dx = 0, chunk_dy = 0;
};

struct ClusterGroupKey
{
    ClusterGroupKey(int dx = 0, int dy = 0, int group = -1) : dx(dx), dy(dy), group(group){};
    bool operator==(const ClusterGroupKey &other) const
    {
        return dx == other.dx && dy == other.dy && group == other.group;
    }
    unsigned hash() const { return mkhash(mkhash(dx, dy), group); }
    int16_t dx, dy, group;
};

struct PlacerMacro
{
    CellInfo *root;
    std::vector<int32_t> conc_cells;
    dict<ClusterGroupKey, std::vector<CellInfo *>> cells;
};

struct PlacerBin
{
    float density;
    // ...
};

struct PlacerPort
{
    // for wirelength data
    static constexpr float invalid = std::numeric_limits<float>::lowest();

    PortRef ref;
    RealPair max_exp{invalid, invalid};
    RealPair min_exp{invalid, invalid};
    bool has_max_exp(Axis axis) const { return max_exp.at(axis) != invalid; }
    bool has_min_exp(Axis axis) const { return min_exp.at(axis) != invalid; }
};

struct PlacerNet
{
    NetInfo *ni;
    bool skip = false;
    RealPair b1, b0; // real bounding box
    RealPair min_exp, x_min_exp;
    RealPair max_exp, x_max_exp;
    RealPair wa_wl;
    // lines up with user indexes; plus one for driver
    std::vector<PlacerPort> ports;
    int hpwl() { return (b1.x - b0.x) + (b1.y - b0.y); }
};

#ifdef NPNR_DISABLE_THREADS
struct ThreadPool
{
    ThreadPool(int){};

    void run(int N, std::function<void(int)> func)
    {
        for (int i = 0; i < N; i++)
            func(i);
    };
};
#else
struct ThreadPool
{
    ThreadPool(int thread_count)
    {
        done.resize(thread_count, false);
        for (int i = 0; i < thread_count; i++) {
            threads.emplace_back([this, i]() { this->worker(i); });
        }
    }
    std::vector<std::thread> threads;
    std::condition_variable cv_start, cv_done;
    std::mutex mutex;

    bool work_available = false;
    bool shutdown = false;
    std::vector<bool> done;
    std::function<void(int)> work;
    int work_count;

    ~ThreadPool()
    {
        {
            std::lock_guard lk(mutex);
            shutdown = true;
        }
        cv_start.notify_all();
        for (auto &t : threads)
            t.join();
    }

    void run(int N, std::function<void(int)> func)
    {
        {
            std::lock_guard lk(mutex);
            work = func;
            work_count = N;
            work_available = true;
            std::fill(done.begin(), done.end(), false);
        }
        cv_start.notify_all();
        {
            std::unique_lock lk(mutex);
            cv_done.wait(lk, [this] { return std::all_of(done.begin(), done.end(), [](bool x) { return x; }); });
            work_available = false;
        }
    }

    void worker(int idx)
    {
        while (true) {
            std::unique_lock lk(mutex);
            cv_start.wait(lk, [this, idx] { return (work_available && !done.at(idx)) || shutdown; });
            if (shutdown) {
                lk.unlock();
                break;
            } else if (work_available && !done.at(idx)) {
                int work_per_thread = (work_count + int(threads.size()) - 1) / threads.size();
                int begin = work_per_thread * idx;
                int end = std::min(work_count, work_per_thread * (idx + 1));
                lk.unlock();

                for (int j = begin; j < end; j++) {
                    work(j);
                }

                lk.lock();
                done.at(idx) = true;
                lk.unlock();
                cv_done.notify_one();
            }
        }
    }
};
#endif

class StaticPlacer
{
    Context *ctx;
    PlacerStaticCfg cfg;

    std::vector<MoveCell> mcells;
    std::vector<ConcreteCell> ccells;
    std::vector<PlacerMacro> macros;
    std::vector<PlacerGroup> groups;
    std::vector<PlacerNet> nets;
    idict<ClusterId> cluster2idx;

    FastBels fast_bels;
    TimingAnalyser tmg;
    ThreadPool pool;

    int width, height;
    int iter = 0;
    bool fft_debug = false;
    bool dump_density = false;

    // legalisation queue
    std::priority_queue<std::pair<int, IdString>> to_legalise;

    void prepare_cells()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            ci->udata = -1;
            // process legacy-ish bel attributes
            if (ci->attrs.count(ctx->id("BEL")) && ci->bel == BelId()) {
                std::string loc_name = ci->attrs.at(ctx->id("BEL")).as_string();
                BelId bel = ctx->getBelByNameStr(loc_name);
                NPNR_ASSERT(ctx->isValidBelForCellType(ci->type, bel));
                NPNR_ASSERT(ctx->checkBelAvail(bel));
                ctx->bindBel(bel, ci, STRENGTH_USER);
            }
        }
    }

    bool lookup_group(IdString type, int &group, StaticRect &rect)
    {
        for (size_t i = 0; i < cfg.cell_groups.size(); i++) {
            const auto &g = cfg.cell_groups.at(i);
            if (g.cell_area.count(type)) {
                group = i;
                rect = g.cell_area.at(type);
                return true;
            }
        }
        return false;
    }

    void init_bels()
    {
        log_info("⌁ initialising bels...\n");
        width = 0;
        height = 0;
        for (auto bel : ctx->getBels()) {
            Loc loc = ctx->getBelLocation(bel);
            width = std::max(width, loc.x + 1);
            height = std::max(height, loc.y + 1);
        }
        dict<IdString, int> beltype2group;
        for (int i = 0; i < int(groups.size()); i++) {
            groups.at(i).loc_area.reset(width, height);
            for (const auto &bel_type : cfg.cell_groups.at(i).cell_area)
                beltype2group[bel_type.first] = i;
        }
        for (auto bel : ctx->getBels()) {
            Loc loc = ctx->getBelLocation(bel);
            IdString type = ctx->getBelType(bel);
            auto fnd = beltype2group.find(type);
            if (fnd == beltype2group.end())
                continue;
            auto size = cfg.cell_groups.at(fnd->second).bel_area.at(type); // TODO: do we care about dimensions too
            auto &group = groups.at(fnd->second);
            for (int dy = 0; dy <= int(size.h); dy++) {
                for (int dx = 0; dx <= int(size.w); dx++) {
                    float h = (dy == int(size.h)) ? (size.h - int(size.h)) : 1;
                    float w = (dx == int(size.w)) ? (size.w - int(size.w)) : 1;
                    group.loc_area.at(loc.x + dx, loc.y + dy) += w * h;
                }
            }
            group.total_area += size.area();
            group.total_bels += 1;
        }
    }

    void init_nets()
    {
        nets.reserve(ctx->nets.size());
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            ni->udata = nets.size();
            nets.emplace_back();

            auto &nd = nets.back();
            nd.ni = ni;
            nd.skip = (ni->driver.cell == nullptr);    // (or global buffer?)
            nd.ports.resize(ni->users.capacity() + 1); // +1 for the driver
            nd.ports.back().ref = ni->driver;
            for (auto usr : ni->users.enumerate()) {
                nd.ports.at(usr.index.idx()).ref = usr.value;
            }
        }
    }

    int add_cell(StaticRect rect, int group, RealPair pos, CellInfo *ci = nullptr)
    {
        int idx = mcells.size();
        mcells.emplace_back();
        auto &m = mcells.back();
        m.rect = rect;
        m.group = group;
        m.pos = pos;
        if (ci) {
            // Is a concrete cell (might be a macro, in which case ci is just one of them...)
            // Can't add concrete cells once we have spacers (we define it such that indices line up between mcells and
            // ccells; spacer cells only exist in mcells)
            NPNR_ASSERT(idx == int(ccells.size()));
            ccells.emplace_back();
            auto &c = ccells.back();
            c.base_cell = ci;
            groups.at(group).concrete_area += rect.area();
        } else {
            // Is a spacer cell
            m.is_spacer = true;
        }
        return idx;
    }

    const float pi = 3.141592653589793f;

    RealPair rand_loc()
    {
        // Box-muller
        float u1 = ctx->rngf(1.0f);
        while (u1 < 1e-5)
            u1 = ctx->rngf(1.0f);
        float u2 = ctx->rngf(1.0f);
        float m = std::sqrt(-2.f * std::log(u1));
        float z0 = m * std::cos(2.f * pi * u2);
        float z1 = m * std::sin(2.f * pi * u2);
        float x = (width / 2.f) + (width / 250.f) * z0;
        float y = (height / 2.f) + (height / 250.f) * z1;
        x = std::min<float>(width - 1.f, std::max<float>(x, 0));
        y = std::min<float>(height - 1.f, std::max<float>(y, 0));
        return RealPair(x, y);
    }

    RealPair cell_loc(CellInfo *ci, bool ref)
    {
        if (ci->udata == -1) {
            // not handled?
            NPNR_ASSERT_MSG(ci->bel != BelId(),
                            stringf("Cell %s of type %s has no bel", ci->name.c_str(ctx), ci->type.c_str(ctx))
                                    .c_str()); // already fixed
            return RealPair(ctx->getBelLocation(ci->bel), 0.5f);
        } else {
            return ref ? mcells.at(ci->udata).ref_pos : mcells.at(ci->udata).pos;
        }
    }

    void init_cells()
    {
        log_info("⌁ initialising cells...\n");
        // Process non-clustered cells and find clusters
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            int cell_group;
            StaticRect rect;
            // Mismatched group case
            if (!lookup_group(ci->type, cell_group, rect)) {
                for (auto bel : ctx->getBels()) {
                    if (ctx->isValidBelForCellType(ci->type, bel) && ctx->checkBelAvail(bel)) {
                        ctx->bindBel(bel, ci, STRENGTH_STRONG);
                        if (!ctx->isBelLocationValid(bel)) {
                            ctx->unbindBel(bel);
                        } else {
                            log_info("    placed potpourri cell '%s' at bel '%s'\n", ctx->nameOf(ci), ctx->nameOfBel(bel));
                            break;
                        }
                    }
                }
                continue;
            }
            if (ci->cluster != ClusterId()) {
                // Defer processing of macro clusters
                int c_idx = cluster2idx(ci->cluster);
                if (c_idx >= int(macros.size())) {
                    macros.emplace_back();
                    macros.back().root = ctx->getClusterRootCell(ci->cluster);
                }
                auto &m = macros.at(c_idx);
                Loc delta = ctx->getClusterOffset(ci);
                m.cells[ClusterGroupKey(delta.x, delta.y, cell_group)].push_back(ci);
            } else {
                // Non-clustered cells can be processed already
                int idx = add_cell(rect, cell_group, rand_loc(), ci);
                ci->udata = idx;
                auto &mc = mcells.at(idx);
                mc.pin_count += int(ci->ports.size());
                if (ci->bel != BelId()) {
                    // Currently; treat all ready-placed cells as fixed (eventually we might do incremental ripups
                    // here...)
                    Loc loc = ctx->getBelLocation(ci->bel);
                    mc.pos.x = loc.x + 0.5;
                    mc.pos.y = loc.y + 0.5;
                    mc.is_fixed = true;
                }
            }
        }
        // Process clustered cells
        for (int i = 0; i < int(macros.size()); i++) {
            auto &m = macros.at(i);
            for (auto &kv : m.cells) {
                const auto &g = cfg.cell_groups.at(kv.first.group);
                // Only treat zero-area cells as zero-area; if this cluster also contains non-zero area cells
                bool has_nonzero = std::any_of(kv.second.begin(), kv.second.end(),
                                               [&](const CellInfo *ci) { return !g.zero_area_cells.count(ci->type); });
                StaticRect cluster_size;
                for (auto ci : kv.second) {
                    if (has_nonzero && g.zero_area_cells.count(ci->type))
                        continue;
                    // Compute an equivalent-area stacked rectange for cells in this cluster group.
                    // There are probably some ugly cases this handles badly.
                    StaticRect r = g.cell_area.at(ci->type);
                    if (r.w > r.h) {
                        // Long and thin, "stack" vertically
                        // Compute height we add to stack
                        if (cluster_size.w < r.w) {
                            cluster_size.h *= (cluster_size.w / r.w);
                            cluster_size.w = r.w;
                        }
                        cluster_size.h += ((r.w * r.h) / cluster_size.w);
                    } else {
                        // "stack" horizontally
                        if (cluster_size.h < r.h) {
                            cluster_size.w *= (cluster_size.h / r.h);
                            cluster_size.h = r.h;
                        }
                        cluster_size.w += ((r.w * r.h) / cluster_size.h);
                    }
                }
                // Now add the moveable cell
                if (cluster_size.area() > 0) {
                    int idx = add_cell(cluster_size, kv.first.group, rand_loc(), kv.second.front());
                    auto &mc = mcells.at(idx);
                    if (kv.second.front()->bel != BelId()) {
                        // Currently; treat all ready-placed cells as fixed (eventually we might do incremental ripups
                        // here...)
                        Loc loc = ctx->getBelLocation(kv.second.front()->bel);
                        mc.pos.x = loc.x + 0.5;
                        mc.pos.y = loc.y + 0.5;
                        mc.is_fixed = true;
                    }
                    for (auto ci : kv.second) {
                        ci->udata = idx;
                        mc.pin_count += int(ci->ports.size());
                    }
                    auto &cc = ccells.at(idx);
                    cc.macro_idx = i;
                    cc.chunk_dx = kv.first.dx;
                    cc.chunk_dy = kv.first.dy;
                    m.conc_cells.push_back(idx);
                }
            }
        }
    }

    const double target_util = 0.8;

    void insert_dark()
    {
        log_info("⌁ inserting dark nodes...\n");
        for (int group = 0; group < int(groups.size()); group++) {
            auto &g = groups.at(group);
            for (auto tile : g.loc_area) {
                if (tile.value > 0.5f)
                    continue;
                StaticRect dark_area(1.0f, 1.0f - tile.value);
                int cell_idx = add_cell(dark_area, group, RealPair(tile.x + 0.5f, tile.y + 0.5f), nullptr /*spacer*/);
                mcells.at(cell_idx).is_dark = true;
            }
        }
    }

    void insert_spacer()
    {
        log_info("⌁ inserting spacers...\n");
        int inserted_spacers = 0;
        for (int group = 0; group < int(groups.size()); group++) {
            const auto &cg = cfg.cell_groups.at(group);
            const auto &g = groups.at(group);
            double util = g.concrete_area / g.total_area;
            log_info("⌁   group %s pre-spacer utilisation %.02f%% (target %.02f%%)\n", ctx->nameOf(cg.name),
                     (util * 100.0), (target_util * 100.0));
            // TODO: better computation of spacer size and placement?
            int spacer_count = (g.total_area * target_util - g.concrete_area) / cg.spacer_rect.area();
            if (spacer_count <= 0)
                continue;
            for (int i = 0; i < spacer_count; i++) {
                add_cell(cg.spacer_rect, group, RealPair(ctx->rngf(width), ctx->rngf(height)), nullptr /*spacer*/);
                ++inserted_spacers;
            }
        }
        log_info("⌁   inserted a total of %d spacers\n", inserted_spacers);
    }

    // TODO: dark node insertion when we have obstructions or non-rectangular placement regions
    int m;
    double bin_w, bin_h;

    std::vector<float> cs_table_fft;
    std::vector<int> work_area_fft;

    void prepare_density_bins()
    {
        // TODO: a m x m grid follows the paper and makes the DCTs easier, but is it actually ideal for non-square
        // FPGAs?
        m = 1 << int(std::ceil(std::log2(std::sqrt(mcells.size() / groups.size()))));
        bin_w = double(width) / m;
        bin_h = double(height) / m;

        for (auto &g : groups) {
            g.density.reset(m, m, 0);
            g.density_fft.reset(m, m, 0);
            g.electro_phi.reset(m, m, 0);
            g.electro_fx.reset(m, m, 0);
            g.electro_fy.reset(m, m, 0);
        }
        cs_table_fft.resize(m * 3 / 2, 0);
        work_area_fft.resize(std::round(std::sqrt(m)) + 2, 0);
        work_area_fft.at(0) = 0;
    }

    template <typename TFunc> void iter_slithers(RealPair pos, StaticRect rect, TFunc func)
    {
        // compute the stamp over bins (this could probably be more efficient?)

        double width = rect.w, height = rect.h;
        double scaled_density = 1.0;
        if (width < bin_w) {
            scaled_density *= (width / bin_w);
            width = bin_w;
        }
        if (height < bin_h) {
            scaled_density *= (height / bin_h);
            height = bin_h;
        }

        double x0 = pos.x, x1 = pos.x + width;
        double y0 = pos.y, y1 = pos.y + height;
        for (int y = int(y0 / bin_h); y <= int(y1 / bin_h); y++) {
            for (int x = int(x0 / bin_w); x <= int(x1 / bin_w); x++) {
                int xb = std::max(0, std::min(x, m - 1));
                int yb = std::max(0, std::min(y, m - 1));
                double slither_w = 1.0, slither_h = 1.0;
                if (yb == int(y0 / bin_h)) // y slithers
                    slither_h = ((yb + 1) * bin_h) - y0;
                else if (yb == int(y1 / bin_h))
                    slither_h = (y1 - yb * bin_h);
                if (xb == int(x0 / bin_w)) // x slithers
                    slither_w = ((xb + 1) * bin_w) - x0;
                else if (xb == int(x1 / bin_w))
                    slither_w = (x1 - xb * bin_w);
                func(xb, yb, scaled_density * slither_w * slither_h);
            }
        }
    };

    void compute_density(int group, bool ref)
    {
        auto &g = groups.at(group);
        // reset
        for (auto entry : g.density)
            entry.value = 0;
        // populate
        for (int idx = 0; idx < int(mcells.size()); idx++) {
            auto &mc = mcells.at(idx);
            if (mc.group != group)
                continue;
            // scale width and height to be at least one bin (local density smoothing from the eplace paper)
            // TODO: should we really do this every iteration?

            auto pos = ref ? mc.ref_pos : mc.pos;
            iter_slithers(pos, mc.rect, [&](int x, int y, float area) { g.density.at(x, y) += area; });
        }
    }

    void compute_overlap()
    {
        // populate for concrete cells only
        for (auto &g : groups)
            g.conc_density.reset(m, m, 0);
        for (int idx = 0; idx < int(ccells.size()); idx++) {
            auto &mc = mcells.at(idx);
            auto &g = groups.at(mc.group);
            auto loc = mc.pos;
            auto size = mc.rect;

            for (int dy = 0; dy <= int(size.h); dy++) {
                for (int dx = 0; dx <= int(size.w); dx++) {
                    float h = (dy == int(size.h)) ? (size.h - int(size.h)) : 1;
                    float w = (dx == int(size.w)) ? (size.w - int(size.w)) : 1;
                    g.conc_density.at(loc.x + dx, loc.y + dy) += w * h;
                }
            }
        }
        std::string overlap_str = "";
        for (int idx = 0; idx < int(groups.size()); idx++) {
            auto &g = groups.at(idx);
            g.overlap = 0;
            float total_area = 0;
            for (auto tile : g.loc_area) {
                // g.overlap += std::max<float>(0, g.conc_density.at(tile.x, tile.y) - tile.value);
                g.overlap += std::max<float>(0, g.conc_density.at(tile.x, tile.y) - 1);
                total_area += g.conc_density.at(tile.x, tile.y);
            }
            g.overlap /= std::max(1.0f, total_area);
            if (!overlap_str.empty())
                overlap_str += ", ";
            overlap_str += stringf("%s=%.1f%%", cfg.cell_groups.at(idx).name.c_str(ctx), g.overlap * 100);
            if (dump_density)
                g.conc_density.write_csv(stringf("out_conc_density_%d_%d.csv", iter, idx));
        }
        log_info("overlap: %s\n", overlap_str.c_str());
    }

    void run_fft(int group)
    {
        // get data into form that fft wants
        auto &g = groups.at(group);
        for (auto entry : g.density)
            g.density_fft.at(entry.x, entry.y) = entry.value;
        if (fft_debug)
            g.density_fft.write_csv(stringf("out_bin_density_%d_%d.csv", iter, group));
        // Based on
        // https://github.com/ALIGN-analoglayout/ALIGN-public/blob/master/PlaceRouteHierFlow/EA_placer/FFT/fft.cpp
        // initial DCT for coefficients
        ddct2d(m, m, -1, g.density_fft.data(), nullptr, work_area_fft.data(), cs_table_fft.data());
        // postprocess coefficients
        for (int x = 0; x < m; x++)
            g.density_fft.at(x, 0) *= 0.5f;
        for (int y = 0; y < m; y++)
            g.density_fft.at(0, y) *= 0.5f;
        for (int x = 0; x < m; x++)
            for (int y = 0; y < m; y++)
                g.density_fft.at(x, y) *= (4.0f / (m * m));
        // scale inputs to IDCT for potentials and field
        for (int x = 0; x < m; x++) {
            float wx = pi * (x / float(m));
            float wx2 = wx * wx;
            for (int y = 0; y < m; y++) {
                float wy = pi * (y / float(m));
                float wy2 = wy * wy;

                float dens = g.density_fft.at(x, y);
                float phi = 0, ex = 0, ey = 0;

                if (x != 0 || y != 0) { // avoid divide by zero...
                    phi = dens / (wx2 + wy2);
                    ex = phi * wx;
                    ey = phi * wy;
                }

                g.electro_phi.at(x, y) = phi;
                g.electro_fx.at(x, y) = ex;
                g.electro_fy.at(x, y) = ey;
            }
        }
        // IDCT for potential; 2D derivatives for field
        ddct2d(m, m, 1, g.electro_phi.data(), nullptr, work_area_fft.data(), cs_table_fft.data());
        ddsct2d(m, m, 1, g.electro_fx.data(), nullptr, work_area_fft.data(), cs_table_fft.data());
        ddcst2d(m, m, 1, g.electro_fy.data(), nullptr, work_area_fft.data(), cs_table_fft.data());
        if (fft_debug) {
            g.electro_phi.write_csv(stringf("out_bin_phi_%d_%d.csv", iter, group));
            g.electro_fx.write_csv(stringf("out_bin_ex_%d_%d.csv", iter, group));
            g.electro_fy.write_csv(stringf("out_bin_ey_%d_%d.csv", iter, group));
        }
    }

    void compute_bounds(PlacerNet &net, Axis axis, bool ref)
    {
        NetInfo *ni = net.ni;
        auto drv_loc = cell_loc(ni->driver.cell, ref);
        // seed with driver location
        net.b1.at(axis) = net.b0.at(axis) = drv_loc.at(axis);
        // iterate over users
        for (auto &usr : ni->users) {
            auto usr_loc = cell_loc(usr.cell, ref);
            net.b1.at(axis) = std::max(net.b1.at(axis), usr_loc.at(axis));
            net.b0.at(axis) = std::min(net.b0.at(axis), usr_loc.at(axis));
        }
    }

    RealPair wl_coeff{0.5f, 0.5f};

    void update_nets(bool ref)
    {
        static constexpr float min_wirelen_force = -300.f;
        pool.run(2 * nets.size(), [&](int i) {
            auto &net = nets.at(i / 2);
            auto axis = (i % 2) ? Axis::Y : Axis::X;
            if (net.skip)
                return;
            net.min_exp.at(axis) = 0;
            net.x_min_exp.at(axis) = 0;
            net.max_exp.at(axis) = 0;
            net.x_max_exp.at(axis) = 0;
            // update bounding box
            compute_bounds(net, axis, ref);
            // compute rough center to subtract from exponents to avoid FP issues (from replace)
            float c = (net.b1.at(axis) + net.b0.at(axis)) / 2.f;
            for (auto &port : net.ports) {
                if (!port.ref.cell)
                    continue;
                RealPair loc = cell_loc(port.ref.cell, ref);
                // update weighted-average model exponents
                float emin = (c - loc.at(axis)) * wl_coeff.at(axis);
                float emax = (loc.at(axis) - c) * wl_coeff.at(axis);

                if (emin > min_wirelen_force) {
                    port.min_exp.at(axis) = std::exp(emin);
                    net.min_exp.at(axis) += port.min_exp.at(axis);
                    net.x_min_exp.at(axis) += loc.at(axis) * port.min_exp.at(axis);
                } else {
                    port.min_exp.at(axis) = PlacerPort::invalid;
                }
                if (emax > min_wirelen_force) {
                    port.max_exp.at(axis) = std::exp(emax);
                    net.max_exp.at(axis) += port.max_exp.at(axis);
                    net.x_max_exp.at(axis) += loc.at(axis) * port.max_exp.at(axis);
                } else {
                    port.max_exp.at(axis) = PlacerPort::invalid;
                }
            }
            net.wa_wl.at(axis) =
                    (net.x_max_exp.at(axis) / net.max_exp.at(axis)) - (net.x_min_exp.at(axis) / net.min_exp.at(axis));
        });
    }

    float wirelen_grad(CellInfo *cell, Axis axis, bool ref)
    {
        float gradient = 0;
        if (cell->udata == -1)
            return 0;
        RealPair loc = cell_loc(cell, ref);
        for (auto &port : cell->ports) {
            NetInfo *ni = port.second.net;
            if (!ni)
                continue;
            auto &nd = nets.at(ni->udata);
            if (nd.skip)
                continue;
            auto &pd = nd.ports.at(port.second.type == PORT_OUT ? (nd.ports.size() - 1) : port.second.user_idx.idx());
            // From Replace
            // TODO: check these derivatives on paper
            double d_min = 0, d_max = 0;
            if (pd.has_min_exp(axis)) {
                double min_sum = nd.min_exp.at(axis), x_min_sum = nd.x_min_exp.at(axis);
                d_min = (min_sum * (pd.min_exp.at(axis) * (1.0f - wl_coeff.at(axis) * loc.at(axis))) +
                         wl_coeff.at(axis) * pd.min_exp.at(axis) * x_min_sum) /
                        (min_sum * min_sum);
            }
            if (pd.has_max_exp(axis)) {
                double max_sum = nd.max_exp.at(axis), x_max_sum = nd.x_max_exp.at(axis);
                d_max = (max_sum * (pd.max_exp.at(axis) * (1.0f + wl_coeff.at(axis) * loc.at(axis))) -
                         wl_coeff.at(axis) * pd.max_exp.at(axis) * x_max_sum) /
                        (max_sum * max_sum);
            }
            float crit = 0.0;
            if (cfg.timing_driven) {
                if (port.second.type == PORT_IN) {
                    crit = tmg.get_criticality(CellPortKey(cell->name, port.first));
                } else if (port.second.type == PORT_OUT) {
                    if (ni && ni->users.entries() < 5) {
                        for (auto usr : ni->users)
                            crit = std::max(crit, tmg.get_criticality(CellPortKey(usr)));
                    }
                }
            }
            float weight = 1.0 + 5 * std::pow(crit, 2);
            gradient += weight * (d_min - d_max);
        }

        return gradient;
    }

    std::vector<float> dens_penalty;
    float nesterov_a = 1.0f;

    void update_gradients(bool ref = true, bool set_prev = true, bool init_penalty = false)
    {
        // TODO: skip non-group cells more efficiently?
        pool.run(groups.size(), [&](int group) {
            compute_density(group, ref);
            run_fft(group);
        });
        update_nets(ref);
        // First loop: back up gradients if required; set to zero; and compute density gradient
        for (auto &cell : mcells) {
            auto &g = groups.at(cell.group);
            if (set_prev && ref) {
                cell.last_wl_grad = cell.ref_wl_grad;
                cell.last_dens_grad = cell.ref_dens_grad;
                cell.last_total_grad = cell.ref_total_grad;
            }
            // wirelength gradient updated based on cell instances in next loop
            (ref ? cell.ref_wl_grad : cell.wl_grad) = RealPair(0, 0);
            // density grad based on bins - do we need to interpolate?
            auto &grad = (ref ? cell.ref_dens_grad : cell.dens_grad);
            grad = RealPair(0, 0);
            iter_slithers((ref ? cell.ref_pos : cell.pos), cell.rect, [&](int x, int y, float area) {
                grad += RealPair(g.electro_fx.at(x, y) * area, g.electro_fy.at(x, y) * area);
            });
            // total gradient computed at the end
            (ref ? cell.ref_total_grad : cell.total_grad) = RealPair(0, 0);
        }
        // Second loop: sum up wirelength gradients across concrete cell instances
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->udata == -1)
                continue;
            auto &mc = mcells.at(ci->udata);
            // TODO: exploit parallelism across axes
            float wl_gx = wirelen_grad(ci, Axis::X, ref);
            float wl_gy = wirelen_grad(ci, Axis::Y, ref);
            (ref ? mc.ref_wl_grad : mc.wl_grad) += RealPair(wl_gx, wl_gy);
        }
        if (init_penalty) {
            // set initial density penalty
            dict<int, float> wirelen_sum;
            dict<int, float> force_sum;
            for (auto &cell : ctx->cells) {
                CellInfo *ci = cell.second.get();
                if (ci->udata == -1)
                    continue;
                auto &mc = mcells.at(ci->udata);
                auto res1 = wirelen_sum.insert({mc.group, std::abs(mc.ref_wl_grad.x) + std::abs(mc.ref_wl_grad.y)});
                if (!res1.second)
                    res1.first->second += std::abs(mc.ref_wl_grad.x) + std::abs(mc.ref_wl_grad.y);
                auto res2 = force_sum.insert({mc.group, std::abs(mc.ref_dens_grad.x) + std::abs(mc.ref_dens_grad.y)});
                if (!res2.second)
                    res2.first->second += std::abs(mc.ref_dens_grad.x) + std::abs(mc.ref_dens_grad.y);
            }
            dens_penalty = std::vector<float>(wirelen_sum.size(), 0.0);
            for (auto &item : wirelen_sum) {
                auto group = item.first;
                auto wirelen = item.second;
                dens_penalty[group] = wirelen / force_sum.at(group);
                log_info(" initial density penalty for %s: %f\n", cfg.cell_groups.at(group).name.c_str(ctx),
                         dens_penalty[group]);
            }
        }
        // Third loop: compute total gradient, and precondition
        // TODO: ALM as well as simple penalty
        for (auto &cell : mcells) {
#if 0
            if (!cell.is_spacer) {
                printf("%d (%f, %f) wirelen_grad: (%f,%f) density_grad: (%f,%f)\n", iter, cell.ref_pos.x,
                       cell.ref_pos.y, cell.ref_wl_grad.x, cell.ref_wl_grad.y, cell.ref_dens_grad.x,
                       cell.ref_dens_grad.y);
            }
#endif
            // Preconditioner from replace for now

            float precond = std::max(1.0f, float(cell.pin_count) + dens_penalty[cell.group] * cell.rect.area());
            if (ref) {
                cell.ref_total_grad =
                        ((cell.ref_wl_grad * -1) - cell.ref_dens_grad * dens_penalty[cell.group]) / precond;
            } else {
                cell.total_grad = ((cell.wl_grad * -1) - cell.dens_grad * dens_penalty[cell.group]) / precond;
            }
        }
    }

    float steplen = 0.01;

    float get_steplen()
    {
        float coord_dist = 0;
        float grad_dist = 0;
        int n = 0;
        for (auto &cell : mcells) {
            if (cell.is_fixed || cell.is_dark)
                continue;
            coord_dist += (cell.ref_pos.x - cell.last_ref_pos.x) * (cell.ref_pos.x - cell.last_ref_pos.x);
            coord_dist += (cell.ref_pos.y - cell.last_ref_pos.y) * (cell.ref_pos.y - cell.last_ref_pos.y);
            grad_dist +=
                    (cell.ref_total_grad.x - cell.last_total_grad.x) * (cell.ref_total_grad.x - cell.last_total_grad.x);
            grad_dist +=
                    (cell.ref_total_grad.y - cell.last_total_grad.y) * (cell.ref_total_grad.y - cell.last_total_grad.y);
            n++;
        }
        coord_dist = std::sqrt(coord_dist / (2 * float(n)));
        grad_dist = std::sqrt(grad_dist / (2 * float(n)));
        log_info("coord_dist: %f grad_dist: %f\n", coord_dist, grad_dist);
        return coord_dist / grad_dist;
        // return 0.1;
    }

    float system_hpwl()
    {
        float hpwl = 0;
        for (auto &net : nets) {
            if (net.skip)
                continue;
            // update bounding box
            compute_bounds(net, Axis::X, false);
            compute_bounds(net, Axis::Y, false);
            hpwl += net.b1.x - net.b0.x;
            hpwl += net.b1.y - net.b0.y;
        }
        return hpwl;
    }

    float system_potential()
    {
        float pot = 0;
        for (auto &cell : mcells) {
            auto &g = groups.at(cell.group);
            iter_slithers(cell.ref_pos, cell.rect,
                          [&](int x, int y, float area) { pot += g.electro_phi.at(x, y) * area; });
        }
        return pot;
    }

    void initialise()
    {
        float initial_steplength = 0.01f;
        // Update current and previous gradients with initial solution
        for (auto &cell : mcells) {
            cell.ref_pos = cell.pos;
        }
        while (true) {
            update_gradients(true, true, /* init_penalty */ true);
            // compute a "fake" previous position based on an arbitrary steplength and said gradients for nesterov
            for (auto &cell : mcells) {
                if (cell.is_fixed || cell.is_dark)
                    continue;
                // save current position in last_pos
                cell.last_pos = cell.pos;
                cell.last_ref_pos = cell.ref_pos;
                // compute previous position; but store it in current for gradient computation
                cell.ref_pos = cell.pos - cell.ref_total_grad * initial_steplength;
            }
            // Compute the previous gradients (albeit into the current state fields)
            update_gradients(true);
            // Now we have the fake previous state in the current state
            for (auto &cell : mcells) {
                if (cell.is_fixed || cell.is_dark)
                    continue;
                std::swap(cell.last_ref_pos, cell.ref_pos);
                std::swap(cell.ref_total_grad, cell.last_total_grad);
                std::swap(cell.ref_wl_grad, cell.last_wl_grad);
                std::swap(cell.ref_dens_grad, cell.last_dens_grad);
            }
            float next_steplen = get_steplen();
            log_info("initial steplen=%f next steplen = %f\n", initial_steplength, next_steplen);
            if (next_steplen != 0 && std::isfinite(next_steplen) && std::abs(next_steplen) < 1e10) {
                break;
            } else {
                initial_steplength *= 10;
            }
        }
        update_timing();
    }

    RealPair clamp_loc(RealPair loc)
    {
        return RealPair(std::max<float>(0, std::min<float>(width - 1, loc.x)),
                        std::max<float>(0, std::min<float>(height - 1, loc.y)));
    }

    void update_chains()
    {
        // Move the post-solve position of a chain towards be the weighted average of its constituents
        // The strength increases with iterations
        float alpha = std::min<float>(std::pow(1.002f, iter) - 1, 1.0f);
        for (int i = 0; i < int(macros.size()); i++) {
            auto &macro = macros.at(i);
            float total_area = 0;
            const float area_epsilon = 0.05;
            RealPair pos(0, 0), ref_pos(0, 0);
            for (int c : macro.conc_cells) {
                auto &mc = mcells.at(c);
                float a = std::max(mc.rect.area(), area_epsilon);
                pos += mc.pos * a;
                ref_pos += mc.ref_pos * a;
                total_area += a;
            }
            pos /= total_area;
            ref_pos /= total_area;
            for (int c : macro.conc_cells) {
                auto &cc = ccells.at(c);
                auto &mc = mcells.at(c);
                mc.pos = mc.pos * (1 - alpha) + (pos + RealPair(cc.chunk_dx, cc.chunk_dy)) * alpha;
                mc.ref_pos = mc.ref_pos * (1 - alpha) + (ref_pos + RealPair(cc.chunk_dx, cc.chunk_dy)) * alpha;
            }
        }
    }

    void step()
    {
        // TODO: update penalties; wirelength factor; etc
        steplen = get_steplen();
        std::string penalty_str = "";
        for (auto p : dens_penalty) {
            penalty_str += stringf("%s%.2f", penalty_str.empty() ? "" : ", ", p);
        }
        log_info("iter=%d steplen=%f a=%f penalty=[%s]\n", iter, steplen, nesterov_a, penalty_str.c_str());
        float a_next = (1.0f + std::sqrt(4.0f * nesterov_a * nesterov_a + 1)) / 2.0f;
        // Update positions using Nesterov's
        for (auto &cell : mcells) {
            if (cell.is_fixed || cell.is_dark)
                continue;
            // save current position in last_pos
            cell.last_ref_pos = cell.ref_pos;
            cell.last_pos = cell.pos;
            // compute new position
            cell.pos = clamp_loc(cell.ref_pos - cell.ref_total_grad * steplen);
            // compute reference position
            cell.ref_pos = clamp_loc(cell.pos + (cell.pos - cell.last_pos) * ((nesterov_a - 1) / a_next));
        }
        nesterov_a = a_next;
        update_chains();
        update_gradients(true);
        log_info("   system potential: %f hpwl: %f\n", system_potential(), system_hpwl());
        compute_overlap();
        if ((iter % 5) == 0)
            update_timing();
    }

    void update_timing()
    {
        if (!cfg.timing_driven)
            return;
        for (auto &net : nets) {
            NetInfo *ni = net.ni;
            if (ni->driver.cell == nullptr)
                continue;
            RealPair drv_loc = cell_loc(ni->driver.cell, false);
            for (auto usr : ni->users.enumerate()) {
                RealPair usr_loc = cell_loc(usr.value.cell, false);
                delay_t est_delay = cfg.timing_c + cfg.timing_mx * std::abs(drv_loc.x - usr_loc.x) + cfg.timing_my * std::abs(drv_loc.y - usr_loc.y);
                tmg.set_route_delay(CellPortKey(usr.value), DelayPair(est_delay));
            }
        }
        tmg.run(false);
    }

    void legalise_step(bool dsp_bram)
    {
        // assume DSP and BRAM are all groups 2+ for now
        for (int i = 0; i < int(ccells.size()); i++) {
            auto &mc = mcells.at(i);
            auto &cc = ccells.at(i);
            if (dsp_bram && mc.group < 2)
                continue;
            if (!dsp_bram && mc.group >= 2)
                continue;
            if (cc.macro_idx != -1 && i != macros.at(cc.macro_idx).root->udata)
                continue;      // not macro root
            if (mc.is_fixed) { // already placed
                NPNR_ASSERT(cc.base_cell->bel != BelId());
                continue;
            }
            enqueue_legalise(i);
        }
        // in the DSP/BRAM step, also merge any cells outside of a group (global buffers, other random crud/singletons)
        for (auto &cell : ctx->cells) {
            if (cell.second->udata == -1)
                enqueue_legalise(cell.second.get());
        }
        log_info("Strict legalising %d cells...\n", int(to_legalise.size()));
        float pre_hpwl = system_hpwl();
        legalise_placement_strict(true);
        update_nets(true);
        float post_hpwl = system_hpwl();
        log_info("HPWL after legalise: %f (delta: %f)\n", post_hpwl, post_hpwl - pre_hpwl);
    }

    void enqueue_legalise(int cell_idx)
    {
        NPNR_ASSERT(cell_idx < int(ccells.size())); // we should never be legalising spacers or dark nodes
        auto &ccell = ccells.at(cell_idx);
        if (ccell.macro_idx != -1) {
            // is a macro
            auto &macro = macros.at(ccell.macro_idx);
            to_legalise.emplace(int(macro.cells.size()), macro.root->name);
        } else {
            to_legalise.emplace(1, ccell.base_cell->name);
        }
    }

    void enqueue_legalise(CellInfo *ci)
    {
        if (ci->udata != -1) {
            // managed by static
            enqueue_legalise(ci->udata);
        } else {
            // special case
            to_legalise.emplace(1, ci->name);
        }
    }
    // Strict placement legalisation, performed after the initial HeAP spreading
    void legalise_placement_strict(bool require_validity = true)
    {
        // At the moment we don't follow the full HeAP algorithm using cuts for legalisation, instead using
        // the simple greedy largest-macro-first approach.
        int ripup_radius = 2;
        int total_iters = 0;
        int total_iters_noreset = 0;
        while (!to_legalise.empty()) {
            auto top = to_legalise.top();
            to_legalise.pop();

            CellInfo *ci = ctx->cells.at(top.second).get();
            // Was now placed, ignore
            if (ci->bel != BelId())
                continue;
            // log_info("   Legalising %s (%s) %d\n", top.second.c_str(ctx), ci->type.c_str(ctx), top.first);
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
            if (total_iters > int(ccells.size())) {
                total_iters = 0;
                ripup_radius = std::max(std::max(width + 1, height + 1), ripup_radius * 2);
            }

            if (total_iters_noreset > std::max(5000, 8 * int(ctx->cells.size()))) {
                log_error("Unable to find legal placement for all cells, design is probably at utilisation limit.\n");
            }

            while (!placed) {
                // Determine a search radius around the solver location (which increases over time) that is clamped to
                // the region constraint for the cell (if applicable)
                int rx = radius, ry = radius;

                // Pick a random X and Y location within our search radius
                int cx, cy;
                if (ci->udata == -1) {
                    cx = width / 2;
                    cy = height / 2;
                } else {
                    cx = int(mcells.at(ci->udata).pos.x);
                    cy = int(mcells.at(ci->udata).pos.y);
                }
                int nx = ctx->rng(2 * rx + 1) + std::max(cx - rx, 0);
                int ny = ctx->rng(2 * ry + 1) + std::max(cy - ry, 0);

                iter++;
                iter_at_radius++;
                if (iter >= (10 * (radius + 1))) {
                    // No luck yet, increase radius
                    radius = std::min(std::max(width + 1, height + 1), radius + 1);
                    while (radius < std::max(width + 1, height + 1)) {
                        // Keep increasing the radius until it will actually increase the number of cells we are
                        // checking (e.g. BRAM and DSP will not be in all cols/rows), so we don't waste effort
                        for (int x = std::max(0, cx - radius); x <= std::min(width + 1, cx + radius); x++) {
                            if (x >= int(fb->size()))
                                break;
                            for (int y = std::max(0, cy - radius); y <= std::min(height + 1, cy + radius); y++) {
                                if (y >= int(fb->at(x).size()))
                                    break;
                                if (fb->at(x).at(y).size() > 0)
                                    goto notempty;
                            }
                        }
                        radius = std::min(std::max(width + 1, height + 1), radius + 1);
                    }
                notempty:
                    iter_at_radius = 0;
                    iter = 0;
                }
                // If our randomly chosen cooridnate is out of bounds; or points to a tile with no relevant bels; ignore
                // it
                if (nx < 0 || nx > width + 1)
                    continue;
                if (ny < 0 || ny > height + 1)
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
                        enqueue_legalise(bound);
                    }
                    ctx->bindBel(bestBel, ci, STRENGTH_WEAK);
                    placed = true;
                    Loc loc = ctx->getBelLocation(bestBel);
                    if (ci->udata != -1) {
                        auto &mc = mcells.at(ci->udata);
                        mc.pos = mc.ref_pos = RealPair(loc, 0.5);
                        mc.is_fixed = true;
                    }
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
                                    if (drv->udata == -1)
                                        continue;
                                    auto drv_loc = mcells.at(drv->udata);
                                    input_len += std::abs(int(drv_loc.pos.x) - nx) + std::abs(int(drv_loc.pos.y) - ny);
                                }
                                if (input_len < best_inp_len) {
                                    best_inp_len = input_len;
                                    bestBel = sz;
                                }
                                break;
                            } else {
                                // It's legal, and we've tried enough. Finish.
                                if (bound != nullptr)
                                    enqueue_legalise(bound);
                                Loc loc = ctx->getBelLocation(sz);
                                if (ci->udata != -1) {
                                    auto &mc = mcells.at(ci->udata);
                                    mc.pos = mc.ref_pos = RealPair(loc, 0.5);
                                    mc.is_fixed = true;
                                }
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
                            if (ci->udata != -1) {
                                auto &mc = mcells.at(target.first->udata);
                                mc.pos = mc.ref_pos = RealPair(loc, 0.5);
                                mc.is_fixed = true;
                            }
                            // log_info("%s %d %d %d\n", target.first->name.c_str(ctx), loc.x, loc.y, loc.z);
                        }
                        for (auto &swap : swaps_made) {
                            // Where we have ripped up cells; add them to the queue
                            if (swap.second != nullptr)
                                enqueue_legalise(swap.second);
                        }

                        placed = true;
                        break;
                    }
                }
            }
        }
    }

  public:
    StaticPlacer(Context *ctx, PlacerStaticCfg cfg)
            : ctx(ctx), cfg(cfg), fast_bels(ctx, true, 8), tmg(ctx), pool(ctx->setting<int>("threads", 8))
    {
        groups.resize(cfg.cell_groups.size());
        tmg.setup_only = true;
        tmg.setup();
        dump_density = ctx->setting<bool>("static/dump_density", false);
    };
    void place()
    {
        log_info("Running Static placer...\n");
        init_bels();
        prepare_cells();
        init_cells();
        init_nets();
        insert_dark();
        insert_spacer();

        prepare_density_bins();
        initialise();
        bool legalised_ip = false;
        while (true) {
            step();
            for (auto &penalty : dens_penalty)
                penalty *= 1.025;
            if (!legalised_ip) {
                float ip_overlap = 0;
                for (int i = cfg.logic_groups; i < int(groups.size()); i++)
                    ip_overlap = std::max(ip_overlap, groups.at(i).overlap);
                if (ip_overlap < 0.15) {
                    legalise_step(true);
                    legalised_ip = true;
                }
            } else {
                float logic_overlap = 0;
                for (int i = 0; i < cfg.logic_groups; i++)
                    logic_overlap = std::max(logic_overlap, groups.at(i).overlap);
                if (logic_overlap < 0.1) {
                    legalise_step(false);
                    break;
                }
            }
            ++iter;
        }
        {
            auto placer1_cfg = Placer1Cfg(ctx);
            placer1_cfg.hpwl_scale_x = cfg.hpwl_scale_x;
            placer1_cfg.hpwl_scale_y = cfg.hpwl_scale_y;
            placer1_refine(ctx, placer1_cfg);
        }
    }
};
}; // namespace

bool placer_static(Context *ctx, PlacerStaticCfg cfg)
{
    StaticPlacer(ctx, cfg).place();
    return true;
}

PlacerStaticCfg::PlacerStaticCfg(Context *ctx)
{
    timing_driven = ctx->setting<bool>("timing_driven");

    hpwl_scale_x = 1;
    hpwl_scale_y = 1;
}

NEXTPNR_NAMESPACE_END

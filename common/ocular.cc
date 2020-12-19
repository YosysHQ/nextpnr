/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
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
#ifdef USE_OPENCL

#include "ocular.h"
#include "nextpnr.h"
#include "opencl.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

/*

OCuLaR - Open Computing Language Router

This is a GPGPU router inspired by Corolla [1] with modifications to make it more
suited to the APIs and environment that nextpnr provides. Much of the technique
detail is based on [2].

[1] Corolla: GPU-Accelerated FPGA Routing Based onSubgraph Dynamic Expansion
Minghua Shen, Guojie Luo
https://ceca.pku.edu.cn/media/lw/137e5df7dec627f988e07d54ff222857.pdf

[2] Work-Efficient Parallel GPU Methods for Single-Source Shortest Paths
Andrew Davidson, Sean Baxter, Michael Garland, John D. Owens
https://escholarship.org/uc/item/8qr166v2
*/

struct OcularRouter
{
    Context *ctx;
    std::unique_ptr<cl::Context> clctx;
    std::unique_ptr<cl::Program> clprog;
    std::unique_ptr<cl::CommandQueue> queue;
    std::unique_ptr<cl::Kernel> ocular_route_k;

    // Some magic constants
    const float delay_scale = 1000.0f; // conversion from float ns to int ps

    const int32_t inf_cost = 0x7FFFFFF;

    // Work partitioning and queue configuration - TODO: make these dynamic
    const int num_workgroups = 64;
    const int near_queue_len = 15000;
    const int far_queue_len = 50000;
    const int dirty_queue_len = 50000;
    const int workgroup_size = /*128*/ 4;
    const int max_nets_in_flight = 16;
    const int queue_chunk_size = 131072;
    const int queue_chunk_count = 512;

    /*
        GPU-side routing graph

        At the moment this is a simple flattened graph. Longer term, ways of
        deduplicating this without excessive startup effort or excessively
        complex GPU-side code should be investigated. This might have to be
        once we have shared-between-arches deduplication cracked in general.

        Because we currently only do forward routing in the GPU, this graph
        only needs to be linked in one direction

        Costs in the graph are currently converted to int32s, to enable use
        of atomic updates to improve determinism
    */
    // Wire locations for bounding box tests
    BackedGPUBuffer<int16_t> wire_x, wire_y;
    // Pointer to start in adjaency list -- by wire index
    BackedGPUBuffer<uint32_t> adj_offset;
    // Adjacency list entries -- downhill wire index and cost
    BackedGPUBuffer<uint32_t> edge_dst_index;
    // PIP costs - these will be increased as time goes on
    // to account for historical congestion
    BackedGPUBuffer<int32_t> edge_cost;
    // The GPU doesn't care about these, but we need to corrolate between
    // an adjacency list index and a concrete PIP when we bind the GPU's
    // result
    std::vector<PipId> edge_pip;

    // Some non-GPU fields that are kept in sync with the GPU wire indices
    struct PerWireData
    {
        WireId w;
    };
    std::vector<PerWireData> wire_data;
    std::unordered_map<WireId, int32_t> wire_to_index;

    int width = 0, height = 0;

    // Similar non-GPU related net data
    struct PerNetData
    {
        NetInfo *ni;
        ArcBounds bb;
        bool undriven;
        bool fixed_routing;
        // We dynamically expand the bounding box margin when routing fails
        int bb_margin = 0;
    };

    std::vector<PerNetData> net_data;

    /*
        Current routing state. We need to maintain the following:
           - current cost of a node, or 'infinity' if it hasn't been visited yet
           - the adjacency list entry (that can be corrolated to a pip) used to reach a node
           - current 'near' queue that nodes/edges are being worked on (per workgroup)
           - next 'near' queue that nearby nodes to explore are added to (per workgroup)
           - next 'far' queue that far-away nodes to explore are added to (per workgroup)
           - current newly-dirtied nodes that will need their costs reset to 'infinity' once the current net is routed
       (per workgroup)
           - number of unique nets bound to a node, to determine congestion-related costs
    */
    BackedGPUBuffer<int32_t> current_cost;
    GPUBuffer<uint32_t> uphill_edge;
    // To avoid copies, we swap 'A' and 'B' between current/next queues at every iteration
    GPUBuffer<uint32_t> near_queue_a, near_queue_b;
    // For the next, added-to queue, this is a count starting from 0 for each group
    // For the current, worked-from queue, this is a prefix sum so we can do a binary search to find work
    BackedGPUBuffer<uint32_t> near_queue_count_a, near_queue_count_b;
    // We maintain two 'far' and 'dirty' queues - one per-workgroup that the router adds to and one chunked per-net that
    // we add to when the workgroup finishes
    GPUBuffer<uint32_t> work_far_queue, work_far_queue_count;
    GPUBuffer<uint32_t> work_dirtied_nodes, work_dirtied_nodes_count;

    ChunkedGPUBuffer<uint32_t> net_far_queue, net_dirty_queue;

    BackedGPUBuffer<uint16_t> bound_count;

    /*
        Current routing configuration
        This structure is per in-flight net
    */
    NPNR_PACKED_STRUCT(struct NetConfig {
        // Net bounding box
        cl_short x0, y0, x1, y1;
        // max size of the near and far queue
        cl_int near_queue_size, far_queue_size;
        // max size of the dirtied nodes structure
        cl_int dirtied_nodes_size;
        // start and end workgroup offsets for the net
        cl_int prev_net_start, prev_net_end;
        cl_int curr_net_start, curr_net_end;
        // current congestion cost
        cl_float curr_cong_cost;
        // near/far threshold
        cl_int near_far_thresh;
        // number of nodes to process per workgroup
        cl_int group_nodes;
    });

    /*
        Purely host-side per-inflight-net configuration
    */
    struct InFlightNet
    {
        // index into the flat list of nets, or -1 if this slot isn't used
        int net_idx = -1;

        int queue_count = 0;
    };

    // CPU side grid->net map, so we don't route overlapping nets at once
    std::vector<int8_t> grid2net;

    /*
        Workgroup configuration
    */
    NPNR_PACKED_STRUCT(struct WorkgroupConfig {
        cl_int net;
        cl_uint size;
    });

    // Route config per in-flight net
    BackedGPUBuffer<NetConfig> route_config;
    std::vector<InFlightNet> net_slots;

    BackedGPUBuffer<WorkgroupConfig> wg_config;

    OcularRouter(Context *ctx)
            : ctx(ctx), clctx(get_opencl_ctx(ctx)), clprog(get_opencl_program(*clctx, "ocular")),
              wire_x(*clctx, CL_MEM_READ_ONLY), wire_y(*clctx, CL_MEM_READ_ONLY), adj_offset(*clctx, CL_MEM_READ_ONLY),
              edge_dst_index(*clctx, CL_MEM_READ_ONLY), edge_cost(*clctx, CL_MEM_READ_WRITE),
              current_cost(*clctx, CL_MEM_READ_WRITE), uphill_edge(*clctx, CL_MEM_READ_WRITE),
              near_queue_a(*clctx, CL_MEM_READ_WRITE), near_queue_b(*clctx, CL_MEM_READ_WRITE),
              near_queue_count_a(*clctx, CL_MEM_READ_WRITE), near_queue_count_b(*clctx, CL_MEM_READ_WRITE),
              work_far_queue(*clctx, CL_MEM_READ_WRITE), work_far_queue_count(*clctx, CL_MEM_READ_WRITE),
              work_dirtied_nodes(*clctx, CL_MEM_READ_WRITE), work_dirtied_nodes_count(*clctx, CL_MEM_READ_WRITE),
              net_far_queue(*clctx, CL_MEM_READ_WRITE, queue_chunk_size, max_nets_in_flight, queue_chunk_count),
              net_dirty_queue(*clctx, CL_MEM_READ_WRITE, queue_chunk_size, max_nets_in_flight, queue_chunk_count),
              bound_count(*clctx, CL_MEM_READ_WRITE), route_config(*clctx, CL_MEM_READ_ONLY),
              wg_config(*clctx, CL_MEM_READ_ONLY)
    {
    }

    void build_graph()
    {
        log_info("Importing routing graph...\n");
        // Build the GPU-oriented, flattened routing graph from the Arch-provided data
        for (auto wire : ctx->getWires()) {
            // Get the centroid of the wire for hit-testing purposes
            ArcBounds wire_loc = ctx->getRouteBoundingBox(wire, wire);
            short cx = (wire_loc.x0 + wire_loc.x1) / 2;
            short cy = (wire_loc.y0 + wire_loc.y1) / 2;

            wire_x.push_back(cx);
            wire_y.push_back(cy);

            PerWireData wd;
            wd.w = wire;
            wire_to_index[wire] = int(wire_data.size());
            wire_data.push_back(wd);

            width = std::max<int>(wire_loc.x1 + 1, width);
            height = std::max<int>(wire_loc.y1 + 1, height);
        }

        // Construct the CSR format adjacency list
        adj_offset.resize(wire_data.size() + 1);

        for (size_t i = 0; i < wire_data.size(); i++) {
            WireId w = wire_data.at(i).w;
            // CSR offset
            adj_offset.at(i) = edge_dst_index.size();
            for (PipId p : ctx->getPipsDownhill(w)) {
                // Ignore permanently unavailable pips, and pips bound before we enter the router (e.g. for gclks)
                if (!ctx->checkPipAvail(p))
                    continue;
                WireId dst = ctx->getPipDstWire(p);
                if (!ctx->checkWireAvail(dst))
                    continue;
                // Compute integer cost; combined cost of the pip and the wire it drives
                int base_cost = int((ctx->getDelayNS(ctx->getPipDelay(p).maxDelay()) +
                                     ctx->getDelayNS(ctx->getWireDelay(dst).maxDelay())) *
                                    delay_scale);
                // Add to the adjacency list
                edge_cost.push_back(base_cost);
                edge_dst_index.push_back(wire_to_index.at(dst));
            }
        }
        // Final offset so we know the total size of the list; for the last node
        adj_offset.at(wire_data.size()) = edge_dst_index.size();
        // Resize some other per-net structures
        current_cost.resize(wire_data.size());
        std::fill(current_cost.begin(), current_cost.end(), inf_cost);
        uphill_edge.resize(wire_data.size());
        bound_count.resize(wire_data.size());
    }

    void import_nets()
    {
        log_info("Importing nets...\n");
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            PerNetData nd;
            nd.ni = ni;
            ni->udata = net_data.size();
            // Initial bounding box is the null space
            nd.bb.x0 = ctx->getGridDimX() - 1;
            nd.bb.y0 = ctx->getGridDimY() - 1;
            nd.bb.x1 = 0;
            nd.bb.y1 = 0;
            if (ni->driver.cell != nullptr) {
                nd.bb.extend(ctx->getBelLocation(ni->driver.cell->bel));
            } else {
                nd.undriven = true;
            }
            for (auto &usr : ni->users) {
                nd.bb.extend(ctx->getBelLocation(usr.cell->bel));
            }
            nd.fixed_routing = false;
            // Check for existing routing (e.g. global clocks routed earlier)
            if (!ni->wires.empty()) {
                bool invalid_route = false;
                for (auto &usr : ni->users) {
                    WireId wire = ctx->getNetinfoSinkWire(ni, usr);
                    if (!ni->wires.count(wire))
                        invalid_route = true;
                    else if (ni->wires.at(wire).strength > STRENGTH_STRONG)
                        nd.fixed_routing = true;
                }
                if (nd.fixed_routing) {
                    if (invalid_route)
                        log_error("Combination of locked and incomplete routing on net '%s' is unsupported.\n",
                                  ctx->nameOf(ni));
                    // Mark wires as used so they have a congestion penalty associated with them
                    for (auto &wire : ni->wires) {
                        int idx = wire_to_index.at(wire.first);
                        NPNR_ASSERT(bound_count.at(idx) == 0); // no overlaps allowed for locked routing
                        bound_count.at(idx)++;
                    }
                } else {
                    // Routing isn't fixed, just rip it up so we don't worry about it
                    ctx->ripupNet(ni->name);
                }
            }
            net_data.push_back(nd);
        }
    }

    void alloc_buffers()
    {
        // Near queues (two because we swap them)
        near_queue_a.resize(near_queue_len * num_workgroups);
        near_queue_count_a.resize(num_workgroups);
        near_queue_b.resize(near_queue_len * num_workgroups);
        near_queue_count_b.resize(num_workgroups);
        // Far queue
        work_far_queue.resize(far_queue_len * num_workgroups);
        work_far_queue_count.resize(num_workgroups);
        // Per-workgroup dirty node list
        work_dirtied_nodes.resize(dirty_queue_len * num_workgroups);
        work_dirtied_nodes_count.resize(num_workgroups);

        route_config.resize(max_nets_in_flight);
        net_slots.resize(max_nets_in_flight);
        wg_config.resize(num_workgroups);
        for (auto &wg : wg_config)
            wg.size = workgroup_size;

        grid2net.resize(width * height, -1);

        // Put the sizes in net config too, so that the GPU sees them
        int workgroup = 0;
        for (auto &nc : route_config) {
            nc.near_queue_size = near_queue_len;
            nc.far_queue_size = far_queue_len;
            nc.dirtied_nodes_size = dirty_queue_len;

            // Allocate one notional workgroup to start with
            nc.curr_net_start = workgroup;
            nc.curr_net_end = workgroup + 1;

            workgroup++;
        }
    }

    // Handling of net bounding box reservations
    void mark_region(int x0, int y0, int x1, int y1, int8_t value)
    {
        for (int y = y0; y <= y1; y++) {
            NPNR_ASSERT(y >= 0 && y < height);
            for (int x = x0; x <= x1; x++) {
                NPNR_ASSERT(x >= 0 && x < width);
                grid2net[y * width + x] = value;
            }
        }
    }

    bool check_region(int x0, int y0, int x1, int y1, int8_t value = -1)
    {
        for (int y = y0; y <= y1; y++) {
            NPNR_ASSERT(y >= 0 && y < height);
            for (int x = x0; x <= x1; x++) {
                NPNR_ASSERT(x >= 0 && x < width);
                if (grid2net[y * width + x] != value)
                    return false;
            }
        }
        return true;
    }

    template <typename T> T prefix_sum(BackedGPUBuffer<T> &in, int start, int end)
    {
        T sum = 0;
        for (int i = start; i < end; i++) {
            sum += in.at(i);
            in.at(i) = sum;
        }
        return sum;
    }

    // If true then the current queue is 'b' and the next queue is 'a'
    bool curr_is_b = false;

    int used_workgroups = 0;

    // Allocation of nets to workgroups
    void distribute_nets()
    {
        // Assume that current queue data has been fetched and prefix-sumed
        auto &nq_count = curr_is_b ? near_queue_count_b : near_queue_count_a;
        int total_queue_count = 0;
        for (int i = 0; i < max_nets_in_flight; i++) {
            auto &rc = route_config.at(i);
            // prefix sum means final entry is the total count
            int count = nq_count.at(rc.curr_net_end - 1);
            net_slots.at(i).queue_count = count;
            total_queue_count += count;
            // rotate curr/prev offsets
            rc.prev_net_start = rc.curr_net_start;
            rc.prev_net_end = rc.curr_net_end;
        }
        // Currently, we always reserve a workgroup here in case we decide to add a net
        int target_workgroups = std::max(max_nets_in_flight, std::min(num_workgroups, total_queue_count));
        // Attempt to split the per-net workload relatively evenly, but adhering to the min-1-workgroup-per-net
        // constraint
        int curr_workgroup = 0;
        for (int i = 0; i < max_nets_in_flight; i++) {
            auto &rc = route_config.at(i);
            int queue_count = net_slots.at(i).queue_count;
            int net_workgroups = 1 + (((target_workgroups - max_nets_in_flight) * queue_count) /
                                      std::max(max_nets_in_flight, total_queue_count));
            rc.curr_net_start = curr_workgroup;
            rc.curr_net_end = curr_workgroup + net_workgroups;
            for (int j = rc.curr_net_start; j < rc.curr_net_end; j++) {
                wg_config.at(j).net = i;
            }
            // Number of queue entries to process per workgroup (N.B. rounding up otherwise we'd lose nodes)
            rc.group_nodes = (queue_count + (net_workgroups - 1)) / net_workgroups;
            curr_workgroup += net_workgroups;
        }
        used_workgroups = curr_workgroup;
        NPNR_ASSERT(used_workgroups <= num_workgroups);
    }

    // Set up the queue and push fixed data
    void gpu_setup()
    {
        log_info("Pushing initial data to GPU...\n");
        queue = std::unique_ptr<cl::CommandQueue>(new cl::CommandQueue(*clctx));
        // Push graph
        wire_x.put(*queue);
        wire_y.put(*queue);
        adj_offset.put(*queue);
        edge_dst_index.put(*queue);
        edge_cost.put(*queue);

        current_cost.put(*queue);
        bound_count.put(*queue);
        // Init kernels and set fixed arguments
        ocular_route_k = std::unique_ptr<cl::Kernel>(new cl::Kernel(*clprog, "ocular_route"));
        ocular_route_k->setArg(0, route_config.buf());
        ocular_route_k->setArg(1, wg_config.buf());
        ocular_route_k->setArg(2, wire_x.buf());
        ocular_route_k->setArg(3, wire_y.buf());
        ocular_route_k->setArg(4, adj_offset.buf());
        ocular_route_k->setArg(5, edge_dst_index.buf());
        ocular_route_k->setArg(6, edge_cost.buf());
        // Near is set dynamically based on A/B
        ocular_route_k->setArg(11, work_far_queue.buf());
        ocular_route_k->setArg(12, work_far_queue_count.buf());
        ocular_route_k->setArg(13, work_dirtied_nodes.buf());
        ocular_route_k->setArg(14, work_dirtied_nodes_count.buf());
        ocular_route_k->setArg(15, current_cost.buf());
        ocular_route_k->setArg(16, uphill_edge.buf());
        ocular_route_k->setArg(17, bound_count.buf());
    }

    // Try and add a net
    bool try_add_net(int net_idx)
    {
        int slot_idx = -1;
        // Search for a free slot
        for (size_t i = 0; i < net_slots.size(); i++) {
            if (net_slots.at(i).net_idx == -1) {
                slot_idx = i;
                break;
            }
        }
        // Check if we found a slot
        if (slot_idx == -1)
            return false;
        auto &nd = net_data.at(net_idx);
        auto &ifn = net_slots.at(slot_idx);
        ifn.net_idx = net_idx;
        auto &cfg = route_config.at(slot_idx);
        // Compute expanded bounding box
        cfg.x0 = std::max<int>(0, nd.bb.x0 - nd.bb_margin);
        cfg.y0 = std::max<int>(0, nd.bb.y0 - nd.bb_margin);
        cfg.x1 = std::min<int>(width - 1, nd.bb.x1 + nd.bb_margin);
        cfg.y1 = std::min<int>(height - 1, nd.bb.y1 + nd.bb_margin);
        // Check for overlaps with other nets being routed
        if (!check_region(cfg.x0, cfg.y0, cfg.x1, cfg.x1))
            return false;
        // Mark as in use
        mark_region(cfg.x0, cfg.y0, cfg.x1, cfg.x1, slot_idx);
        // Add the starting wire to the relevant near queue chunk
        auto &nq_count = curr_is_b ? near_queue_count_b : near_queue_count_a;
        auto &nq_buf = curr_is_b ? near_queue_b : near_queue_a;
        // Only one entry, in the first chunk, prefix sum means following chunks are '1' too
        for (int i = cfg.prev_net_start; i < cfg.prev_net_end; i++)
            nq_count.at(i) = 1;
        cfg.group_nodes = 1;
        // Get source index
        WireId src_wire = ctx->getNetinfoSourceWire(nd.ni);
        NPNR_ASSERT(src_wire != WireId());
        int32_t src_wire_idx = wire_to_index.at(src_wire);
        // Add to queue
        nq_buf.write(*queue, cfg.prev_net_start * near_queue_len, src_wire_idx);
        // Start cost of zero
        current_cost.write(*queue, src_wire_idx, 0);
        // Threshold - FIXME
        cfg.near_far_thresh = 3000;
        return true;
    }

    void per_iter_put()
    {
        auto &nq_count = curr_is_b ? near_queue_count_b : near_queue_count_a;
        nq_count.put(*queue);
        route_config.put(*queue);
        wg_config.put(*queue);
    }

    void do_route()
    {
        log_info("Doing route...\n");
        // Initial distribution; based on zero queue length for all nets
        distribute_nets();
        int added_nets = 0;
        // TEST: just add some random high-fanout nets as a good test
        for (size_t i = 0; i < net_data.size(); i++) {
            auto &n = net_data.at(i);
            if (n.fixed_routing)
                continue; // nothing to do
            if (n.ni->driver.cell == nullptr || int(n.ni->users.size() < 10))
                continue;
            if (try_add_net(i)) {
                log_info("    added net %s\n", ctx->nameOf(n.ni));
                ++added_nets;
                if (added_nets >= max_nets_in_flight)
                    break;
            }
        }
        for (size_t i = 0; i < 10; i++) {
            // Push per-iter data
            per_iter_put();
            // Set pointers to current queue
            ocular_route_k->setArg(7, (curr_is_b ? near_queue_b : near_queue_a).buf());
            ocular_route_k->setArg(8, (curr_is_b ? near_queue_count_b : near_queue_count_a).buf());
            ocular_route_k->setArg(9, (curr_is_b ? near_queue_a : near_queue_b).buf());
            ocular_route_k->setArg(10, (curr_is_b ? near_queue_count_a : near_queue_count_b).buf());
            // Run kernel :D
            log_info("    running with %d workgroups...\n", used_workgroups);
            queue->enqueueNDRangeKernel(*ocular_route_k, cl::NullRange, cl::NDRange(used_workgroups * workgroup_size),
                                        cl::NDRange(workgroup_size));
            queue->flush();
            // Fetch count
            auto &next_count = curr_is_b ? near_queue_count_a : near_queue_count_b;
            next_count.get(*queue);
            for (int i = 0; i < used_workgroups; i++) {
                log_info("%d: %u\n", i, next_count.at(i));
            }
            for (int i = 0; i < max_nets_in_flight; i++) {
                prefix_sum(next_count, route_config.at(i).curr_net_start, route_config.at(i).curr_net_end);
            }
            curr_is_b = !curr_is_b;
            distribute_nets();
        }
    }

    bool operator()()
    {
        build_graph();
        import_nets();
        alloc_buffers();
        gpu_setup();
        do_route();
        return true;
    }
};

bool router_ocular(Context *ctx)
{
    OcularRouter router(ctx);
    return router();
}

NEXTPNR_NAMESPACE_END

#endif

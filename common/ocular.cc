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

    // Some magic constants
    const float delay_scale = 1000.0f; // conversion from float ns to int ps

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

    // Similar non-GPU related net data
    struct PerNetData
    {
        NetInfo *ni;
        ArcBounds bb;
        bool undriven;
        bool fixed_routing;
    };

    const int32_t inf_cost = 0x7FFFFFF;

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
    GPUBuffer<uint32_t> near_queue_count_a, near_queue_count_b;
    // We don't have A/B for the far queue, because it is never directly worked from
    GPUBuffer<uint32_t> far_queue, far_queue_count;

    GPUBuffer<uint32_t> dirtied_nodes;
    BackedGPUBuffer<uint8_t> bound_count;

    /*
        Current routing configuration
        This structure is per in-flight net
    */
    NPNR_PACKED_STRUCT(struct RouteConfig {
        // Net bounding box
        cl_short x0, y0, x1, y1;
        // max size of the near and far queue
        cl_int near_queue_size, far_queue_size;
        // max size of the dirtied nodes structure
        cl_int dirtied_nodes_size;
        // start and end workgroup offsets for the net
        cl_int net_start, net_end;
        // current congestion cost
        cl_float curr_cong_cost;
    });

    // Route config per in-flight net
    GPUBuffer<RouteConfig> route_config;

    OcularRouter(Context *ctx)
            : ctx(ctx), clctx(get_opencl_ctx(ctx)), clprog(get_opencl_program(*clctx, "ocular")),
              wire_x(*clctx, CL_MEM_READ_ONLY), wire_y(*clctx, CL_MEM_READ_ONLY), adj_offset(*clctx, CL_MEM_READ_ONLY),
              edge_dst_index(*clctx, CL_MEM_READ_ONLY), edge_cost(*clctx, CL_MEM_READ_WRITE),
              current_cost(*clctx, CL_MEM_READ_WRITE), uphill_edge(*clctx, CL_MEM_READ_WRITE),
              near_queue_a(*clctx, CL_MEM_READ_WRITE), near_queue_b(*clctx, CL_MEM_READ_WRITE),
              near_queue_count_a(*clctx, CL_MEM_READ_WRITE), near_queue_count_b(*clctx, CL_MEM_READ_WRITE),
              far_queue(*clctx, CL_MEM_READ_WRITE), far_queue_count(*clctx, CL_MEM_READ_WRITE),
              dirtied_nodes(*clctx, CL_MEM_READ_WRITE), bound_count(*clctx, CL_MEM_READ_WRITE),
              route_config(*clctx, CL_MEM_READ_ONLY)
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
                edge_pip.push_back(p);
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
        }
    }

    bool operator()()
    {
        // The sequence of things to do
        build_graph();
        import_nets();
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

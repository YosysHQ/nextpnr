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
    // Number of entries in adjacency list -- by wire index
    BackedGPUBuffer<int16_t> adj_size;
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
    OcularRouter(Context *ctx)
            : clctx(get_opencl_ctx(ctx)), clprog(get_opencl_program(*clctx, "ocular")),
              wire_x(*clctx, CL_MEM_READ_ONLY), wire_y(*clctx, CL_MEM_READ_ONLY), adj_size(*clctx, CL_MEM_READ_ONLY),
              adj_offset(*clctx, CL_MEM_READ_ONLY), edge_dst_index(*clctx, CL_MEM_READ_ONLY),
              edge_cost(*clctx, CL_MEM_READ_ONLY)
    {
    }
};

bool router_ocular(Context *ctx)
{
    OcularRouter router(ctx);
    return true;
}

NEXTPNR_NAMESPACE_END

#endif

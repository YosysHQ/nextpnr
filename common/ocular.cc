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
    OcularRouter(Context *ctx) : clctx(get_opencl_ctx(ctx)), clprog(get_opencl_program(*clctx, "ocular")) {}
};

bool router_ocular(Context *ctx)
{
    OcularRouter router(ctx);
    return true;
}

NEXTPNR_NAMESPACE_END

#endif

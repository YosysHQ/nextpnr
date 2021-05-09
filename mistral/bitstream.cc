/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN
namespace {
struct MistralBitgen
{
    MistralBitgen(Context *ctx) : ctx(ctx), cv(ctx->cyclonev){};
    Context *ctx;
    CycloneV *cv;

    void init()
    {
        ctx->init_base_bitstream();
        // Default options
        cv->opt_b_set(CycloneV::ALLOW_DEVICE_WIDE_OUTPUT_ENABLE_DIS, true);
        cv->opt_n_set(CycloneV::CRC_DIVIDE_ORDER, 8);
        cv->opt_b_set(CycloneV::CVP_CONF_DONE_EN, true);
        cv->opt_b_set(CycloneV::DEVICE_WIDE_RESET_EN, true);
        cv->opt_n_set(CycloneV::DRIVE_STRENGTH, 8);
        cv->opt_b_set(CycloneV::IOCSR_READY_FROM_CSR_DONE_EN, true);
        cv->opt_b_set(CycloneV::NCEO_DIS, true);
        cv->opt_b_set(CycloneV::OCT_DONE_DIS, true);
        cv->opt_r_set(CycloneV::OPT_A, 0x1dff);
        cv->opt_r_set(CycloneV::OPT_B, 0xffffff402dffffffULL);
        cv->opt_b_set(CycloneV::RELEASE_CLEARS_BEFORE_TRISTATES_DIS, true);
        cv->opt_b_set(CycloneV::RETRY_CONFIG_ON_ERROR_EN, true);
        cv->opt_r_set(CycloneV::START_UP_CLOCK, 0x3F);
    }

    void write_routing()
    {
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            for (auto wire : sorted_ref(ni->wires)) {
                PipId pip = wire.second.pip;
                if (pip == PipId())
                    continue;
                WireId src = ctx->getPipSrcWire(pip), dst = ctx->getPipDstWire(pip);
                // Only write out routes that are entirely in the Mistral domain. Everything else is dealt with
                // specially
                if (src.is_nextpnr_created() || dst.is_nextpnr_created())
                    continue;
                cv->rnode_link(src.node, dst.node);
            }
        }
    }

    void run()
    {
        cv->clear();
        init();
        write_routing();
    }
};
} // namespace

void Arch::build_bitstream()
{
    MistralBitgen gen(getCtx());
    gen.run();
}

NEXTPNR_NAMESPACE_END

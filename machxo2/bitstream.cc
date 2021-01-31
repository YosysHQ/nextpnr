/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
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

#include <fstream>

#include "bitstream.h"
#include "config.h"

NEXTPNR_NAMESPACE_BEGIN

// These seem simple enough to do inline for now.
namespace BaseConfigs {
    void config_empty_lcmxo2_1200hc(ChipConfig &cc)
    {
        cc.chip_name = "LCMXO2-1200HC";

        cc.tiles["EBR_R6C11:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C15:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C18:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C21:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C2:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C5:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C8:EBR1"].add_unknown(0, 12);

        cc.tiles["PT4:CFG0"].add_unknown(5, 30);
        cc.tiles["PT4:CFG0"].add_unknown(5, 32);
        cc.tiles["PT4:CFG0"].add_unknown(5, 36);

        cc.tiles["PT7:CFG3"].add_unknown(5, 18);
    }
} // namespace BaseConfigs

void write_bitstream(Context *ctx, std::string text_config_file)
{
    ChipConfig cc;

    switch (ctx->args.type) {
    case ArchArgs::LCMXO2_1200HC:
        BaseConfigs::config_empty_lcmxo2_1200hc(cc);
        break;
    default:
        NPNR_ASSERT_FALSE("Unsupported device type");
    }

    // Configure chip
    if (!text_config_file.empty()) {
        std::ofstream out_config(text_config_file);
        out_config << cc;
    }
}

NEXTPNR_NAMESPACE_END

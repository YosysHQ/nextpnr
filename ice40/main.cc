/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
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

#ifdef MAIN_EXECUTABLE

#include <fstream>
#include "bitstream.h"
#include "command.h"
#include "design_utils.h"
#include "jsonparse.h"
#include "log.h"
#include "pcf.h"
#include "timing.h"

USING_NEXTPNR_NAMESPACE

class Ice40CommandHandler : public CommandHandler
{
  public:
    Ice40CommandHandler(int argc, char **argv);
    virtual ~Ice40CommandHandler(){};
    std::unique_ptr<Context> createContext() override;
    void setupArchContext(Context *ctx) override;
    void validate() override;
    void customAfterLoad(Context *ctx) override;
    void customBitstream(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

Ice40CommandHandler::Ice40CommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description Ice40CommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
#ifdef ICE40_HX1K_ONLY
    specific.add_options()("hx1k", "set device type to iCE40HX1K");
#else
    specific.add_options()("lp384", "set device type to iCE40LP384");
    specific.add_options()("lp1k", "set device type to iCE40LP1K");
    specific.add_options()("lp8k", "set device type to iCE40LP8K");
    specific.add_options()("hx1k", "set device type to iCE40HX1K");
    specific.add_options()("hx8k", "set device type to iCE40HX8K");
    specific.add_options()("up5k", "set device type to iCE40UP5K");
#endif
    specific.add_options()("package", po::value<std::string>(), "set device package");
    specific.add_options()("pcf", po::value<std::string>(), "PCF constraints file to ingest");
    specific.add_options()("asc", po::value<std::string>(), "asc bitstream file to write");
    specific.add_options()("read", po::value<std::string>(), "asc bitstream file to read");
    specific.add_options()("promote-logic",
                           "enable promotion of 'logic' globals (in addition to clk/ce/sr by default)");
    specific.add_options()("no-promote-globals", "disable all global promotion");
    specific.add_options()("opt-timing", "run post-placement timing optimisation pass (experimental)");
    specific.add_options()("tmfuzz", "run path delay estimate fuzzer");
    specific.add_options()("pcf-allow-unconstrained", "don't require PCF to constrain all IO");

    return specific;
}
void Ice40CommandHandler::validate()
{
    conflicting_options(vm, "read", "json");
    if ((vm.count("lp384") + vm.count("lp1k") + vm.count("lp8k") + vm.count("hx1k") + vm.count("hx8k") +
         vm.count("up5k")) > 1)
        log_error("Only one device type can be set\n");
}

void Ice40CommandHandler::customAfterLoad(Context *ctx)
{
    if (vm.count("pcf")) {
        std::string filename = vm["pcf"].as<std::string>();
        std::ifstream pcf(filename);
        if (!apply_pcf(ctx, filename, pcf))
            log_error("Loading PCF failed.\n");
    } else {
        log_warning("No PCF file specified; IO pins will be placed automatically\n");
    }
}
void Ice40CommandHandler::customBitstream(Context *ctx)
{
    if (vm.count("asc")) {
        std::string filename = vm["asc"].as<std::string>();
        std::ofstream f(filename);
        write_asc(ctx, f);
    }
}

void Ice40CommandHandler::setupArchContext(Context *ctx)
{
    if (vm.count("tmfuzz"))
        ice40DelayFuzzerMain(ctx);

    if (vm.count("read")) {
        std::string filename = vm["read"].as<std::string>();
        std::ifstream f(filename);
        if (!read_asc(ctx, f))
            log_error("Loading ASC failed.\n");
    }
}

std::unique_ptr<Context> Ice40CommandHandler::createContext()
{
    if (vm.count("lp384")) {
        chipArgs.type = ArchArgs::LP384;
        chipArgs.package = "qn32";
    }

    if (vm.count("lp1k")) {
        chipArgs.type = ArchArgs::LP1K;
        chipArgs.package = "tq144";
    }

    if (vm.count("lp8k")) {
        chipArgs.type = ArchArgs::LP8K;
        chipArgs.package = "ct256";
    }

    if (vm.count("hx1k")) {
        chipArgs.type = ArchArgs::HX1K;
        chipArgs.package = "tq144";
    }

    if (vm.count("hx8k")) {
        chipArgs.type = ArchArgs::HX8K;
        chipArgs.package = "ct256";
    }

    if (vm.count("up5k")) {
        chipArgs.type = ArchArgs::UP5K;
        chipArgs.package = "sg48";
    }

    if (chipArgs.type == ArchArgs::NONE) {
        chipArgs.type = ArchArgs::HX1K;
        chipArgs.package = "tq144";
    }
#ifdef ICE40_HX1K_ONLY
    if (chipArgs.type != ArchArgs::HX1K) {
        log_error("This version of nextpnr-ice40 is built with HX1K-support only.\n");
    }
#endif

    if (vm.count("package"))
        chipArgs.package = vm["package"].as<std::string>();

    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));

    if (vm.count("promote-logic"))
        ctx->settings[ctx->id("promote_logic")] = "1";
    if (vm.count("no-promote-globals"))
        ctx->settings[ctx->id("no_promote_globals")] = "1";
    if (vm.count("opt-timing"))
        ctx->settings[ctx->id("opt_timing")] = "1";
    if (vm.count("pcf-allow-unconstrained"))
        ctx->settings[ctx->id("pcf_allow_unconstrained")] = "1";

    return ctx;
}

int main(int argc, char *argv[])
{
    Ice40CommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

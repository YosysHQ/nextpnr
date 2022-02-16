/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  Miodrag Milanovic <micko@yosyshq.com>
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
#include "log.h"
#include "pcf.h"
#include "timing.h"

USING_NEXTPNR_NAMESPACE

class Ice40CommandHandler : public CommandHandler
{
  public:
    Ice40CommandHandler(int argc, char **argv);
    virtual ~Ice40CommandHandler(){};
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
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
    if (Arch::is_available(ArchArgs::LP384))
        specific.add_options()("lp384", "set device type to iCE40LP384");
    if (Arch::is_available(ArchArgs::LP1K))
        specific.add_options()("lp1k", "set device type to iCE40LP1K");
    if (Arch::is_available(ArchArgs::LP4K))
        specific.add_options()("lp4k", "set device type to iCE40LP4K");
    if (Arch::is_available(ArchArgs::LP8K))
        specific.add_options()("lp8k", "set device type to iCE40LP8K");
    if (Arch::is_available(ArchArgs::HX1K))
        specific.add_options()("hx1k", "set device type to iCE40HX1K");
    if (Arch::is_available(ArchArgs::HX8K))
        specific.add_options()("hx4k", "set device type to iCE40HX4K");
    if (Arch::is_available(ArchArgs::HX4K))
        specific.add_options()("hx8k", "set device type to iCE40HX8K");
    if (Arch::is_available(ArchArgs::UP3K))
        specific.add_options()("up3k", "set device type to iCE40UP3K");
    if (Arch::is_available(ArchArgs::UP5K))
        specific.add_options()("up5k", "set device type to iCE40UP5K");
    if (Arch::is_available(ArchArgs::U1K))
        specific.add_options()("u1k", "set device type to iCE5LP1K");
    if (Arch::is_available(ArchArgs::U2K))
        specific.add_options()("u2k", "set device type to iCE5LP2K");
    if (Arch::is_available(ArchArgs::U4K))
        specific.add_options()("u4k", "set device type to iCE5LP4K");
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
    if ((vm.count("lp384") + vm.count("lp1k") + vm.count("lp4k") + vm.count("lp8k") + vm.count("hx1k") +
         vm.count("hx4k") + vm.count("hx8k") + vm.count("up3k") + vm.count("up5k") + vm.count("u1k") + vm.count("u2k") +
         vm.count("u4k")) > 1)
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

std::unique_ptr<Context> Ice40CommandHandler::createContext(dict<std::string, Property> &values)
{
    ArchArgs chipArgs;
    chipArgs.type = ArchArgs::NONE;
    if (vm.count("lp384")) {
        chipArgs.type = ArchArgs::LP384;
        chipArgs.package = "qn32";
    }

    if (vm.count("lp1k")) {
        chipArgs.type = ArchArgs::LP1K;
        chipArgs.package = "tq144";
    }

    if (vm.count("lp4k")) {
        chipArgs.type = ArchArgs::LP4K;
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

    if (vm.count("hx4k")) {
        chipArgs.type = ArchArgs::HX4K;
        chipArgs.package = "tq144";
    }

    if (vm.count("hx8k")) {
        chipArgs.type = ArchArgs::HX8K;
        chipArgs.package = "ct256";
    }

    if (vm.count("up3k")) {
        chipArgs.type = ArchArgs::UP3K;
        chipArgs.package = "sg48";
    }

    if (vm.count("up5k")) {
        chipArgs.type = ArchArgs::UP5K;
        chipArgs.package = "sg48";
    }

    if (vm.count("u1k")) {
        chipArgs.type = ArchArgs::U1K;
        chipArgs.package = "sg48";
    }

    if (vm.count("u2k")) {
        chipArgs.type = ArchArgs::U2K;
        chipArgs.package = "sg48";
    }

    if (vm.count("u4k")) {
        chipArgs.type = ArchArgs::U4K;
        chipArgs.package = "sg48";
    }

    if (vm.count("package"))
        chipArgs.package = vm["package"].as<std::string>();
    else
        log_warning("Use of default value for --package is deprecated. Please add '--package %s' to arguments.\n",
                    chipArgs.package.c_str());

    if (values.find("arch.name") != values.end()) {
        std::string arch_name = values["arch.name"].as_string();
        if (arch_name != "ice40")
            log_error("Unsupported architecture '%s'.\n", arch_name.c_str());
    }
    if (values.find("arch.type") != values.end()) {
        std::string arch_type = values["arch.type"].as_string();
        if (chipArgs.type != ArchArgs::NONE)
            log_error("Overriding architecture is unsupported.\n");

        if (arch_type == "lp384") {
            chipArgs.type = ArchArgs::LP384;
        }
        if (arch_type == "lp1k") {
            chipArgs.type = ArchArgs::LP1K;
        }
        if (arch_type == "lp4k") {
            chipArgs.type = ArchArgs::LP4K;
        }
        if (arch_type == "lp8k") {
            chipArgs.type = ArchArgs::LP8K;
        }
        if (arch_type == "hx1k") {
            chipArgs.type = ArchArgs::HX1K;
        }
        if (arch_type == "hx4k") {
            chipArgs.type = ArchArgs::HX4K;
        }
        if (arch_type == "hx8k") {
            chipArgs.type = ArchArgs::HX8K;
        }
        if (arch_type == "up3k") {
            chipArgs.type = ArchArgs::UP3K;
        }
        if (arch_type == "up5k") {
            chipArgs.type = ArchArgs::UP5K;
        }
        if (arch_type == "u1k") {
            chipArgs.type = ArchArgs::U1K;
        }
        if (arch_type == "u2k") {
            chipArgs.type = ArchArgs::U2K;
        }
        if (arch_type == "u4k") {
            chipArgs.type = ArchArgs::U4K;
        }
        if (chipArgs.type == ArchArgs::NONE)
            log_error("Unsupported FPGA type '%s'.\n", arch_type.c_str());
    }
    if (values.find("arch.package") != values.end()) {
        if (vm.count("package"))
            log_error("Overriding architecture is unsupported.\n");
        chipArgs.package = values["arch.package"].as_string();
    }

    if (chipArgs.type == ArchArgs::NONE) {
        chipArgs.type = ArchArgs::HX1K;
        chipArgs.package = "tq144";
    }

    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    for (auto &val : values)
        ctx->settings[ctx->id(val.first)] = val.second;

    ctx->settings[ctx->id("arch.package")] = ctx->archArgs().package;
    if (vm.count("promote-logic"))
        ctx->settings[id_promote_logic] = Property::State::S1;
    if (vm.count("no-promote-globals"))
        ctx->settings[id_no_promote_globals] = Property::State::S1;
    if (vm.count("opt-timing"))
        ctx->settings[id_opt_timing] = Property::State::S1;
    if (vm.count("pcf-allow-unconstrained"))
        ctx->settings[id_pcf_allow_unconstrained] = Property::State::S1;
    return ctx;
}

int main(int argc, char *argv[])
{
    Ice40CommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

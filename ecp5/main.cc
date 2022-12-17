/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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
#include "timing.h"
#include "util.h"

USING_NEXTPNR_NAMESPACE

class ECP5CommandHandler : public CommandHandler
{
  public:
    ECP5CommandHandler(int argc, char **argv);
    virtual ~ECP5CommandHandler(){};
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customAfterLoad(Context *ctx) override;
    void validate() override;
    void customBitstream(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

ECP5CommandHandler::ECP5CommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description ECP5CommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    if (Arch::is_available(ArchArgs::LFE5U_12F))
        specific.add_options()("12k", "set device type to LFE5U-12F");
    if (Arch::is_available(ArchArgs::LFE5U_25F))
        specific.add_options()("25k", "set device type to LFE5U-25F");
    if (Arch::is_available(ArchArgs::LFE5U_45F))
        specific.add_options()("45k", "set device type to LFE5U-45F");
    if (Arch::is_available(ArchArgs::LFE5U_85F))
        specific.add_options()("85k", "set device type to LFE5U-85F");
    if (Arch::is_available(ArchArgs::LFE5UM_25F))
        specific.add_options()("um-25k", "set device type to LFE5UM-25F");
    if (Arch::is_available(ArchArgs::LFE5UM_45F))
        specific.add_options()("um-45k", "set device type to LFE5UM-45F");
    if (Arch::is_available(ArchArgs::LFE5UM_85F))
        specific.add_options()("um-85k", "set device type to LFE5UM-85F");
    if (Arch::is_available(ArchArgs::LFE5UM5G_25F))
        specific.add_options()("um5g-25k", "set device type to LFE5UM5G-25F");
    if (Arch::is_available(ArchArgs::LFE5UM5G_45F))
        specific.add_options()("um5g-45k", "set device type to LFE5UM5G-45F");
    if (Arch::is_available(ArchArgs::LFE5UM5G_85F))
        specific.add_options()("um5g-85k", "set device type to LFE5UM5G-85F");

    specific.add_options()("package", po::value<std::string>(), "select device package (defaults to CABGA381)");
    specific.add_options()("speed", po::value<int>(), "select device speedgrade (6, 7 or 8)");

    specific.add_options()("basecfg", po::value<std::string>(),
                           "base chip configuration in Trellis text format (deprecated)");
    specific.add_options()("override-basecfg", po::value<std::string>(),
                           "base chip configuration in Trellis text format");
    specific.add_options()("textcfg", po::value<std::string>(), "textual configuration in Trellis format to write");

    specific.add_options()("lpf", po::value<std::vector<std::string>>(), "LPF pin constraint file(s)");
    specific.add_options()("lpf-allow-unconstrained", "don't require LPF file(s) to constrain all IO");

    specific.add_options()(
            "out-of-context",
            "disable IO buffer insertion and global promotion/routing, for building pre-routed blocks (experimental)");
    specific.add_options()("disable-router-lutperm", "don't allow the router to permute LUT inputs");

    return specific;
}
void ECP5CommandHandler::validate()
{
    if ((vm.count("25k") + vm.count("45k") + vm.count("85k")) > 1)
        log_error("Only one device type can be set\n");
}

void ECP5CommandHandler::customBitstream(Context *ctx)
{
    std::string basecfg;
    if (vm.count("basecfg")) {
        log_warning("--basecfg is deprecated.\nIf you are using a default baseconfig (from prjtrellis/misc/basecfgs), "
                    "these are now embedded in nextpnr - please remove --basecfg.\nIf you are using a non-standard "
                    "baseconfig in a special application, switch to using --override-basecfg.\n");
        basecfg = vm["basecfg"].as<std::string>();
    } else if (vm.count("override-basecfg")) {
        basecfg = vm["basecfg"].as<std::string>();
    }

    if (bool_or_default(ctx->settings, ctx->id("arch.ooc")) && vm.count("textcfg"))
        log_error("bitstream generation is not available in out-of-context mode (use --write to create a post-PnR JSON "
                  "design)\n");

    if (vm.count("textcfg")) {
        std::string textcfg = vm["textcfg"].as<std::string>();
        write_bitstream(ctx, basecfg, textcfg);
    }
}

static std::string speedString(ArchArgs::SpeedGrade speed)
{
    switch (speed) {
    case ArchArgs::SPEED_6:
        return "6";
    case ArchArgs::SPEED_7:
        return "7";
    case ArchArgs::SPEED_8:
        return "8";
    case ArchArgs::SPEED_8_5G:
        return "8";
    }
    return "";
}

std::unique_ptr<Context> ECP5CommandHandler::createContext(dict<std::string, Property> &values)
{
    ArchArgs chipArgs;
    chipArgs.type = ArchArgs::NONE;
    if (vm.count("12k"))
        chipArgs.type = ArchArgs::LFE5U_12F;
    if (vm.count("25k"))
        chipArgs.type = ArchArgs::LFE5U_25F;
    if (vm.count("45k"))
        chipArgs.type = ArchArgs::LFE5U_45F;
    if (vm.count("85k"))
        chipArgs.type = ArchArgs::LFE5U_85F;
    if (vm.count("um-25k"))
        chipArgs.type = ArchArgs::LFE5UM_25F;
    if (vm.count("um-45k"))
        chipArgs.type = ArchArgs::LFE5UM_45F;
    if (vm.count("um-85k"))
        chipArgs.type = ArchArgs::LFE5UM_85F;
    if (vm.count("um5g-25k"))
        chipArgs.type = ArchArgs::LFE5UM5G_25F;
    if (vm.count("um5g-45k"))
        chipArgs.type = ArchArgs::LFE5UM5G_45F;
    if (vm.count("um5g-85k"))
        chipArgs.type = ArchArgs::LFE5UM5G_85F;
    if (vm.count("package"))
        chipArgs.package = vm["package"].as<std::string>();

    if (vm.count("speed")) {
        int speed = vm["speed"].as<int>();
        switch (speed) {
        case 6:
            chipArgs.speed = ArchArgs::SPEED_6;
            break;
        case 7:
            chipArgs.speed = ArchArgs::SPEED_7;
            break;
        case 8:
            chipArgs.speed = ArchArgs::SPEED_8;
            break;
        default:
            log_error("Unsupported speed grade '%d'\n", speed);
        }
    } else {
        if (chipArgs.type == ArchArgs::LFE5UM5G_25F || chipArgs.type == ArchArgs::LFE5UM5G_45F ||
            chipArgs.type == ArchArgs::LFE5UM5G_85F) {
            chipArgs.speed = ArchArgs::SPEED_8;
        } else
            chipArgs.speed = ArchArgs::SPEED_6;
    }
    if (values.find("arch.name") != values.end()) {
        std::string arch_name = values["arch.name"].as_string();
        if (arch_name != "ecp5")
            log_error("Unsupported architecture '%s'.\n", arch_name.c_str());
    }
    if (values.find("arch.type") != values.end()) {
        std::string arch_type = values["arch.type"].as_string();
        if (chipArgs.type != ArchArgs::NONE)
            log_error("Overriding architecture is unsupported.\n");

        if (arch_type == "lfe5u_12f")
            chipArgs.type = ArchArgs::LFE5U_12F;
        if (arch_type == "lfe5u_25f")
            chipArgs.type = ArchArgs::LFE5U_25F;
        if (arch_type == "lfe5u_45f")
            chipArgs.type = ArchArgs::LFE5U_45F;
        if (arch_type == "lfe5u_85f")
            chipArgs.type = ArchArgs::LFE5U_85F;
        if (arch_type == "lfe5um_25f")
            chipArgs.type = ArchArgs::LFE5UM_25F;
        if (arch_type == "lfe5um_45f")
            chipArgs.type = ArchArgs::LFE5UM_45F;
        if (arch_type == "lfe5um_85f")
            chipArgs.type = ArchArgs::LFE5UM_85F;
        if (arch_type == "lfe5um5g_25f")
            chipArgs.type = ArchArgs::LFE5UM5G_25F;
        if (arch_type == "lfe5um5g_45f")
            chipArgs.type = ArchArgs::LFE5UM5G_45F;
        if (arch_type == "lfe5um5g_85f")
            chipArgs.type = ArchArgs::LFE5UM5G_85F;

        if (chipArgs.type == ArchArgs::NONE)
            log_error("Unsupported FPGA type '%s'.\n", arch_type.c_str());
    }
    if (values.find("arch.package") != values.end()) {
        if (vm.count("package"))
            log_error("Overriding architecture is unsupported.\n");
        chipArgs.package = values["arch.package"].as_string();
    }
    if (values.find("arch.speed") != values.end()) {
        std::string arch_speed = values["arch.speed"].as_string();
        if (arch_speed == "6")
            chipArgs.speed = ArchArgs::SPEED_6;
        else if (arch_speed == "7")
            chipArgs.speed = ArchArgs::SPEED_7;
        else if (arch_speed == "8")
            chipArgs.speed = ArchArgs::SPEED_8;
        else
            log_error("Unsupported speed '%s'.\n", arch_speed.c_str());
    }
    if (chipArgs.type == ArchArgs::NONE)
        chipArgs.type = ArchArgs::LFE5U_45F;

    if (chipArgs.package.empty()) {
        chipArgs.package = "CABGA381";
        log_warning("Use of default value for --package is deprecated. Please add '--package %s' to arguments.\n",
                    chipArgs.package.c_str());
    }

    if (chipArgs.type == ArchArgs::LFE5UM5G_25F || chipArgs.type == ArchArgs::LFE5UM5G_45F ||
        chipArgs.type == ArchArgs::LFE5UM5G_85F) {
        if (chipArgs.speed != ArchArgs::SPEED_8)
            log_error("Only speed grade 8 is available for 5G parts\n");
        else
            chipArgs.speed = ArchArgs::SPEED_8_5G;
    }

    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    for (auto &val : values)
        ctx->settings[ctx->id(val.first)] = val.second;
    ctx->settings[ctx->id("arch.package")] = ctx->archArgs().package;
    ctx->settings[ctx->id("arch.speed")] = speedString(ctx->archArgs().speed);
    if (vm.count("out-of-context"))
        ctx->settings[ctx->id("arch.ooc")] = 1;
    if (vm.count("disable-router-lutperm"))
        ctx->settings[ctx->id("arch.disable_router_lutperm")] = 1;
    return ctx;
}

void ECP5CommandHandler::customAfterLoad(Context *ctx)
{
    if (vm.count("lpf")) {
        std::vector<std::string> files = vm["lpf"].as<std::vector<std::string>>();
        for (const auto &filename : files) {
            std::ifstream in(filename);
            if (!in)
                log_error("failed to open LPF file '%s'\n", filename.c_str());
            if (!ctx->apply_lpf(filename, in))
                log_error("failed to parse LPF file '%s'\n", filename.c_str());
        }

        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_obuf") ||
                ci->type == ctx->id("$nextpnr_iobuf")) {
                if (!ci->attrs.count(id_LOC)) {
                    if (vm.count("lpf-allow-unconstrained"))
                        log_warning("IO '%s' is unconstrained in LPF and will be automatically placed\n",
                                    cell.first.c_str(ctx));
                    else
                        log_error("IO '%s' is unconstrained in LPF (override this error with "
                                  "--lpf-allow-unconstrained)\n",
                                  cell.first.c_str(ctx));
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    ECP5CommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

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
    specific.add_options()("device", po::value<std::string>(), "device name");
    specific.add_options()("list-devices", "list all supported device names");
    specific.add_options()("12k", "set device type to LFE5U-12F (deprecated)");
    specific.add_options()("25k", "set device type to LFE5U-25F (deprecated)");
    specific.add_options()("45k", "set device type to LFE5U-45F (deprecated)");
    specific.add_options()("85k", "set device type to LFE5U-85F (deprecated)");
    specific.add_options()("um-25k", "set device type to LFE5UM-25F (deprecated)");
    specific.add_options()("um-45k", "set device type to LFE5UM-45F (deprecated)");
    specific.add_options()("um-85k", "set device type to LFE5UM-85F (deprecated)");
    specific.add_options()("um5g-25k", "set device type to LFE5UM5G-25F (deprecated)");
    specific.add_options()("um5g-45k", "set device type to LFE5UM5G-45F (deprecated)");
    specific.add_options()("um5g-85k", "set device type to LFE5UM5G-85F (deprecated)");

    specific.add_options()("package", po::value<std::string>(), "select device package (defaults to CABGA381)  (deprecated)");
    specific.add_options()("speed", po::value<int>(), "select device speedgrade (6, 7 or 8)  (deprecated)");

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
    if ((vm.count("12k") + vm.count("25k") + vm.count("45k") + vm.count("85k") + 
         vm.count("um-25k") + vm.count("um-45k") + vm.count("um-85k") + 
         vm.count("um5g-25k") + vm.count("um5g-45k") + vm.count("um5g-85k") + 
         vm.count("device")) > 1)
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


std::unique_ptr<Context> ECP5CommandHandler::createContext(dict<std::string, Property> &values)
{
    ArchArgs chipArgs;
    if (vm.count("list-devices")) {
        Arch::list_devices();
        exit(0);
    }
    if (vm.count("device"))
        chipArgs.device = vm["device"].as<std::string>();
    else if (vm.count("12k"))
        chipArgs.device = "LFE5U-12F";
    else if (vm.count("25k"))
        chipArgs.device = "LFE5U-25F";
    else if (vm.count("45k"))
        chipArgs.device = "LFE5U-45F";
    else if (vm.count("85k"))
        chipArgs.device = "LFE5U-85F";
    else if (vm.count("um-25k"))
        chipArgs.device = "LFE5UM-25F";
    else if (vm.count("um-45k"))
        chipArgs.device = "LFE5UM-45F";
    else if (vm.count("um-85k"))
        chipArgs.device = "LFE5UM-85F";
    else if (vm.count("um5g-25k"))
        chipArgs.device = "LFE5UM5G-25F";
    else if (vm.count("um5g-45k"))
        chipArgs.device = "LFE5UM5G-45F";
    else if (vm.count("um5g-85k"))
        chipArgs.device = "LFE5UM5G-85F";

    if (values.find("arch.name") != values.end()) {
        std::string arch_name = values["arch.name"].as_string();
        if (arch_name != "ecp5")
            log_error("Unsupported architecture '%s'.\n", arch_name.c_str());
    }
    if (values.find("arch.type") != values.end()) {
        std::string arch_type = values["arch.type"].as_string();
        if (!chipArgs.device.empty())
            log_error("Overriding architecture is unsupported.\n");
        chipArgs.device = arch_type;
    }

    if (chipArgs.device.empty())
        chipArgs.device = "LFE5UM-45F";

    if (!vm.count("device")) {
        if (vm.count("speed")) {
            int speed = vm["speed"].as<int>();
            switch (speed) {
            case 6:
                chipArgs.device += "-6";
                break;
            case 7:
                chipArgs.device += "-7";
                break;
            case 8:
                chipArgs.device += "-8";
                break;
            default:
                log_error("Unsupported speed grade '%d'\n", speed);
            }
        } else {
            if (strstr(chipArgs.device.c_str(),"LFE5UM5G")) {
                chipArgs.device += "-8";
            } else
                chipArgs.device += "-6";
        }
        if (vm.count("package")) {
            std::string package = vm["package"].as<std::string>();
            if (strcasecmp(package.c_str(), "csfBGA285")==0) {
                chipArgs.device += "MG285C";
            } else if (strcasecmp(package.c_str(), "caBGA256")==0) {
                chipArgs.device += "BG256C";
            } else if (strcasecmp(package.c_str(), "caBGA381")==0) {
                chipArgs.device += "BG381C";
            } else if (strcasecmp(package.c_str(), "caBGA554")==0) {
                chipArgs.device += "BG554C";
            } else if (strcasecmp(package.c_str(), "caBGA756")==0) {
                chipArgs.device += "BG756C";
            } else {
                log_error("Unsupported package '%s'\n", package.c_str());
            }
        }
        else {
            chipArgs.device += "BG381C";
            log_warning("Use of default value for --package is deprecated. Please add '--package caBGA381' to arguments.\n");
        }
    }

    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    for (auto &val : values)
        ctx->settings[ctx->id(val.first)] = val.second;
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

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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
    std::unique_ptr<Context> createContext() override;
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
    specific.add_options()("25k", "set device type to LFE5U-25F");
    specific.add_options()("45k", "set device type to LFE5U-45F");
    specific.add_options()("85k", "set device type to LFE5U-85F");
    specific.add_options()("um-25k", "set device type to LFE5UM-25F");
    specific.add_options()("um-45k", "set device type to LFE5UM-45F");
    specific.add_options()("um-85k", "set device type to LFE5UM-85F");
    specific.add_options()("um5g-25k", "set device type to LFE5UM5G-25F");
    specific.add_options()("um5g-45k", "set device type to LFE5UM5G-45F");
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

    std::string textcfg;
    if (vm.count("textcfg"))
        textcfg = vm["textcfg"].as<std::string>();

    write_bitstream(ctx, basecfg, textcfg);
}

std::unique_ptr<Context> ECP5CommandHandler::createContext()
{
    chipArgs.type = ArchArgs::LFE5U_45F;

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
    else
        chipArgs.package = "CABGA381";
    if (chipArgs.type == ArchArgs::LFE5UM5G_25F || chipArgs.type == ArchArgs::LFE5UM5G_45F ||
        chipArgs.type == ArchArgs::LFE5UM5G_85F) {
        if (vm.count("speed") && vm["speed"].as<int>() != 8)
            log_error("Only speed grade 8 is available for 5G parts\n");
        chipArgs.speed = ArchArgs::SPEED_8_5G;
    } else {
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
            chipArgs.speed = ArchArgs::SPEED_6;
        }
    }

    return std::unique_ptr<Context>(new Context(chipArgs));
}

void ECP5CommandHandler::customAfterLoad(Context *ctx)
{
    if (vm.count("lpf")) {
        std::vector<std::string> files = vm["lpf"].as<std::vector<std::string>>();
        for (const auto &filename : files) {
            std::ifstream in(filename);
            if (!in)
                log_error("failed to open LPF file '%s'\n", filename.c_str());
            if (!ctx->applyLPF(filename, in))
                log_error("failed to parse LPF file '%s'\n", filename.c_str());
        }

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_obuf") ||
                ci->type == ctx->id("$nextpnr_iobuf")) {
                if (!ci->attrs.count(ctx->id("LOC"))) {
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

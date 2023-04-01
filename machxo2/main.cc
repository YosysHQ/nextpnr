/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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

#ifdef MAIN_EXECUTABLE

#include <fstream>
#include "bitstream.h"
#include "command.h"
#include "design_utils.h"
#include "log.h"
#include "timing.h"

USING_NEXTPNR_NAMESPACE

class MachXO2CommandHandler : public CommandHandler
{
  public:
    MachXO2CommandHandler(int argc, char **argv);
    virtual ~MachXO2CommandHandler(){};
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customAfterLoad(Context *ctx) override;
    void customBitstream(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

MachXO2CommandHandler::MachXO2CommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description MachXO2CommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    specific.add_options()("device", po::value<std::string>(), "device name");
    specific.add_options()("list-devices", "list all supported device names");
    specific.add_options()("textcfg", po::value<std::string>(), "textual configuration in Trellis format to write");

    specific.add_options()("lpf", po::value<std::vector<std::string>>(), "LPF pin constraint file(s)");
    specific.add_options()("lpf-allow-unconstrained", "don't require LPF file(s) to constrain all IO");

    specific.add_options()("disable-router-lutperm", "don't allow the router to permute LUT inputs");

    return specific;
}

void MachXO2CommandHandler::customBitstream(Context *ctx)
{
    std::string textcfg;
    if (vm.count("textcfg"))
        textcfg = vm["textcfg"].as<std::string>();

    write_bitstream(ctx, textcfg);
}

std::unique_ptr<Context> MachXO2CommandHandler::createContext(dict<std::string, Property> &values)
{
    ArchArgs chipArgs;
    if (vm.count("list-devices")) {
        Arch::list_devices();
        exit(0);
    }
    if (!vm.count("device")) {
        log_error("device must be specified on the command line (e.g. --device LCMXO2-1200HC-4SG32C)\n");
    }
    chipArgs.device = vm["device"].as<std::string>();
    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    if (vm.count("disable-router-lutperm"))
        ctx->settings[ctx->id("arch.disable_router_lutperm")] = 1;
    return ctx;
}

void MachXO2CommandHandler::customAfterLoad(Context *ctx)
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
    MachXO2CommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

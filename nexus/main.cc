/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  gatecat <gatecat@ds0.me>
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
#include "command.h"
#include "design_utils.h"
#include "jsonwrite.h"
#include "log.h"
#include "timing.h"

USING_NEXTPNR_NAMESPACE

class NexusCommandHandler : public CommandHandler
{
  public:
    NexusCommandHandler(int argc, char **argv);
    virtual ~NexusCommandHandler(){};
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customBitstream(Context *ctx) override;
    void customAfterLoad(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

NexusCommandHandler::NexusCommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description NexusCommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    specific.add_options()("device", po::value<std::string>(), "device name");
    specific.add_options()("list-devices", "list all supported device names");
    specific.add_options()("fasm", po::value<std::string>(), "fasm file to write");
    specific.add_options()("pdc", po::value<std::string>(), "physical constraints file");
    specific.add_options()("no-post-place-opt", "disable post-place repacking (debugging use only)");
    specific.add_options()("no-pack-lutff", "disable packing (clustering) LUTs and FFs together");
    specific.add_options()("carry-lutff-ratio", po::value<float>(),
                           "ratio of FFs to be added to carry-chain LUT clusters");
    specific.add_options()("estimate-delay-mult", po::value<int>(), "multiplier for the estimate delay");

    return specific;
}

void NexusCommandHandler::customBitstream(Context *ctx)
{
    if (vm.count("fasm")) {
        std::string filename = vm["fasm"].as<std::string>();
        std::ofstream out(filename);
        if (!out)
            log_error("Failed to open output FASM file %s.\n", filename.c_str());
        ctx->write_fasm(out);
    }
}

std::unique_ptr<Context> NexusCommandHandler::createContext(dict<std::string, Property> &values)
{
    ArchArgs chipArgs;
    if (vm.count("list-devices")) {
        Arch::list_devices();
        exit(0);
    }
    if (!vm.count("device")) {
        log_error("device must be specified on the command line (e.g. --device LIFCL-40-9BG400CES)\n");
    }
    chipArgs.device = vm["device"].as<std::string>();
    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    if (vm.count("no-post-place-opt"))
        ctx->settings[id_no_post_place_opt] = Property::State::S1;
    if (vm.count("no-pack-lutff"))
        ctx->settings[id_no_pack_lutff] = Property::State::S1;
    if (vm.count("carry-lutff-ratio")) {
        float ratio = vm["carry-lutff-ratio"].as<float>();
        if (ratio < 0.0f || ratio > 1.0f) {
            log_error("Carry LUT+FF packing ration must be between 0.0 and 1.0");
        }
        ctx->settings[id_carry_lutff_ratio] = ratio;
    }
    if (vm.count("estimate-delay-mult"))
        ctx->settings[ctx->id("estimate-delay-mult")] = vm["estimate-delay-mult"].as<int>();
    return ctx;
}

void NexusCommandHandler::customAfterLoad(Context *ctx)
{
    if (vm.count("pdc")) {
        std::string filename = vm["pdc"].as<std::string>();
        std::ifstream in(filename);
        if (!in)
            log_error("Failed to open input PDC file %s.\n", filename.c_str());
        ctx->read_pdc(in);
    }
}

int main(int argc, char *argv[])
{
    NexusCommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

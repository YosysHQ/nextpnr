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
#include "command.h"
#include "design_utils.h"
#include "log.h"
#include "timing.h"

USING_NEXTPNR_NAMESPACE

class GenericCommandHandler : public CommandHandler
{
  public:
    GenericCommandHandler(int argc, char **argv);
    virtual ~GenericCommandHandler(){};
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customBitstream(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

GenericCommandHandler::GenericCommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description GenericCommandHandler::getArchOptions()
{
    std::string all_uarches = ViaductArch::list();
    std::string uarch_help = stringf("viaduct micro-arch to use (available: %s)", all_uarches.c_str());
    po::options_description specific("Architecture specific options");
    specific.add_options()("uarch", po::value<std::string>(), uarch_help.c_str());
    specific.add_options()("no-iobs", "disable automatic IO buffer insertion");
    specific.add_options()("vopt,o", po::value<std::vector<std::string>>(), "options to pass to the viaduct uarch");

    return specific;
}

void GenericCommandHandler::customBitstream(Context *ctx) {}

std::unique_ptr<Context> GenericCommandHandler::createContext(dict<std::string, Property> &values)
{
    ArchArgs chipArgs;
    if (values.find("arch.name") != values.end()) {
        std::string arch_name = values["arch.name"].as_string();
        if (arch_name != "generic")
            log_error("Unsupported architecture '%s'.\n", arch_name.c_str());
    }
    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    if (vm.count("no-iobs"))
        ctx->settings[ctx->id("disable_iobs")] = Property::State::S1;
    if (vm.count("uarch")) {
        std::string uarch_name = vm["uarch"].as<std::string>();
        dict<std::string, std::string> args; // TODO
        if (vm.count("vopt")) {
            std::vector<std::string> options = vm["vopt"].as<std::vector<std::string>>();
            for (const auto &opt : options) {
                size_t epos = opt.find('=');
                if (epos == std::string::npos)
                    args[opt] = "";
                else
                    args[opt.substr(0, epos)] = opt.substr(epos + 1);
            }
        }
        auto uarch = ViaductArch::create(uarch_name, args);
        if (!uarch) {
            std::string all_uarches = ViaductArch::list();
            log_error("Unknown viaduct uarch '%s'; available options: '%s'\n", uarch_name.c_str(), all_uarches.c_str());
        }
        ctx->uarch = std::move(uarch);
        if (vm.count("gui"))
            ctx->uarch->with_gui = true;
        ctx->uarch->init(ctx.get());
    } else if (vm.count("vopt")) {
        log_error("Viaduct options passed in non-viaduct mode!\n");
    } else if (vm.count("gui")) {
        log_error("nextpnr-generic GUI only supported in viaduct mode!\n");
    }
    return ctx;
}

int main(int argc, char *argv[])
{
    GenericCommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

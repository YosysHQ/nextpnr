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

class HimbaechelCommandHandler : public CommandHandler
{
  public:
    HimbaechelCommandHandler(int argc, char **argv);
    virtual ~HimbaechelCommandHandler(){};
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customBitstream(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

HimbaechelCommandHandler::HimbaechelCommandHandler(int argc, char **argv) : CommandHandler(argc, argv)
{
    init_share_dirname();
}

po::options_description HimbaechelCommandHandler::getArchOptions()
{
    std::string all_uarches = HimbaechelArch::list();
    std::string uarch_help = stringf("himbächel micro-arch to use (available: %s)", all_uarches.c_str());
    po::options_description specific("Architecture specific options");
    specific.add_options()("device", po::value<std::string>(), "name of device to use");
    specific.add_options()("chipdb", po::value<std::string>(), "override path to chip database file");
    specific.add_options()("list-uarch", "list included uarches");
    specific.add_options()("vopt,o", po::value<std::vector<std::string>>(), "options to pass to the himbächel uarch");

    return specific;
}

void HimbaechelCommandHandler::customBitstream(Context *ctx) {}

std::unique_ptr<Context> HimbaechelCommandHandler::createContext(dict<std::string, Property> &values)
{
    ArchArgs chipArgs;
    if (values.find("arch.name") != values.end()) {
        std::string arch_name = values["arch.name"].as_string();
        if (arch_name != "himbaechel")
            log_error("Unsupported architecture '%s'.\n", arch_name.c_str());
    }
    if (vm.count("list-uarch")) {
        std::string uarches = HimbaechelArch::list();
        log_info("Supported uarches: %s\n", uarches.c_str());
        exit(0);
    }
    if (!vm.count("device"))
        log_error("device must be specified\n");
    chipArgs.device = vm["device"].as<std::string>();

    if (vm.count("chipdb")) {
        chipArgs.chipdb_override = vm["chipdb"].as<std::string>();
    }

    if (vm.count("vopt")) {
        std::vector<std::string> options = vm["vopt"].as<std::vector<std::string>>();
        for (const auto &opt : options) {
            size_t epos = opt.find('=');
            if (epos == std::string::npos)
                chipArgs.options[opt] = "";
            else
                chipArgs.options[opt.substr(0, epos)] = opt.substr(epos + 1);
        }
    }
    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    if (vm.count("gui"))
        ctx->uarch->with_gui = true;
    ctx->uarch->init(ctx.get());
    ctx->late_init();
    return ctx;
}

int main(int argc, char *argv[])
{
    HimbaechelCommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

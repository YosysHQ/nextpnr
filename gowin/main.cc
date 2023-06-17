/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2020  Pepijn de Vos <pepijn@symbioticeda.com>
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
#include <locale>
#include <regex>
#include "command.h"
#include "design_utils.h"
#include "log.h"
#include "timing.h"

USING_NEXTPNR_NAMESPACE

class GowinCommandHandler : public CommandHandler
{
  public:
    GowinCommandHandler(int argc, char **argv);
    virtual ~GowinCommandHandler(){};
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customAfterLoad(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

GowinCommandHandler::GowinCommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description GowinCommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    specific.add_options()("device", po::value<std::string>(), "device name");
    specific.add_options()("family", po::value<std::string>(), "family name");
    specific.add_options()("cst", po::value<std::string>(), "physical constraints file");
    specific.add_options()("enable-globals", "enable separate routing of the clocks");
    specific.add_options()("disable-globals", "disable separate routing of the clocks");
    specific.add_options()("enable-auto-longwires", "automatic detection and routing of long wires");
    return specific;
}

std::unique_ptr<Context> GowinCommandHandler::createContext(dict<std::string, Property> &values)
{
    if (!vm.count("device")) {
        log_error("The device must be specified\n");
    }

    std::regex devicere = std::regex("GW1N([SZ]?)[A-Z]*-(LV|UV|UX)([0-9])(C?).*");
    std::smatch match;
    std::string device = vm["device"].as<std::string>();
    bool GW2 = device == "GW2A-LV18PG256C8/I7";

    if (!GW2 && !std::regex_match(device, match, devicere)) {
        log_error("Invalid device %s\n", device.c_str());
    }
    ArchArgs chipArgs;
    chipArgs.gui = vm.count("gui") != 0;
    if (vm.count("family")) {
        chipArgs.family = vm["family"].as<std::string>();
    } else {
        if (!GW2) {
            char buf[36];
            // GW1N and GW1NR variants share the same database.
            // Most Gowin devices are a System in Package with some SDRAM wirebonded to a GPIO bank.
            // However, it appears that the S series with embedded ARM core are unique silicon.
            snprintf(buf, 36, "GW1N%s-%s", match[1].str().c_str(), match[3].str().c_str());
            chipArgs.family = buf;
        } else {
            chipArgs.family = "GW2A-18";
        }
    }
    if (!GW2) {
        chipArgs.partnumber = match[0];
    } else {
        chipArgs.partnumber = device;
    }

    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    // routing options
    ctx->settings[ctx->id("arch.enable-globals")] = 1;
    ctx->settings[ctx->id("arch.enable-auto-longwires")] = 0;
    if (vm.count("disable-globals")) {
        ctx->settings[ctx->id("arch.enable-globals")] = 0;
    }
    if (vm.count("enable-auto-longwires")) {
        ctx->settings[ctx->id("arch.enable-auto-longwires")] = 1;
    }
    // XXX disable clock lines for now
    if (GW2) {
        ctx->settings[ctx->id("arch.enable-globals")] = 0;
    }
    return ctx;
}

void GowinCommandHandler::customAfterLoad(Context *ctx)
{
    if (vm.count("cst")) {
        std::string filename = vm["cst"].as<std::string>();
        std::ifstream in(filename);
        if (!in)
            log_error("Failed to open input CST file %s.\n", filename.c_str());
        ctx->read_cst(in);
    }
}

int main(int argc, char *argv[])
{
    GowinCommandHandler handler(argc, argv);
    return handler.exec();
}
#endif

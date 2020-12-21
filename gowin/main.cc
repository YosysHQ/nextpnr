/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Wolf <claire@symbioticeda.com>
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
    std::unique_ptr<Context> createContext(std::unordered_map<std::string, Property> &values) override;
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
    specific.add_options()("cst", po::value<std::string>(), "physical constraints file");
    return specific;
}

std::unique_ptr<Context> GowinCommandHandler::createContext(std::unordered_map<std::string, Property> &values)
{
    std::regex devicere = std::regex("GW1N([A-Z]*)-(LV|UV)([0-9])([A-Z]{2}[0-9]+)(C[0-9]/I[0-9])");
    std::smatch match;
    std::string device = vm["device"].as<std::string>();
    if(!std::regex_match(device, match, devicere)) {
        log_error("Invalid device %s\n", device.c_str());
    }
    ArchArgs chipArgs;
    char buf[32];
    snprintf(buf, 32, "GW1N%s-%s", match[1].str().c_str(), match[3].str().c_str());
    chipArgs.device = buf;
    snprintf(buf, 32, "GW1N-%s", match[3].str().c_str());
    chipArgs.family = buf;
    chipArgs.package = match[4];
    chipArgs.speed = match[5];
    return std::unique_ptr<Context>(new Context(chipArgs));
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

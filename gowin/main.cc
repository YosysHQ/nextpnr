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
    specific.add_options()("family", po::value<std::string>(), "device family");
    specific.add_options()("package", po::value<std::string>(), "device package");
    specific.add_options()("speed", po::value<std::string>(), "device speed grade");
    specific.add_options()("pdc", po::value<std::string>(), "physical constraints file");
    return specific;
}

std::unique_ptr<Context> GowinCommandHandler::createContext(std::unordered_map<std::string, Property> &values)
{
    ArchArgs chipArgs;
    chipArgs.device = vm["device"].as<std::string>();
    return std::unique_ptr<Context>(new Context(chipArgs));
}

void GowinCommandHandler::customAfterLoad(Context *ctx)
{
    // if (vm.count("pdc")) {
    //     std::string filename = vm["pdc"].as<std::string>();
    //     std::ifstream in(filename);
    //     if (!in)
    //         log_error("Failed to open input PDC file %s.\n", filename.c_str());
    //     ctx->read_pdc(in);
    // }
}

int main(int argc, char *argv[])
{
    GowinCommandHandler handler(argc, argv);
    return handler.exec();
}
#endif

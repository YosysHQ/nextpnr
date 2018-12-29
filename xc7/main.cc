/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
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
#include "jsonparse.h"
#include "log.h"
#include "pcf.h"
#include "timing.h"
#include "xdl.h"

USING_NEXTPNR_NAMESPACE

class Xc7CommandHandler : public CommandHandler
{
  public:
    Xc7CommandHandler(int argc, char **argv);
    virtual ~Xc7CommandHandler(){};
    std::unique_ptr<Context> createContext() override;
    void setupArchContext(Context *ctx) override;
    void validate() override;
    void customAfterLoad(Context *ctx) override;
    void customBitstream(Context *ctx) override;

  protected:
    po::options_description getArchOptions();
};

Xc7CommandHandler::Xc7CommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description Xc7CommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    specific.add_options()("z020", "set device type to xc7z020");
    specific.add_options()("vx980", "set device type to xc7v980");
    specific.add_options()("package", po::value<std::string>(), "set device package");
    specific.add_options()("pcf", po::value<std::string>(), "PCF constraints file to ingest");
    specific.add_options()("xdl", po::value<std::string>(), "XDL file to write");
    //    specific.add_options()("tmfuzz", "run path delay estimate fuzzer");
    return specific;
}
void Xc7CommandHandler::validate()
{
    conflicting_options(vm, "read", "json");
    //    if ((vm.count("lp384") + vm.count("lp1k") + vm.count("lp8k") + vm.count("hx1k") + vm.count("hx8k") +
    //         vm.count("up5k")) > 1)
    //        log_error("Only one device type can be set\n");
}

void Xc7CommandHandler::customAfterLoad(Context *ctx)
{
    if (vm.count("pcf")) {
        std::string filename = vm["pcf"].as<std::string>();
        std::ifstream pcf(filename);
        if (!apply_pcf(ctx, filename, pcf))
            log_error("Loading PCF failed.\n");
    }
}
void Xc7CommandHandler::customBitstream(Context *ctx)
{
    if (vm.count("xdl")) {
        std::string filename = vm["xdl"].as<std::string>();
        std::ofstream f(filename);
        write_xdl(ctx, f);
    }
}

void Xc7CommandHandler::setupArchContext(Context *ctx)
{
}

std::unique_ptr<Context> Xc7CommandHandler::createContext()
{
    if (vm.count("z020")) {
        chipArgs.type = ArchArgs::Z020;
        chipArgs.package = "clg400";
    }

    if (vm.count("vx980")) {
        chipArgs.type = ArchArgs::VX980;
        chipArgs.package = "ffg1926";
    }


    if (chipArgs.type == ArchArgs::NONE) {
        chipArgs.type = ArchArgs::Z020;
        chipArgs.package = "clg400";
    }

    if (vm.count("package"))
        chipArgs.package = vm["package"].as<std::string>();

    return std::unique_ptr<Context>(new Context(chipArgs));
}

int main(int argc, char *argv[])
{
    Xc7CommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

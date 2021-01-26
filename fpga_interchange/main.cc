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
#include "jsonwrite.h"
#include "log.h"
#include "timing.h"

USING_NEXTPNR_NAMESPACE

class FpgaInterchangeCommandHandler : public CommandHandler
{
  public:
    FpgaInterchangeCommandHandler(int argc, char **argv);
    virtual ~FpgaInterchangeCommandHandler(){};
    std::unique_ptr<Context> createContext(std::unordered_map<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customBitstream(Context *ctx) override;
    void customAfterLoad(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

FpgaInterchangeCommandHandler::FpgaInterchangeCommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description FpgaInterchangeCommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    specific.add_options()("chipdb", po::value<std::string>(), "name of chip database binary");
    specific.add_options()("xdc", po::value<std::vector<std::string>>(), "XDC-style constraints file");
    specific.add_options()("phys", po::value<std::string>(), "FPGA interchange Physical netlist to write");

    return specific;
}

void FpgaInterchangeCommandHandler::customBitstream(Context *ctx)
{
    if (vm.count("phys")) {
        std::string filename = vm["phys"].as<std::string>();
        ctx->writePhysicalNetlist(filename);
    }
}

std::unique_ptr<Context> FpgaInterchangeCommandHandler::createContext(std::unordered_map<std::string, Property> &values)
{
    ArchArgs chipArgs;
    if (!vm.count("chipdb")) {
        log_error("chip database binary must be provided\n");
    }
    chipArgs.chipdb = vm["chipdb"].as<std::string>();
    return std::unique_ptr<Context>(new Context(chipArgs));
}

void FpgaInterchangeCommandHandler::customAfterLoad(Context *ctx)
{
}

int main(int argc, char *argv[])
{
    FpgaInterchangeCommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

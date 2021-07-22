/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2021  Symbiflow Authors
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

#include <chrono>
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
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
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
    specific.add_options()("xdc", po::value<std::vector<std::string>>(), "XDC-style constraints file to read");
    specific.add_options()("netlist", po::value<std::string>(), "FPGA interchange logical netlist to read");
    specific.add_options()("phys", po::value<std::string>(), "FPGA interchange Physical netlist to write");
    specific.add_options()("package", po::value<std::string>(), "Package to use");
    specific.add_options()("rebuild-lookahead", "Ignore lookahead cache and rebuild");
    specific.add_options()("dont-write-lookahead", "Don't write the lookahead file");
    specific.add_options()("disable-lut-mapping-cache", "Disable caching of LUT mapping solutions in site router");

    return specific;
}

void FpgaInterchangeCommandHandler::customBitstream(Context *ctx)
{
    if (vm.count("phys")) {
        std::string filename = vm["phys"].as<std::string>();
        ctx->write_physical_netlist(filename);
    }
}

std::unique_ptr<Context> FpgaInterchangeCommandHandler::createContext(dict<std::string, Property> &values)
{
    auto start = std::chrono::high_resolution_clock::now();

    ArchArgs chipArgs;
    chipArgs.rebuild_lookahead = vm.count("rebuild_lookahead") != 0;
    chipArgs.dont_write_lookahead = vm.count("dont_write_lookahead") != 0;
    chipArgs.disable_lut_mapping_cache = vm.count("disable-lut-mapping-cache") != 0;

    if (!vm.count("chipdb")) {
        log_error("chip database binary must be provided\n");
    }
    chipArgs.chipdb = vm["chipdb"].as<std::string>();
    if (vm.count("package")) {
        chipArgs.package = vm["package"].as<std::string>();
    }

    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));

    if (vm.count("verbose")) {
        ctx->verbose = true;
    }
    if (vm.count("debug")) {
        ctx->verbose = true;
        ctx->debug = true;
    }

    ctx->init();

    if (vm.count("netlist")) {
        ctx->read_logical_netlist(vm["netlist"].as<std::string>());
    }

    if (vm.count("xdc")) {
        for (auto &x : vm["xdc"].as<std::vector<std::string>>()) {
            ctx->parse_xdc(x);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    log_info("createContext time %.02fs\n", std::chrono::duration<float>(end - start).count());

    return ctx;
}

void FpgaInterchangeCommandHandler::customAfterLoad(Context *ctx) {}

int main(int argc, char *argv[])
{
    FpgaInterchangeCommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

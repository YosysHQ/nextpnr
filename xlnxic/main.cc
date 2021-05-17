/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

class XilinxCommandHandler : public CommandHandler
{
  public:
    XilinxCommandHandler(int argc, char **argv);
    virtual ~XilinxCommandHandler(){};
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customBitstream(Context *ctx) override;
    void customAfterLoad(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

XilinxCommandHandler::XilinxCommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description XilinxCommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    specific.add_options()("chipdb", po::value<std::string>(), "name of chip database binary");
    specific.add_options()("package", po::value<std::string>(), "name of device package");
    specific.add_options()("xdc", po::value<std::string>(), "XDC constraints file");
    specific.add_options()("write-log", po::value<std::string>(), "logical interchange netlist to write");
    specific.add_options()("write-phys", po::value<std::string>(), "logical interchange netlist to write");
    return specific;
}

void XilinxCommandHandler::customBitstream(Context *ctx)
{
    if (vm.count("write-log"))
        ctx->write_logical(vm["write-log"].as<std::string>());
    if (vm.count("write-phys"))
        ctx->write_physical(vm["write-phys"].as<std::string>());
}

std::unique_ptr<Context> XilinxCommandHandler::createContext(dict<std::string, Property> &values)
{
    ArchArgs chipArgs;

    if (!vm.count("chipdb")) {
        log_error("chip database binary must be provided\n");
    }
    chipArgs.chipdb = vm["chipdb"].as<std::string>();

    if (vm.count("package"))
        chipArgs.package = vm["package"].as<std::string>();

    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));

    if (vm.count("verbose")) {
        ctx->verbose = true;
    }
    if (vm.count("debug")) {
        ctx->verbose = true;
        ctx->debug = true;
    }

    ctx->late_init();

    return ctx;
}

void XilinxCommandHandler::customAfterLoad(Context *ctx)
{
    if (vm.count("xdc")) {
        std::string filename = vm["xdc"].as<std::string>();
        std::ifstream in(filename);
        if (!in)
            log_error("Failed to open input XDC file %s.\n", filename.c_str());
        ctx->read_xdc(in);
    }
}

int main(int argc, char *argv[])
{
    XilinxCommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

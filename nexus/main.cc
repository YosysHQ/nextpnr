/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
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
    std::unique_ptr<Context> createContext(std::unordered_map<std::string, Property> &values) override;
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
    specific.add_options()("chipdb", po::value<std::string>(), "name of chip database binary");
    specific.add_options()("device", po::value<std::string>(), "device name");
    specific.add_options()("fasm", po::value<std::string>(), "fasm file to write");
    specific.add_options()("pdc", po::value<std::string>(), "physical constraints file");
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

std::unique_ptr<Context> NexusCommandHandler::createContext(std::unordered_map<std::string, Property> &values)
{
    ArchArgs chipArgs;
    chipArgs.chipdb = vm["chipdb"].as<std::string>();
    chipArgs.device = vm["device"].as<std::string>();
    return std::unique_ptr<Context>(new Context(chipArgs));
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

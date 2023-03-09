/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
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
#include "bitstream.h"
#include "command.h"
#include "design_utils.h"
#include "log.h"
#include "timing.h"

USING_NEXTPNR_NAMESPACE

class MachXO2CommandHandler : public CommandHandler
{
  public:
    MachXO2CommandHandler(int argc, char **argv);
    virtual ~MachXO2CommandHandler(){};
    std::unique_ptr<Context> createContext(dict<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customBitstream(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

MachXO2CommandHandler::MachXO2CommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description MachXO2CommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    specific.add_options()("device", po::value<std::string>(), "device name");
    specific.add_options()("list-devices", "list all supported device names");
    specific.add_options()("textcfg", po::value<std::string>(), "textual configuration in Trellis format to write");
    // specific.add_options()("lpf", po::value<std::vector<std::string>>(), "LPF pin constraint file(s)");

    return specific;
}

void MachXO2CommandHandler::customBitstream(Context *ctx)
{
    std::string textcfg;
    if (vm.count("textcfg"))
        textcfg = vm["textcfg"].as<std::string>();

    write_bitstream(ctx, textcfg);
}

std::unique_ptr<Context> MachXO2CommandHandler::createContext(dict<std::string, Property> &values)
{
    ArchArgs chipArgs;
    if (vm.count("list-devices")) {
        Arch::list_devices();
        exit(0);
    }
    if (!vm.count("device")) {
        log_error("device must be specified on the command line (e.g. --device LCMXO2-1200HC-4SG32C)\n");
    }
    chipArgs.device = vm["device"].as<std::string>();
    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    return ctx;
}

int main(int argc, char *argv[])
{
    MachXO2CommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

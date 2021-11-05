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
    if (Arch::is_available(ArchArgs::LCMXO2_256HC))
        specific.add_options()("256", "set device type to LCMXO2-256HC");
    if (Arch::is_available(ArchArgs::LCMXO2_640HC))
        specific.add_options()("640", "set device type to LCMXO2-640HC");
    if (Arch::is_available(ArchArgs::LCMXO2_1200HC))
        specific.add_options()("1200", "set device type to LCMXO2-1200HC");
    if (Arch::is_available(ArchArgs::LCMXO2_2000HC))
        specific.add_options()("2000", "set device type to LCMXO2-2000HC");
    if (Arch::is_available(ArchArgs::LCMXO2_4000HC))
        specific.add_options()("4000", "set device type to LCMXO2-4000HC");
    if (Arch::is_available(ArchArgs::LCMXO2_7000HC))
        specific.add_options()("7000", "set device type to LCMXO2-7000HC");

    specific.add_options()("package", po::value<std::string>(), "select device package");
    specific.add_options()("speed", po::value<int>(), "select device speedgrade (1 to 6 inclusive)");

    specific.add_options()("override-basecfg", po::value<std::string>(),
                           "base chip configuration in Trellis text format");
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
    chipArgs.type = ArchArgs::NONE;
    if (vm.count("256"))
        chipArgs.type = ArchArgs::LCMXO2_256HC;
    if (vm.count("640"))
        chipArgs.type = ArchArgs::LCMXO2_640HC;
    if (vm.count("1200"))
        chipArgs.type = ArchArgs::LCMXO2_1200HC;
    if (vm.count("2000"))
        chipArgs.type = ArchArgs::LCMXO2_2000HC;
    if (vm.count("4000"))
        chipArgs.type = ArchArgs::LCMXO2_4000HC;
    if (vm.count("7000"))
        chipArgs.type = ArchArgs::LCMXO2_7000HC;
    if (vm.count("package"))
        chipArgs.package = vm["package"].as<std::string>();

    if (values.find("arch.name") != values.end()) {
        std::string arch_name = values["arch.name"].as_string();
        if (arch_name != "machxo2")
            log_error("Unsuported architecture '%s'.\n", arch_name.c_str());
    }

    auto ctx = std::unique_ptr<Context>(new Context(chipArgs));
    return ctx;
}

int main(int argc, char *argv[])
{
    MachXO2CommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

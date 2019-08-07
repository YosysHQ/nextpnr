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

class GenericCommandHandler : public CommandHandler
{
  public:
    GenericCommandHandler(int argc, char **argv);
    virtual ~GenericCommandHandler(){};
    std::unique_ptr<Context> createContext(std::unordered_map<std::string, Property> &values) override;
    void setupArchContext(Context *ctx) override{};
    void customBitstream(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

GenericCommandHandler::GenericCommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description GenericCommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    specific.add_options()("generic", "set device type to generic");
    return specific;
}

void GenericCommandHandler::customBitstream(Context *ctx) {}

std::unique_ptr<Context> GenericCommandHandler::createContext(std::unordered_map<std::string, Property> &values)
{
    ArchArgs chipArgs;
    if (values.find("arch.name") != values.end()) {
        std::string arch_name = values["arch.name"].as_string();
        if (arch_name != "generic")
            log_error("Unsuported architecture '%s'.\n", arch_name.c_str());
    }
    return std::unique_ptr<Context>(new Context(chipArgs));
}

int main(int argc, char *argv[])
{
    GenericCommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

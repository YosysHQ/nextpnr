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
#include "util.h"
#include "textcfg.h"

USING_NEXTPNR_NAMESPACE

class LeuctraCommandHandler : public CommandHandler
{
  public:
    LeuctraCommandHandler(int argc, char **argv);
    virtual ~LeuctraCommandHandler(){};
    std::unique_ptr<Context> createContext() override;
    void setupArchContext(Context *ctx) override{};
    void customAfterLoad(Context *ctx) override;
    void customBitstream(Context *ctx) override;

  protected:
    po::options_description getArchOptions() override;
};

LeuctraCommandHandler::LeuctraCommandHandler(int argc, char **argv) : CommandHandler(argc, argv) {}

po::options_description LeuctraCommandHandler::getArchOptions()
{
    po::options_description specific("Architecture specific options");
    specific.add_options()("device", po::value<std::string>(), "select device");
    specific.add_options()("package", po::value<std::string>(), "select device package");
    specific.add_options()("speed", po::value<std::string>(), "select device speedgrade");
    specific.add_options()("ucf", po::value<std::vector<std::string>>(), "UCF pin constraint file(s)");
    specific.add_options()("ucf-allow-unconstrained", "don't require UCF file(s) to constrain all IO");
    specific.add_options()("textcfg", po::value<std::string>(), "textual configuration in Leuctra format to write");
    return specific;
}

void LeuctraCommandHandler::customBitstream(Context *ctx) {
    if (vm.count("textcfg")) {
        std::string filename = vm["textcfg"].as<std::string>();
        std::ofstream f(filename);
        write_textcfg(ctx, f);
    }
}

std::unique_ptr<Context> LeuctraCommandHandler::createContext()
{
    if (vm.count("device"))
        chipArgs.device = vm["device"].as<std::string>();
    else
	chipArgs.device = "xc6slx9";
    if (vm.count("package"))
        chipArgs.package = vm["package"].as<std::string>();
    if (vm.count("speed"))
        chipArgs.speed = vm["speed"].as<std::string>();
    return std::unique_ptr<Context>(new Context(chipArgs));
}

void LeuctraCommandHandler::customAfterLoad(Context *ctx)
{
    if (vm.count("ucf")) {
        std::vector<std::string> files = vm["ucf"].as<std::vector<std::string>>();
        for (const auto &filename : files) {
            std::ifstream in(filename);
            if (!in)
                log_error("failed to open UCF file '%s'\n", filename.c_str());
            if (!ctx->applyUCF(filename, in))
                log_error("failed to parse UCF file '%s'\n", filename.c_str());
        }

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_obuf") ||
                ci->type == ctx->id("$nextpnr_iobuf")) {
                if (!ci->attrs.count(ctx->id("LOC"))) {
                    if (vm.count("ucf-allow-unconstrained"))
                        log_warning("IO '%s' is unconstrained in UCF and will be automatically placed\n",
                                    cell.first.c_str(ctx));
                    else
                        log_error("IO '%s' is unconstrained in UCF (override this error with "
                                  "--ucf-allow-unconstrained)\n",
                                  cell.first.c_str(ctx));
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    LeuctraCommandHandler handler(argc, argv);
    return handler.exec();
}

#endif

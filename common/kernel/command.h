/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  Miodrag Milanovic <micko@yosyshq.com>
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

#ifndef COMMAND_H
#define COMMAND_H

#include <boost/program_options.hpp>
#include <fstream>
#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

namespace po = boost::program_options;

class CommandHandler
{
  public:
    CommandHandler(int argc, char **argv);
    virtual ~CommandHandler() {}

    int exec();
    void load_json(Context *ctx, std::string filename);
    void clear();
    void run_script_hook(const std::string &name);

  protected:
    virtual void setupArchContext(Context *ctx) = 0;
    virtual std::unique_ptr<Context> createContext(dict<std::string, Property> &values) = 0;
    virtual po::options_description getArchOptions() = 0;
    virtual void validate() {}
    virtual void customAfterLoad(Context * /*ctx*/) {}
    virtual void customBitstream(Context * /*ctx*/) {}
    void conflicting_options(const boost::program_options::variables_map &vm, const char *opt1, const char *opt2);

  private:
    bool parseOptions();
    bool executeBeforeContext();
    void setupContext(Context *ctx);
    int executeMain(std::unique_ptr<Context> ctx);
    po::options_description getGeneralOptions();
    void printFooter();

  protected:
    po::variables_map vm;

  private:
    po::options_description options;
    po::positional_options_description pos;
    int argc;
    char **argv;
    std::ofstream logfile;
};

// Relative directory functions from Yosys
bool check_file_exists(std::string filename, bool is_exec);
void init_share_dirname();
std::string proc_self_dirname();
std::string proc_share_dirname();

NEXTPNR_NAMESPACE_END

#endif // COMMAND_H

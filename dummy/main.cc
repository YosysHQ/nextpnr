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

#include <QApplication>
#include <boost/filesystem/convenience.hpp>
#include <boost/program_options.hpp>
#include "application.h"
#include "log.h"
#include "mainwindow.h"
#include "nextpnr.h"
#include "pybindings.h"
#include "version.h"

USING_NEXTPNR_NAMESPACE

int main(int argc, char *argv[])
{
    try {

        namespace po = boost::program_options;
        int rc = 0;

        log_files.push_back(stdout);

        po::options_description options("Allowed options");
        options.add_options()("help,h", "show help");
        options.add_options()("verbose,v", "verbose output");
        options.add_options()("force,f", "keep running after errors");
        options.add_options()("gui", "start gui");
        options.add_options()("run", po::value<std::vector<std::string>>(),
                              "python file to execute");
        options.add_options()("version,V", "show version");
        po::positional_options_description pos;
        pos.add("run", -1);

        po::variables_map vm;
        try {
            po::parsed_options parsed = po::command_line_parser(argc, argv)
                                                .options(options)
                                                .positional(pos)
                                                .run();

            po::store(parsed, vm);

            po::notify(vm);
        }

        catch (std::exception &e) {
            std::cout << e.what() << "\n";
            return 1;
        }

        if (vm.count("help") || argc == 1) {
            std::cout << boost::filesystem::basename(argv[0])
                      << " -- Next Generation Place and Route (git "
                         "sha1 " GIT_COMMIT_HASH_STR ")\n";
            std::cout << "\n";
            std::cout << options << "\n";
            return argc != 1;
        }

        if (vm.count("version")) {
            std::cout << boost::filesystem::basename(argv[0])
                      << " -- Next Generation Place and Route (git "
                         "sha1 " GIT_COMMIT_HASH_STR ")\n";
            return 1;
        }

        Context ctx(ArchArgs{});
        init_python(argv[0]);
        python_export_global("ctx", ctx);

        if (vm.count("verbose")) {
            ctx.verbose = true;
        }

        if (vm.count("force")) {
            ctx.force = true;
        }

        if (vm.count("seed")) {
            ctx.rngseed(vm["seed"].as<int>());
        }

        if (vm.count("run")) {
            std::vector<std::string> files =
                    vm["run"].as<std::vector<std::string>>();
            for (auto filename : files)
                execute_python_file(filename.c_str());
        }

        if (vm.count("gui")) {
            Application a(argc, argv);
            MainWindow w(&ctx);
            w.show();

            rc = a.exec();
        }
        deinit_python();
        return rc;
    } catch (log_execution_error_exception) {
#if defined(_MSC_VER)
        _exit(EXIT_FAILURE);
#else
        _Exit(EXIT_FAILURE);
#endif
    }
}

#endif

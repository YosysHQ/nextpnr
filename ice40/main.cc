/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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
#include <fstream>
#include <iostream>
#include "bitstream.h"
#include "jsonparse.h"
#include "log.h"
#include "mainwindow.h"
#include "nextpnr.h"
#include "pack.h"
#include "pcf.h"
#include "place.h"
#include "pybindings.h"
#include "route.h"
#include "version.h"

void svg_dump_el(const GraphicElement &el)
{
    float scale = 10.0, offset = 10.0;
    std::string style = "stroke=\"black\" stroke-width=\"0.1\" fill=\"none\"";

    if (el.type == GraphicElement::G_BOX) {
        std::cout << "<rect x=\"" << (offset + scale * el.x1) << "\" y=\""
                  << (offset + scale * el.y1) << "\" height=\""
                  << (scale * (el.y2 - el.y1)) << "\" width=\""
                  << (scale * (el.x2 - el.x1)) << "\" " << style << "/>\n";
    }

    if (el.type == GraphicElement::G_LINE) {
        std::cout << "<line x1=\"" << (offset + scale * el.x1) << "\" y1=\""
                  << (offset + scale * el.y1) << "\" x2=\""
                  << (offset + scale * el.x2) << "\" y2=\""
                  << (offset + scale * el.y2) << "\" " << style << "/>\n";
    }
}

int main(int argc, char *argv[])
{
    namespace po = boost::program_options;
    int rc = 0;
    bool verbose = false;
    std::string str;

    log_files.push_back(stdout);

    po::options_description options("Allowed options");
    options.add_options()("help,h", "show help");
    options.add_options()("verbose,v", "verbose output");
    options.add_options()("gui", "start gui");
    options.add_options()("svg", "dump SVG file");
    options.add_options()("pack-only",
                          "pack design only without placement or routing");

    options.add_options()("run", po::value<std::vector<std::string>>(),
                          "python file to execute");
    options.add_options()("json", po::value<std::string>(),
                          "JSON design file to ingest");
    options.add_options()("pcf", po::value<std::string>(),
                          "PCF constraints file to ingest");
    options.add_options()("asc", po::value<std::string>(),
                          "asc bitstream file to write");
    options.add_options()("version,V", "show version");
    options.add_options()("lp384", "set device type to iCE40LP384");
    options.add_options()("lp1k", "set device type to iCE40LP1K");
    options.add_options()("lp8k", "set device type to iCE40LP8K");
    options.add_options()("hx1k", "set device type to iCE40HX1K");
    options.add_options()("hx8k", "set device type to iCE40HX8K");
    options.add_options()("up5k", "set device type to iCE40UP5K");
    options.add_options()("package", po::value<std::string>(),
                          "set device package");
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
    help:
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

    if (vm.count("verbose")) {
        verbose = true;
    }

    ChipArgs chipArgs;

    if (vm.count("lp384")) {
        if (chipArgs.type != ChipArgs::NONE)
            goto help;
        chipArgs.type = ChipArgs::LP384;
        chipArgs.package = "qn32";
    }

    if (vm.count("lp1k")) {
        if (chipArgs.type != ChipArgs::NONE)
            goto help;
        chipArgs.type = ChipArgs::LP1K;
        chipArgs.package = "tq144";
    }

    if (vm.count("lp8k")) {
        if (chipArgs.type != ChipArgs::NONE)
            goto help;
        chipArgs.type = ChipArgs::LP8K;
        chipArgs.package = "ct256";
    }

    if (vm.count("hx1k")) {
        if (chipArgs.type != ChipArgs::NONE)
            goto help;
        chipArgs.type = ChipArgs::HX1K;
        chipArgs.package = "tq144";
    }

    if (vm.count("hx8k")) {
        if (chipArgs.type != ChipArgs::NONE)
            goto help;
        chipArgs.type = ChipArgs::HX8K;
        chipArgs.package = "ct256";
    }

    if (vm.count("up5k")) {
        if (chipArgs.type != ChipArgs::NONE)
            goto help;
        chipArgs.type = ChipArgs::UP5K;
        chipArgs.package = "sg48";
    }

    if (chipArgs.type == ChipArgs::NONE) {
        chipArgs.type = ChipArgs::HX1K;
        chipArgs.package = "tq144";
    }
#ifdef ICE40_HX1K_ONLY
    if (chipArgs.type != ChipArgs::HX1K) {
        std::cout << "This version of nextpnr-ice40 is built with HX1K-support "
                     "only.\n";
        return 1;
    }
#endif

    if (vm.count("package"))
        chipArgs.package = vm["package"].as<std::string>();

    Design design(chipArgs);
    init_python(argv[0]);
    python_export_global("design", design);
    python_export_global("chip", design.chip);

    if (vm.count("svg")) {
        std::cout << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                     "xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n";
        for (auto bel : design.chip.getBels()) {
            std::cout << "<!-- " << design.chip.getBelName(bel) << " -->\n";
            for (auto &el : design.chip.getBelGraphics(bel))
                svg_dump_el(el);
        }
        std::cout << "<!-- Frame -->\n";
        for (auto &el : design.chip.getFrameGraphics())
            svg_dump_el(el);
        std::cout << "</svg>\n";
    }

    if (vm.count("json")) {
        std::string filename = vm["json"].as<std::string>();
        std::istream *f = new std::ifstream(filename);

        parse_json_file(f, filename, &design);

        if (vm.count("pcf")) {
            std::ifstream pcf(vm["pcf"].as<std::string>());
            apply_pcf(&design, pcf);
        }

        pack_design(&design);
        if (!vm.count("pack-only")) {
            place_design(&design);
            route_design(&design, verbose);
        }
    }

    if (vm.count("asc")) {
        std::string filename = vm["asc"].as<std::string>();
        std::ofstream f(filename);
        write_asc(design, f);
    }

    if (vm.count("run")) {
        std::vector<std::string> files =
                vm["run"].as<std::vector<std::string>>();
        for (auto filename : files)
            execute_python_file(filename.c_str());
    }

    if (vm.count("gui")) {
        QApplication a(argc, argv);
        MainWindow w(&design);
        w.show();

        rc = a.exec();
    }
    deinit_python();
    return rc;
}

#endif

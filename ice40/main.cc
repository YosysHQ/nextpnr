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

#ifndef NO_GUI
#include <QApplication>
#include "application.h"
#include "mainwindow.h"
#endif
#ifndef NO_PYTHON
#include "pybindings.h"
#endif

#include <boost/filesystem/convenience.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <fstream>
#include <iostream>
#include "bitstream.h"
#include "design_utils.h"
#include "jsonparse.h"
#include "log.h"
#include "nextpnr.h"
#include "pcf.h"
#include "place_legaliser.h"
#include "timing.h"
#include "version.h"

USING_NEXTPNR_NAMESPACE

void svg_dump_decal(const Context *ctx, const DecalXY &decal)
{
    const float scale = 10.0, offset = 10.0;
    const std::string style = "stroke=\"black\" stroke-width=\"0.1\" fill=\"none\"";

    for (auto &el : ctx->getDecalGraphics(decal.decal)) {
        if (el.type == GraphicElement::TYPE_BOX) {
            std::cout << "<rect x=\"" << (offset + scale * (decal.x + el.x1)) << "\" y=\""
                      << (offset + scale * (decal.y + el.y1)) << "\" height=\"" << (scale * (el.y2 - el.y1))
                      << "\" width=\"" << (scale * (el.x2 - el.x1)) << "\" " << style << "/>\n";
        }

        if (el.type == GraphicElement::TYPE_LINE) {
            std::cout << "<line x1=\"" << (offset + scale * (decal.x + el.x1)) << "\" y1=\""
                      << (offset + scale * (decal.y + el.y1)) << "\" x2=\"" << (offset + scale * (decal.x + el.x2))
                      << "\" y2=\"" << (offset + scale * (decal.y + el.y2)) << "\" " << style << "/>\n";
        }
    }
}

void conflicting_options(const boost::program_options::variables_map &vm, const char *opt1, const char *opt2)
{
    if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) && !vm[opt2].defaulted()) {
        std::string msg = "Conflicting options '" + std::string(opt1) + "' and '" + std::string(opt1) + "'.";
        log_error("%s\n", msg.c_str());
    }
}

int main(int argc, char *argv[])
{
    try {
        namespace po = boost::program_options;
        namespace pt = boost::property_tree;
        int rc = 0;
        std::string str;

        log_files.push_back(stdout);

        po::options_description options("Allowed options");
        options.add_options()("help,h", "show help");
        options.add_options()("verbose,v", "verbose output");
        options.add_options()("debug", "debug output");
        options.add_options()("force,f", "keep running after errors");
#ifndef NO_GUI
        options.add_options()("gui", "start gui");
#endif
        options.add_options()("svg", "dump SVG file");
        options.add_options()("pack-only", "pack design only without placement or routing");

        po::positional_options_description pos;
#ifndef NO_PYTHON
        options.add_options()("run", po::value<std::vector<std::string>>(), "python file to execute");
        pos.add("run", -1);
#endif
        options.add_options()("json", po::value<std::string>(), "JSON design file to ingest");
        options.add_options()("pcf", po::value<std::string>(), "PCF constraints file to ingest");
        options.add_options()("asc", po::value<std::string>(), "asc bitstream file to write");
        options.add_options()("read", po::value<std::string>(), "asc bitstream file to read");
        options.add_options()("seed", po::value<int>(), "seed value for random number generator");
        options.add_options()("slack_redist_iter", po::value<int>(), "number of iterations between slack redistribution");
        options.add_options()("version,V", "show version");
        options.add_options()("tmfuzz", "run path delay estimate fuzzer");
        options.add_options()("test", "check architecture database integrity");
#ifdef ICE40_HX1K_ONLY
        options.add_options()("hx1k", "set device type to iCE40HX1K");
#else
        options.add_options()("lp384", "set device type to iCE40LP384");
        options.add_options()("lp1k", "set device type to iCE40LP1K");
        options.add_options()("lp8k", "set device type to iCE40LP8K");
        options.add_options()("hx1k", "set device type to iCE40HX1K");
        options.add_options()("hx8k", "set device type to iCE40HX8K");
        options.add_options()("up5k", "set device type to iCE40UP5K");
#endif
        options.add_options()("freq", po::value<double>(), "set target frequency for design in MHz");
        options.add_options()("no-tmdriv", "disable timing-driven placement");
        options.add_options()("package", po::value<std::string>(), "set device package");
        options.add_options()("save", po::value<std::string>(), "project file to write");
        options.add_options()("load", po::value<std::string>(), "project file to read");

        po::variables_map vm;
        try {
            po::parsed_options parsed = po::command_line_parser(argc, argv).options(options).positional(pos).run();

            po::store(parsed, vm);

            po::notify(vm);
        } catch (std::exception &e) {
            std::cout << e.what() << "\n";
            return 1;
        }

        conflicting_options(vm, "read", "json");
#ifndef ICE40_HX1K_ONLY
        if ((vm.count("lp384") + vm.count("lp1k") + vm.count("lp8k") + vm.count("hx1k") + vm.count("hx8k") +
             vm.count("up5k")) > 1)
            log_error("Only one device type can be set\n");
#endif
        if (vm.count("help") || argc == 1) {
        help:
            std::cout << boost::filesystem::basename(argv[0]) << " -- Next Generation Place and Route (git "
                                                                 "sha1 " GIT_COMMIT_HASH_STR ")\n";
            std::cout << "\n";
            std::cout << options << "\n";
            return argc != 1;
        }

        if (vm.count("version")) {
            std::cout << boost::filesystem::basename(argv[0]) << " -- Next Generation Place and Route (git "
                                                                 "sha1 " GIT_COMMIT_HASH_STR ")\n";
            return 1;
        }

        if (vm.count("load")) {
            try {
                pt::ptree root;
                std::string filename = vm["load"].as<std::string>();
                pt::read_json(filename, root);
                log_info("Loading project %s...\n", filename.c_str());
                log_break();
                vm.clear();

                int version = root.get<int>("project.version");
                if (version != 1)
                    log_error("Wrong project format version.\n");

                std::string arch_name = root.get<std::string>("project.arch.name");
                if (arch_name != "ice40")
                    log_error("Unsuported project architecture.\n");

                std::string arch_type = root.get<std::string>("project.arch.type");
                vm.insert(std::make_pair(arch_type, po::variable_value()));

                std::string arch_package = root.get<std::string>("project.arch.package");
                vm.insert(std::make_pair("package", po::variable_value(arch_package, false)));

                auto project = root.get_child("project");
                if (project.count("input")) {
                    auto input = project.get_child("input");
                    if (input.count("json"))
                        vm.insert(std::make_pair("json", po::variable_value(input.get<std::string>("json"), false)));
                    if (input.count("pcf"))
                        vm.insert(std::make_pair("pcf", po::variable_value(input.get<std::string>("pcf"), false)));
                }
                if (project.count("params")) {
                    auto params = project.get_child("params");
                    if (params.count("freq"))
                        vm.insert(std::make_pair("freq", po::variable_value(params.get<double>("freq"), false)));
                    if (params.count("seed"))
                        vm.insert(std::make_pair("seed", po::variable_value(params.get<int>("seed"), false)));
                }
                po::notify(vm);
            } catch (...) {
                log_error("Error loading project file.\n");
            }
        }

        ArchArgs chipArgs;

        if (vm.count("lp384")) {
            if (chipArgs.type != ArchArgs::NONE)
                goto help;
            chipArgs.type = ArchArgs::LP384;
            chipArgs.package = "qn32";
        }

        if (vm.count("lp1k")) {
            if (chipArgs.type != ArchArgs::NONE)
                goto help;
            chipArgs.type = ArchArgs::LP1K;
            chipArgs.package = "tq144";
        }

        if (vm.count("lp8k")) {
            if (chipArgs.type != ArchArgs::NONE)
                goto help;
            chipArgs.type = ArchArgs::LP8K;
            chipArgs.package = "ct256";
        }

        if (vm.count("hx1k")) {
            if (chipArgs.type != ArchArgs::NONE)
                goto help;
            chipArgs.type = ArchArgs::HX1K;
            chipArgs.package = "tq144";
        }

        if (vm.count("hx8k")) {
            if (chipArgs.type != ArchArgs::NONE)
                goto help;
            chipArgs.type = ArchArgs::HX8K;
            chipArgs.package = "ct256";
        }

        if (vm.count("up5k")) {
            if (chipArgs.type != ArchArgs::NONE)
                goto help;
            chipArgs.type = ArchArgs::UP5K;
            chipArgs.package = "sg48";
        }

        if (chipArgs.type == ArchArgs::NONE) {
            chipArgs.type = ArchArgs::HX1K;
            chipArgs.package = "tq144";
        }
#ifdef ICE40_HX1K_ONLY
        if (chipArgs.type != ArchArgs::HX1K) {
            std::cout << "This version of nextpnr-ice40 is built with "
                         "HX1K-support "
                         "only.\n";
            return 1;
        }
#endif

        if (vm.count("package"))
            chipArgs.package = vm["package"].as<std::string>();

        if (vm.count("save")) {
            Context ctx(chipArgs);
            std::string filename = vm["save"].as<std::string>();
            std::ofstream f(filename);
            pt::ptree root;
            root.put("project.version", 1);
            root.put("project.name", boost::filesystem::basename(filename));
            root.put("project.arch.name", ctx.archId().c_str(&ctx));
            root.put("project.arch.type", ctx.archArgsToId(chipArgs).c_str(&ctx));
            root.put("project.arch.package", chipArgs.package);
            if (vm.count("json"))
                root.put("project.input.json", vm["json"].as<std::string>());
            if (vm.count("pcf"))
                root.put("project.input.pcf", vm["pcf"].as<std::string>());
            if (vm.count("freq"))
                root.put("project.params.freq", vm["freq"].as<double>());
            if (vm.count("seed"))
                root.put("project.params.seed", vm["seed"].as<int>());
            pt::write_json(f, root);
            return 1;
        }

        std::unique_ptr<Context> ctx = std::unique_ptr<Context>(new Context(chipArgs));

        if (vm.count("verbose")) {
            ctx->verbose = true;
        }

        if (vm.count("debug")) {
            ctx->verbose = true;
            ctx->debug = true;
        }

        if (vm.count("force")) {
            ctx->force = true;
        }

        if (vm.count("seed")) {
            ctx->rngseed(vm["seed"].as<int>());
        }

        if (vm.count("slack_redist_iter")) {
            ctx->slack_redist_iter = vm["slack_redist_iter"].as<int>();
        }

        if (vm.count("svg")) {
            std::cout << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                         "xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n";
            for (auto bel : ctx->getBels()) {
                std::cout << "<!-- " << ctx->getBelName(bel).str(ctx.get()) << " -->\n";
                svg_dump_decal(ctx.get(), ctx->getBelDecal(bel));
            }
            std::cout << "<!-- Frame -->\n";
            svg_dump_decal(ctx.get(), ctx->getFrameDecal());
            std::cout << "</svg>\n";
        }

        if (vm.count("test"))
            ctx->archcheck();

        if (vm.count("tmfuzz")) {
            std::vector<WireId> src_wires, dst_wires;

            /*for (auto w : ctx->getWires())
                src_wires.push_back(w);*/
            for (auto b : ctx->getBels()) {
                if (ctx->getBelType(b) == TYPE_ICESTORM_LC) {
                    src_wires.push_back(ctx->getBelPinWire(b, PIN_O));
                }
                if (ctx->getBelType(b) == TYPE_SB_IO) {
                    src_wires.push_back(ctx->getBelPinWire(b, PIN_D_IN_0));
                }
            }

            for (auto b : ctx->getBels()) {
                if (ctx->getBelType(b) == TYPE_ICESTORM_LC) {
                    dst_wires.push_back(ctx->getBelPinWire(b, PIN_I0));
                    dst_wires.push_back(ctx->getBelPinWire(b, PIN_I1));
                    dst_wires.push_back(ctx->getBelPinWire(b, PIN_I2));
                    dst_wires.push_back(ctx->getBelPinWire(b, PIN_I3));
                    dst_wires.push_back(ctx->getBelPinWire(b, PIN_CEN));
                    dst_wires.push_back(ctx->getBelPinWire(b, PIN_CIN));
                }
                if (ctx->getBelType(b) == TYPE_SB_IO) {
                    dst_wires.push_back(ctx->getBelPinWire(b, PIN_D_OUT_0));
                    dst_wires.push_back(ctx->getBelPinWire(b, PIN_OUTPUT_ENABLE));
                }
            }

            ctx->shuffle(src_wires);
            ctx->shuffle(dst_wires);

            for (int i = 0; i < int(src_wires.size()) && i < int(dst_wires.size()); i++) {
                delay_t actual_delay;
                WireId src = src_wires[i], dst = dst_wires[i];
                if (!ctx->getActualRouteDelay(src, dst, actual_delay))
                    continue;
                printf("%s %s %.3f %.3f %d %d %d %d %d %d\n", ctx->getWireName(src).c_str(ctx.get()),
                       ctx->getWireName(dst).c_str(ctx.get()), ctx->getDelayNS(actual_delay),
                       ctx->getDelayNS(ctx->estimateDelay(src, dst)), ctx->chip_info->wire_data[src.index].x,
                       ctx->chip_info->wire_data[src.index].y, ctx->chip_info->wire_data[src.index].type,
                       ctx->chip_info->wire_data[dst.index].x, ctx->chip_info->wire_data[dst.index].y,
                       ctx->chip_info->wire_data[dst.index].type);
            }
        }

        if (vm.count("freq")) {
            ctx->target_freq = vm["freq"].as<double>() * 1e6;
            ctx->user_freq = true;
        } else {
            log_warning("Target frequency not specified. Will optimise for max frequency.\n");
        }

        ctx->timing_driven = true;
        if (vm.count("no-tmdriv"))
            ctx->timing_driven = false;

        if (vm.count("read")) {
            std::string filename = vm["read"].as<std::string>();
            std::ifstream f(filename);
            if (!read_asc(ctx.get(), f))
                log_error("Loading ASC failed.\n");
        }

#ifndef NO_GUI
        if (vm.count("gui")) {
            Application a(argc, argv);
            MainWindow w(std::move(ctx), chipArgs);
            if (vm.count("json")) {
                std::string filename = vm["json"].as<std::string>();
                std::string pcf = "";
                if (vm.count("pcf"))
                    pcf = vm["pcf"].as<std::string>();
                w.load_json(filename, pcf);
            }
            w.show();

            return a.exec();
        }
#endif
        if (vm.count("json")) {
            std::string filename = vm["json"].as<std::string>();
            std::ifstream f(filename);
            if (!parse_json_file(f, filename, ctx.get()))
                log_error("Loading design failed.\n");

            if (vm.count("pcf")) {
                std::ifstream pcf(vm["pcf"].as<std::string>());
                if (!apply_pcf(ctx.get(), pcf))
                    log_error("Loading PCF failed.\n");
            }

            if (!ctx->pack() && !ctx->force)
                log_error("Packing design failed.\n");
            ctx->check();
            print_utilisation(ctx.get());
            if (!vm.count("pack-only")) {
                if (!ctx->place() && !ctx->force)
                    log_error("Placing design failed.\n");
                ctx->check();
                if (!ctx->route() && !ctx->force)
                    log_error("Routing design failed.\n");
            }
        }

        if (vm.count("asc")) {
            std::string filename = vm["asc"].as<std::string>();
            std::ofstream f(filename);
            write_asc(ctx.get(), f);
        }

#ifndef NO_PYTHON
        if (vm.count("run")) {
            init_python(argv[0], true);
            python_export_global("ctx", *ctx.get());

            std::vector<std::string> files = vm["run"].as<std::vector<std::string>>();
            for (auto filename : files)
                execute_python_file(filename.c_str());

            deinit_python();
        }
#endif
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

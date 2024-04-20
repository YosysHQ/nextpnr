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

#ifndef NO_GUI
#include <QApplication>
#include "application.h"
#include "mainwindow.h"
#endif
#ifndef NO_PYTHON
#include "pybindings.h"
#endif

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <random>
#include <set>

#include "command.h"
#include "design_utils.h"
#include "json_frontend.h"
#include "jsonwrite.h"
#include "log.h"
#include "timing.h"
#include "util.h"
#include "version.h"

#if defined(_WIN32)
#include <io.h>
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <dirent.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/sysctl.h>
#endif

NEXTPNR_NAMESPACE_BEGIN

static std::string npnr_share_dirname;

#ifdef _WIN32
bool check_file_exists(std::string filename, bool) { return _access(filename.c_str(), 0) == 0; }
#else
bool check_file_exists(std::string filename, bool is_exec)
{
    return access(filename.c_str(), is_exec ? X_OK : F_OK) == 0;
}
#endif

#if defined(__linux__) || defined(__CYGWIN__)
std::string proc_self_dirname()
{
    char path[PATH_MAX];
    ssize_t buflen = readlink("/proc/self/exe", path, sizeof(path));
    if (buflen < 0) {
        log_error("readlink(\"/proc/self/exe\") failed: %s\n", strerror(errno));
    }
    while (buflen > 0 && path[buflen - 1] != '/')
        buflen--;
    return std::string(path, buflen);
}
#elif defined(__FreeBSD__) || defined(__NetBSD__)
std::string proc_self_dirname()
{
#ifdef __NetBSD__
    int mib[4] = {CTL_KERN, KERN_PROC_ARGS, getpid(), KERN_PROC_PATHNAME};
#else
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
#endif
    size_t buflen;
    char *buffer;
    std::string path;
    if (sysctl(mib, 4, NULL, &buflen, NULL, 0) != 0)
        log_error("sysctl failed: %s\n", strerror(errno));
    buffer = (char *)malloc(buflen);
    if (buffer == NULL)
        log_error("malloc failed: %s\n", strerror(errno));
    if (sysctl(mib, 4, buffer, &buflen, NULL, 0) != 0)
        log_error("sysctl failed: %s\n", strerror(errno));
    while (buflen > 0 && buffer[buflen - 1] != '/')
        buflen--;
    path.assign(buffer, buflen);
    free(buffer);
    return path;
}
#elif defined(__APPLE__)
std::string proc_self_dirname()
{
    char *path = NULL;
    uint32_t buflen = 0;
    while (_NSGetExecutablePath(path, &buflen) != 0)
        path = (char *)realloc((void *)path, buflen);
    while (buflen > 0 && path[buflen - 1] != '/')
        buflen--;
    std::string str(path, buflen);
    free(path);
    return str;
}
#elif defined(_WIN32)
std::string proc_self_dirname()
{
    int i = 0;
#ifdef _WIN32
    char longpath[MAX_PATH + 1];
    char shortpath[MAX_PATH + 1];
#else
    WCHAR longpath[MAX_PATH + 1];
    TCHAR shortpath[MAX_PATH + 1];
#endif
    if (!GetModuleFileName(0, longpath, MAX_PATH + 1))
        log_error("GetModuleFileName() failed.\n");
    if (!GetShortPathName(longpath, shortpath, MAX_PATH + 1))
        log_error("GetShortPathName() failed.\n");
    while (shortpath[i] != 0)
        i++;
    while (i > 0 && shortpath[i - 1] != '/' && shortpath[i - 1] != '\\')
        shortpath[--i] = 0;
    std::string path;
    for (i = 0; shortpath[i]; i++)
        path += char(shortpath[i]);
    return path;
}
#elif defined(EMSCRIPTEN) || defined(__wasm)
std::string proc_self_dirname() { return "/"; }
#elif defined(__OpenBSD__) || defined(__DragonFly__)
char npnr_path[PATH_MAX];
char *npnr_argv0;

std::string proc_self_dirname(void)
{
    char buf[PATH_MAX + 1] = "", *path, *p;
    // if case argv[0] contains a valid path, return it
    if (strlen(npnr_path) > 0 && (p = strrchr(npnr_path, '/')) != NULL) {
        snprintf(buf, sizeof buf, "%*s/", (int)(npnr_path - p), npnr_path);
        return buf;
    }
    // if argv[0] does not, reconstruct the path out of $PATH
    path = strdup(getenv("PATH"));
    if (!path)
        log_error("getenv(\"PATH\") failed: %s\n", strerror(errno));
    for (p = strtok(path, ":"); p; p = strtok(NULL, ":")) {
        snprintf(buf, sizeof buf, "%s/%s", p, npnr_argv0);
        if (access(buf, X_OK) == 0) {
            *(strrchr(buf, '/') + 1) = '\0';
            free(path);
            return buf;
        }
    }
    free(path);
    log_error("Can't determine nextpnr executable path\n.");
    return NULL;
}
#else
#error "Don't know how to determine process executable base path!"
#endif

#if defined(EMSCRIPTEN) || defined(__wasm)
void init_share_dirname() { npnr_share_dirname = "/share/"; }
#else
void init_share_dirname()
{
    std::string proc_self_path = proc_self_dirname();
#if defined(_WIN32) && !defined(nextpnr_WIN32_UNIX_DIR)
    std::string proc_share_path = proc_self_path + "share\\";
    if (check_file_exists(proc_share_path, true)) {
        npnr_share_dirname = proc_share_path;
        return;
    }
    proc_share_path = proc_self_path + "..\\share\\" + "nextpnr\\";
    if (check_file_exists(proc_share_path, true)) {
        npnr_share_dirname = proc_share_path;
        return;
    }
#else
    std::string proc_share_path = proc_self_path + "share/";
    if (check_file_exists(proc_share_path, true)) {
        npnr_share_dirname = proc_share_path;
        return;
    }
    proc_share_path = proc_self_path + "../share/" + "nextpnr/";
    if (check_file_exists(proc_share_path, true)) {
        npnr_share_dirname = proc_share_path;
        return;
    }
#ifdef nextpnr_DATDIR
    proc_share_path = nextpnr_DATDIR "/";
    if (check_file_exists(proc_share_path, true)) {
        npnr_share_dirname = proc_share_path;
        return;
    }
#endif
#endif
}
#endif

std::string proc_share_dirname()
{
    if (npnr_share_dirname.empty())
        log_error("init_share_dirname: unable to determine share/ directory!\n");
    return npnr_share_dirname;
}

struct no_separator : std::numpunct<char>
{
  protected:
    virtual string_type do_grouping() const { return "\000"; } // groups of 0 (disable)
};

CommandHandler::CommandHandler(int argc, char **argv) : argc(argc), argv(argv)
{
    try {
        std::locale loc("");
        std::locale::global(std::locale(loc, new no_separator()));
    } catch (const std::runtime_error &e) {
        // the locale is broken in this system, so leave it as it is
    }
    log_streams.clear();

#if defined(__OpenBSD__) || defined(__DragonFly__)
    // save the executable origin for proc_self_dirname()
    npnr_argv0 = argv[0];
    realpath(npnr_argv0, npnr_path);
#endif
}

bool CommandHandler::parseOptions()
{
    options.add(getGeneralOptions()).add(getArchOptions());
    try {
        po::parsed_options parsed =
                po::command_line_parser(argc, argv)
                        .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
                        .options(options)
                        .positional(pos)
                        .run();
        po::store(parsed, vm);
        po::notify(vm);
        return true;
    } catch (std::exception &e) {
        std::cout << e.what() << "\n";
        return false;
    }
}

bool CommandHandler::executeBeforeContext()
{
    if (vm.count("help") || argc == 1) {
        std::cerr << boost::filesystem::path(argv[0]).stem()
                  << " -- Next Generation Place and Route (Version " GIT_DESCRIBE_STR ")\n";
        std::cerr << options << "\n";
        return argc != 1;
    }

    if (vm.count("version")) {
        std::cerr << boost::filesystem::path(argv[0]).stem()
                  << " -- Next Generation Place and Route (Version " GIT_DESCRIBE_STR ")\n";
        return true;
    }
    validate();

    if (vm.count("quiet")) {
        log_streams.push_back(std::make_pair(&std::cerr, LogLevel::WARNING_MSG));
    } else {
        log_streams.push_back(std::make_pair(&std::cerr, LogLevel::LOG_MSG));
    }

    if (vm.count("Werror")) {
        log_warn_as_error = true;
    }

    if (vm.count("log")) {
        std::string logfilename = vm["log"].as<std::string>();
        logfile.open(logfilename);
        if (!logfile.is_open())
            log_error("Failed to open log file '%s' for writing.\n", logfilename.c_str());
        log_streams.push_back(std::make_pair(&logfile, LogLevel::LOG_MSG));
    }
    return false;
}

po::options_description CommandHandler::getGeneralOptions()
{
    po::options_description general("General options");
    general.add_options()("help,h", "show help");
    general.add_options()("verbose,v", "verbose output");
    general.add_options()("quiet,q", "quiet mode, only errors and warnings displayed");
    general.add_options()("Werror", "Turn warnings into errors");
    general.add_options()("log,l", po::value<std::string>(),
                          "log file, all log messages are written to this file regardless of -q");
    general.add_options()("debug", "debug output");
    general.add_options()("debug-placer", "debug output from placer only");
    general.add_options()("debug-router", "debug output from router only");
    general.add_options()("threads", po::value<int>(), "number of threads for passes where this is configurable");

    general.add_options()("force,f", "keep running after errors");
#ifndef NO_GUI
    general.add_options()("gui", "start gui");
    general.add_options()("gui-no-aa", "disable anti aliasing (use together with --gui option)");
#endif
#ifndef NO_PYTHON
    general.add_options()("run", po::value<std::vector<std::string>>(),
                          "python file to execute instead of default flow");
    pos.add("run", -1);
    general.add_options()("pre-pack", po::value<std::vector<std::string>>(), "python file to run before packing");
    general.add_options()("pre-place", po::value<std::vector<std::string>>(), "python file to run before placement");
    general.add_options()("pre-route", po::value<std::vector<std::string>>(), "python file to run before routing");
    general.add_options()("post-route", po::value<std::vector<std::string>>(), "python file to run after routing");
    general.add_options()("on-failure", po::value<std::vector<std::string>>(),
                          "python file to run in event of crash for design introspection");

#endif
    general.add_options()("json", po::value<std::string>(), "JSON design file to ingest");
    general.add_options()("write", po::value<std::string>(), "JSON design file to write");
    general.add_options()("top", po::value<std::string>(), "name of top module");
    general.add_options()("seed", po::value<int>(), "seed value for random number generator");
    general.add_options()("randomize-seed,r", "randomize seed value for random number generator");

    general.add_options()(
            "placer", po::value<std::string>(),
            std::string("placer algorithm to use; available: " + boost::algorithm::join(Arch::availablePlacers, ", ") +
                        "; default: " + Arch::defaultPlacer)
                    .c_str());

    general.add_options()(
            "router", po::value<std::string>(),
            std::string("router algorithm to use; available: " + boost::algorithm::join(Arch::availableRouters, ", ") +
                        "; default: " + Arch::defaultRouter)
                    .c_str());

    general.add_options()("slack_redist_iter", po::value<int>(), "number of iterations between slack redistribution");
    general.add_options()("cstrweight", po::value<float>(), "placer weighting for relative constraint satisfaction");
    general.add_options()("starttemp", po::value<float>(), "placer SA start temperature");

    general.add_options()("pack-only", "pack design only without placement or routing");
    general.add_options()("no-route", "process design without routing");
    general.add_options()("no-place", "process design without placement");
    general.add_options()("no-pack", "process design without packing");

    general.add_options()("ignore-loops", "ignore combinational loops in timing analysis");
    general.add_options()("ignore-rel-clk", "ignore clock-to-clock relations in timing checks");

    general.add_options()("version,V", "show version");
    general.add_options()("test", "check architecture database integrity");
    general.add_options()("freq", po::value<double>(), "set target frequency for design in MHz");
    general.add_options()("timing-allow-fail", "allow timing to fail in design");
    general.add_options()("no-tmdriv", "disable timing-driven placement");
    general.add_options()("sdf", po::value<std::string>(), "SDF delay back-annotation file to write");
    general.add_options()("sdf-cvc", "enable tweaks for SDF file compatibility with the CVC simulator");
    general.add_options()("no-print-critical-path-source",
                          "disable printing of the line numbers associated with each net in the critical path");

    general.add_options()("placer-heap-alpha", po::value<float>(), "placer heap alpha value (float, default: 0.1)");
    general.add_options()("placer-heap-beta", po::value<float>(), "placer heap beta value (float, default: 0.9)");
    general.add_options()("placer-heap-critexp", po::value<int>(),
                          "placer heap criticality exponent (int, default: 2)");
    general.add_options()("placer-heap-timingweight", po::value<int>(), "placer heap timing weight (int, default: 10)");
    general.add_options()("placer-heap-cell-placement-timeout", po::value<int>(),
                          "allow placer to attempt up to max(10000, total cells^2 / N) iterations to place a cell (int "
                          "N, default: 8, 0 for no timeout)");

    general.add_options()("static-dump-density", "write density csv files during placer-static flow");


#if !defined(NPNR_DISABLE_THREADS)
    general.add_options()("parallel-refine", "use new experimental parallelised engine for placement refinement");
#endif

    general.add_options()("router2-heatmap", po::value<std::string>(),
                          "prefix for router2 resource congestion heatmaps");

    general.add_options()("tmg-ripup", "enable experimental timing-driven ripup in router");
    general.add_options()("router2-tmg-ripup",
                          "enable experimental timing-driven ripup in router (deprecated; use --tmg-ripup instead)");

    general.add_options()("router2-alt-weights", "use alternate router2 weights");

    general.add_options()("report", po::value<std::string>(),
                          "write timing and utilization report in JSON format to file");
    general.add_options()("detailed-timing-report", "Append detailed net timing data to the JSON report");

    general.add_options()("placed-svg", po::value<std::string>(), "write render of placement to SVG file");
    general.add_options()("routed-svg", po::value<std::string>(), "write render of routing to SVG file");

    return general;
}

namespace {
static CommandHandler *global_command_handler = nullptr;
void script_terminate_handler()
{
    if (global_command_handler != nullptr)
        global_command_handler->run_script_hook("on-failure");
}
}; // namespace

void CommandHandler::setupContext(Context *ctx)
{
    if (ctx->settings.find(ctx->id("seed")) != ctx->settings.end())
        ctx->rngstate = ctx->setting<uint64_t>("seed");

    if (vm.count("verbose")) {
        ctx->verbose = true;
    }

    if (vm.count("debug")) {
        ctx->verbose = true;
        ctx->debug = true;
    }

    if (vm.count("no-print-critical-path-source")) {
        ctx->disable_critical_path_source_print = true;
    }

    if (vm.count("force")) {
        ctx->force = true;
    }

    if (vm.count("seed")) {
        ctx->rngseed(vm["seed"].as<int>());
    }

    if (vm.count("threads")) {
        ctx->settings[ctx->id("threads")] = vm["threads"].as<int>();
    }

    if (vm.count("randomize-seed")) {
        std::random_device randDev{};
        std::uniform_int_distribution<int> distrib{1};
        auto seed = distrib(randDev);
        ctx->rngseed(seed);
        log_info("Generated random seed: %d\n", seed);
    }

    if (vm.count("slack_redist_iter")) {
        ctx->settings[ctx->id("slack_redist_iter")] = vm["slack_redist_iter"].as<int>();
        if (vm.count("freq") && vm["freq"].as<double>() == 0) {
            ctx->settings[ctx->id("auto_freq")] = true;
#ifndef NO_GUI
            if (!vm.count("gui"))
#endif
                log_warning("Target frequency not specified. Will optimise for max frequency.\n");
        }
    }

    if (vm.count("ignore-loops")) {
        ctx->settings[ctx->id("timing/ignoreLoops")] = true;
    }

    if (vm.count("ignore-rel-clk")) {
        ctx->settings[ctx->id("timing/ignoreRelClk")] = true;
    }

    if (vm.count("timing-allow-fail")) {
        ctx->settings[ctx->id("timing/allowFail")] = true;
    }

    if (vm.count("placer")) {
        std::string placer = vm["placer"].as<std::string>();
        if (std::find(Arch::availablePlacers.begin(), Arch::availablePlacers.end(), placer) ==
            Arch::availablePlacers.end())
            log_error("Placer algorithm '%s' is not supported (available options: %s)\n", placer.c_str(),
                      boost::algorithm::join(Arch::availablePlacers, ", ").c_str());
        ctx->settings[ctx->id("placer")] = placer;
    }

    if (vm.count("router")) {
        std::string router = vm["router"].as<std::string>();
        if (std::find(Arch::availableRouters.begin(), Arch::availableRouters.end(), router) ==
            Arch::availableRouters.end())
            log_error("Router algorithm '%s' is not supported (available options: %s)\n", router.c_str(),
                      boost::algorithm::join(Arch::availableRouters, ", ").c_str());
        ctx->settings[ctx->id("router")] = router;
    }

    if (vm.count("cstrweight")) {
        ctx->settings[ctx->id("placer1/constraintWeight")] = std::to_string(vm["cstrweight"].as<float>());
    }
    if (vm.count("starttemp")) {
        ctx->settings[ctx->id("placer1/startTemp")] = std::to_string(vm["starttemp"].as<float>());
    }

    if (vm.count("freq")) {
        auto freq = vm["freq"].as<double>();
        if (freq > 0)
            ctx->settings[ctx->id("target_freq")] = std::to_string(freq * 1e6);
    }

    if (vm.count("no-tmdriv"))
        ctx->settings[ctx->id("timing_driven")] = false;

    if (vm.count("placer-heap-alpha"))
        ctx->settings[ctx->id("placerHeap/alpha")] = std::to_string(vm["placer-heap-alpha"].as<float>());

    if (vm.count("placer-heap-beta"))
        ctx->settings[ctx->id("placerHeap/beta")] = std::to_string(vm["placer-heap-beta"].as<float>());

    if (vm.count("placer-heap-critexp"))
        ctx->settings[ctx->id("placerHeap/criticalityExponent")] = std::to_string(vm["placer-heap-critexp"].as<int>());

    if (vm.count("placer-heap-timingweight"))
        ctx->settings[ctx->id("placerHeap/timingWeight")] = std::to_string(vm["placer-heap-timingweight"].as<int>());

    if (vm.count("placer-heap-cell-placement-timeout"))
        ctx->settings[ctx->id("placerHeap/cellPlacementTimeout")] =
                std::to_string(std::max(0, vm["placer-heap-cell-placement-timeout"].as<int>()));

    if (vm.count("parallel-refine"))
        ctx->settings[ctx->id("placerHeap/parallelRefine")] = true;

    if (vm.count("router2-heatmap"))
        ctx->settings[ctx->id("router2/heatmap")] = vm["router2-heatmap"].as<std::string>();
    if (vm.count("tmg-ripup") || vm.count("router2-tmg-ripup"))
        ctx->settings[ctx->id("router/tmg_ripup")] = true;

    if (vm.count("router2-alt-weights"))
        ctx->settings[ctx->id("router2/alt-weights")] = true;

    if (vm.count("static-dump-density"))
        ctx->settings[ctx->id("static/dump_density")] = true;


    // Setting default values
    if (ctx->settings.find(ctx->id("target_freq")) == ctx->settings.end())
        ctx->settings[ctx->id("target_freq")] = std::to_string(12e6);
    if (ctx->settings.find(ctx->id("timing_driven")) == ctx->settings.end())
        ctx->settings[ctx->id("timing_driven")] = true;
    if (ctx->settings.find(ctx->id("slack_redist_iter")) == ctx->settings.end())
        ctx->settings[ctx->id("slack_redist_iter")] = 0;
    if (ctx->settings.find(ctx->id("auto_freq")) == ctx->settings.end())
        ctx->settings[ctx->id("auto_freq")] = false;
    if (ctx->settings.find(ctx->id("placer")) == ctx->settings.end())
        ctx->settings[ctx->id("placer")] = Arch::defaultPlacer;
    if (ctx->settings.find(ctx->id("router")) == ctx->settings.end())
        ctx->settings[ctx->id("router")] = Arch::defaultRouter;

    ctx->settings[ctx->id("arch.name")] = std::string(ctx->archId().c_str(ctx));
    ctx->settings[ctx->id("arch.type")] = std::string(ctx->archArgsToId(ctx->archArgs()).c_str(ctx));
    ctx->settings[ctx->id("seed")] = ctx->rngstate;

    if (ctx->settings.find(ctx->id("placerHeap/alpha")) == ctx->settings.end())
        ctx->settings[ctx->id("placerHeap/alpha")] = std::to_string(0.1);
    if (ctx->settings.find(ctx->id("placerHeap/beta")) == ctx->settings.end())
        ctx->settings[ctx->id("placerHeap/beta")] = std::to_string(0.9);
    if (ctx->settings.find(ctx->id("placerHeap/criticalityExponent")) == ctx->settings.end())
        ctx->settings[ctx->id("placerHeap/criticalityExponent")] = std::to_string(2);
    if (ctx->settings.find(ctx->id("placerHeap/timingWeight")) == ctx->settings.end())
        ctx->settings[ctx->id("placerHeap/timingWeight")] = std::to_string(10);

    if (vm.count("detailed-timing-report")) {
        ctx->detailed_timing_report = true;
    }
}

int CommandHandler::executeMain(std::unique_ptr<Context> ctx)
{
    if (vm.count("on-failure")) {
        global_command_handler = this;
        std::set_terminate(script_terminate_handler);
    }
    if (vm.count("test")) {
        ctx->archcheck();
        return 0;
    }

    if (vm.count("top")) {
        ctx->settings[ctx->id("frontend/top")] = vm["top"].as<std::string>();
    }

#ifndef NO_GUI
    if (vm.count("gui")) {
        Application a(argc, argv, (vm.count("gui-no-aa") > 0));
        MainWindow w(std::move(ctx), this);
        try {
            if (vm.count("json")) {
                std::string filename = vm["json"].as<std::string>();
                std::ifstream f(filename);
                if (!parse_json(f, filename, w.getContext()))
                    log_error("Loading design failed.\n");
                customAfterLoad(w.getContext());
                w.notifyChangeContext();
                w.updateActions();
            } else
                w.notifyChangeContext();
        } catch (log_execution_error_exception) {
            // show error is handled by gui itself
        }
        w.show();

        return a.exec();
    }
#endif
    if (vm.count("json")) {
        std::string filename = vm["json"].as<std::string>();
        std::ifstream f(filename);
        if (!parse_json(f, filename, ctx.get()))
            log_error("Loading design failed.\n");

        customAfterLoad(ctx.get());
    }

#ifndef NO_PYTHON
    init_python(argv[0]);
    python_export_global("ctx", *ctx);

    if (vm.count("run")) {

        std::vector<std::string> files = vm["run"].as<std::vector<std::string>>();
        for (auto filename : files)
            execute_python_file(filename.c_str());
    } else
#endif
            if (ctx->design_loaded) {
        bool do_pack = vm.count("pack-only") != 0 || vm.count("no-pack") == 0;
        bool do_place = vm.count("pack-only") == 0 && vm.count("no-place") == 0;
        bool do_route = vm.count("pack-only") == 0 && vm.count("no-route") == 0;

        if (do_pack) {
            run_script_hook("pre-pack");
            if (!ctx->pack() && !ctx->force)
                log_error("Packing design failed.\n");
        }
        ctx->check();
        print_utilisation(ctx.get());

        if (do_place) {
            run_script_hook("pre-place");
            bool saved_debug = ctx->debug;
            if (vm.count("debug-placer"))
                ctx->debug = true;
            if (!ctx->place() && !ctx->force)
                log_error("Placing design failed.\n");
            ctx->debug = saved_debug;
            ctx->check();
            if (vm.count("placed-svg"))
                ctx->writeSVG(vm["placed-svg"].as<std::string>(), "scale=50 hide_routing");
        }

        if (do_route) {
            run_script_hook("pre-route");
            bool saved_debug = ctx->debug;
            if (vm.count("debug-router"))
                ctx->debug = true;
            if (!ctx->route() && !ctx->force)
                log_error("Routing design failed.\n");
            ctx->debug = saved_debug;
            run_script_hook("post-route");
            if (vm.count("routed-svg"))
                ctx->writeSVG(vm["routed-svg"].as<std::string>(), "scale=500");
        }

        customBitstream(ctx.get());
    }

    if (vm.count("write")) {
        std::string filename = vm["write"].as<std::string>();
        std::ofstream f(filename);
        if (!write_json_file(f, filename, ctx.get()))
            log_error("Saving design failed.\n");
    }

    if (vm.count("sdf")) {
        std::string filename = vm["sdf"].as<std::string>();
        std::ofstream f(filename);
        if (!f)
            log_error("Failed to open SDF file '%s' for writing.\n", filename.c_str());
        ctx->writeSDF(f, vm.count("sdf-cvc"));
    }

    if (vm.count("report")) {
        std::string filename = vm["report"].as<std::string>();
        std::ofstream f(filename);
        if (!f)
            log_error("Failed to open report file '%s' for writing.\n", filename.c_str());
        ctx->writeJsonReport(f);
    }

#ifndef NO_PYTHON
    deinit_python();
#endif

    return had_nonfatal_error ? 1 : 0;
}

void CommandHandler::conflicting_options(const boost::program_options::variables_map &vm, const char *opt1,
                                         const char *opt2)
{
    if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) && !vm[opt2].defaulted()) {
        std::string msg = "Conflicting options '" + std::string(opt1) + "' and '" + std::string(opt2) + "'.";
        log_error("%s\n", msg.c_str());
    }
}

void CommandHandler::printFooter()
{
    int warning_count = get_or_default(message_count_by_level, LogLevel::WARNING_MSG, 0),
        error_count = get_or_default(message_count_by_level, LogLevel::ERROR_MSG, 0);
    if (warning_count > 0 || error_count > 0)
        log_always("%d warning%s, %d error%s\n", warning_count, warning_count == 1 ? "" : "s", error_count,
                   error_count == 1 ? "" : "s");
}

int CommandHandler::exec()
{
    try {
        if (!parseOptions())
            return -1;

        if (executeBeforeContext())
            return 0;

        dict<std::string, Property> values;
        std::unique_ptr<Context> ctx = createContext(values);
        setupContext(ctx.get());
        setupArchContext(ctx.get());
        int rc = executeMain(std::move(ctx));
        printFooter();
        log_break();
        log_info("Program finished normally.\n");
        return rc;
    } catch (log_execution_error_exception) {
        printFooter();
        return -1;
    }
}

void CommandHandler::load_json(Context *ctx, std::string filename)
{
    setupContext(ctx);
    setupArchContext(ctx);
    {
        std::ifstream f(filename);
        if (!parse_json(f, filename, ctx))
            log_error("Loading design failed.\n");
    }
}

void CommandHandler::clear() { vm.clear(); }

void CommandHandler::run_script_hook(const std::string &name)
{
#ifndef NO_PYTHON
    if (vm.count(name)) {
        std::vector<std::string> files = vm[name].as<std::vector<std::string>>();
        for (auto filename : files)
            execute_python_file(filename.c_str());
    }
#endif
}

NEXTPNR_NAMESPACE_END

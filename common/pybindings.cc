/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#ifndef NO_PYTHON

#include "pybindings.h"
#include "arch_pybindings.h"
#include "jsonparse.h"
#include "nextpnr.h"

#include <fstream>
#include <memory>
#include <signal.h>
NEXTPNR_NAMESPACE_BEGIN

// Required to determine concatenated module name (which differs for different
// archs)
#define PASTER(x, y) x##_##y
#define EVALUATOR(x, y) PASTER(x, y)
#define MODULE_NAME EVALUATOR(nextpnrpy, ARCHNAME)
#define PYINIT_MODULE_NAME EVALUATOR(&PyInit_nextpnrpy, ARCHNAME)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Architecture-specific bindings should be created in the below function, which
// must be implemented in all architectures
void arch_wrap_python();

bool operator==(const PortRef &a, const PortRef &b) { return (a.cell == b.cell) && (a.port == b.port); }

// Load a JSON file into a design
void parse_json_shim(std::string filename, Context &d)
{
    std::ifstream inf(filename);
    if (!inf)
        throw std::runtime_error("failed to open file " + filename);
    parse_json_file(inf, filename, &d);
}

// Create a new Chip and load design from json file
Context *load_design_shim(std::string filename, ArchArgs args)
{
    Context *d = new Context(args);
    parse_json_shim(filename, *d);
    return d;
}

void translate_assertfail(const assertion_failure &e)
{
    // Use the Python 'C' API to set up an exception object
    PyErr_SetString(PyExc_AssertionError, e.what());
}

namespace PythonConversion {
template <> struct string_converter<PortRef &>
{
    inline PortRef from_str(Context *ctx, std::string name) { NPNR_ASSERT_FALSE("PortRef from_str not implemented"); }

    inline std::string to_str(Context *ctx, const PortRef &pr)
    {
        return pr.cell->name.str(ctx) + "." + pr.port.str(ctx);
    }
};

} // namespace PythonConversion

BOOST_PYTHON_MODULE(MODULE_NAME)
{
    register_exception_translator<assertion_failure>(&translate_assertfail);

    using namespace PythonConversion;

    class_<GraphicElement>("GraphicElement")
            .def_readwrite("type", &GraphicElement::type)
            .def_readwrite("x1", &GraphicElement::x1)
            .def_readwrite("y1", &GraphicElement::y1)
            .def_readwrite("x2", &GraphicElement::x2)
            .def_readwrite("y2", &GraphicElement::y2)
            .def_readwrite("text", &GraphicElement::text);

    enum_<PortType>("PortType")
            .value("PORT_IN", PORT_IN)
            .value("PORT_OUT", PORT_OUT)
            .value("PORT_INOUT", PORT_INOUT)
            .export_values();

    typedef std::unordered_map<IdString, std::string> AttrMap;
    typedef std::unordered_map<IdString, PortInfo> PortMap;
    typedef std::unordered_map<IdString, IdString> PinMap;

    class_<BaseCtx, BaseCtx *, boost::noncopyable>("BaseCtx", no_init);

    auto ci_cls = class_<ContextualWrapper<CellInfo &>>("CellInfo", no_init);
    readwrite_wrapper<CellInfo &, decltype(&CellInfo::name), &CellInfo::name, conv_to_str<IdString>,
                      conv_from_str<IdString>>::def_wrap(ci_cls, "name");
    readwrite_wrapper<CellInfo &, decltype(&CellInfo::type), &CellInfo::type, conv_to_str<IdString>,
                      conv_from_str<IdString>>::def_wrap(ci_cls, "type");
    readonly_wrapper<CellInfo &, decltype(&CellInfo::attrs), &CellInfo::attrs, wrap_context<AttrMap &>>::def_wrap(
            ci_cls, "attrs");
    readonly_wrapper<CellInfo &, decltype(&CellInfo::params), &CellInfo::params, wrap_context<AttrMap &>>::def_wrap(
            ci_cls, "params");
    readonly_wrapper<CellInfo &, decltype(&CellInfo::ports), &CellInfo::ports, wrap_context<PortMap &>>::def_wrap(
            ci_cls, "ports");
    readwrite_wrapper<CellInfo &, decltype(&CellInfo::bel), &CellInfo::bel, conv_to_str<BelId>,
                      conv_from_str<BelId>>::def_wrap(ci_cls, "bel");
    readwrite_wrapper<CellInfo &, decltype(&CellInfo::belStrength), &CellInfo::belStrength, pass_through<PlaceStrength>,
                      pass_through<PlaceStrength>>::def_wrap(ci_cls, "belStrength");
    readonly_wrapper<CellInfo &, decltype(&CellInfo::pins), &CellInfo::pins, wrap_context<PinMap &>>::def_wrap(ci_cls,
                                                                                                               "pins");

    auto pi_cls = class_<ContextualWrapper<PortInfo &>>("PortInfo", no_init);
    readwrite_wrapper<PortInfo &, decltype(&PortInfo::name), &PortInfo::name, conv_to_str<IdString>,
                      conv_from_str<IdString>>::def_wrap(pi_cls, "name");
    readonly_wrapper<PortInfo &, decltype(&PortInfo::net), &PortInfo::net, deref_and_wrap<NetInfo>>::def_wrap(pi_cls,
                                                                                                              "net");
    readwrite_wrapper<PortInfo &, decltype(&PortInfo::type), &PortInfo::type, pass_through<PortType>,
                      pass_through<PortType>>::def_wrap(pi_cls, "type");

    typedef std::vector<PortRef> PortRefVector;
    typedef std::unordered_map<WireId, PipMap> WireMap;

    auto ni_cls = class_<ContextualWrapper<NetInfo &>>("NetInfo", no_init);
    readwrite_wrapper<NetInfo &, decltype(&NetInfo::name), &NetInfo::name, conv_to_str<IdString>,
                      conv_from_str<IdString>>::def_wrap(ni_cls, "name");
    readwrite_wrapper<NetInfo &, decltype(&NetInfo::driver), &NetInfo::driver, wrap_context<PortRef &>,
                      unwrap_context<PortRef &>>::def_wrap(ni_cls, "driver");
    readonly_wrapper<NetInfo &, decltype(&NetInfo::users), &NetInfo::users, wrap_context<PortRefVector &>>::def_wrap(
            ni_cls, "users");
    readonly_wrapper<NetInfo &, decltype(&NetInfo::wires), &NetInfo::wires, wrap_context<WireMap &>>::def_wrap(ni_cls,
                                                                                                               "wires");

    auto pr_cls = class_<ContextualWrapper<PortRef &>>("PortRef", no_init);
    readonly_wrapper<PortRef &, decltype(&PortRef::cell), &PortRef::cell, deref_and_wrap<CellInfo>>::def_wrap(pr_cls,
                                                                                                              "cell");
    readwrite_wrapper<PortRef &, decltype(&PortRef::port), &PortRef::port, conv_to_str<IdString>,
                      conv_from_str<IdString>>::def_wrap(pr_cls, "port");
    readwrite_wrapper<PortRef &, decltype(&PortRef::budget), &PortRef::budget, pass_through<delay_t>,
                      pass_through<delay_t>>::def_wrap(pr_cls, "budget");

    auto pm_cls = class_<ContextualWrapper<PipMap &>>("PipMap", no_init);
    readwrite_wrapper<PipMap &, decltype(&PipMap::pip), &PipMap::pip, conv_to_str<PipId>,
                      conv_from_str<PipId>>::def_wrap(pm_cls, "pip");
    readwrite_wrapper<PipMap &, decltype(&PipMap::strength), &PipMap::strength, pass_through<PlaceStrength>,
                      pass_through<PlaceStrength>>::def_wrap(pm_cls, "strength");

    def("parse_json", parse_json_shim);
    def("load_design", load_design_shim, return_value_policy<manage_new_object>());

    WRAP_MAP(AttrMap, pass_through<std::string>, "AttrMap");
    WRAP_MAP(PortMap, wrap_context<PortInfo &>, "PortMap");
    WRAP_MAP(PinMap, conv_to_str<IdString>, "PinMap");
    WRAP_MAP(WireMap, wrap_context<PipMap &>, "WireMap");

    WRAP_VECTOR(PortRefVector, wrap_context<PortRef &>);

    arch_wrap_python();
}

#ifdef MAIN_EXECUTABLE
static wchar_t *program;
#endif

void init_python(const char *executable, bool first)
{
#ifdef MAIN_EXECUTABLE
    program = Py_DecodeLocale(executable, NULL);
    if (program == NULL) {
        fprintf(stderr, "Fatal error: cannot decode executable filename\n");
        exit(1);
    }
    try {
        if (first)
            PyImport_AppendInittab(TOSTRING(MODULE_NAME), PYINIT_MODULE_NAME);
        Py_SetProgramName(program);
        Py_Initialize();
        if (first)
            PyImport_ImportModule(TOSTRING(MODULE_NAME));
    } catch (boost::python::error_already_set const &) {
        // Parse and output the exception
        std::string perror_str = parse_python_exception();
        std::cout << "Error in Python: " << perror_str << std::endl;
    }
    signal(SIGINT, SIG_DFL);
#endif
}

void deinit_python()
{
#ifdef MAIN_EXECUTABLE
    Py_Finalize();
    PyMem_RawFree(program);
#endif
}

void execute_python_file(const char *python_file)
{
    try {
        FILE *fp = fopen(python_file, "r");
        if (fp == NULL) {
            fprintf(stderr, "Fatal error: file not found %s\n", python_file);
            exit(1);
        }
        PyRun_SimpleFile(fp, python_file);
        fclose(fp);
    } catch (boost::python::error_already_set const &) {
        // Parse and output the exception
        std::string perror_str = parse_python_exception();
        std::cout << "Error in Python: " << perror_str << std::endl;
    }
}

NEXTPNR_NAMESPACE_END

#endif // NO_PYTHON

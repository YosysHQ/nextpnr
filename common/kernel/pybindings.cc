/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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
#include "json_frontend.h"
#include "log.h"
#include "nextpnr.h"
#include "rust.h"

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
void arch_wrap_python(py::module &m);

bool operator==(const PortRef &a, const PortRef &b) { return (a.cell == b.cell) && (a.port == b.port); }

// Load a JSON file into a design
void parse_json_shim(std::string filename, Context &d)
{
    std::ifstream inf(filename);
    if (!inf)
        throw std::runtime_error("failed to open file " + filename);
    parse_json(inf, filename, &d);
}

// Create a new Chip and load design from json file
Context *load_design_shim(std::string filename, ArchArgs args)
{
    Context *d = new Context(args);
    parse_json_shim(filename, *d);
    return d;
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

template <> struct string_converter<Property>
{
    inline Property from_str(Context *ctx, std::string s) { return Property::from_string(s); }

    inline std::string to_str(Context *ctx, Property p) { return p.to_string(); }
};

} // namespace PythonConversion

std::string loc_repr_py(Loc loc) { return stringf("Loc(%d, %d, %d)", loc.x, loc.y, loc.z); }

PYBIND11_EMBEDDED_MODULE(MODULE_NAME, m)
{
    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p)
                std::rethrow_exception(p);
        } catch (const assertion_failure &e) {
            PyErr_SetString(PyExc_AssertionError, e.what());
        }
    });

    using namespace PythonConversion;

    py::enum_<GraphicElement::type_t>(m, "GraphicElementType")
            .value("TYPE_NONE", GraphicElement::TYPE_NONE)
            .value("TYPE_LINE", GraphicElement::TYPE_LINE)
            .value("TYPE_ARROW", GraphicElement::TYPE_ARROW)
            .value("TYPE_BOX", GraphicElement::TYPE_BOX)
            .value("TYPE_CIRCLE", GraphicElement::TYPE_CIRCLE)
            .value("TYPE_LABEL", GraphicElement::TYPE_LABEL)
            .export_values();

    py::enum_<GraphicElement::style_t>(m, "GraphicElementStyle")
            .value("STYLE_GRID", GraphicElement::STYLE_GRID)
            .value("STYLE_FRAME", GraphicElement::STYLE_FRAME)
            .value("STYLE_HIDDEN", GraphicElement::STYLE_HIDDEN)
            .value("STYLE_INACTIVE", GraphicElement::STYLE_INACTIVE)
            .value("STYLE_ACTIVE", GraphicElement::STYLE_ACTIVE)
            .export_values();

    py::class_<GraphicElement>(m, "GraphicElement")
            .def(py::init<GraphicElement::type_t, GraphicElement::style_t, float, float, float, float, float>(),
                 py::arg("type"), py::arg("style"), py::arg("x1"), py::arg("y1"), py::arg("x2"), py::arg("y2"),
                 py::arg("z"))
            .def_readwrite("type", &GraphicElement::type)
            .def_readwrite("x1", &GraphicElement::x1)
            .def_readwrite("y1", &GraphicElement::y1)
            .def_readwrite("x2", &GraphicElement::x2)
            .def_readwrite("y2", &GraphicElement::y2)
            .def_readwrite("text", &GraphicElement::text);

    py::enum_<PortType>(m, "PortType")
            .value("PORT_IN", PORT_IN)
            .value("PORT_OUT", PORT_OUT)
            .value("PORT_INOUT", PORT_INOUT)
            .export_values();

    py::enum_<PlaceStrength>(m, "PlaceStrength")
            .value("STRENGTH_NONE", STRENGTH_NONE)
            .value("STRENGTH_WEAK", STRENGTH_WEAK)
            .value("STRENGTH_STRONG", STRENGTH_STRONG)
            .value("STRENGTH_FIXED", STRENGTH_FIXED)
            .value("STRENGTH_LOCKED", STRENGTH_LOCKED)
            .value("STRENGTH_USER", STRENGTH_USER)
            .export_values();

    py::class_<DelayPair>(m, "DelayPair")
            .def(py::init<>())
            .def(py::init<delay_t>())
            .def(py::init<delay_t, delay_t>())
            .def_readwrite("min_delay", &DelayPair::min_delay)
            .def_readwrite("max_delay", &DelayPair::max_delay)
            .def("minDelay", &DelayPair::minDelay)
            .def("maxDelay", &DelayPair::maxDelay);

    py::class_<DelayQuad>(m, "DelayQuad")
            .def(py::init<>())
            .def(py::init<delay_t>())
            .def(py::init<delay_t, delay_t>())
            .def(py::init<delay_t, delay_t, delay_t, delay_t>())
            .def(py::init<DelayPair, DelayPair>())
            .def_readwrite("rise", &DelayQuad::rise)
            .def_readwrite("fall", &DelayQuad::fall)
            .def("minDelay", &DelayQuad::minDelay)
            .def("minRiseDelay", &DelayQuad::minRiseDelay)
            .def("minFallDelay", &DelayQuad::minFallDelay)
            .def("maxDelay", &DelayQuad::maxDelay)
            .def("maxRiseDelay", &DelayQuad::maxRiseDelay)
            .def("maxFallDelay", &DelayQuad::maxFallDelay)
            .def("delayPair", &DelayQuad::delayPair);

    typedef dict<IdString, Property> AttrMap;
    typedef dict<IdString, PortInfo> PortMap;
    typedef dict<IdString, IdString> IdIdMap;
    typedef dict<IdString, std::unique_ptr<Region>> RegionMap;

    py::class_<BaseCtx>(m, "BaseCtx");

    auto loc_cls = py::class_<Loc>(m, "Loc")
                           .def(py::init<int, int, int>())
                           .def_readwrite("x", &Loc::x)
                           .def_readwrite("y", &Loc::y)
                           .def_readwrite("z", &Loc::z)
                           .def("__repr__", loc_repr_py);

    auto ci_cls = py::class_<ContextualWrapper<CellInfo &>>(m, "CellInfo");
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
    readonly_wrapper<CellInfo &, decltype(&CellInfo::bel), &CellInfo::bel, conv_to_str<BelId>>::def_wrap(ci_cls, "bel");
    readwrite_wrapper<CellInfo &, decltype(&CellInfo::belStrength), &CellInfo::belStrength, pass_through<PlaceStrength>,
                      pass_through<PlaceStrength>>::def_wrap(ci_cls, "belStrength");

    fn_wrapper_1a_v<CellInfo &, decltype(&CellInfo::addInput), &CellInfo::addInput, conv_from_str<IdString>>::def_wrap(
            ci_cls, "addInput");
    fn_wrapper_1a_v<CellInfo &, decltype(&CellInfo::addOutput), &CellInfo::addOutput,
                    conv_from_str<IdString>>::def_wrap(ci_cls, "addOutput");
    fn_wrapper_1a_v<CellInfo &, decltype(&CellInfo::addInout), &CellInfo::addInout, conv_from_str<IdString>>::def_wrap(
            ci_cls, "addInout");

    fn_wrapper_2a_v<CellInfo &, decltype(&CellInfo::setParam), &CellInfo::setParam, conv_from_str<IdString>,
                    conv_from_str<Property>>::def_wrap(ci_cls, "setParam");
    fn_wrapper_1a_v<CellInfo &, decltype(&CellInfo::unsetParam), &CellInfo::unsetParam,
                    conv_from_str<IdString>>::def_wrap(ci_cls, "unsetParam");
    fn_wrapper_2a_v<CellInfo &, decltype(&CellInfo::setAttr), &CellInfo::setAttr, conv_from_str<IdString>,
                    conv_from_str<Property>>::def_wrap(ci_cls, "setAttr");
    fn_wrapper_1a_v<CellInfo &, decltype(&CellInfo::unsetAttr), &CellInfo::unsetAttr,
                    conv_from_str<IdString>>::def_wrap(ci_cls, "unsetAttr");

    auto pi_cls = py::class_<ContextualWrapper<PortInfo &>>(m, "PortInfo");
    readwrite_wrapper<PortInfo &, decltype(&PortInfo::name), &PortInfo::name, conv_to_str<IdString>,
                      conv_from_str<IdString>>::def_wrap(pi_cls, "name");
    readonly_wrapper<PortInfo &, decltype(&PortInfo::net), &PortInfo::net, deref_and_wrap<NetInfo>>::def_wrap(pi_cls,
                                                                                                              "net");
    readwrite_wrapper<PortInfo &, decltype(&PortInfo::type), &PortInfo::type, pass_through<PortType>,
                      pass_through<PortType>>::def_wrap(pi_cls, "type");

    typedef indexed_store<PortRef> PortRefVector;
    typedef dict<WireId, PipMap> WireMap;
    typedef pool<BelId> BelSet;
    typedef pool<WireId> WireSet;

    auto ni_cls = py::class_<ContextualWrapper<NetInfo &>>(m, "NetInfo");
    readwrite_wrapper<NetInfo &, decltype(&NetInfo::name), &NetInfo::name, conv_to_str<IdString>,
                      conv_from_str<IdString>>::def_wrap(ni_cls, "name");
    readonly_wrapper<NetInfo &, decltype(&NetInfo::driver), &NetInfo::driver, wrap_context<PortRef>>::def_wrap(
            ni_cls, "driver");
    readonly_wrapper<NetInfo &, decltype(&NetInfo::users), &NetInfo::users, wrap_context<PortRefVector &>>::def_wrap(
            ni_cls, "users");
    readonly_wrapper<NetInfo &, decltype(&NetInfo::wires), &NetInfo::wires, wrap_context<WireMap &>>::def_wrap(ni_cls,
                                                                                                               "wires");

    auto pr_cls = py::class_<ContextualWrapper<PortRef>>(m, "PortRef");
    readonly_wrapper<PortRef, decltype(&PortRef::cell), &PortRef::cell, deref_and_wrap<CellInfo>>::def_wrap(pr_cls,
                                                                                                            "cell");
    readonly_wrapper<PortRef, decltype(&PortRef::port), &PortRef::port, conv_to_str<IdString>>::def_wrap(pr_cls,
                                                                                                         "port");

    auto pm_cls = py::class_<ContextualWrapper<PipMap &>>(m, "PipMap");
    readwrite_wrapper<PipMap &, decltype(&PipMap::pip), &PipMap::pip, conv_to_str<PipId>,
                      conv_from_str<PipId>>::def_wrap(pm_cls, "pip");
    readwrite_wrapper<PipMap &, decltype(&PipMap::strength), &PipMap::strength, pass_through<PlaceStrength>,
                      pass_through<PlaceStrength>>::def_wrap(pm_cls, "strength");

    m.def("parse_json", parse_json_shim);
    m.def("load_design", load_design_shim, py::return_value_policy::take_ownership);
#ifdef USE_RUST
    m.def("example_printnets", example_printnets);
#endif

    auto region_cls = py::class_<ContextualWrapper<Region &>>(m, "Region");
    readwrite_wrapper<Region &, decltype(&Region::name), &Region::name, conv_to_str<IdString>,
                      conv_from_str<IdString>>::def_wrap(region_cls, "name");
    readwrite_wrapper<Region &, decltype(&Region::constr_bels), &Region::constr_bels, pass_through<bool>,
                      pass_through<bool>>::def_wrap(region_cls, "constr_bels");
    readwrite_wrapper<Region &, decltype(&Region::constr_wires), &Region::constr_wires, pass_through<bool>,
                      pass_through<bool>>::def_wrap(region_cls, "constr_bels");
    readwrite_wrapper<Region &, decltype(&Region::constr_pips), &Region::constr_pips, pass_through<bool>,
                      pass_through<bool>>::def_wrap(region_cls, "constr_pips");
    readonly_wrapper<Region &, decltype(&Region::bels), &Region::bels, wrap_context<BelSet &>>::def_wrap(region_cls,
                                                                                                         "bels");
    readonly_wrapper<Region &, decltype(&Region::wires), &Region::wires, wrap_context<WireSet &>>::def_wrap(region_cls,
                                                                                                            "wires");

    auto hierarchy_cls = py::class_<ContextualWrapper<HierarchicalCell &>>(m, "HierarchicalCell");
    readwrite_wrapper<HierarchicalCell &, decltype(&HierarchicalCell::name), &HierarchicalCell::name,
                      conv_to_str<IdString>, conv_from_str<IdString>>::def_wrap(hierarchy_cls, "name");
    readwrite_wrapper<HierarchicalCell &, decltype(&HierarchicalCell::type), &HierarchicalCell::type,
                      conv_to_str<IdString>, conv_from_str<IdString>>::def_wrap(hierarchy_cls, "type");
    readwrite_wrapper<HierarchicalCell &, decltype(&HierarchicalCell::parent), &HierarchicalCell::parent,
                      conv_to_str<IdString>, conv_from_str<IdString>>::def_wrap(hierarchy_cls, "parent");
    readwrite_wrapper<HierarchicalCell &, decltype(&HierarchicalCell::fullpath), &HierarchicalCell::fullpath,
                      conv_to_str<IdString>, conv_from_str<IdString>>::def_wrap(hierarchy_cls, "fullpath");

    readonly_wrapper<HierarchicalCell &, decltype(&HierarchicalCell::leaf_cells), &HierarchicalCell::leaf_cells,
                     wrap_context<IdIdMap &>>::def_wrap(hierarchy_cls, "leaf_cells");
    readonly_wrapper<HierarchicalCell &, decltype(&HierarchicalCell::nets), &HierarchicalCell::nets,
                     wrap_context<IdIdMap &>>::def_wrap(hierarchy_cls, "nets");
    readonly_wrapper<HierarchicalCell &, decltype(&HierarchicalCell::hier_cells), &HierarchicalCell::hier_cells,
                     wrap_context<IdIdMap &>>::def_wrap(hierarchy_cls, "hier_cells");
    WRAP_MAP(m, AttrMap, conv_to_str<Property>, "AttrMap");
    WRAP_MAP(m, PortMap, wrap_context<PortInfo &>, "PortMap");
    WRAP_MAP(m, IdIdMap, conv_to_str<IdString>, "IdIdMap");
    WRAP_MAP(m, WireMap, wrap_context<PipMap &>, "WireMap");
    WRAP_MAP_UPTR(m, RegionMap, "RegionMap");

    WRAP_INDEXSTORE(m, PortRefVector, wrap_context<PortRef>);

    typedef dict<IdString, ClockFmax> ClockFmaxMap;
    WRAP_MAP(m, ClockFmaxMap, pass_through<ClockFmax>, "ClockFmaxMap");

    auto clk_fmax_cls = py::class_<ClockFmax>(m, "ClockFmax")
                                .def_readonly("achieved", &ClockFmax::achieved)
                                .def_readonly("constraint", &ClockFmax::constraint);

    auto tmg_result_cls = py::class_<ContextualWrapper<TimingResult &>>(m, "TimingResult");
    readonly_wrapper<TimingResult &, decltype(&TimingResult::clock_fmax), &TimingResult::clock_fmax,
                     wrap_context<ClockFmaxMap &>>::def_wrap(tmg_result_cls, "clock_fmax");
    arch_wrap_python(m);
}

void (*python_sighandler)(int) = nullptr;

void init_python(const char *executable)
{
#ifdef MAIN_EXECUTABLE
    static const char *python_argv[1];
    python_argv[0] = executable;
    py::initialize_interpreter(true, 1, python_argv);
    py::module::import(TOSTRING(MODULE_NAME));
    PyRun_SimpleString("from " TOSTRING(MODULE_NAME) " import *");
    python_sighandler = signal(SIGINT, SIG_DFL);
#endif
}

void deinit_python()
{
#ifdef MAIN_EXECUTABLE
    py::finalize_interpreter();
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
        if (python_sighandler)
            signal(SIGINT, python_sighandler);
        int result = PyRun_SimpleFile(fp, python_file);
        signal(SIGINT, SIG_DFL);
        fclose(fp);
        if (result == -1) {
            log_error("Error occurred while executing Python script %s\n", python_file);
        }
    } catch (py::error_already_set const &) {
        // Parse and output the exception
        std::string perror_str = parse_python_exception();
        signal(SIGINT, SIG_DFL);
        log_error("Error in Python: %s\n", perror_str.c_str());
    }
}

NEXTPNR_NAMESPACE_END

#endif // NO_PYTHON

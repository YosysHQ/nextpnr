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

#include "arch_pybindings.h"
#include "nextpnr.h"
#include "pybindings.h"
#include "pywrappers.h"

using namespace pybind11::literals;

NEXTPNR_NAMESPACE_BEGIN
namespace PythonConversion {
template <> struct string_converter<const IdString &>
{
    const IdString &from_str(Context *ctx, std::string name) { NPNR_ASSERT_FALSE("unsupported"); }

    std::string to_str(Context *ctx, const IdString &id) { return id.str(ctx); }
};

} // namespace PythonConversion

void arch_wrap_python(py::module &m)
{
    using namespace PythonConversion;

    typedef linear_range<BelId> BelRange;
    typedef linear_range<WireId> WireRange;
    typedef linear_range<PipId> AllPipRange;

    typedef const std::vector<PipId> &UphillPipRange;
    typedef const std::vector<PipId> &DownhillPipRange;

    typedef const std::vector<BelBucketId> &BelBucketRange;
    typedef const std::vector<BelId> &BelRangeForBelBucket;
    typedef const std::vector<BelPin> &BelPinRange;

    auto arch_cls = py::class_<Arch, BaseCtx>(m, "Arch").def(py::init<ArchArgs>());

    auto ctx_cls = py::class_<Context, Arch>(m, "Context")
                           .def("checksum", &Context::checksum)
                           .def("pack", &Context::pack)
                           .def("place", &Context::place)
                           .def("route", &Context::route);

    auto belpin_cls =
            py::class_<BelPin>(m, "BelPin").def_readwrite("bel", &BelPin::bel).def_readwrite("pin", &BelPin::pin);

    typedef dict<IdString, std::unique_ptr<CellInfo>> CellMap;
    typedef dict<IdString, std::unique_ptr<NetInfo>> NetMap;
    typedef dict<IdString, HierarchicalCell> HierarchyMap;
    typedef dict<IdString, IdString> AliasMap;
    typedef dict<IdString, HierarchicalCell> HierarchyMap;

#include "arch_pybindings_shared.h"

    // Generic arch construction API
    fn_wrapper_4a_v<Context, decltype(&Context::addWire), &Context::addWire, conv_from_str<IdStringList>,
                    conv_from_str<IdString>, pass_through<int>, pass_through<int>>::def_wrap(ctx_cls, "addWire",
                                                                                             "name"_a, "type"_a, "x"_a,
                                                                                             "y"_a);
    fn_wrapper_6a_v<Context, decltype(&Context::addPip), &Context::addPip, conv_from_str<IdStringList>,
                    conv_from_str<IdString>, conv_from_str<WireId>, conv_from_str<WireId>, pass_through<delay_t>,
                    pass_through<Loc>>::def_wrap(ctx_cls, "addPip", "name"_a, "type"_a, "srcWire"_a, "dstWire"_a,
                                                 "delay"_a, "loc"_a);

    fn_wrapper_5a_v<Context, decltype(&Context::addBel), &Context::addBel, conv_from_str<IdStringList>,
                    conv_from_str<IdString>, pass_through<Loc>, pass_through<bool>,
                    pass_through<bool>>::def_wrap(ctx_cls, "addBel", "name"_a, "type"_a, "loc"_a, "gb"_a, "hidden"_a);
    fn_wrapper_3a_v<Context, decltype(&Context::addBelInput), &Context::addBelInput, conv_from_str<BelId>,
                    conv_from_str<IdString>, conv_from_str<WireId>>::def_wrap(ctx_cls, "addBelInput", "bel"_a, "name"_a,
                                                                              "wire"_a);
    fn_wrapper_3a_v<Context, decltype(&Context::addBelOutput), &Context::addBelOutput, conv_from_str<BelId>,
                    conv_from_str<IdString>, conv_from_str<WireId>>::def_wrap(ctx_cls, "addBelOutput", "bel"_a,
                                                                              "name"_a, "wire"_a);
    fn_wrapper_3a_v<Context, decltype(&Context::addBelInout), &Context::addBelInout, conv_from_str<BelId>,
                    conv_from_str<IdString>, conv_from_str<WireId>>::def_wrap(ctx_cls, "addBelInout", "bel"_a, "name"_a,
                                                                              "wire"_a);
    fn_wrapper_4a_v<Context, decltype(&Context::addBelPin), &Context::addBelPin, conv_from_str<BelId>,
                    conv_from_str<IdString>, conv_from_str<WireId>, pass_through<PortType>>::def_wrap(ctx_cls,
                                                                                                      "addBelPin",
                                                                                                      "bel"_a, "name"_a,
                                                                                                      "wire"_a,
                                                                                                      "type"_a);

    fn_wrapper_2a_v<Context, decltype(&Context::addGroupBel), &Context::addGroupBel, conv_from_str<IdStringList>,
                    conv_from_str<BelId>>::def_wrap(ctx_cls, "addGroupBel", "group"_a, "bel"_a);
    fn_wrapper_2a_v<Context, decltype(&Context::addGroupWire), &Context::addGroupWire, conv_from_str<IdStringList>,
                    conv_from_str<WireId>>::def_wrap(ctx_cls, "addGroupWire", "group"_a, "wire"_a);
    fn_wrapper_2a_v<Context, decltype(&Context::addGroupPip), &Context::addGroupPip, conv_from_str<IdStringList>,
                    conv_from_str<PipId>>::def_wrap(ctx_cls, "addGroupPip", "group"_a, "pip"_a);
    fn_wrapper_2a_v<Context, decltype(&Context::addGroupGroup), &Context::addGroupGroup, conv_from_str<IdStringList>,
                    conv_from_str<IdStringList>>::def_wrap(ctx_cls, "addGroupGroup", "group"_a, "grp"_a);

    fn_wrapper_2a_v<Context, decltype(&Context::addDecalGraphic), &Context::addDecalGraphic,
                    conv_from_str<IdStringList>, pass_through<GraphicElement>>::def_wrap(ctx_cls, "addDecalGraphic",
                                                                                         (py::arg("decal"), "graphic"));
    fn_wrapper_4a_v<Context, decltype(&Context::setWireDecal), &Context::setWireDecal, conv_from_str<WireId>,
                    pass_through<float>, pass_through<float>, conv_from_str<IdStringList>>::def_wrap(ctx_cls,
                                                                                                     "setWireDecal",
                                                                                                     "wire"_a, "x"_a,
                                                                                                     "y"_a, "decal"_a);
    fn_wrapper_4a_v<Context, decltype(&Context::setPipDecal), &Context::setPipDecal, conv_from_str<PipId>,
                    pass_through<float>, pass_through<float>, conv_from_str<IdStringList>>::def_wrap(ctx_cls,
                                                                                                     "setPipDecal",
                                                                                                     "pip"_a, "x"_a,
                                                                                                     "y"_a, "decal"_a);
    fn_wrapper_4a_v<Context, decltype(&Context::setBelDecal), &Context::setBelDecal, conv_from_str<BelId>,
                    pass_through<float>, pass_through<float>, conv_from_str<IdStringList>>::def_wrap(ctx_cls,
                                                                                                     "setBelDecal",
                                                                                                     "bel"_a, "x"_a,
                                                                                                     "y"_a, "decal"_a);
    fn_wrapper_4a_v<Context, decltype(&Context::setGroupDecal), &Context::setGroupDecal, conv_from_str<GroupId>,
                    pass_through<float>, pass_through<float>, conv_from_str<IdStringList>>::def_wrap(ctx_cls,
                                                                                                     "setGroupDecal",
                                                                                                     "group"_a, "x"_a,
                                                                                                     "y"_a, "decal"_a);

    fn_wrapper_3a_v<Context, decltype(&Context::setWireAttr), &Context::setWireAttr, conv_from_str<WireId>,
                    conv_from_str<IdString>, pass_through<std::string>>::def_wrap(ctx_cls, "setWireAttr", "wire"_a,
                                                                                  "key"_a, "value"_a);
    fn_wrapper_3a_v<Context, decltype(&Context::setBelAttr), &Context::setBelAttr, conv_from_str<BelId>,
                    conv_from_str<IdString>, pass_through<std::string>>::def_wrap(ctx_cls, "setBelAttr", "bel"_a,
                                                                                  "key"_a, "value"_a);
    fn_wrapper_3a_v<Context, decltype(&Context::setPipAttr), &Context::setPipAttr, conv_from_str<PipId>,
                    conv_from_str<IdString>, pass_through<std::string>>::def_wrap(ctx_cls, "setPipAttr", "pip"_a,
                                                                                  "key"_a, "value"_a);

    fn_wrapper_1a_v<Context, decltype(&Context::setLutK), &Context::setLutK, pass_through<int>>::def_wrap(
            ctx_cls, "setLutK", "K"_a);
    fn_wrapper_2a_v<Context, decltype(&Context::setDelayScaling), &Context::setDelayScaling, pass_through<double>,
                    pass_through<double>>::def_wrap(ctx_cls, "setDelayScaling", "scale"_a, "offset"_a);

    fn_wrapper_2a_v<Context, decltype(&Context::addCellTimingClock), &Context::addCellTimingClock,
                    conv_from_str<IdString>, conv_from_str<IdString>>::def_wrap(ctx_cls, "addCellTimingClock", "cell"_a,
                                                                                "port"_a);
    fn_wrapper_4a_v<Context, decltype(&Context::addCellTimingDelay), &Context::addCellTimingDelay,
                    conv_from_str<IdString>, conv_from_str<IdString>, conv_from_str<IdString>,
                    pass_through<delay_t>>::def_wrap(ctx_cls, "addCellTimingDelay", "cell"_a, "fromPort"_a, "toPort"_a,
                                                     "delay"_a);
    fn_wrapper_5a_v<Context, decltype(&Context::addCellTimingSetupHold), &Context::addCellTimingSetupHold,
                    conv_from_str<IdString>, conv_from_str<IdString>, conv_from_str<IdString>, pass_through<delay_t>,
                    pass_through<delay_t>>::def_wrap(ctx_cls, "addCellTimingSetupHold", "cell"_a, "port"_a, "clock"_a,
                                                     "setup"_a, "hold"_a);
    fn_wrapper_4a_v<Context, decltype(&Context::addCellTimingClockToOut), &Context::addCellTimingClockToOut,
                    conv_from_str<IdString>, conv_from_str<IdString>, conv_from_str<IdString>,
                    pass_through<delay_t>>::def_wrap(ctx_cls, "addCellTimingClockToOut", "cell"_a, "port"_a, "clock"_a,
                                                     "clktoq"_a);

    fn_wrapper_2a_v<Context, decltype(&Context::clearCellBelPinMap), &Context::clearCellBelPinMap,
                    conv_from_str<IdString>, conv_from_str<IdString>>::def_wrap(ctx_cls, "clearCellBelPinMap", "cell"_a,
                                                                                "cell_pin"_a);
    fn_wrapper_3a_v<Context, decltype(&Context::addCellBelPinMapping), &Context::addCellBelPinMapping,
                    conv_from_str<IdString>, conv_from_str<IdString>,
                    conv_from_str<IdString>>::def_wrap(ctx_cls, "addCellBelPinMapping", "cell"_a, "cell_pin"_a,
                                                       "bel_pin"_a);

    WRAP_RANGE(m, Bel, conv_to_str<BelId>);
    WRAP_RANGE(m, Wire, conv_to_str<WireId>);
    WRAP_RANGE(m, AllPip, conv_to_str<PipId>);

    WRAP_MAP_UPTR(m, CellMap, "IdCellMap");
    WRAP_MAP_UPTR(m, NetMap, "IdNetMap");
    WRAP_MAP(m, HierarchyMap, wrap_context<HierarchicalCell &>, "HierarchyMap");
    WRAP_VECTOR(m, const std::vector<IdString>, conv_to_str<IdString>);
    WRAP_VECTOR(m, const std::vector<PipId>, conv_to_str<PipId>);
}

NEXTPNR_NAMESPACE_END

#endif

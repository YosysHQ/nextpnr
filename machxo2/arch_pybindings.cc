/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <dave@ds0.me>
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

NEXTPNR_NAMESPACE_BEGIN
namespace PythonConversion {
template <> struct string_converter<const IdString &>
{
    const IdString &from_str(Context *ctx, std::string name) { NPNR_ASSERT_FALSE("unsupported"); }

    std::string to_str(Context *ctx, const IdString &id) { return id.str(ctx); }
};
} // namespace PythonConversion

void arch_wrap_python()
{
    using namespace PythonConversion;

    auto arch_cls = class_<Arch, Arch *, bases<BaseCtx>, boost::noncopyable>("Arch", init<ArchArgs>());

    auto dxy_cls = class_<ContextualWrapper<DecalXY>>("DecalXY_", no_init);
    readwrite_wrapper<DecalXY, decltype(&DecalXY::decal), &DecalXY::decal, conv_to_str<DecalId>,
                      conv_from_str<DecalId>>::def_wrap(dxy_cls, "decal");
    readwrite_wrapper<DecalXY, decltype(&DecalXY::x), &DecalXY::x, pass_through<float>, pass_through<float>>::def_wrap(
            dxy_cls, "x");
    readwrite_wrapper<DecalXY, decltype(&DecalXY::y), &DecalXY::y, pass_through<float>, pass_through<float>>::def_wrap(
            dxy_cls, "y");

    auto ctx_cls = class_<Context, Context *, bases<Arch>, boost::noncopyable>("Context", no_init)
                           .def("checksum", &Context::checksum)
                           .def("pack", &Context::pack)
                           .def("place", &Context::place)
                           .def("route", &Context::route);

    class_<BelPin>("BelPin").def_readwrite("bel", &BelPin::bel).def_readwrite("pin", &BelPin::pin);

    class_<DelayInfo>("DelayInfo").def("maxDelay", &DelayInfo::maxDelay).def("minDelay", &DelayInfo::minDelay);

    fn_wrapper_1a<Context, decltype(&Context::getBelType), &Context::getBelType, conv_to_str<IdString>,
                  conv_from_str<BelId>>::def_wrap(ctx_cls, "getBelType");
    fn_wrapper_1a<Context, decltype(&Context::checkBelAvail), &Context::checkBelAvail, pass_through<bool>,
                  conv_from_str<BelId>>::def_wrap(ctx_cls, "checkBelAvail");
    fn_wrapper_1a<Context, decltype(&Context::getBelChecksum), &Context::getBelChecksum, pass_through<uint32_t>,
                  conv_from_str<BelId>>::def_wrap(ctx_cls, "getBelChecksum");
    fn_wrapper_3a_v<Context, decltype(&Context::bindBel), &Context::bindBel, conv_from_str<BelId>,
                    addr_and_unwrap<CellInfo>, pass_through<PlaceStrength>>::def_wrap(ctx_cls, "bindBel");
    fn_wrapper_1a_v<Context, decltype(&Context::unbindBel), &Context::unbindBel, conv_from_str<BelId>>::def_wrap(
            ctx_cls, "unbindBel");
    fn_wrapper_1a<Context, decltype(&Context::getBoundBelCell), &Context::getBoundBelCell, deref_and_wrap<CellInfo>,
                  conv_from_str<BelId>>::def_wrap(ctx_cls, "getBoundBelCell");
    fn_wrapper_1a<Context, decltype(&Context::getConflictingBelCell), &Context::getConflictingBelCell,
                  deref_and_wrap<CellInfo>, conv_from_str<BelId>>::def_wrap(ctx_cls, "getConflictingBelCell");
    fn_wrapper_0a<Context, decltype(&Context::getBels), &Context::getBels,
                  wrap_context<const std::vector<BelId> &>>::def_wrap(ctx_cls, "getBels");

    fn_wrapper_2a<Context, decltype(&Context::getBelPinWire), &Context::getBelPinWire, conv_to_str<WireId>,
                  conv_from_str<BelId>, conv_from_str<IdString>>::def_wrap(ctx_cls, "getBelPinWire");
    fn_wrapper_1a<Context, decltype(&Context::getWireBelPins), &Context::getWireBelPins,
                  wrap_context<const std::vector<BelPin> &>, conv_from_str<WireId>>::def_wrap(ctx_cls,
                                                                                              "getWireBelPins");

    fn_wrapper_1a<Context, decltype(&Context::getWireChecksum), &Context::getWireChecksum, pass_through<uint32_t>,
                  conv_from_str<WireId>>::def_wrap(ctx_cls, "getWireChecksum");
    fn_wrapper_3a_v<Context, decltype(&Context::bindWire), &Context::bindWire, conv_from_str<WireId>,
                    addr_and_unwrap<NetInfo>, pass_through<PlaceStrength>>::def_wrap(ctx_cls, "bindWire");
    fn_wrapper_1a_v<Context, decltype(&Context::unbindWire), &Context::unbindWire, conv_from_str<WireId>>::def_wrap(
            ctx_cls, "unbindWire");
    fn_wrapper_1a<Context, decltype(&Context::checkWireAvail), &Context::checkWireAvail, pass_through<bool>,
                  conv_from_str<WireId>>::def_wrap(ctx_cls, "checkWireAvail");
    fn_wrapper_1a<Context, decltype(&Context::getBoundWireNet), &Context::getBoundWireNet, deref_and_wrap<NetInfo>,
                  conv_from_str<WireId>>::def_wrap(ctx_cls, "getBoundWireNet");
    fn_wrapper_1a<Context, decltype(&Context::getConflictingWireNet), &Context::getConflictingWireNet,
                  deref_and_wrap<NetInfo>, conv_from_str<WireId>>::def_wrap(ctx_cls, "getConflictingWireNet");

    fn_wrapper_0a<Context, decltype(&Context::getWires), &Context::getWires,
                  wrap_context<const std::vector<WireId> &>>::def_wrap(ctx_cls, "getWires");

    fn_wrapper_0a<Context, decltype(&Context::getPips), &Context::getPips,
                  wrap_context<const std::vector<PipId> &>>::def_wrap(ctx_cls, "getPips");
    fn_wrapper_1a<Context, decltype(&Context::getPipChecksum), &Context::getPipChecksum, pass_through<uint32_t>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipChecksum");
    fn_wrapper_3a_v<Context, decltype(&Context::bindPip), &Context::bindPip, conv_from_str<PipId>,
                    addr_and_unwrap<NetInfo>, pass_through<PlaceStrength>>::def_wrap(ctx_cls, "bindPip");
    fn_wrapper_1a_v<Context, decltype(&Context::unbindPip), &Context::unbindPip, conv_from_str<PipId>>::def_wrap(
            ctx_cls, "unbindPip");
    fn_wrapper_1a<Context, decltype(&Context::checkPipAvail), &Context::checkPipAvail, pass_through<bool>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "checkPipAvail");
    fn_wrapper_1a<Context, decltype(&Context::getBoundPipNet), &Context::getBoundPipNet, deref_and_wrap<NetInfo>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getBoundPipNet");
    fn_wrapper_1a<Context, decltype(&Context::getConflictingPipNet), &Context::getConflictingPipNet,
                  deref_and_wrap<NetInfo>, conv_from_str<PipId>>::def_wrap(ctx_cls, "getConflictingPipNet");

    fn_wrapper_1a<Context, decltype(&Context::getPipsDownhill), &Context::getPipsDownhill,
                  wrap_context<const std::vector<PipId> &>, conv_from_str<WireId>>::def_wrap(ctx_cls,
                                                                                             "getPipsDownhill");
    fn_wrapper_1a<Context, decltype(&Context::getPipsUphill), &Context::getPipsUphill,
                  wrap_context<const std::vector<PipId> &>, conv_from_str<WireId>>::def_wrap(ctx_cls, "getPipsUphill");
    fn_wrapper_1a<Context, decltype(&Context::getWireAliases), &Context::getWireAliases,
                  wrap_context<const std::vector<PipId> &>, conv_from_str<WireId>>::def_wrap(ctx_cls, "getWireAliases");

    fn_wrapper_1a<Context, decltype(&Context::getPipSrcWire), &Context::getPipSrcWire, conv_to_str<WireId>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipSrcWire");
    fn_wrapper_1a<Context, decltype(&Context::getPipDstWire), &Context::getPipDstWire, conv_to_str<WireId>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipDstWire");
    fn_wrapper_1a<Context, decltype(&Context::getPipDelay), &Context::getPipDelay, pass_through<DelayInfo>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipDelay");

    fn_wrapper_1a<Context, decltype(&Context::getDelayFromNS), &Context::getDelayFromNS, pass_through<DelayInfo>,
                  pass_through<double>>::def_wrap(ctx_cls, "getDelayFromNS");

    fn_wrapper_0a<Context, decltype(&Context::getChipName), &Context::getChipName, pass_through<std::string>>::def_wrap(
            ctx_cls, "getChipName");
    fn_wrapper_0a<Context, decltype(&Context::archId), &Context::archId, conv_to_str<IdString>>::def_wrap(ctx_cls,
                                                                                                          "archId");

    fn_wrapper_3a<Context, decltype(&Context::constructDecalXY), &Context::constructDecalXY, wrap_context<DecalXY>,
                  conv_from_str<DecalId>, pass_through<float>, pass_through<float>>::def_wrap(ctx_cls, "DecalXY");

    typedef std::unordered_map<IdString, std::unique_ptr<CellInfo>> CellMap;
    typedef std::unordered_map<IdString, std::unique_ptr<NetInfo>> NetMap;
    typedef std::unordered_map<IdString, HierarchicalCell> HierarchyMap;

    readonly_wrapper<Context, decltype(&Context::cells), &Context::cells, wrap_context<CellMap &>>::def_wrap(ctx_cls,
                                                                                                             "cells");
    readonly_wrapper<Context, decltype(&Context::nets), &Context::nets, wrap_context<NetMap &>>::def_wrap(ctx_cls,
                                                                                                          "nets");

    fn_wrapper_2a_v<Context, decltype(&Context::addClock), &Context::addClock, conv_from_str<IdString>,
                    pass_through<float>>::def_wrap(ctx_cls, "addClock");

    // Generic arch construction API
    fn_wrapper_4a_v<Context, decltype(&Context::addWire), &Context::addWire, conv_from_str<IdString>,
                    conv_from_str<IdString>, pass_through<int>, pass_through<int>>::def_wrap(ctx_cls, "addWire",
                                                                                             (arg("name"), "type", "x",
                                                                                              "y"));
    fn_wrapper_6a_v<Context, decltype(&Context::addPip), &Context::addPip, conv_from_str<IdString>,
                    conv_from_str<IdString>, conv_from_str<IdString>, conv_from_str<IdString>, pass_through<DelayInfo>,
                    pass_through<Loc>>::def_wrap(ctx_cls, "addPip",
                                                 (arg("name"), "type", "srcWire", "dstWire", "delay", "loc"));
    fn_wrapper_5a_v<Context, decltype(&Context::addAlias), &Context::addAlias, conv_from_str<IdString>,
                    conv_from_str<IdString>, conv_from_str<IdString>, conv_from_str<IdString>,
                    pass_through<DelayInfo>>::def_wrap(ctx_cls, "addAlias",
                                                       (arg("name"), "type", "srcWire", "dstWire", "delay"));

    fn_wrapper_4a_v<Context, decltype(&Context::addBel), &Context::addBel, conv_from_str<IdString>,
                    conv_from_str<IdString>, pass_through<Loc>, pass_through<bool>>::def_wrap(ctx_cls, "addBel",
                                                                                              (arg("name"), "type",
                                                                                               "loc", "gb"));
    fn_wrapper_3a_v<Context, decltype(&Context::addBelInput), &Context::addBelInput, conv_from_str<IdString>,
                    conv_from_str<IdString>, conv_from_str<IdString>>::def_wrap(ctx_cls, "addBelInput",
                                                                                (arg("bel"), "name", "wire"));
    fn_wrapper_3a_v<Context, decltype(&Context::addBelOutput), &Context::addBelOutput, conv_from_str<IdString>,
                    conv_from_str<IdString>, conv_from_str<IdString>>::def_wrap(ctx_cls, "addBelOutput",
                                                                                (arg("bel"), "name", "wire"));
    fn_wrapper_3a_v<Context, decltype(&Context::addBelInout), &Context::addBelInout, conv_from_str<IdString>,
                    conv_from_str<IdString>, conv_from_str<IdString>>::def_wrap(ctx_cls, "addBelInout",
                                                                                (arg("bel"), "name", "wire"));

    fn_wrapper_2a_v<Context, decltype(&Context::addGroupBel), &Context::addGroupBel, conv_from_str<IdString>,
                    conv_from_str<IdString>>::def_wrap(ctx_cls, "addGroupBel", (arg("group"), "bel"));
    fn_wrapper_2a_v<Context, decltype(&Context::addGroupWire), &Context::addGroupWire, conv_from_str<IdString>,
                    conv_from_str<IdString>>::def_wrap(ctx_cls, "addGroupWire", (arg("group"), "wire"));
    fn_wrapper_2a_v<Context, decltype(&Context::addGroupPip), &Context::addGroupPip, conv_from_str<IdString>,
                    conv_from_str<IdString>>::def_wrap(ctx_cls, "addGroupPip", (arg("group"), "pip"));
    fn_wrapper_2a_v<Context, decltype(&Context::addGroupGroup), &Context::addGroupPip, conv_from_str<IdString>,
                    conv_from_str<IdString>>::def_wrap(ctx_cls, "addGroupGroup", (arg("group"), "grp"));

    fn_wrapper_2a_v<Context, decltype(&Context::addDecalGraphic), &Context::addDecalGraphic, conv_from_str<DecalId>,
                    pass_through<GraphicElement>>::def_wrap(ctx_cls, "addDecalGraphic", (arg("decal"), "graphic"));
    fn_wrapper_2a_v<Context, decltype(&Context::setWireDecal), &Context::setWireDecal, conv_from_str<DecalId>,
                    unwrap_context<DecalXY>>::def_wrap(ctx_cls, "setWireDecal", (arg("wire"), "decalxy"));
    fn_wrapper_2a_v<Context, decltype(&Context::setPipDecal), &Context::setPipDecal, conv_from_str<DecalId>,
                    unwrap_context<DecalXY>>::def_wrap(ctx_cls, "setPipDecal", (arg("pip"), "decalxy"));
    fn_wrapper_2a_v<Context, decltype(&Context::setBelDecal), &Context::setBelDecal, conv_from_str<DecalId>,
                    unwrap_context<DecalXY>>::def_wrap(ctx_cls, "setBelDecal", (arg("bel"), "decalxy"));
    fn_wrapper_2a_v<Context, decltype(&Context::setGroupDecal), &Context::setGroupDecal, conv_from_str<DecalId>,
                    unwrap_context<DecalXY>>::def_wrap(ctx_cls, "setGroupDecal", (arg("group"), "decalxy"));

    fn_wrapper_3a_v<Context, decltype(&Context::setWireAttr), &Context::setWireAttr, conv_from_str<DecalId>,
                    conv_from_str<IdString>, pass_through<std::string>>::def_wrap(ctx_cls, "setWireAttr",
                                                                                  (arg("wire"), "key", "value"));
    fn_wrapper_3a_v<Context, decltype(&Context::setBelAttr), &Context::setBelAttr, conv_from_str<DecalId>,
                    conv_from_str<IdString>, pass_through<std::string>>::def_wrap(ctx_cls, "setBelAttr",
                                                                                  (arg("bel"), "key", "value"));
    fn_wrapper_3a_v<Context, decltype(&Context::setPipAttr), &Context::setPipAttr, conv_from_str<DecalId>,
                    conv_from_str<IdString>, pass_through<std::string>>::def_wrap(ctx_cls, "setPipAttr",
                                                                                  (arg("pip"), "key", "value"));

    fn_wrapper_1a_v<Context, decltype(&Context::setLutK), &Context::setLutK, pass_through<int>>::def_wrap(
            ctx_cls, "setLutK", arg("K"));
    fn_wrapper_2a_v<Context, decltype(&Context::setDelayScaling), &Context::setDelayScaling, pass_through<double>,
                    pass_through<double>>::def_wrap(ctx_cls, "setDelayScaling", (arg("scale"), "offset"));

    fn_wrapper_2a_v<Context, decltype(&Context::addCellTimingClock), &Context::addCellTimingClock,
                    conv_from_str<IdString>, conv_from_str<IdString>>::def_wrap(ctx_cls, "addCellTimingClock",
                                                                                (arg("cell"), "port"));
    fn_wrapper_4a_v<Context, decltype(&Context::addCellTimingDelay), &Context::addCellTimingDelay,
                    conv_from_str<IdString>, conv_from_str<IdString>, conv_from_str<IdString>,
                    pass_through<DelayInfo>>::def_wrap(ctx_cls, "addCellTimingDelay",
                                                       (arg("cell"), "fromPort", "toPort", "delay"));
    fn_wrapper_5a_v<Context, decltype(&Context::addCellTimingSetupHold), &Context::addCellTimingSetupHold,
                    conv_from_str<IdString>, conv_from_str<IdString>, conv_from_str<IdString>, pass_through<DelayInfo>,
                    pass_through<DelayInfo>>::def_wrap(ctx_cls, "addCellTimingSetupHold",
                                                       (arg("cell"), "port", "clock", "setup", "hold"));
    fn_wrapper_4a_v<Context, decltype(&Context::addCellTimingClockToOut), &Context::addCellTimingClockToOut,
                    conv_from_str<IdString>, conv_from_str<IdString>, conv_from_str<IdString>,
                    pass_through<DelayInfo>>::def_wrap(ctx_cls, "addCellTimingClockToOut",
                                                       (arg("cell"), "port", "clock", "clktoq"));

    WRAP_MAP_UPTR(CellMap, "IdCellMap");
    WRAP_MAP_UPTR(NetMap, "IdNetMap");
    WRAP_MAP(HierarchyMap, wrap_context<HierarchicalCell &>, "HierarchyMap");
    WRAP_VECTOR(const std::vector<IdString>, conv_to_str<IdString>);
}

NEXTPNR_NAMESPACE_END

#endif
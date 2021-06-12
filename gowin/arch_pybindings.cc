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

    auto arch_cls = py::class_<Arch, BaseCtx>(m, "Arch").def(py::init<ArchArgs>());

    auto dxy_cls = py::class_<ContextualWrapper<DecalXY>>(m, "DecalXY_");
    readwrite_wrapper<DecalXY, decltype(&DecalXY::decal), &DecalXY::decal, conv_to_str<DecalId>,
                      conv_from_str<DecalId>>::def_wrap(dxy_cls, "decal");
    readwrite_wrapper<DecalXY, decltype(&DecalXY::x), &DecalXY::x, pass_through<float>, pass_through<float>>::def_wrap(
            dxy_cls, "x");
    readwrite_wrapper<DecalXY, decltype(&DecalXY::y), &DecalXY::y, pass_through<float>, pass_through<float>>::def_wrap(
            dxy_cls, "y");

    auto ctx_cls = py::class_<Context, Arch>(m, "Context")
                           .def("checksum", &Context::checksum)
                           .def("pack", &Context::pack)
                           .def("place", &Context::place)
                           .def("route", &Context::route);

    py::class_<BelPin>(m, "BelPin").def_readwrite("bel", &BelPin::bel).def_readwrite("pin", &BelPin::pin);

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

    fn_wrapper_1a<Context, decltype(&Context::getPipSrcWire), &Context::getPipSrcWire, conv_to_str<WireId>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipSrcWire");
    fn_wrapper_1a<Context, decltype(&Context::getPipDstWire), &Context::getPipDstWire, conv_to_str<WireId>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipDstWire");
    fn_wrapper_1a<Context, decltype(&Context::getPipDelay), &Context::getPipDelay, pass_through<DelayQuad>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipDelay");

    fn_wrapper_1a<Context, decltype(&Context::getDelayFromNS), &Context::getDelayFromNS, pass_through<delay_t>,
                  pass_through<double>>::def_wrap(ctx_cls, "getDelayFromNS");

    fn_wrapper_0a<Context, decltype(&Context::getChipName), &Context::getChipName, pass_through<std::string>>::def_wrap(
            ctx_cls, "getChipName");
    fn_wrapper_0a<Context, decltype(&Context::archId), &Context::archId, conv_to_str<IdString>>::def_wrap(ctx_cls,
                                                                                                          "archId");

    fn_wrapper_3a<Context, decltype(&Context::constructDecalXY), &Context::constructDecalXY, wrap_context<DecalXY>,
                  conv_from_str<DecalId>, pass_through<float>, pass_through<float>>::def_wrap(ctx_cls, "DecalXY");

    typedef dict<IdString, std::unique_ptr<CellInfo>> CellMap;
    typedef dict<IdString, std::unique_ptr<NetInfo>> NetMap;
    typedef dict<IdString, HierarchicalCell> HierarchyMap;

    readonly_wrapper<Context, decltype(&Context::cells), &Context::cells, wrap_context<CellMap &>>::def_wrap(ctx_cls,
                                                                                                             "cells");
    readonly_wrapper<Context, decltype(&Context::nets), &Context::nets, wrap_context<NetMap &>>::def_wrap(ctx_cls,
                                                                                                          "nets");

    fn_wrapper_2a_v<Context, decltype(&Context::addClock), &Context::addClock, conv_from_str<IdString>,
                    pass_through<float>>::def_wrap(ctx_cls, "addClock");

    WRAP_MAP_UPTR(m, CellMap, "IdCellMap");
    WRAP_MAP_UPTR(m, NetMap, "IdNetMap");
    WRAP_MAP(m, HierarchyMap, wrap_context<HierarchicalCell &>, "HierarchyMap");
    WRAP_VECTOR(m, const std::vector<IdString>, conv_to_str<IdString>);
}

NEXTPNR_NAMESPACE_END

#endif

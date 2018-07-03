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
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

namespace PythonConversion {

template <> struct string_converter<BelId>
{
    BelId from_str(Context *ctx, std::string name) { return ctx->getBelByName(ctx->id(name)); }

    std::string to_str(Context *ctx, BelId id) { return ctx->getBelName(id).str(ctx); }
};

template <> struct string_converter<BelType>
{
    BelType from_str(Context *ctx, std::string name) { return ctx->belTypeFromId(ctx->id(name)); }

    std::string to_str(Context *ctx, BelType typ) { return ctx->belTypeToId(typ).str(ctx); }
};

template <> struct string_converter<WireId>
{
    WireId from_str(Context *ctx, std::string name) { return ctx->getWireByName(ctx->id(name)); }

    std::string to_str(Context *ctx, WireId id) { return ctx->getWireName(id).str(ctx); }
};

template <> struct string_converter<PipId>
{
    PipId from_str(Context *ctx, std::string name) { return ctx->getPipByName(ctx->id(name)); }

    std::string to_str(Context *ctx, PipId id) { return ctx->getPipName(id).str(ctx); }
};

} // namespace PythonConversion

void arch_wrap_python()
{
    using namespace PythonConversion;
    class_<ArchArgs>("ArchArgs").def_readwrite("type", &ArchArgs::type);

    enum_<decltype(std::declval<ArchArgs>().type)>("iCE40Type")
            .value("NONE", ArchArgs::NONE)
            .value("LP384", ArchArgs::LP384)
            .value("LP1K", ArchArgs::LP1K)
            .value("LP8K", ArchArgs::LP8K)
            .value("HX1K", ArchArgs::HX1K)
            .value("HX8K", ArchArgs::HX8K)
            .value("UP5K", ArchArgs::UP5K)
            .export_values();

    class_<BelId>("BelId").def_readwrite("index", &BelId::index);

    class_<WireId>("WireId").def_readwrite("index", &WireId::index);

    class_<PipId>("PipId").def_readwrite("index", &PipId::index);

    class_<BelPin>("BelPin").def_readwrite("bel", &BelPin::bel).def_readwrite("pin", &BelPin::pin);

    enum_<PortPin>("PortPin")
#define X(t) .value("PIN_" #t, PIN_##t)

#include "portpins.inc"
            ;
#undef X

    auto arch_cls = class_<Arch, Arch *, bases<BaseCtx>, boost::noncopyable>("Arch", init<ArchArgs>());
    auto ctx_cls = class_<Context, Context *, bases<Arch>, boost::noncopyable>("Context", no_init)
                           .def("checksum", &Context::checksum);

    fn_wrapper_1a<Context, typeof(&Context::getBelType), &Context::getBelType, conv_to_str<BelType>,
                  conv_from_str<BelId>>::def_wrap(ctx_cls, "getBelType");
    fn_wrapper_1a<Context, typeof(&Context::checkBelAvail), &Context::checkBelAvail, pass_through<bool>,
                  conv_from_str<BelId>>::def_wrap(ctx_cls, "checkBelAvail");

    fn_wrapper_0a<Context, typeof(&Context::getBels), &Context::getBels, wrap_context<BelRange>>::def_wrap(ctx_cls,
                                                                                                           "getBels");
    fn_wrapper_0a<Context, typeof(&Context::getWires), &Context::getWires, wrap_context<WireRange>>::def_wrap(
            ctx_cls, "getWires");
    fn_wrapper_0a<Context, typeof(&Context::getPips), &Context::getPips, wrap_context<AllPipRange>>::def_wrap(
            ctx_cls, "getPips");

    fn_wrapper_1a<Context, typeof(&Context::getPipsDownhill), &Context::getPipsDownhill, wrap_context<PipRange>,
                  conv_from_str<WireId>>::def_wrap(ctx_cls, "getPipsDownhill");
    fn_wrapper_1a<Context, typeof(&Context::getPipsUphill), &Context::getPipsUphill, wrap_context<PipRange>,
                  conv_from_str<WireId>>::def_wrap(ctx_cls, "getPipsUphill");

    fn_wrapper_1a<Context, typeof(&Context::getPipSrcWire), &Context::getPipSrcWire, conv_to_str<WireId>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipSrcWire");
    fn_wrapper_1a<Context, typeof(&Context::getPipDstWire), &Context::getPipDstWire, conv_to_str<WireId>,
                  conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipDstWire");

    typedef std::unordered_map<IdString, std::unique_ptr<CellInfo>> CellMap;

    readonly_wrapper<Context, typeof(&Context::cells), &Context::cells, wrap_context<CellMap &>>::def_wrap(ctx_cls,
                                                                                                           "cells");
    /*
            .def("getBelByName", &Arch::getBelByName)
            .def("getWireByName", &Arch::getWireByName)
            .def("getBelName", &Arch::getBelName)
            .def("getWireName", &Arch::getWireName)
            .def("getBels", &Arch::getBels)
            .def("getWireBelPin", &Arch::getWireBelPin)
            .def("getBelPinUphill", &Arch::getBelPinUphill)
            .def("getBelPinsDownhill", &Arch::getBelPinsDownhill)
            .def("getWires", &Arch::getWires)
            .def("getPipByName", &Arch::getPipByName)
            .def("getPipName", &Arch::getPipName)
            .def("getPips", &Arch::getPips)
            .def("getPipSrcWire", &Arch::getPipSrcWire)
            .def("getPipDstWire", &Arch::getPipDstWire)
            .def("getPipDelay", &Arch::getPipDelay)
            .def("getPipsDownhill", &Arch::getPipsDownhill)
            .def("getPipsUphill", &Arch::getPipsUphill)
            .def("getWireAliases", &Arch::getWireAliases)
            .def("estimatePosition", &Arch::estimatePosition)
            .def("estimateDelay", &Arch::estimateDelay);
    */

    WRAP_RANGE(Bel, conv_to_str<BelId>);
    WRAP_RANGE(Wire, conv_to_str<WireId>);
    WRAP_RANGE(AllPip, conv_to_str<PipId>);
    WRAP_RANGE(Pip, conv_to_str<PipId>);

    WRAP_MAP_UPTR(CellMap, "IdCellMap");
    // WRAP_RANGE(BelPin);
    // WRAP_RANGE(Wire);
    // WRAP_RANGE(AllPip);
    // WRAP_RANGE(Pip);
}

NEXTPNR_NAMESPACE_END

#endif // NO_PYTHON

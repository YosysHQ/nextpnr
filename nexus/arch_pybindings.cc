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

#include "arch_pybindings.h"
#include "nextpnr.h"
#include "pybindings.h"

NEXTPNR_NAMESPACE_BEGIN

void arch_wrap_python()
{
    using namespace PythonConversion;
    class_<ArchArgs>("ArchArgs").def_readwrite("chipdb", &ArchArgs::chipdb).def_readwrite("device", &ArchArgs::device);

    class_<BelId>("BelId").def_readwrite("index", &BelId::index).def_readwrite("tile", &BelId::tile);

    class_<WireId>("WireId").def_readwrite("index", &WireId::index).def_readwrite("tile", &WireId::tile);

    class_<PipId>("PipId").def_readwrite("index", &PipId::index).def_readwrite("tile", &PipId::tile);

    class_<BelPin>("BelPin").def_readwrite("bel", &BelPin::bel).def_readwrite("pin", &BelPin::pin);

    auto arch_cls = class_<Arch, Arch *, bases<BaseCtx>, boost::noncopyable>("Arch", init<ArchArgs>());
    auto ctx_cls = class_<Context, Context *, bases<Arch>, boost::noncopyable>("Context", no_init)
                           .def("checksum", &Context::checksum)
                           .def("pack", &Context::pack)
                           .def("place", &Context::place)
                           .def("route", &Context::route);

    typedef std::unordered_map<IdString, std::unique_ptr<CellInfo>> CellMap;
    typedef std::unordered_map<IdString, std::unique_ptr<NetInfo>> NetMap;
    typedef std::unordered_map<IdString, HierarchicalCell> HierarchyMap;
    typedef std::unordered_map<IdString, IdString> AliasMap;

    auto belpin_cls = class_<ContextualWrapper<BelPin>>("BelPin", no_init);
    readonly_wrapper<BelPin, decltype(&BelPin::bel), &BelPin::bel, conv_to_str<BelId>>::def_wrap(belpin_cls, "bel");
    readonly_wrapper<BelPin, decltype(&BelPin::pin), &BelPin::pin, conv_to_str<IdString>>::def_wrap(belpin_cls, "pin");

    typedef UpDownhillPipRange PipRange;
    typedef WireBelPinRange BelPinRange;

#include "arch_pybindings_shared.h"

    WRAP_RANGE(Bel, conv_to_str<BelId>);
    WRAP_RANGE(Wire, conv_to_str<WireId>);
    WRAP_RANGE(AllPip, conv_to_str<PipId>);
    WRAP_RANGE(UpDownhillPip, conv_to_str<PipId>);
    WRAP_RANGE(WireBelPin, wrap_context<BelPin>);

    WRAP_MAP_UPTR(CellMap, "IdCellMap");
    WRAP_MAP_UPTR(NetMap, "IdNetMap");
    WRAP_MAP(HierarchyMap, wrap_context<HierarchicalCell &>, "HierarchyMap");
}

NEXTPNR_NAMESPACE_END

#endif // NO_PYTHON

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
#include "bitstream.h"
#include "nextpnr.h"
#include "pybindings.h"

NEXTPNR_NAMESPACE_BEGIN

void arch_wrap_python(py::module &m)
{
    using namespace PythonConversion;
    py::class_<ArchArgs>(m, "ArchArgs").def_readwrite("device", &ArchArgs::device);

    py::class_<BelId>(m, "BelId").def_readwrite("index", &BelId::index);

    py::class_<WireId>(m, "WireId").def_readwrite("index", &WireId::index);

    py::class_<PipId>(m, "PipId").def_readwrite("index", &PipId::index);

    auto arch_cls = py::class_<Arch, BaseCtx>(m, "Arch").def(py::init<ArchArgs>());
    auto ctx_cls = py::class_<Context, Arch>(m, "Context")
                           .def("checksum", &Context::checksum)
                           .def("pack", &Context::pack)
                           .def("place", &Context::place)
                           .def("route", &Context::route);

    typedef dict<IdString, std::unique_ptr<CellInfo>> CellMap;
    typedef dict<IdString, std::unique_ptr<NetInfo>> NetMap;
    typedef dict<IdString, IdString> AliasMap;
    typedef dict<IdString, HierarchicalCell> HierarchyMap;

    auto belpin_cls = py::class_<ContextualWrapper<BelPin>>(m, "BelPin");
    readonly_wrapper<BelPin, decltype(&BelPin::bel), &BelPin::bel, conv_to_str<BelId>>::def_wrap(belpin_cls, "bel");
    readonly_wrapper<BelPin, decltype(&BelPin::pin), &BelPin::pin, conv_to_str<IdString>>::def_wrap(belpin_cls, "pin");

    typedef PipRange UphillPipRange;
    typedef PipRange DownhillPipRange;

    typedef const std::vector<BelBucketId> &BelBucketRange;
    typedef const std::vector<BelId> &BelRangeForBelBucket;
#include "arch_pybindings_shared.h"

    WRAP_RANGE(m, Bel, conv_to_str<BelId>);
    WRAP_RANGE(m, Wire, conv_to_str<WireId>);
    WRAP_RANGE(m, AllPip, conv_to_str<PipId>);
    WRAP_RANGE(m, Pip, conv_to_str<PipId>);
    WRAP_RANGE(m, BelPin, wrap_context<BelPin>);

    WRAP_MAP_UPTR(m, CellMap, "IdCellMap");
    WRAP_MAP_UPTR(m, NetMap, "IdNetMap");
    WRAP_MAP(m, HierarchyMap, wrap_context<HierarchicalCell &>, "HierarchyMap");

    m.def("write_bitstream", &write_bitstream);
}

NEXTPNR_NAMESPACE_END

#endif // NO_PYTHON

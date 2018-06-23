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

void arch_wrap_python()
{
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

    class_<Arch>("Arch", init<ArchArgs>())
            .def("getBelByName", &Arch::getBelByName)
            .def("getWireByName", &Arch::getWireByName)
            .def("getBelName", &Arch::getBelName)
            .def("getWireName", &Arch::getWireName)
            .def("getBels", &Arch::getBels)
            .def("getBelType", &Arch::getBelType)
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

    WRAP_RANGE(Bel);
    WRAP_RANGE(BelPin);
    WRAP_RANGE(Wire);
    WRAP_RANGE(AllPip);
    WRAP_RANGE(Pip);
}

NEXTPNR_NAMESPACE_END

#endif // NO_PYTHON
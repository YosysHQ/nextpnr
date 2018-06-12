/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

#include "pybindings.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

void arch_wrap_python()
{
    class_<ChipArgs>("ChipArgs").def_readwrite("type", &ChipArgs::type);

    enum_<decltype(std::declval<ChipArgs>().type)>("iCE40Type")
            .value("NONE", ChipArgs::NONE)
            .value("LP384", ChipArgs::LP384)
            .value("LP1K", ChipArgs::LP1K)
            .value("LP8K", ChipArgs::LP8K)
            .value("HX1K", ChipArgs::HX1K)
            .value("HX8K", ChipArgs::HX8K)
            .value("UP5K", ChipArgs::UP5K)
            .export_values();

    class_<BelId>("BelId").def_readwrite("index", &BelId::index);

    class_<WireId>("WireId").def_readwrite("index", &WireId::index);

    class_<PipId>("PipId").def_readwrite("index", &PipId::index);

    class_<BelPin>("BelPin")
            .def_readwrite("bel", &BelPin::bel)
            .def_readwrite("pin", &BelPin::pin);

    enum_<PortPin>("PortPin")
#define X(t) .value("PIN_" #t, PIN_##t)
#include "portpins.inc"
            ;
#undef X

    class_<Chip>("Chip", init<ChipArgs>())
            .def("getBelByName", &Chip::getBelByName)
            .def("getWireByName", &Chip::getWireByName)
            .def("getBelName", &Chip::getBelName)
            .def("getWireName", &Chip::getWireName)
            .def("getBels", &Chip::getBels)
            .def("getBelType", &Chip::getBelType)
            .def("getWireBelPin", &Chip::getWireBelPin)
            .def("getBelPinUphill", &Chip::getBelPinUphill)
            .def("getBelPinsDownhill", &Chip::getBelPinsDownhill)
            .def("getWires", &Chip::getWires)
            .def("getPipByName", &Chip::getPipByName)
            .def("getPipName", &Chip::getPipName)
            .def("getPips", &Chip::getPips)
            .def("getPipSrcWire", &Chip::getPipSrcWire)
            .def("getPipDstWire", &Chip::getPipDstWire)
            .def("getPipDelay", &Chip::getPipDelay)
            .def("getPipsDownhill", &Chip::getPipsDownhill)
            .def("getPipsUphill", &Chip::getPipsUphill)
            .def("getWireAliases", &Chip::getWireAliases)
            .def("getBelPosition", &Chip::getBelPosition)
            .def("getWirePosition", &Chip::getWirePosition);

    WRAP_RANGE(Bel);
    WRAP_RANGE(BelPin);
    WRAP_RANGE(Wire);
    WRAP_RANGE(AllPip);
    WRAP_RANGE(Pip);
}

NEXTPNR_NAMESPACE_END

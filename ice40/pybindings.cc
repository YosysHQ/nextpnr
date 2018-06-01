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

#include "design.h"
#include "chip.h"
#include <utility>
#include <stdexcept>
#include <boost/python.hpp>
#include <boost/python/enum.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>

using namespace boost::python;

void arch_wrap_python() {
    class_<ChipArgs>("ChipArgs")
            .def_readwrite("type", &ChipArgs::type);

    enum_<decltype(std::declval<ChipArgs>().type)>("iCE40Type")
            .value("NONE", ChipArgs::NONE)
            .value("LP384", ChipArgs::LP384)
            .value("LP1K", ChipArgs::LP1K)
            .value("LP8K", ChipArgs::LP8K)
            .value("HX1K", ChipArgs::HX1K)
            .value("HX8K", ChipArgs::HX8K)
            .value("UP5K", ChipArgs::UP5K)
            .export_values();

    class_<BelId>("BelId")
            .def_readwrite("index", &BelId::index)
            .def("nil", &BelId::nil);

    class_<WireId>("WireId")
            .def_readwrite("index", &WireId::index)
            .def("nil", &WireId::nil);

    class_<Chip>("Chip", init<ChipArgs>())
            .def("getBelByName", &Chip::getBelByName)
            .def("getWireByName", &Chip::getWireByName)
            .def("getBelName", &Chip::getBelName)
            .def("getWireName", &Chip::getWireName)
            .def("getBels", &Chip::getBels)
            .def("getWires", &Chip::getWires);


}

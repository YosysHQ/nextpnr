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

}

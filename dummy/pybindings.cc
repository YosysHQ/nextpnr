#include "design.h"
#include "chip.h"
#include <utility>
#include <stdexcept>
#include <boost/python.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>

using namespace boost::python;

void arch_wrap_python() {
    class_<ChipArgs>("ChipArgs");
}

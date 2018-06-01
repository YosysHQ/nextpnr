#include "design.h"
#include "chip.h"

#include <boost/python.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
using namespace boost::python;

#define PASTER(x,y) x ## _ ## y
#define EVALUATOR(x,y)  PASTER(x,y)
#define MODULE_NAME EVALUATOR(nextpnrpy, ARCHNAME)

BOOST_PYTHON_MODULE (MODULE_NAME) {

}

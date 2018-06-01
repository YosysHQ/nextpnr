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
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>

using namespace boost::python;

#define PASTER(x, y) x ## _ ## y
#define EVALUATOR(x, y)  PASTER(x,y)
#define MODULE_NAME EVALUATOR(nextpnrpy, ARCHNAME)

/*
A wrapper for a Pythonised nextpnr Iterator. The actual class wrapped is a
pair<Iterator, Iterator> containing (current, end)
*/
template<typename T>
struct iterator_wrapper {
    typedef decltype(*(std::declval<T>())) value_t;

    static value_t next(std::pair<T, T> &iter) {
        if (iter.first != iter.second) {
            value_t val = *iter.first;
            ++iter.first;
            return val;
        } else {
            PyErr_SetString(PyExc_StopIteration, "End of range reached");
            boost::python::throw_error_already_set();
            // Should be unreachable, but prevent control may reach end of non-void
            throw std::runtime_error("unreachable");
        }
    }

    static void wrap(const char *python_name) {
        class_<std::pair<T, T>>(python_name, no_init)
                .def("next", next);
    }
};

/*
A wrapper for a nextpnr Range. Ranges should have two functions, begin()
and end() which return iterator-like objects supporting ++, * and !=
Full STL iterator semantics are not required, unlike the standard Boost wrappers
*/
inline object pass_through(object const &o) { return o; }

template<typename T>
struct range_wrapper {
    typedef decltype(std::declval<T>().begin()) iterator_t;

    static std::pair<iterator_t, iterator_t> iter(T &range) {
        return std::make_pair(range.begin(), range.end());
    }

    static void wrap(const char *range_name, const char *iter_name) {
        class_<T>(range_name, no_init)
                .def("__iter__", iter);
        iterator_wrapper<iterator_t>().wrap(iter_name);
    }
};

#define WRAP_RANGE(t) range_wrapper<t##Range>().wrap(#t "Range", #t "Iterator")

BOOST_PYTHON_MODULE (MODULE_NAME) {
    // From Chip.h
    class_<ChipArgs>("ChipArgs");
    WRAP_RANGE(Bels);
    WRAP_RANGE(AllWires);
    WRAP_RANGE(WireDelay);
    WRAP_RANGE(BelPin);

}

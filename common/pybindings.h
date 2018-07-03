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

#ifndef COMMON_PYBINDINGS_H
#define COMMON_PYBINDINGS_H

#include <Python.h>
#include <boost/python.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <stdexcept>
#include <utility>
#include "pycontainers.h"
#include "pywrappers.h"

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

using namespace boost::python;

std::string parse_python_exception();

template <typename Tn> void python_export_global(const char *name, Tn &x)
{
    PyObject *m, *d;
    m = PyImport_AddModule("__main__");
    if (m == NULL)
        return;
    d = PyModule_GetDict(m);
    try {
        PyObject *p = incref(object(boost::ref(x)).ptr());
        PyDict_SetItemString(d, name, p);
    } catch (boost::python::error_already_set const &) {
        // Parse and output the exception
        std::string perror_str = parse_python_exception();
        std::cout << "Error in Python: " << perror_str << std::endl;
        std::terminate();
    }
};

void init_python(const char *executable, bool first);

void deinit_python();

void execute_python_file(const char *python_file);

// Defauld IdString conversions
namespace PythonConversion {

template <> struct string_converter<IdString>
{
    inline IdString from_str(Context *ctx, std::string name) { return ctx->id(name); }

    inline std::string to_str(Context *ctx, IdString id) { return id.str(ctx); }
};

template <> struct string_converter<const IdString>
{
    inline IdString from_str(Context *ctx, std::string name) { return ctx->id(name); }

    inline std::string to_str(Context *ctx, IdString id) { return id.str(ctx); }
};

} // namespace PythonConversion

NEXTPNR_NAMESPACE_END

#endif /* end of include guard: COMMON_PYBINDINGS_HH */

#ifndef NO_PYTHON

#include <Python.h>
#include <pybind11/pybind11.h>
#include "nextpnr.h"

namespace py = pybind11;

NEXTPNR_NAMESPACE_BEGIN

// Parses the value of the active python exception
// NOTE SHOULD NOT BE CALLED IF NO EXCEPTION
std::string parse_python_exception()
{
    PyObject *type_ptr = NULL, *value_ptr = NULL, *traceback_ptr = NULL;
    // Fetch the exception info from the Python C API
    PyErr_Fetch(&type_ptr, &value_ptr, &traceback_ptr);

    // Fallback error
    std::string ret("Unfetchable Python error");
    // If the fetch got a type pointer, parse the type into the exception string
    if (type_ptr != NULL) {
        py::object obj = py::reinterpret_borrow<py::object>(type_ptr);
        // If a valid string extraction is available, use it
        //  otherwise use fallback
        if (py::isinstance<py::str>(obj))
            ret = obj.cast<std::string>();
        else
            ret = "Unknown exception type";
    }
    // Do the same for the exception value (the stringification of the
    // exception)
    if (value_ptr != NULL) {
        py::object obj = py::reinterpret_borrow<py::object>(value_ptr);
        if (py::isinstance<py::str>(obj))
            ret += ": " + obj.cast<std::string>();
        else
            ret += std::string(": Unparseable Python error: ");
    }
    // Parse lines from the traceback using the Python traceback module
    if (traceback_ptr != NULL) {
        py::handle h_tb(traceback_ptr);
        // Load the traceback module and the format_tb function
        py::object tb(py::module::import("traceback"));
        py::object fmt_tb(tb.attr("format_tb"));
        // Call format_tb to get a list of traceback strings
        py::object tb_list(fmt_tb(h_tb));
        // Join the traceback strings into a single string
        py::object tb_str(py::str("\n") + tb_list);
        // Extract the string, check the extraction, and fallback in necessary
        if (py::isinstance<py::str>(tb_str))
            ret += ": " + tb_str.cast<std::string>();
        else
            ret += std::string(": Unparseable Python traceback");
    }
    return ret;
}

NEXTPNR_NAMESPACE_END

#endif // NO_PYTHON
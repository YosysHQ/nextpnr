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

#ifndef COMMON_PYBINDINGS_H
#define COMMON_PYBINDINGS_H

#include "pycontainers.h"

#include <utility>
#include <stdexcept>
#include <boost/python.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
using namespace boost::python;

/*
A wrapper to enable custom type/ID to/from string conversions
 */
template <typename T> struct string_wrapper {
	template<typename F>
	struct from_pystring_converter {
		from_pystring_converter() {
			converter::registry::push_back(
					&convertible,
					&construct,
					boost::python::type_id<T>());
		};

		static void* convertible(PyObject* object) {
			return PyUnicode_Check(object) ? object : 0;
		}

		static void construct(
				PyObject* object,
				converter::rvalue_from_python_stage1_data* data) {
			const wchar_t* value = PyUnicode_AsUnicode(object);
			const std::wstring value_ws(value);
			if (value == 0) throw_error_already_set();
			void* storage = (
					(boost::python::converter::rvalue_from_python_storage<T>*)
							data)->storage.bytes;
			new (storage) T(fn(std::string(value_ws.begin(), value_ws.end())));
			data->convertible = storage;
		}

		static  F fn;
	};

	template<typename F> struct to_str_wrapper {
		static F fn;
		std::string str(T& x) {
			return fn(x);
		}
	};

	template<typename F1, typename F2> static void wrap(const char *type_name, F1 to_str_fn, F2 from_str_fn) {
		from_pystring_converter<F2>::fn = from_str_fn;
		from_pystring_converter<F2>();
		to_str_wrapper<F1>::fn = to_str_fn;
		class_<T>(type_name, no_init).def("__str__", to_str_wrapper<F1>::str);
	};
};


void init_python(const char *executable);
void deinit_python();

void execute_python_file(const char* python_file);
std::string parse_python_exception();
void arch_appendinittab();

#endif /* end of include guard: COMMON_PYBINDINGS_HH */

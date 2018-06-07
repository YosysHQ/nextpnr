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

#ifndef COMMON_PYCONTAINERS_H
#define COMMON_PYCONTAINERS_H

#include <utility>
#include <stdexcept>
#include <type_traits>
#include <boost/python.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>

using namespace boost::python;

/*
A wrapper for a Pythonised nextpnr Iterator. The actual class wrapped is a
pair<Iterator, Iterator> containing (current, end)
*/

template<typename T, typename P>
struct iterator_wrapper {
	typedef decltype(*(std::declval<T>())) value_t;

	static value_t next(std::pair <T, T> &iter) {
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
		class_ < std::pair < T, T >> (python_name, no_init)
				.def("__next__", next, P());
	}
};

/*
A wrapper for a nextpnr Range. Ranges should have two functions, begin()
and end() which return iterator-like objects supporting ++, * and !=
Full STL iterator semantics are not required, unlike the standard Boost wrappers
*/

template<typename T, typename P = return_value_policy<return_by_value>>
struct range_wrapper {
	typedef decltype(std::declval<T>().begin()) iterator_t;

	static std::pair <iterator_t, iterator_t> iter(T &range) {
		return std::make_pair(range.begin(), range.end());
	}

	static void wrap(const char *range_name, const char *iter_name) {
		class_<T>(range_name, no_init)
				.def("__iter__", iter);
		iterator_wrapper<iterator_t, P>().wrap(iter_name);
	}
	typedef iterator_wrapper<iterator_t, P> iter_wrap;
};

#define WRAP_RANGE(t) range_wrapper<t##Range>().wrap(#t "Range", #t "Iterator")

/*
Wrapper for a map, either an unordered_map, regular map or dict
 */
inline void KeyError() { PyErr_SetString(PyExc_KeyError, "Key not found"); }

template<typename T>
struct map_wrapper {
	typedef typename std::remove_cv<typename std::remove_reference<typename T::key_type>::type>::type K;
	typedef typename T::mapped_type V;
	typedef typename T::value_type KV;

	static V &get(T &x, K const &i) {
		if (x.find(i) != x.end()) return x.at(i);
		KeyError();
		std::terminate();
	}

	static void set(T &x, K const &i, V const &v) {
		x[i] = v;
	}

	static void del(T const &x, K const &i) {
		if (x.find(i) != x.end()) x.erase(i);
		else KeyError();
		std::terminate();
	}

	static void wrap(const char *map_name, const char *kv_name, const char *iter_name) {
		class_<KV>(kv_name)
				.def_readonly("first", &KV::first)
				.def_readwrite("second", &KV::second);
		typedef range_wrapper<T, return_value_policy<copy_non_const_reference>> rw;
		typename rw::iter_wrap().wrap(iter_name);
		class_<T>(map_name, no_init)
		        .def("__iter__", rw::iter)
				.def("__len__", &T::size)
				.def("__getitem__", get, return_internal_reference<>())
				.def("__setitem__", set, with_custodian_and_ward<1,2>());
	}
};

#define WRAP_MAP(t, name) map_wrapper<t>().wrap(#name, #name "KeyValue", #name "Iterator")

#endif

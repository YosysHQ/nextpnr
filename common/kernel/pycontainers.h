/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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

#include <pybind11/pybind11.h>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include "nextpnr.h"
#include "pywrappers.h"

NEXTPNR_NAMESPACE_BEGIN

namespace py = pybind11;

inline void KeyError()
{
    PyErr_SetString(PyExc_KeyError, "Key not found");
    throw py::error_already_set();
}

/*
A wrapper for a Pythonised nextpnr Iterator. The actual class wrapped is a
pair<Iterator, Iterator> containing (current, end), wrapped in a ContextualWrapper

*/

template <typename T, py::return_value_policy P, typename value_conv = PythonConversion::pass_through<T>>
struct iterator_wrapper
{
    typedef decltype(*(std::declval<T>())) value_t;

    typedef PythonConversion::ContextualWrapper<std::pair<T, T>> wrapped_iter_t;
    using return_t = typename value_conv::ret_type;

    static return_t next(wrapped_iter_t &iter)
    {
        if (iter.base.first != iter.base.second) {
            return_t val = value_conv()(iter.ctx, *iter.base.first);
            ++iter.base.first;
            return val;
        } else {
            PyErr_SetString(PyExc_StopIteration, "End of range reached");
            throw py::error_already_set();
        }
    }

    static void wrap(py::module &m, const char *python_name)
    {
        py::class_<wrapped_iter_t>(m, python_name).def("__next__", next, P);
    }
};

/*
A pair that doesn't automatically become a tuple
*/
template <typename Ta, typename Tb> struct iter_pair
{
    iter_pair(){};
    iter_pair(const Ta &first, const Tb &second) : first(first), second(second){};
    Ta first;
    Tb second;
};

/*
A wrapper for a nextpnr Range. Ranges should have two functions, begin()
and end() which return iterator-like objects supporting ++, * and !=
Full STL iterator semantics are not required, unlike the standard Boost wrappers
*/

template <typename T, py::return_value_policy P = py::return_value_policy::copy,
          typename value_conv = PythonConversion::pass_through<T>>
struct range_wrapper
{
    typedef decltype(std::declval<T>().begin()) iterator_t;
    typedef decltype(*(std::declval<iterator_t>())) value_t;
    typedef typename PythonConversion::ContextualWrapper<T> wrapped_range;
    typedef typename PythonConversion::ContextualWrapper<std::pair<iterator_t, iterator_t>> wrapped_pair;
    static wrapped_pair iter(wrapped_range &range)
    {
        return wrapped_pair(range.ctx, std::make_pair(range.base.begin(), range.base.end()));
    }

    static std::string repr(wrapped_range &range)
    {
        PythonConversion::string_converter<value_t> conv;
        bool first = true;
        std::stringstream ss;
        ss << "[";
        for (const auto &item : range.base) {
            if (!first)
                ss << ", ";
            ss << "'" << conv.to_str(range.ctx, item) << "'";
            first = false;
        }
        ss << "]";
        return ss.str();
    }

    static void wrap(py::module &m, const char *range_name, const char *iter_name)
    {
        py::class_<wrapped_range>(m, range_name).def("__iter__", iter).def("__repr__", repr);
        iterator_wrapper<iterator_t, P, value_conv>().wrap(m, iter_name);
    }

    typedef iterator_wrapper<iterator_t, P, value_conv> iter_wrap;
};

#define WRAP_RANGE(m, t, conv)                                                                                         \
    range_wrapper<t##Range, py::return_value_policy::copy, conv>().wrap(m, #t "Range", #t "Iterator")

/*
A wrapper for a vector or similar structure. With support for conversion
*/

template <typename T, py::return_value_policy P = py::return_value_policy::copy,
          typename value_conv = PythonConversion::pass_through<T>>
struct vector_wrapper
{
    typedef decltype(std::declval<T>().begin()) iterator_t;
    typedef decltype(*(std::declval<iterator_t>())) value_t;
    typedef typename PythonConversion::ContextualWrapper<T &> wrapped_vector;
    typedef typename PythonConversion::ContextualWrapper<std::pair<iterator_t, iterator_t>> wrapped_pair;
    using return_t = typename value_conv::ret_type;
    static wrapped_pair iter(wrapped_vector &range)
    {
        return wrapped_pair(range.ctx, std::make_pair(range.base.begin(), range.base.end()));
    }

    static std::string repr(wrapped_vector &range)
    {
        PythonConversion::string_converter<value_t> conv;
        bool first = true;
        std::stringstream ss;
        ss << "[";
        for (const auto &item : range.base) {
            if (!first)
                ss << ", ";
            ss << "'" << conv.to_str(range.ctx, item) << "'";
            first = false;
        }
        ss << "]";
        return ss.str();
    }

    static int len(wrapped_vector &range) { return range.base.size(); }

    static return_t getitem(wrapped_vector &range, int i)
    {
        return value_conv()(range.ctx, boost::ref(range.base.at(i)));
    }

    static void wrap(py::module &m, const char *range_name, const char *iter_name)
    {
        py::class_<wrapped_vector>(m, range_name)
                .def("__iter__", iter)
                .def("__repr__", repr)
                .def("__len__", len)
                .def("__getitem__", getitem);

        iterator_wrapper<iterator_t, P, value_conv>().wrap(m, iter_name);
    }

    typedef iterator_wrapper<iterator_t, P, value_conv> iter_wrap;
};

#define WRAP_VECTOR(m, t, conv) vector_wrapper<t, py::return_value_policy::copy, conv>().wrap(m, #t, #t "Iterator")

template <typename T, py::return_value_policy P = py::return_value_policy::copy,
          typename value_conv = PythonConversion::pass_through<T>>
struct indexed_store_wrapper
{
    typedef decltype(std::declval<T>().begin()) iterator_t;
    typedef decltype(*(std::declval<iterator_t>())) value_t;
    typedef typename PythonConversion::ContextualWrapper<T &> wrapped_vector;
    typedef typename PythonConversion::ContextualWrapper<std::pair<iterator_t, iterator_t>> wrapped_pair;
    using return_t = typename value_conv::ret_type;
    static wrapped_pair iter(wrapped_vector &range)
    {
        return wrapped_pair(range.ctx, std::make_pair(range.base.begin(), range.base.end()));
    }

    static std::string repr(wrapped_vector &range)
    {
        PythonConversion::string_converter<value_t> conv;
        bool first = true;
        std::stringstream ss;
        ss << "[";
        for (const auto &item : range.base) {
            if (!first)
                ss << ", ";
            ss << "'" << conv.to_str(range.ctx, item) << "'";
            first = false;
        }
        ss << "]";
        return ss.str();
    }

    static int len(wrapped_vector &range) { return range.base.capacity(); }

    static py::object getitem(wrapped_vector &range, int i)
    {
        store_index<std::remove_reference_t<value_t>> idx(i);
        if (!range.base.count(idx))
            throw py::none();
        return py::cast(value_conv()(range.ctx, boost::ref(range.base.at(idx))));
    }

    static void wrap(py::module &m, const char *range_name, const char *iter_name)
    {
        py::class_<wrapped_vector>(m, range_name)
                .def("__iter__", iter)
                .def("__repr__", repr)
                .def("__len__", len)
                .def("__getitem__", getitem);

        iterator_wrapper<iterator_t, P, value_conv>().wrap(m, iter_name);
    }

    typedef iterator_wrapper<iterator_t, P, value_conv> iter_wrap;
};

#define WRAP_INDEXSTORE(m, t, conv)                                                                                    \
    indexed_store_wrapper<t, py::return_value_policy::copy, conv>().wrap(m, #t, #t "Iterator")

/*
Wrapper for a pair, allows accessing either using C++-style members (.first and
.second) or as a Python iterable and indexable object
*/
template <typename T1, typename T2> struct pair_wrapper
{
    typedef std::pair<T1, T2> T;

    struct pair_iterator_wrapper
    {
        static py::object next(iter_pair<T &, int> &iter)
        {
            if (iter.second == 0) {
                iter.second++;
                return py::cast(iter.first.first);
            } else if (iter.second == 1) {
                iter.second++;
                return py::cast(iter.first.second);
            } else {
                PyErr_SetString(PyExc_StopIteration, "End of range reached");
                throw py::error_already_set();
            }
        }

        static void wrap(py::module &m, const char *python_name)
        {
            py::class_<iter_pair<T &, int>>(m, python_name).def("__next__", next);
        }
    };

    static py::object get(T &x, int i)
    {
        if ((i >= 2) || (i < 0))
            KeyError();
        return (i == 1) ? py::object(x.second) : py::object(x.first);
    }

    static void set(T &x, int i, py::object val)
    {
        if ((i >= 2) || (i < 0))
            KeyError();
        if (i == 0)
            x.first = val.cast<T1>();
        if (i == 1)
            x.second = val.cast<T2>();
    }

    static int len(T & /*x*/) { return 2; }

    static iter_pair<T &, int> iter(T &x) { return iter_pair<T &, int>(boost::ref(x), 0); };

    static void wrap(py::module &m, const char *pair_name, const char *iter_name)
    {
        pair_iterator_wrapper::wrap(m, iter_name);
        py::class_<T>(m, pair_name)
                .def("__iter__", iter)
                .def("__len__", len)
                .def("__getitem__", get)
                .def("__setitem__", set, py::keep_alive<1, 2>())
                .def_readwrite("first", &T::first)
                .def_readwrite("second", &T::second);
    }
};

/*
Special case of above for map key/values
 */
template <typename T1, typename T2, typename value_conv> struct map_pair_wrapper
{
    typedef std::pair<T1, T2> T;
    typedef PythonConversion::ContextualWrapper<T &> wrapped_pair;
    typedef typename T::second_type V;

    struct pair_iterator_wrapper
    {
        static py::object next(iter_pair<wrapped_pair &, int> &iter)
        {
            if (iter.second == 0) {
                iter.second++;
                return py::cast(PythonConversion::string_converter<decltype(iter.first.base.first)>().to_str(
                        iter.first.ctx, iter.first.base.first));
            } else if (iter.second == 1) {
                iter.second++;
                return py::cast(value_conv()(iter.first.ctx, iter.first.base.second));
            } else {
                PyErr_SetString(PyExc_StopIteration, "End of range reached");
                throw py::error_already_set();
            }
        }

        static void wrap(py::module &m, const char *python_name)
        {
            py::class_<iter_pair<wrapped_pair &, int>>(m, python_name).def("__next__", next);
        }
    };

    static py::object get(wrapped_pair &x, int i)
    {
        if ((i >= 2) || (i < 0))
            KeyError();
        return (i == 1) ? py::cast(value_conv()(x.ctx, x.base.second))
                        : py::cast(PythonConversion::string_converter<decltype(x.base.first)>().to_str(x.ctx,
                                                                                                       x.base.first));
    }

    static int len(wrapped_pair & /*x*/) { return 2; }

    static iter_pair<wrapped_pair &, int> iter(wrapped_pair &x)
    {
        return iter_pair<wrapped_pair &, int>(boost::ref(x), 0);
    };

    static std::string first_getter(wrapped_pair &t)
    {
        return PythonConversion::string_converter<decltype(t.base.first)>().to_str(t.ctx, t.base.first);
    }

    static typename value_conv::ret_type second_getter(wrapped_pair &t) { return value_conv()(t.ctx, t.base.second); }

    static void wrap(py::module &m, const char *pair_name, const char *iter_name)
    {
        pair_iterator_wrapper::wrap(m, iter_name);
        py::class_<wrapped_pair>(m, pair_name)
                .def("__iter__", iter)
                .def("__len__", len)
                .def("__getitem__", get)
                .def_property_readonly("first", first_getter)
                .def_property_readonly("second", second_getter);
    }
};

/*
Wrapper for a map, either an unordered_map, regular map or dict
 */

template <typename T, typename value_conv> struct map_wrapper
{
    typedef typename std::remove_cv<typename std::remove_reference<typename T::key_type>::type>::type K;
    typedef typename T::mapped_type V;
    typedef typename value_conv::ret_type wrapped_V;
    typedef typename T::value_type KV;
    typedef typename PythonConversion::ContextualWrapper<T &> wrapped_map;

    static wrapped_V get(wrapped_map &x, std::string const &i)
    {
        K k = PythonConversion::string_converter<K>().from_str(x.ctx, i);
        if (x.base.find(k) != x.base.end())
            return value_conv()(x.ctx, x.base.at(k));
        KeyError();

        // Should be unreachable, but prevent control may reach end of non-void
        throw std::runtime_error("unreachable");
    }

    static void set(wrapped_map &x, std::string const &i, V const &v)
    {
        x.base[PythonConversion::string_converter<K>().from_str(x.ctx, i)] = v;
    }

    static size_t len(wrapped_map &x) { return x.base.size(); }

    static void del(T const &x, std::string const &i)
    {
        K k = PythonConversion::string_converter<K>().from_str(x.ctx, i);
        if (x.base.find(k) != x.base.end())
            x.base.erase(k);
        else
            KeyError();
    }

    static bool contains(wrapped_map &x, std::string const &i)
    {
        K k = PythonConversion::string_converter<K>().from_str(x.ctx, i);
        return x.base.count(k);
    }

    static void wrap(py::module &m, const char *map_name, const char *kv_name, const char *kv_iter_name,
                     const char *iter_name)
    {
        map_pair_wrapper<typename KV::first_type, typename KV::second_type, value_conv>::wrap(m, kv_name, kv_iter_name);
        typedef range_wrapper<T &, py::return_value_policy::copy, PythonConversion::wrap_context<KV &>> rw;
        typename rw::iter_wrap().wrap(m, iter_name);
        py::class_<wrapped_map>(m, map_name)
                .def("__iter__", rw::iter)
                .def("__len__", len)
                .def("__contains__", contains)
                .def("__getitem__", get)
                .def("__setitem__", set, py::keep_alive<1, 2>());
    }
};

/*
Special case of above for map key/values where value is a unique_ptr
 */
template <typename T1, typename T2> struct map_pair_wrapper_uptr
{
    typedef std::pair<T1, T2> T;
    typedef PythonConversion::ContextualWrapper<T &> wrapped_pair;
    typedef typename T::second_type::element_type V;

    struct pair_iterator_wrapper
    {
        static py::object next(iter_pair<wrapped_pair &, int> &iter)
        {
            if (iter.second == 0) {
                iter.second++;
                return py::cast(PythonConversion::string_converter<decltype(iter.first.base.first)>().to_str(
                        iter.first.ctx, iter.first.base.first));
            } else if (iter.second == 1) {
                iter.second++;
                return py::cast(
                        PythonConversion::ContextualWrapper<V &>(iter.first.ctx, *iter.first.base.second.get()));
            } else {
                PyErr_SetString(PyExc_StopIteration, "End of range reached");
                throw py::error_already_set();
            }
        }

        static void wrap(py::module &m, const char *python_name)
        {
            py::class_<iter_pair<wrapped_pair &, int>>(m, python_name).def("__next__", next);
        }
    };

    static py::object get(wrapped_pair &x, int i)
    {
        if (i >= 2 || i < 0)
            KeyError();
        return i == 1 ? py::cast(PythonConversion::ContextualWrapper<V &>(x.ctx, *x.base.second.get()))
                      : py::cast(PythonConversion::string_converter<decltype(x.base.first)>().to_str(x.ctx,
                                                                                                     x.base.first));
    }

    static int len(wrapped_pair & /*x*/) { return 2; }

    static iter_pair<wrapped_pair &, int> iter(wrapped_pair &x)
    {
        return iter_pair<wrapped_pair &, int>(boost::ref(x), 0);
    };

    static std::string first_getter(wrapped_pair &t)
    {
        return PythonConversion::string_converter<decltype(t.base.first)>().to_str(t.ctx, t.base.first);
    }

    static PythonConversion::ContextualWrapper<V &> second_getter(wrapped_pair &t)
    {
        return PythonConversion::ContextualWrapper<V &>(t.ctx, *t.base.second.get());
    }

    static void wrap(py::module &m, const char *pair_name, const char *iter_name)
    {
        pair_iterator_wrapper::wrap(m, iter_name);
        py::class_<wrapped_pair>(m, pair_name)
                .def("__iter__", iter)
                .def("__len__", len)
                .def("__getitem__", get)
                .def_property_readonly("first", first_getter)
                .def_property_readonly("second", second_getter);
    }
};

/*
Wrapper for a map, either an unordered_map, regular map or dict
 */

template <typename T> struct map_wrapper_uptr
{
    typedef typename std::remove_cv<typename std::remove_reference<typename T::key_type>::type>::type K;
    typedef typename T::mapped_type::pointer V;
    typedef typename T::mapped_type::element_type &Vr;
    typedef typename T::value_type KV;
    typedef typename PythonConversion::ContextualWrapper<T &> wrapped_map;

    static PythonConversion::ContextualWrapper<Vr> get(wrapped_map &x, std::string const &i)
    {
        K k = PythonConversion::string_converter<K>().from_str(x.ctx, i);
        if (x.base.find(k) != x.base.end())
            return PythonConversion::ContextualWrapper<Vr>(x.ctx, *x.base.at(k).get());
        KeyError();

        // Should be unreachable, but prevent control may reach end of non-void
        throw std::runtime_error("unreachable");
    }

    static void set(wrapped_map &x, std::string const &i, V const &v)
    {
        x.base[PythonConversion::string_converter<K>().from_str(x.ctx, i)] = typename T::mapped_type(v);
    }

    static size_t len(wrapped_map &x) { return x.base.size(); }

    static void del(T const &x, std::string const &i)
    {
        K k = PythonConversion::string_converter<K>().from_str(x.ctx, i);
        if (x.base.find(k) != x.base.end())
            x.base.erase(k);
        else
            KeyError();
    }

    static bool contains(wrapped_map &x, std::string const &i)
    {
        K k = PythonConversion::string_converter<K>().from_str(x.ctx, i);
        return x.base.count(k);
    }

    static void wrap(py::module &m, const char *map_name, const char *kv_name, const char *kv_iter_name,
                     const char *iter_name)
    {
        map_pair_wrapper_uptr<typename KV::first_type, typename KV::second_type>::wrap(m, kv_name, kv_iter_name);
        typedef range_wrapper<T &, py::return_value_policy::copy, PythonConversion::wrap_context<KV &>> rw;
        typename rw::iter_wrap().wrap(m, iter_name);
        py::class_<wrapped_map>(m, map_name)
                .def("__iter__", rw::iter)
                .def("__len__", len)
                .def("__contains__", contains)
                .def("__getitem__", get)
                .def("__setitem__", set, py::keep_alive<1, 2>());
    }
};

#define WRAP_MAP(m, t, conv, name)                                                                                     \
    map_wrapper<t, conv>().wrap(m, #name, #name "KeyValue", #name "KeyValueIter", #name "Iterator")
#define WRAP_MAP_UPTR(m, t, name)                                                                                      \
    map_wrapper_uptr<t>().wrap(m, #name, #name "KeyValue", #name "KeyValueIter", #name "Iterator")

NEXTPNR_NAMESPACE_END

#endif

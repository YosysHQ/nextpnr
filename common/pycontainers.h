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

#ifndef COMMON_PYCONTAINERS_H
#define COMMON_PYCONTAINERS_H

#include <boost/python.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include "nextpnr.h"
#include "pywrappers.h"

NEXTPNR_NAMESPACE_BEGIN

using namespace boost::python;

inline void KeyError() { PyErr_SetString(PyExc_KeyError, "Key not found"); }

/*
A wrapper for a Pythonised nextpnr Iterator. The actual class wrapped is a
pair<Iterator, Iterator> containing (current, end), wrapped in a ContextualWrapper

*/

template <typename T, typename P, typename value_conv = PythonConversion::pass_through<T>> struct iterator_wrapper
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
            boost::python::throw_error_already_set();
            // Should be unreachable, but prevent control may reach end of
            // non-void
            throw std::runtime_error("unreachable");
        }
    }

    static void wrap(const char *python_name)
    {
        class_<wrapped_iter_t>(python_name, no_init).def("__next__", next, P());
    }
};

/*
A wrapper for a nextpnr Range. Ranges should have two functions, begin()
and end() which return iterator-like objects supporting ++, * and !=
Full STL iterator semantics are not required, unlike the standard Boost wrappers
*/

template <typename T, typename P = return_value_policy<return_by_value>,
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

    static void wrap(const char *range_name, const char *iter_name)
    {
        class_<wrapped_range>(range_name, no_init).def("__iter__", iter).def("__repr__", repr);
        iterator_wrapper<iterator_t, P, value_conv>().wrap(iter_name);
    }

    typedef iterator_wrapper<iterator_t, P, value_conv> iter_wrap;
};

#define WRAP_RANGE(t, conv)                                                                                            \
    range_wrapper<t##Range, return_value_policy<return_by_value>, conv>().wrap(#t "Range", #t "Iterator")

/*
A wrapper for a vector or similar structure. With support for conversion
*/

template <typename T, typename P = return_value_policy<return_by_value>,
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

    static void wrap(const char *range_name, const char *iter_name)
    {
        class_<wrapped_vector>(range_name, no_init)
                .def("__iter__", iter)
                .def("__repr__", repr)
                .def("__len__", len)
                .def("__getitem__", getitem);

        iterator_wrapper<iterator_t, P, value_conv>().wrap(iter_name);
    }

    typedef iterator_wrapper<iterator_t, P, value_conv> iter_wrap;
};

#define WRAP_VECTOR(t, conv) vector_wrapper<t, return_value_policy<return_by_value>, conv>().wrap(#t, #t "Iterator")

/*
Wrapper for a pair, allows accessing either using C++-style members (.first and
.second) or as a Python iterable and indexable object
*/
template <typename T1, typename T2> struct pair_wrapper
{
    typedef std::pair<T1, T2> T;

    struct pair_iterator_wrapper
    {
        static object next(std::pair<T &, int> &iter)
        {
            if (iter.second == 0) {
                iter.second++;
                return object(iter.first.first);
            } else if (iter.second == 1) {
                iter.second++;
                return object(iter.first.second);
            } else {
                PyErr_SetString(PyExc_StopIteration, "End of range reached");
                boost::python::throw_error_already_set();
                // Should be unreachable, but prevent control may reach end of
                // non-void
                throw std::runtime_error("unreachable");
            }
        }

        static void wrap(const char *python_name)
        {
            class_<std::pair<T &, int>>(python_name, no_init).def("__next__", next);
        }
    };

    static object get(T &x, int i)
    {
        if ((i >= 2) || (i < 0))
            KeyError();
        return (i == 1) ? object(x.second) : object(x.first);
    }

    static void set(T &x, int i, object val)
    {
        if ((i >= 2) || (i < 0))
            KeyError();
        if (i == 0)
            x.first = extract<T1>(val);
        if (i == 1)
            x.second = extract<T2>(val);
    }

    static int len(T &x) { return 2; }

    static std::pair<T &, int> iter(T &x) { return std::make_pair(boost::ref(x), 0); };

    static void wrap(const char *pair_name, const char *iter_name)
    {
        pair_iterator_wrapper::wrap(iter_name);
        class_<T>(pair_name, no_init)
                .def("__iter__", iter)
                .def("__len__", len)
                .def("__getitem__", get)
                .def("__setitem__", set, with_custodian_and_ward<1, 2>())
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
        static object next(std::pair<wrapped_pair &, int> &iter)
        {
            if (iter.second == 0) {
                iter.second++;
                return object(PythonConversion::string_converter<decltype(iter.first.base.first)>().to_str(
                        iter.first.ctx, iter.first.base.first));
            } else if (iter.second == 1) {
                iter.second++;
                return object(value_conv()(iter.first.ctx, iter.first.base.second));
            } else {
                PyErr_SetString(PyExc_StopIteration, "End of range reached");
                boost::python::throw_error_already_set();
                // Should be unreachable, but prevent control may reach end of
                // non-void
                throw std::runtime_error("unreachable");
            }
        }

        static void wrap(const char *python_name)
        {
            class_<std::pair<wrapped_pair &, int>>(python_name, no_init).def("__next__", next);
        }
    };

    static object get(wrapped_pair &x, int i)
    {
        if ((i >= 2) || (i < 0))
            KeyError();
        return (i == 1) ? object(value_conv()(x.ctx, x.base.second)) : object(x.base.first);
    }

    static int len(wrapped_pair &x) { return 2; }

    static std::pair<wrapped_pair &, int> iter(wrapped_pair &x) { return std::make_pair(boost::ref(x), 0); };

    static std::string first_getter(wrapped_pair &t)
    {
        return PythonConversion::string_converter<decltype(t.base.first)>().to_str(t.ctx, t.base.first);
    }

    static typename value_conv::ret_type second_getter(wrapped_pair &t) { return value_conv()(t.ctx, t.base.second); }

    static void wrap(const char *pair_name, const char *iter_name)
    {
        pair_iterator_wrapper::wrap(iter_name);
        class_<wrapped_pair>(pair_name, no_init)
                .def("__iter__", iter)
                .def("__len__", len)
                .def("__getitem__", get)
                .add_property("first", first_getter)
                .add_property("second", second_getter);
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
        std::terminate();
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
        std::terminate();
    }

    static void wrap(const char *map_name, const char *kv_name, const char *kv_iter_name, const char *iter_name)
    {
        map_pair_wrapper<typename KV::first_type, typename KV::second_type, value_conv>::wrap(kv_name, kv_iter_name);
        typedef range_wrapper<T &, return_value_policy<return_by_value>, PythonConversion::wrap_context<KV &>> rw;
        typename rw::iter_wrap().wrap(iter_name);
        class_<wrapped_map>(map_name, no_init)
                .def("__iter__", rw::iter)
                .def("__len__", len)
                .def("__getitem__", get)
                .def("__setitem__", set, with_custodian_and_ward<1, 2>());
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
        static object next(std::pair<wrapped_pair &, int> &iter)
        {
            if (iter.second == 0) {
                iter.second++;
                return object(PythonConversion::string_converter<decltype(iter.first.base.first)>().to_str(
                        iter.first.ctx, iter.first.base.first));
            } else if (iter.second == 1) {
                iter.second++;
                return object(PythonConversion::ContextualWrapper<V &>(iter.first.ctx, *iter.first.base.second.get()));
            } else {
                PyErr_SetString(PyExc_StopIteration, "End of range reached");
                boost::python::throw_error_already_set();
                // Should be unreachable, but prevent control may reach end of
                // non-void
                throw std::runtime_error("unreachable");
            }
        }

        static void wrap(const char *python_name)
        {
            class_<std::pair<wrapped_pair &, int>>(python_name, no_init).def("__next__", next);
        }
    };

    static object get(wrapped_pair &x, int i)
    {
        if ((i >= 2) || (i < 0))
            KeyError();
        return (i == 1) ? object(PythonConversion::ContextualWrapper<V &>(x.ctx, *x.base.second.get()))
                        : object(x.base.first);
    }

    static int len(wrapped_pair &x) { return 2; }

    static std::pair<wrapped_pair &, int> iter(wrapped_pair &x) { return std::make_pair(boost::ref(x), 0); };

    static std::string first_getter(wrapped_pair &t)
    {
        return PythonConversion::string_converter<decltype(t.base.first)>().to_str(t.ctx, t.base.first);
    }

    static PythonConversion::ContextualWrapper<V &> second_getter(wrapped_pair &t)
    {
        return PythonConversion::ContextualWrapper<V &>(t.ctx, *t.base.second.get());
    }

    static void wrap(const char *pair_name, const char *iter_name)
    {
        pair_iterator_wrapper::wrap(iter_name);
        class_<wrapped_pair>(pair_name, no_init)
                .def("__iter__", iter)
                .def("__len__", len)
                .def("__getitem__", get)
                .add_property("first", first_getter)
                .add_property("second", second_getter);
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
        std::terminate();
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
        std::terminate();
    }

    static void wrap(const char *map_name, const char *kv_name, const char *kv_iter_name, const char *iter_name)
    {
        map_pair_wrapper_uptr<typename KV::first_type, typename KV::second_type>::wrap(kv_name, kv_iter_name);
        typedef range_wrapper<T &, return_value_policy<return_by_value>, PythonConversion::wrap_context<KV &>> rw;
        typename rw::iter_wrap().wrap(iter_name);
        class_<wrapped_map>(map_name, no_init)
                .def("__iter__", rw::iter)
                .def("__len__", len)
                .def("__getitem__", get)
                .def("__setitem__", set, with_custodian_and_ward<1, 2>());
    }
};

#define WRAP_MAP(t, conv, name)                                                                                        \
    map_wrapper<t, conv>().wrap(#name, #name "KeyValue", #name "KeyValueIter", #name "Iterator")
#define WRAP_MAP_UPTR(t, name)                                                                                         \
    map_wrapper_uptr<t>().wrap(#name, #name "KeyValue", #name "KeyValueIter", #name "Iterator")

NEXTPNR_NAMESPACE_END

#endif

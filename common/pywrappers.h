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

#ifndef PYWRAPPERS_H
#define PYWRAPPERS_H

#include <boost/function_types/function_arity.hpp>
#include <boost/function_types/function_type.hpp>
#include <boost/function_types/parameter_types.hpp>
#include <boost/function_types/result_type.hpp>
#include <boost/python.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <utility>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

using namespace boost::python;

namespace PythonConversion {
template <typename T> struct ContextualWrapper
{
    Context *ctx;
    T base;

    ContextualWrapper(Context *c, T &&x) : ctx(c), base(x){};

    operator T() { return base; };
    typedef T base_type;
};

// Action options
class do_nothing
{
    template <typename T> T operator()(Context *ctx, T x) { return x; }

    template <typename T> using ret_type = T;
    template <typename T> using arg_type = T;
};

class wrap_context
{
    template <typename T> ContextualWrapper<T> operator()(Context *ctx, T x) { return ContextualWrapper<T>(ctx, x); }

    template <typename T> using ret_type = ContextualWrapper<T>;
};

class unwrap_context
{
    template <typename T> T operator()(Context *ctx, ContextualWrapper<T> x) { return x.base; }

    template <typename T> using arg_type = ContextualWrapper<T>;
};

// Function wrapper
// Example: one parameter, one return
template <typename Class, typename FuncT, FuncT fn, typename rv_conv, typename arg1_conv> struct function_wrapper
{
    using result_type = typename boost::function_types::result_type<FuncT>::type;
    using arg1_type = typename boost::mpl::at_c<boost::function_types::parameter_types<FuncT>, 1>::type;
    using class_type = ContextualWrapper<Class>;

    static typename rv_conv::ret_type wrapped_fn(class_type &cls, typename arg1_conv::arg_type arg1)
    {
        return rv_conv()(cls.ctx, cls.base.*fn(arg1_conv()(cls.ctx, arg1)));
    }
};

} // namespace PythonConversion

NEXTPNR_NAMESPACE_END

#endif

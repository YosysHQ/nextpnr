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

#ifndef PYWRAPPERS_H
#define PYWRAPPERS_H

#include <pybind11/pybind11.h>
#include <utility>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

namespace py = pybind11;

namespace PythonConversion {
template <typename T> struct ContextualWrapper
{
    Context *ctx;
    T base;

    inline ContextualWrapper(Context *c, T x) : ctx(c), base(x) {}

    inline operator T() { return base; }
    typedef T base_type;
};

template <typename T> struct WrapIfNotContext
{
    typedef ContextualWrapper<T> maybe_wrapped_t;
};

template <> struct WrapIfNotContext<Context>
{
    typedef Context maybe_wrapped_t;
};

template <typename T> inline Context *get_ctx(typename WrapIfNotContext<T>::maybe_wrapped_t &wrp_ctx)
{
    return wrp_ctx.ctx;
}

template <> inline Context *get_ctx<Context>(WrapIfNotContext<Context>::maybe_wrapped_t &unwrp_ctx)
{
    return &unwrp_ctx;
}

template <typename T> inline T &get_base(typename WrapIfNotContext<T>::maybe_wrapped_t &wrp_ctx)
{
    return wrp_ctx.base;
}

template <> inline Context &get_base<Context>(WrapIfNotContext<Context>::maybe_wrapped_t &unwrp_ctx)
{
    return unwrp_ctx;
}

template <typename T> ContextualWrapper<T> wrap_ctx(Context *ctx, T x) { return ContextualWrapper<T>(ctx, x); }

// Dummy class, to be implemented by users
template <typename T> struct string_converter;

class bad_wrap
{
};

// Action options
template <typename T> struct pass_through
{
    inline T operator()(Context * /*ctx*/, T x) { return x; }

    using ret_type = T;
    using arg_type = T;
};

template <typename T> struct wrap_context
{
    inline ContextualWrapper<T> operator()(Context *ctx, T x) { return ContextualWrapper<T>(ctx, x); }

    using arg_type = T;
    using ret_type = ContextualWrapper<T>;
};

template <typename T> struct unwrap_context
{
    inline T operator()(Context * /*ctx*/, ContextualWrapper<T> x) { return x.base; }

    using ret_type = T;
    using arg_type = ContextualWrapper<T>;
};

template <typename T> struct conv_from_str
{
    inline T operator()(Context *ctx, std::string x) { return string_converter<T>().from_str(ctx, x); }

    using ret_type = T;
    using arg_type = std::string;
};

template <typename T> struct conv_to_str
{
    inline std::string operator()(Context *ctx, T x) { return string_converter<T>().to_str(ctx, x); }

    using ret_type = std::string;
    using arg_type = T;
};

template <typename T> struct deref_and_wrap
{
    inline ContextualWrapper<T &> operator()(Context *ctx, T *x)
    {
        if (x == nullptr)
            throw bad_wrap();
        return ContextualWrapper<T &>(ctx, *x);
    }

    using arg_type = T *;
    using ret_type = ContextualWrapper<T &>;
};

template <typename T> struct addr_and_unwrap
{
    inline T *operator()(Context * /*ctx*/, ContextualWrapper<T &> x) { return &(x.base); }

    using arg_type = ContextualWrapper<T &>;
    using ret_type = T *;
};

// Function wrapper
// Zero parameters, one return
template <typename Class, typename FuncT, FuncT fn, typename rv_conv> struct fn_wrapper_0a
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_result_type = typename rv_conv::ret_type;

    static py::object wrapped_fn(class_type &cls)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        try {
            return py::cast(rv_conv()(ctx, (base.*fn)()));
        } catch (bad_wrap &) {
            return py::none();
        }
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }
};

// One parameter, one return
template <typename Class, typename FuncT, FuncT fn, typename rv_conv, typename arg1_conv> struct fn_wrapper_1a
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_result_type = typename rv_conv::ret_type;
    using conv_arg1_type = typename arg1_conv::arg_type;

    static py::object wrapped_fn(class_type &cls, conv_arg1_type arg1)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        try {
            return py::cast(rv_conv()(ctx, (base.*fn)(arg1_conv()(ctx, arg1))));
        } catch (bad_wrap &) {
            return py::none();
        }
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }
};

// Two parameters, one return
template <typename Class, typename FuncT, FuncT fn, typename rv_conv, typename arg1_conv, typename arg2_conv>
struct fn_wrapper_2a
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_result_type = typename rv_conv::ret_type;
    using conv_arg1_type = typename arg1_conv::arg_type;
    using conv_arg2_type = typename arg2_conv::arg_type;

    static py::object wrapped_fn(class_type &cls, conv_arg1_type arg1, conv_arg2_type arg2)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        try {
            return py::cast(rv_conv()(ctx, (base.*fn)(arg1_conv()(ctx, arg1), arg2_conv()(ctx, arg2))));
        } catch (bad_wrap &) {
            return py::none();
        }
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }
};

// Three parameters, one return
template <typename Class, typename FuncT, FuncT fn, typename rv_conv, typename arg1_conv, typename arg2_conv,
          typename arg3_conv>
struct fn_wrapper_3a
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_result_type = typename rv_conv::ret_type;
    using conv_arg1_type = typename arg1_conv::arg_type;
    using conv_arg2_type = typename arg2_conv::arg_type;
    using conv_arg3_type = typename arg3_conv::arg_type;

    static py::object wrapped_fn(class_type &cls, conv_arg1_type arg1, conv_arg2_type arg2, conv_arg3_type arg3)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        try {
            return py::cast(
                    rv_conv()(ctx, (base.*fn)(arg1_conv()(ctx, arg1), arg2_conv()(ctx, arg2), arg3_conv()(ctx, arg3))));
        } catch (bad_wrap &) {
            return py::none();
        }
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }
};

// Zero parameters void
template <typename Class, typename FuncT, FuncT fn> struct fn_wrapper_0a_v
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;

    static void wrapped_fn(class_type &cls)
    {
        Class &base = get_base<Class>(cls);
        return (base.*fn)();
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }
};

// One parameter, void
template <typename Class, typename FuncT, FuncT fn, typename arg1_conv> struct fn_wrapper_1a_v
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_arg1_type = typename arg1_conv::arg_type;

    static void wrapped_fn(class_type &cls, conv_arg1_type arg1)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        (base.*fn)(arg1_conv()(ctx, arg1));
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }

    template <typename WrapCls, typename Ta>
    static void def_wrap(WrapCls cls_, const char *name, Ta a = py::arg("arg1"))
    {
        cls_.def(name, wrapped_fn, a);
    }
};

// Two parameters, no return
template <typename Class, typename FuncT, FuncT fn, typename arg1_conv, typename arg2_conv> struct fn_wrapper_2a_v
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_arg1_type = typename arg1_conv::arg_type;
    using conv_arg2_type = typename arg2_conv::arg_type;

    static void wrapped_fn(class_type &cls, conv_arg1_type arg1, conv_arg2_type arg2)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        (base.*fn)(arg1_conv()(ctx, arg1), arg2_conv()(ctx, arg2));
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }

    template <typename WrapCls, typename... Ta> static void def_wrap(WrapCls cls_, const char *name, Ta... a)
    {
        cls_.def(name, wrapped_fn, a...);
    }
};

// Three parameters, no return
template <typename Class, typename FuncT, FuncT fn, typename arg1_conv, typename arg2_conv, typename arg3_conv>
struct fn_wrapper_3a_v
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_arg1_type = typename arg1_conv::arg_type;
    using conv_arg2_type = typename arg2_conv::arg_type;
    using conv_arg3_type = typename arg3_conv::arg_type;

    static void wrapped_fn(class_type &cls, conv_arg1_type arg1, conv_arg2_type arg2, conv_arg3_type arg3)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        (base.*fn)(arg1_conv()(ctx, arg1), arg2_conv()(ctx, arg2), arg3_conv()(ctx, arg3));
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }

    template <typename WrapCls, typename... Ta> static void def_wrap(WrapCls cls_, const char *name, Ta... a)
    {
        cls_.def(name, wrapped_fn, a...);
    }
};

// Four parameters, no return
template <typename Class, typename FuncT, FuncT fn, typename arg1_conv, typename arg2_conv, typename arg3_conv,
          typename arg4_conv>
struct fn_wrapper_4a_v
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_arg1_type = typename arg1_conv::arg_type;
    using conv_arg2_type = typename arg2_conv::arg_type;
    using conv_arg3_type = typename arg3_conv::arg_type;
    using conv_arg4_type = typename arg4_conv::arg_type;

    static void wrapped_fn(class_type &cls, conv_arg1_type arg1, conv_arg2_type arg2, conv_arg3_type arg3,
                           conv_arg4_type arg4)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        (base.*fn)(arg1_conv()(ctx, arg1), arg2_conv()(ctx, arg2), arg3_conv()(ctx, arg3), arg4_conv()(ctx, arg4));
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }

    template <typename WrapCls, typename... Ta> static void def_wrap(WrapCls cls_, const char *name, Ta... a)
    {
        cls_.def(name, wrapped_fn, a...);
    }
};

// Five parameters, no return
template <typename Class, typename FuncT, FuncT fn, typename arg1_conv, typename arg2_conv, typename arg3_conv,
          typename arg4_conv, typename arg5_conv>
struct fn_wrapper_5a_v
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_arg1_type = typename arg1_conv::arg_type;
    using conv_arg2_type = typename arg2_conv::arg_type;
    using conv_arg3_type = typename arg3_conv::arg_type;
    using conv_arg4_type = typename arg4_conv::arg_type;
    using conv_arg5_type = typename arg5_conv::arg_type;

    static void wrapped_fn(class_type &cls, conv_arg1_type arg1, conv_arg2_type arg2, conv_arg3_type arg3,
                           conv_arg4_type arg4, conv_arg5_type arg5)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        (base.*fn)(arg1_conv()(ctx, arg1), arg2_conv()(ctx, arg2), arg3_conv()(ctx, arg3), arg4_conv()(ctx, arg4),
                   arg5_conv()(ctx, arg5));
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }

    template <typename WrapCls, typename... Ta> static void def_wrap(WrapCls cls_, const char *name, Ta... a)
    {
        cls_.def(name, wrapped_fn, a...);
    }
};

// Six parameters, no return
template <typename Class, typename FuncT, FuncT fn, typename arg1_conv, typename arg2_conv, typename arg3_conv,
          typename arg4_conv, typename arg5_conv, typename arg6_conv>
struct fn_wrapper_6a_v
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_arg1_type = typename arg1_conv::arg_type;
    using conv_arg2_type = typename arg2_conv::arg_type;
    using conv_arg3_type = typename arg3_conv::arg_type;
    using conv_arg4_type = typename arg4_conv::arg_type;
    using conv_arg5_type = typename arg5_conv::arg_type;
    using conv_arg6_type = typename arg6_conv::arg_type;

    static void wrapped_fn(class_type &cls, conv_arg1_type arg1, conv_arg2_type arg2, conv_arg3_type arg3,
                           conv_arg4_type arg4, conv_arg5_type arg5, conv_arg6_type arg6)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        (base.*fn)(arg1_conv()(ctx, arg1), arg2_conv()(ctx, arg2), arg3_conv()(ctx, arg3), arg4_conv()(ctx, arg4),
                   arg5_conv()(ctx, arg5), arg6_conv()(ctx, arg6));
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name) { cls_.def(name, wrapped_fn); }

    template <typename WrapCls, typename... Ta> static void def_wrap(WrapCls cls_, const char *name, Ta... a)
    {
        cls_.def(name, wrapped_fn, a...);
    }
};

// Wrapped getter
template <typename Class, typename MemT, MemT mem, typename v_conv> struct readonly_wrapper
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_val_type = typename v_conv::ret_type;

    static py::object wrapped_getter(class_type &cls)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        try {
            return py::cast(v_conv()(ctx, (base.*mem)));
        } catch (bad_wrap &) {
            return py::none();
        }
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name)
    {
        cls_.def_property_readonly(name, wrapped_getter);
    }
};

// Wrapped getter/setter
template <typename Class, typename MemT, MemT mem, typename get_conv, typename set_conv> struct readwrite_wrapper
{
    using class_type = typename WrapIfNotContext<Class>::maybe_wrapped_t;
    using conv_val_type = typename get_conv::ret_type;

    static py::object wrapped_getter(class_type &cls)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        try {
            return py::cast(get_conv()(ctx, (base.*mem)));
        } catch (bad_wrap &) {
            return py::none();
        }
    }

    using conv_arg_type = typename set_conv::arg_type;

    static void wrapped_setter(class_type &cls, conv_arg_type val)
    {
        Context *ctx = get_ctx<Class>(cls);
        Class &base = get_base<Class>(cls);
        (base.*mem) = set_conv()(ctx, val);
    }

    template <typename WrapCls> static void def_wrap(WrapCls cls_, const char *name)
    {
        cls_.def_property(name, wrapped_getter, wrapped_setter);
    }
};

} // namespace PythonConversion

NEXTPNR_NAMESPACE_END

#endif

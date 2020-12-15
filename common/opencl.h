/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
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
#ifndef OPENCL_H
#define OPENCL_H
#ifdef USE_OPENCL

#include "nextpnr.h"
#define __CL_ENABLE_EXCEPTIONS
#include <CL/cl.hpp>

NEXTPNR_NAMESPACE_BEGIN

std::unique_ptr<cl::Context> get_opencl_ctx(Context *ctx);
std::unique_ptr<cl::Program> get_opencl_program(cl::Context &clctx, const std::string &name);
const std::string &get_opencl_source(const std::string &name);

NEXTPNR_NAMESPACE_END

#endif
#endif

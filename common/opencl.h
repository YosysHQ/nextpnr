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

#include "log.h"
#include "nextpnr.h"

#define __CL_ENABLE_EXCEPTIONS
#include <CL/cl.hpp>

NEXTPNR_NAMESPACE_BEGIN

std::unique_ptr<cl::Context> get_opencl_ctx(Context *ctx);
std::unique_ptr<cl::Program> get_opencl_program(cl::Context &clctx, const std::string &name);
const std::string &get_opencl_source(const std::string &name);

// A wrapper to manage GPU buffers
template <typename T> struct GPUBuffer
{
    const cl::Context &ctx;
    cl_mem_flags flags;
    cl::Buffer *buf = nullptr;
    size_t m_size = 0, max_size = 0;

    GPUBuffer<T>(const cl::Context &ctx, cl_mem_flags flags, size_t init_size = 0) : ctx(ctx), flags(flags)
    {
        if (init_size > 0)
            resize(init_size);
    }

    // If current size is less than new_size; increase the size of the buffer
    void resize(size_t new_size)
    {
        if (max_size < new_size) {
            cl_int err;
            delete buf;
            buf = new cl::Buffer(ctx, flags, sizeof(T) * new_size, nullptr, &err);
            if (err != CL_SUCCESS) {
                log_error("Allocation of CL buffer of size %d failed: %d\n", int(new_size), err);
            }
            max_size = new_size;
        }
        m_size = new_size;
    }

    // From host vector to GPU
    void put_vec(cl::CommandQueue &queue, const std::vector<T> &vec)
    {
        resize(vec.size());
        cl::copy(queue, vec.begin(), vec.end(), *buf);
    }

    // From GPU to host vector
    void get_vec(cl::CommandQueue &queue, std::vector<T> &vec)
    {
        vec.resize(m_size);
        if (m_size == 0)
            return;
        cl::copy(queue, *buf, vec.begin(), vec.end());
    }

    ~GPUBuffer() { delete buf; }

    size_t size() const { return m_size; };

    operator cl::Buffer &()
    {
        NPNR_ASSERT(buf != nullptr);
        return *buf;
    }
};

// As above, but manages the backing store too
template <typename T> struct BackedGPUBuffer : public GPUBuffer<T>
{
    std::vector<T> backing;
    bool size_inconsistent = false;

    BackedGPUBuffer<T>(const cl::Context &ctx, cl_mem_flags flags, size_t init_size = 0)
            : GPUBuffer<T>(ctx, flags, init_size)
    {
        backing.resize(init_size);
    }

    void resize(size_t new_size)
    {
        backing.resize(new_size);
        GPUBuffer<T>::resize(new_size);
    }

    void put(cl::CommandQueue &queue) { GPUBuffer<T>::put_vec(queue, backing); }

    void get(cl::CommandQueue &queue) { GPUBuffer<T>::get_vec(queue, backing); }

    T &at(size_t index) { return backing.at(index); }
    T &operator[](size_t index) { return backing.at(index); }
    void push_back(const T &val) { backing.push_back(val); }

    typename std::vector<T>::iterator begin() { return backing.begin(); }
    typename std::vector<T>::iterator end() { return backing.end(); }
};

NEXTPNR_NAMESPACE_END

#endif
#endif

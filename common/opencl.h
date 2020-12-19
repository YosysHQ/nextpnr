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
    cl::Buffer *m_buf = nullptr;
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
            delete m_buf;
            m_buf = new cl::Buffer(ctx, flags, sizeof(T) * new_size, nullptr, &err);
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
        cl::copy(queue, vec.begin(), vec.end(), *m_buf);
    }

    // From GPU to host vector
    void get_vec(cl::CommandQueue &queue, std::vector<T> &vec)
    {
        vec.resize(m_size);
        if (m_size == 0)
            return;
        cl::copy(queue, *m_buf, vec.begin(), vec.end());
    }

    ~GPUBuffer() { delete m_buf; }

    size_t size() const { return m_size; };

    // Write directly to the GPU
    void write(cl::CommandQueue &queue, size_t offset, const T &value)
    {
        queue.enqueueWriteBuffer(*m_buf, true, sizeof(T) * offset, sizeof(T), &value);
    }
    void write(cl::CommandQueue &queue, size_t offset, const std::vector<T> &values)
    {
        queue.enqueueWriteBuffer(*m_buf, true, sizeof(T) * offset, sizeof(T) * values.size(), values.data());
    }

    cl::Buffer &buf()
    {
        NPNR_ASSERT(m_buf != nullptr);
        return *m_buf;
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

// A buffer that is split into chunks that can be dynamically allocated to different 'owners'
// This is currently designed to be optimal with a relatively small number of large chunks
template <typename Tobj, typename Tkey = uint8_t> struct ChunkedGPUBuffer
{
    // Magic value to indicate that a chunk is free
    static const Tkey no_owner = std::numeric_limits<Tkey>::max();

    ChunkedGPUBuffer<Tobj, Tkey>(const cl::Context &ctx, cl_mem_flags flags, size_t chunk_size, size_t owner_count,
                                 size_t init_chunk_count = 0)
            : chunk_size(chunk_size), owner_count(owner_count), chunk_count(init_chunk_count), dirty(true),
              pool(ctx, flags, chunk_size * init_chunk_count), chunk2owner(ctx, CL_MEM_READ_ONLY, init_chunk_count),
              owner2chunk(owner_count + 1)
    {
        NPNR_ASSERT(owner_count < no_owner);
        for (auto &cto : chunk2owner)
            cto = no_owner;
        for (size_t i = 0; i < init_chunk_count; i++)
            owner2chunk.at(owner_count).push_back(i);
    }
    size_t chunk_size, owner_count, chunk_count;
    bool dirty;
    // All chunks
    GPUBuffer<Tobj> pool;
    // Mapping from chunk to owner index
    BackedGPUBuffer<Tkey> chunk2owner;
    // Owner to chunk mapping - entry N is free chunks here
    std::vector<std::vector<size_t>> owner2chunk;

    // Add chunks to the pool - this will destroy the pool content and is mainly intended for delayed init cases
    void extend(size_t new_size)
    {
        size_t old_size = chunk_count;
        if (new_size == old_size)
            return;
        NPNR_ASSERT(new_size > old_size);
        pool.resize(new_size * chunk_size);
        chunk_count = new_size;
        chunk2owner.resize(new_size);
        for (size_t i = old_size; i < new_size; i++) {
            chunk2owner[i] = no_owner;
            // Add to the free list
            owner2chunk.at(owner_count).push_back(i);
        }
        dirty = true;
    }

    // Request N chunks for an owner (must be bigger than the current allocation)
    bool request(Tkey owner, size_t new_count)
    {
        NPNR_ASSERT(owner < owner_count);
        size_t old_count = owner2chunk.at(owner).size();
        if (new_count == old_count)
            return true;
        NPNR_ASSERT(new_count > old_count);
        // Not enough free chunks
        if (owner2chunk.at(owner_count).size() < (new_count - old_count))
            return false;
        // Do the allocation
        for (size_t i = old_count; i < new_count; i++) {
            size_t chunk = owner2chunk.at(owner_count).back();
            owner2chunk.at(owner_count).pop_back();
            chunk2owner.at(chunk) = owner;
            owner2chunk.at(owner).push_back(chunk);
        }
        dirty = true;
        return true;
    }

    // Release all chunks owned by an owner
    void release(Tkey owner)
    {
        for (auto chunk : owner2chunk.at(owner)) {
            chunk2owner.at(chunk) = no_owner;
            owner2chunk.at(chunk_count).push_back(chunk);
        }
        owner2chunk.at(owner).clear();
    }

    void sync_mapping(cl::CommandQueue &queue)
    {
        if (dirty) {
            chunk2owner.put(queue);
            dirty = false;
        }
    }
};

NEXTPNR_NAMESPACE_END

#endif
#endif

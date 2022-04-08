/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-22  gatecat <gatecat@ds0.me>
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

#ifndef INDEXED_STORE_H
#define INDEXED_STORE_H

#include <algorithm>
#include <limits>
#include <type_traits>
#include <vector>

#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

template <typename T> struct store_index
{
    int32_t m_index = -1;
    store_index() = default;
    explicit store_index(int32_t index) : m_index(index){};
    int32_t idx() const { return m_index; }
    void set(int32_t index) { m_index = index; }
    bool empty() const { return m_index == -1; }
    bool operator==(const store_index<T> &other) const { return m_index == other.m_index; }
    bool operator!=(const store_index<T> &other) const { return m_index != other.m_index; }
    bool operator<(const store_index<T> &other) const { return m_index < other.m_index; }
    unsigned int hash() const { return m_index; }

    operator bool() const { return !empty(); }
    operator int() const = delete;
    bool operator!() const { return empty(); }
};

// "Slotted" indexed object store
template <typename T> class indexed_store
{
  private:
    // This should move to using std::optional at some point
    class slot
    {
      private:
        alignas(T) unsigned char storage[sizeof(T)];
        int32_t next_free;
        bool active;
        inline T &obj() { return reinterpret_cast<T &>(storage); }
        inline const T &obj() const { return reinterpret_cast<const T &>(storage); }
        friend class indexed_store<T>;

      public:
        slot() : next_free(std::numeric_limits<int32_t>::max()), active(false){};
        slot(slot &&other) : next_free(other.next_free), active(other.active)
        {
            if (active)
                ::new (static_cast<void *>(&storage)) T(std::move(other.obj()));
        };

        slot(const slot &other) : next_free(other.next_free), active(other.active)
        {
            if (active)
                ::new (static_cast<void *>(&storage)) T(other.obj());
        };

        template <class... Args> void create(Args &&...args)
        {
            NPNR_ASSERT(!active);
            active = true;
            ::new (static_cast<void *>(&storage)) T(std::forward<Args &&>(args)...);
        }
        bool empty() const { return !active; }
        T &get()
        {
            NPNR_ASSERT(active);
            return reinterpret_cast<T &>(storage);
        }
        const T &get() const
        {
            NPNR_ASSERT(active);
            return reinterpret_cast<const T &>(storage);
        }
        void free(int32_t first_free)
        {
            NPNR_ASSERT(active);
            obj().~T();
            active = false;
            next_free = first_free;
        }
        ~slot()
        {
            if (active)
                obj().~T();
        }
    };

    std::vector<slot> slots;
    int32_t first_free = 0;
    int32_t active_count = 0;

  public:
    // Create a new entry and return its index
    template <class... Args> store_index<T> add(Args &&...args)
    {
        ++active_count;
        if (first_free == int32_t(slots.size())) {
            slots.emplace_back();
            slots.back().create(std::forward<Args &&>(args)...);
            ++first_free;
            return store_index<T>(int32_t(slots.size()) - 1);
        } else {
            int32_t idx = first_free;
            auto &slot = slots.at(idx);
            first_free = slot.next_free;
            slot.create(std::forward<Args &&>(args)...);
            return store_index<T>(idx);
        }
    }

    // Remove an entry at an index
    void remove(store_index<T> idx)
    {
        --active_count;
        slots.at(idx.m_index).free(first_free);
        first_free = idx.m_index;
    }

    void clear()
    {
        active_count = 0;
        first_free = 0;
        slots.clear();
    }

    // Number of live entries
    int32_t entries() const { return active_count; }
    bool empty() const { return (entries() == 0); }

    // Reserve a certain amount of space
    void reserve(int32_t size) { slots.reserve(size); }

    // Check if an index exists
    int32_t count(store_index<T> idx)
    {
        if (idx.m_index < 0 || idx.m_index >= int32_t(slots.size()))
            return 0;
        return slots.at(idx.m_index).empty() ? 0 : 1;
    }

    // Get an item by index
    T &at(store_index<T> idx) { return slots.at(idx.m_index).get(); }
    const T &at(store_index<T> idx) const { return slots.at(idx.m_index).get(); }
    T &operator[](store_index<T> idx) { return slots.at(idx.m_index).get(); }
    const T &operator[](store_index<T> idx) const { return slots.at(idx.m_index).get(); }

    // Total size of the container
    int32_t capacity() const { return int32_t(slots.size()); }

    // Iterate over items
    template <typename It, typename S> class enumerated_iterator;

    class iterator
    {
      private:
        indexed_store *base;
        int32_t index = 0;

      public:
        iterator(indexed_store *base, int32_t index) : base(base), index(index){};
        inline bool operator!=(const iterator &other) const { return other.index != index; }
        inline bool operator==(const iterator &other) const { return other.index == index; }
        inline iterator operator++()
        {
            // skip over unused slots
            do {
                index++;
            } while (index < int32_t(base->slots.size()) && !base->slots.at(index).active);
            return *this;
        }
        inline iterator operator++(int)
        {
            iterator prior(*this);
            do {
                index++;
            } while (index < int32_t(base->slots.size()) && !base->slots.at(index).active);
            return prior;
        }
        T &operator*() { return base->at(store_index<T>(index)); }
        template <typename It, typename S> friend class indexed_store::enumerated_iterator;
    };
    iterator begin()
    {
        auto it = iterator{this, -1};
        ++it;
        return it;
    }
    iterator end() { return iterator{this, int32_t(slots.size())}; }

    class const_iterator
    {
      private:
        const indexed_store *base;
        int32_t index = 0;

      public:
        const_iterator(const indexed_store *base, int32_t index) : base(base), index(index){};
        inline bool operator!=(const const_iterator &other) const { return other.index != index; }
        inline bool operator==(const const_iterator &other) const { return other.index == index; }
        inline const_iterator operator++()
        {
            // skip over unused slots
            do {
                index++;
            } while (index < int32_t(base->slots.size()) && !base->slots.at(index).active);
            return *this;
        }
        inline const_iterator operator++(int)
        {
            iterator prior(*this);
            do {
                index++;
            } while (index < int32_t(base->slots.size()) && !base->slots.at(index).active);
            return prior;
        }
        const T &operator*() { return base->at(store_index<T>(index)); }
        template <typename It, typename S> friend class indexed_store::enumerated_iterator;
    };
    const_iterator begin() const
    {
        auto it = const_iterator{this, -1};
        ++it;
        return it;
    }
    const_iterator end() const { return const_iterator{this, int32_t(slots.size())}; }

    template <typename S> struct enumerated_item
    {
        enumerated_item(int32_t index, T &value) : index(index), value(value){};
        store_index<std::remove_cv_t<S>> index;
        S &value;
    };

    template <typename It, typename S> class enumerated_iterator
    {
      private:
        It base;

      public:
        enumerated_iterator(const It &base) : base(base){};
        inline bool operator!=(const enumerated_iterator<It, S> &other) const { return other.base != base; }
        inline bool operator==(const enumerated_iterator<It, S> &other) const { return other.base == base; }
        inline enumerated_iterator<It, S> operator++()
        {
            ++base;
            return *this;
        }
        inline enumerated_iterator<It, S> operator++(int)
        {
            iterator prior(*this);
            ++base;
            return prior;
        }
        enumerated_item<S> operator*() { return enumerated_item<S>{base.index, *base}; }
    };

    template <typename It, typename S> struct enumerated_range
    {
        enumerated_range(const It &begin, const It &end) : m_begin(begin), m_end(end){};
        enumerated_iterator<It, S> m_begin, m_end;
        enumerated_iterator<It, S> begin() { return m_begin; }
        enumerated_iterator<It, S> end() { return m_end; }
    };

    enumerated_range<iterator, T> enumerate() { return enumerated_range<iterator, T>{begin(), end()}; }
    enumerated_range<const_iterator, const T> enumerate() const
    {
        return enumerated_range<iterator, T>{begin(), end()};
    }
};

NEXTPNR_NAMESPACE_END

#endif

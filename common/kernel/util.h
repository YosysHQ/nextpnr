/*
 *  nextpnr -- Next Generation Place and Route
 *
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

#ifndef UTIL_H
#define UTIL_H

#include <map>
#include <set>
#include <string>
#include "nextpnr.h"

#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

// Get a value from a map-style container, returning default if value is not
// found
template <typename Container, typename KeyType, typename ValueType>
ValueType get_or_default(const Container &ct, const KeyType &key, ValueType def = ValueType())
{
    auto found = ct.find(key);
    if (found == ct.end())
        return def;
    else
        return found->second;
};

// Get a value from a map-style container, returning default if value is not
// found (forces string)
template <typename Container, typename KeyType>
std::string str_or_default(const Container &ct, const KeyType &key, std::string def = "")
{
    auto found = ct.find(key);
    if (found == ct.end())
        return def;
    else {
        return found->second;
    }
};

template <typename KeyType>
std::string str_or_default(const dict<KeyType, Property> &ct, const KeyType &key, std::string def = "")
{
    auto found = ct.find(key);
    if (found == ct.end())
        return def;
    else {
        if (!found->second.is_string)
            log_error("Expecting string value but got integer %d.\n", int(found->second.intval));
        return found->second.as_string();
    }
};

// Get a value from a map-style container, converting to int, and returning
// default if value is not found
template <typename Container, typename KeyType> int int_or_default(const Container &ct, const KeyType &key, int def = 0)
{
    auto found = ct.find(key);
    if (found == ct.end())
        return def;
    else
        return std::stoi(found->second);
};

template <typename KeyType> int int_or_default(const dict<KeyType, Property> &ct, const KeyType &key, int def = 0)
{
    auto found = ct.find(key);
    if (found == ct.end())
        return def;
    else {
        if (found->second.is_string) {
            try {
                return std::stoi(found->second.as_string());
            } catch (std::invalid_argument &e) {
                log_error("Expecting numeric value but got '%s'.\n", found->second.as_string().c_str());
            }
        } else
            return found->second.as_int64();
    }
};

// As above, but convert to bool
template <typename Container, typename KeyType>
bool bool_or_default(const Container &ct, const KeyType &key, bool def = false)
{
    return bool(int_or_default(ct, key, int(def)));
};

// Get only value from a forward iterator begin/end pair.
//
// Generates assertion failure if std::distance(begin, end) != 1.
template <typename ForwardIterator>
inline const typename ForwardIterator::reference get_only_value(ForwardIterator begin, ForwardIterator end)
{
    NPNR_ASSERT(begin != end);
    const typename ForwardIterator::reference ret = *begin;
    ++begin;
    NPNR_ASSERT(begin == end);
    return ret;
}

// Get only value from a forward iterator range pair.
//
// Generates assertion failure if std::distance(r.begin(), r.end()) != 1.
template <typename ForwardRange> inline auto get_only_value(ForwardRange r)
{
    auto b = r.begin();
    auto e = r.end();
    return get_only_value(b, e);
}

// From Yosys
// https://github.com/YosysHQ/yosys/blob/0fb4224ebca86156a1296b9210116d9a9cbebeed/kernel/utils.h#L131
template <typename T, typename C = std::less<T>> struct TopoSort
{
    bool analyze_loops, found_loops;
    std::map<T, std::set<T, C>, C> database;
    std::set<std::set<T, C>> loops;
    std::vector<T> sorted;

    TopoSort()
    {
        analyze_loops = true;
        found_loops = false;
    }

    void node(T n)
    {
        if (database.count(n) == 0)
            database[n] = std::set<T, C>();
    }

    void edge(T left, T right)
    {
        node(left);
        database[right].insert(left);
    }

    void sort_worker(const T &n, std::set<T, C> &marked_cells, std::set<T, C> &active_cells,
                     std::vector<T> &active_stack)
    {
        if (active_cells.count(n)) {
            found_loops = true;
            if (analyze_loops) {
                std::set<T, C> loop;
                for (int i = int(active_stack.size()) - 1; i >= 0; i--) {
                    loop.insert(active_stack[i]);
                    if (active_stack[i] == n)
                        break;
                }
                loops.insert(loop);
            }
            return;
        }

        if (marked_cells.count(n))
            return;

        if (!database.at(n).empty()) {
            if (analyze_loops)
                active_stack.push_back(n);
            active_cells.insert(n);

            for (auto &left_n : database.at(n))
                sort_worker(left_n, marked_cells, active_cells, active_stack);

            if (analyze_loops)
                active_stack.pop_back();
            active_cells.erase(n);
        }

        marked_cells.insert(n);
        sorted.push_back(n);
    }

    bool sort()
    {
        loops.clear();
        sorted.clear();
        found_loops = false;

        std::set<T, C> marked_cells;
        std::set<T, C> active_cells;
        std::vector<T> active_stack;

        for (auto &it : database)
            sort_worker(it.first, marked_cells, active_cells, active_stack);

        NPNR_ASSERT(sorted.size() == database.size());
        return !found_loops;
    }
};

template <typename T> struct reversed_range_t
{
    T &obj;
    explicit reversed_range_t(T &obj) : obj(obj){};
    auto begin() { return obj.rbegin(); }
    auto end() { return obj.rend(); }
};

template <typename T> reversed_range_t<T> reversed_range(T &obj) { return reversed_range_t<T>(obj); }

NEXTPNR_NAMESPACE_END

#endif

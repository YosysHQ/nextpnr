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
#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include "nextpnr.h"
NEXTPNR_NAMESPACE_BEGIN

// A simple framework for performance counters (to be extended...)

struct TimeCounter
{
    TimeCounter() : name(""), total(0.0f){};
    explicit TimeCounter(const std::string &name) : name(name), total(0.0f){};
    std::string name;
    std::chrono::duration<float> total;
    float seconds() { return total.count(); }
    void log() { log_info("%s time: %.02fs\n", name.c_str(), seconds()); }
};

struct ScopedTimer
{
    using clk = std::chrono::high_resolution_clock;
    TimeCounter &ctr;
    clk::time_point start_time;
    ScopedTimer(TimeCounter &ctr) : ctr(ctr), start_time(clk::now()) {}
    ~ScopedTimer()
    {
        auto end_time = clk::now();
        ctr.total += std::chrono::duration<float>(end_time - start_time);
    }
};

NEXTPNR_NAMESPACE_END

#endif

/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#ifndef NEXTPNR_H
#error Include "archdefs.h" via "nextpnr.h" only.
#endif

NEXTPNR_NAMESPACE_BEGIN

typedef float delay_t;

struct DelayInfo
{
    delay_t delay = 0;

    delay_t minRaiseDelay() const { return delay; }
    delay_t maxRaiseDelay() const { return delay; }

    delay_t minFallDelay() const { return delay; }
    delay_t maxFallDelay() const { return delay; }

    delay_t minDelay() const { return delay; }
    delay_t maxDelay() const { return delay; }

    DelayInfo operator+(const DelayInfo &other) const
    {
        DelayInfo ret;
        ret.delay = this->delay + other.delay;
        return ret;
    }
};

typedef IdString BelId;
typedef IdString WireId;
typedef IdString PipId;
typedef IdString GroupId;
typedef IdString DecalId;

struct ArchNetInfo
{
};
struct ArchCellInfo
{
};

NEXTPNR_NAMESPACE_END

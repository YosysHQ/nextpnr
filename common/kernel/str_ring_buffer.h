/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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
#ifndef STR_RING_BUFFER_H
#define STR_RING_BUFFER_H

#include <array>
#include <string>

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

// A ring buffer of strings, so we can return a simple const char * pointer for %s formatting - inspired by how logging
// in Yosys works Let's just hope noone tries to log more than 100 things in one call....
class StrRingBuffer
{
  private:
    static const size_t N = 100;
    std::array<std::string, N> buffer;
    size_t index = 0;

  public:
    std::string &next();
};

NEXTPNR_NAMESPACE_END

#endif /* STR_RING_BUFFER_H */

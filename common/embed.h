/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  whitequark <whitequark@whitequark.org>
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

#ifndef EMBED_H
#define EMBED_H

#include "nextpnr.h"
NEXTPNR_NAMESPACE_BEGIN

#if !defined(EXTERNAL_CHIPDB_ROOT) && !defined(WIN32)

struct EmbeddedFile
{
    static EmbeddedFile *head;

    std::string filename;
    const void *content;
    EmbeddedFile *next = nullptr;

    EmbeddedFile(const std::string &filename, const void *content) : filename(filename), content(content)
    {
        next = head;
        head = this;
    }
};

#endif

const void *get_chipdb(const std::string &filename);

NEXTPNR_NAMESPACE_END

#endif // EMBED_H

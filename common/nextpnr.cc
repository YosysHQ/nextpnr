/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

std::unordered_map<std::string, int> *IdString::database_str_to_idx = nullptr;
std::vector<const std::string *> *IdString::database_idx_to_str = nullptr;

void IdString::initialize()
{
    database_str_to_idx = new std::unordered_map<std::string, int>;
    database_idx_to_str = new std::vector<const std::string *>;
    initialize_add("", 0);
    initialize_chip();
}

void IdString::initialize_add(const char *s, int idx)
{
    assert(database_str_to_idx->count(s) == 0);
    assert(int(database_idx_to_str->size()) == idx);
    auto insert_rc = database_str_to_idx->insert({s, idx});
    database_idx_to_str->push_back(&insert_rc.first->first);
}

NEXTPNR_NAMESPACE_END

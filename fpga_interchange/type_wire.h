/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#ifndef TYPE_WIRE_H
#define TYPE_WIRE_H

#include <algorithm>
#include <vector>

#include "hashlib.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

#include "lookahead.capnp.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context;

struct TypeWireId
{
    TypeWireId() : type(-1), index(-1) {}
    TypeWireId(const Context *ctx, WireId wire_inst);

    explicit TypeWireId(lookahead_storage::TypeWireId::Reader reader);
    void to_builder(lookahead_storage::TypeWireId::Builder builder) const;

    bool operator==(const TypeWireId &other) const { return type == other.type && index == other.index; }
    bool operator!=(const TypeWireId &other) const { return type != other.type || index != other.index; }
    bool operator<(const TypeWireId &other) const
    {
        return type < other.type || (type == other.type && index < other.index);
    }

    unsigned int hash() const { return mkhash(type, index); }

    int32_t type;
    int32_t index;
};

struct TypeWirePair
{
    TypeWireId src;
    TypeWireId dst;

    TypeWirePair() = default;
    explicit TypeWirePair(lookahead_storage::TypeWirePair::Reader reader);
    void to_builder(lookahead_storage::TypeWirePair::Builder builder) const;

    bool operator==(const TypeWirePair &other) const { return src == other.src && dst == other.dst; }
    bool operator!=(const TypeWirePair &other) const { return src != other.src || dst != other.dst; }

    unsigned int hash() const { return mkhash(src.hash(), dst.hash()); }
};

struct TypeWireSet
{
  public:
    TypeWireSet(const Context *ctx, WireId wire);
    unsigned int hash() const { return hash_; }

    bool operator==(const TypeWireSet &other) const { return wire_types_ == other.wire_types_; }
    bool operator!=(const TypeWireSet &other) const { return wire_types_ != other.wire_types_; }

  private:
    unsigned int hash_;
    std::vector<TypeWireId> wire_types_;
};

NEXTPNR_NAMESPACE_END

#endif /* TYPE_WIRE_H */

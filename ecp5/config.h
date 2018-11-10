/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#ifndef ECP5_CONFIG_H
#define ECP5_CONFIG_H

#include <map>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// This represents configuration at "FASM" level, in terms of routing arcs and non-routing configuration settings -
// either words or enums.

// A connection in a tile
struct ConfigArc
{
    std::string sink;
    std::string source;
    inline bool operator==(const ConfigArc &other) const { return other.source == source && other.sink == sink; }
};

std::ostream &operator<<(std::ostream &out, const ConfigArc &arc);

std::istream &operator>>(std::istream &in, ConfigArc &arc);

// A configuration setting in a tile that takes one or more bits (such as LUT init)
struct ConfigWord
{
    std::string name;
    std::vector<bool> value;
    inline bool operator==(const ConfigWord &other) const { return other.name == name && other.value == value; }
};

std::ostream &operator<<(std::ostream &out, const ConfigWord &cw);

std::istream &operator>>(std::istream &in, ConfigWord &cw);

// A configuration setting in a tile that takes an enumeration value (such as IO type)
struct ConfigEnum
{
    std::string name;
    std::string value;
    inline bool operator==(const ConfigEnum &other) const { return other.name == name && other.value == value; }
};

std::ostream &operator<<(std::ostream &out, const ConfigEnum &ce);

std::istream &operator>>(std::istream &in, ConfigEnum &ce);

// An unknown bit, specified by position only
struct ConfigUnknown
{
    int frame, bit;
    inline bool operator==(const ConfigUnknown &other) const { return other.frame == frame && other.bit == bit; }
};

std::ostream &operator<<(std::ostream &out, const ConfigUnknown &tc);

std::istream &operator>>(std::istream &in, ConfigUnknown &ce);

struct TileConfig
{
    std::vector<ConfigArc> carcs;
    std::vector<ConfigWord> cwords;
    std::vector<ConfigEnum> cenums;
    std::vector<ConfigUnknown> cunknowns;
    int total_known_bits = 0;

    void add_arc(const std::string &sink, const std::string &source);
    void add_word(const std::string &name, const std::vector<bool> &value);
    void add_enum(const std::string &name, const std::string &value);
    void add_unknown(int frame, int bit);

    std::string to_string() const;
    static TileConfig from_string(const std::string &str);

    bool empty() const;
};

std::ostream &operator<<(std::ostream &out, const TileConfig &tc);

std::istream &operator>>(std::istream &in, TileConfig &ce);

// A group of tiles to configure at once for a particular feature that is split across tiles
// TileGroups are currently for non-routing configuration only
struct TileGroup
{
    std::vector<std::string> tiles;
    TileConfig config;
};

// This represents the configuration of a chip at a high level
class ChipConfig
{
  public:
    std::string chip_name;
    std::vector<std::string> metadata;
    std::map<std::string, TileConfig> tiles;
    std::vector<TileGroup> tilegroups;
    std::map<uint16_t, std::vector<uint16_t>> bram_data;
};

std::ostream &operator<<(std::ostream &out, const ChipConfig &cc);

std::istream &operator>>(std::istream &in, ChipConfig &cc);

NEXTPNR_NAMESPACE_END

#endif

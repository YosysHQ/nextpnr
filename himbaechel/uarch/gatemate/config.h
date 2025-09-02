/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#ifndef GATEMATE_CONFIG_H
#define GATEMATE_CONFIG_H

#include <map>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

struct ConfigWord
{
    std::string name;
    std::vector<bool> value;
    inline bool operator==(const ConfigWord &other) const { return other.name == name && other.value == value; }
};

std::ostream &operator<<(std::ostream &out, const ConfigWord &cw);

struct TileConfig
{
    std::vector<ConfigWord> cwords;
    std::map<std::string, std::vector<bool>> added;

    void add_word(const std::string &name, const std::vector<bool> &value, const char *c=nullptr);

    std::string to_string() const;

    bool empty() const;
};

std::ostream &operator<<(std::ostream &out, const TileConfig &tc);

struct CfgLoc
{
    int die;
    int x;
    int y;

    inline bool operator==(const CfgLoc &other) const { return other.die == die && other.x == x && other.y == y; }
    inline bool operator!=(const CfgLoc &other) const { return other.die != die || x != other.x || y == other.y; }

    inline bool operator<(const CfgLoc &other) const
    {
        return die < other.die ||
               ((die == other.die && y < other.y) || (die == other.die && y == other.y && x < other.x));
    }
};

class ChipConfig
{
  public:
    std::string chip_name;
    std::string chip_package;
    std::map<CfgLoc, TileConfig> tiles;
    std::map<CfgLoc, TileConfig> brams;
    std::map<int, TileConfig> serdes;
    std::map<int, TileConfig> configs;

    // Block RAM initialisation
    std::map<CfgLoc, std::vector<uint8_t>> bram_data;
};

std::ostream &operator<<(std::ostream &out, const ChipConfig &cc);

NEXTPNR_NAMESPACE_END

#endif

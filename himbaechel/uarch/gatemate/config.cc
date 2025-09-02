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

#include "config.h"
#include <boost/range/adaptor/reversed.hpp>
#include <iomanip>
#include <set>
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

#define fmt(x) (static_cast<const std::ostringstream &>(std::ostringstream() << x).str())

inline std::string to_string(const std::vector<bool> &bv)
{
    std::ostringstream os;
    for (auto bit : boost::adaptors::reverse(bv))
        os << (bit ? '1' : '0');
    return os.str();
}

std::ostream &operator<<(std::ostream &out, const ConfigWord &cw)
{
    out << cw.name << " " << to_string(cw.value) << std::endl;
    return out;
}

std::ostream &operator<<(std::ostream &out, const TileConfig &tc)
{
    auto sorted = tc.cwords;
    std::sort(sorted.begin(), sorted.end(),
              [](const ConfigWord &a, const ConfigWord &b) {
                  return a.name < b.name;
              });
    for (const auto &cword : sorted)
        out << cword;
    return out;
}

void TileConfig::add_word(const std::string &name, const std::vector<bool> &value, const char *c)
{
    if (!added.count(name)) {
        cwords.push_back({name, value});
        added.emplace(name, value);
    } else {
        auto val = added.at(name);
        if (val != value)
            log_warning("Trying to add value to already assigned word %s for %s\n", name.c_str(), c ? c : "");
    }
}

std::string TileConfig::to_string() const
{
    std::stringstream ss;
    ss << *this;
    return ss.str();
}

bool TileConfig::empty() const { return cwords.empty(); }

std::ostream &operator<<(std::ostream &out, const ChipConfig &cc)
{
    out << ".device " << cc.chip_name << std::endl << std::endl;
    for (const auto &config : cc.configs) {
        if (!config.second.empty()) {
            out << ".config " << config.first << " " << std::endl;
            out << config.second;
            out << std::endl;
        }
    }
    for (const auto &ser : cc.serdes) {
        if (!ser.second.empty()) {
            out << ".serdes " << ser.first << " " << std::endl;
            out << ser.second;
            out << std::endl;
        }
    }
    for (const auto &tile : cc.tiles) {
        if (!tile.second.empty()) {
            out << ".tile " << tile.first.die << " " << tile.first.x << " " << tile.first.y << std::endl;
            out << tile.second;
            out << std::endl;
        }
    }
    for (const auto &bram : cc.brams) {
        if (!bram.second.empty()) {
            out << ".bram " << bram.first.die << " " << bram.first.x << " " << bram.first.y << std::endl;
            out << bram.second;
            out << std::endl;
        }
    }
    for (const auto &bram : cc.bram_data) {
        if (!bram.second.empty()) {
            out << ".bram_init " << bram.first.die << " " << bram.first.x << " " << bram.first.y << std::endl;
            std::ios_base::fmtflags f(out.flags());
            for (size_t i = 0; i < bram.second.size(); i++) {
                out << std::setw(2) << std::setfill('0') << std::hex << (int)bram.second.at(i);
                if (i % 40 == 39)
                    out << std::endl;
                else
                    out << " ";
            }
            out.flags(f);
            out << std::endl;
        }
    }
    return out;
}

NEXTPNR_NAMESPACE_END

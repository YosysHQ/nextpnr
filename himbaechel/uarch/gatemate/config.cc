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

inline std::istream &operator>>(std::istream &in, std::vector<bool> &bv)
{
    bv.clear();
    std::string s;
    in >> s;
    for (auto c : boost::adaptors::reverse(s)) {
        assert((c == '0') || (c == '1'));
        bv.push_back((c == '1'));
    }
    return in;
}

// Skip whitespace, optionally including newlines
inline void skip_blank(std::istream &in, bool nl = false)
{
    int c = in.peek();
    while (in && (((c == ' ') || (c == '\t')) || (nl && ((c == '\n') || (c == '\r'))))) {
        in.get();
        c = in.peek();
    }
}
// Return true if end of line (or file)
inline bool skip_check_eol(std::istream &in)
{
    skip_blank(in, false);
    if (!in)
        return false;
    int c = in.peek();
    // Comments count as end of line
    if (c == '#') {
        in.get();
        c = in.peek();
        while (in && c != EOF && c != '\n') {
            in.get();
            c = in.peek();
        }
        return true;
    }
    return (c == EOF || c == '\n');
}

// Skip past blank lines and comments
inline void skip(std::istream &in)
{
    skip_blank(in, true);
    while (in && (in.peek() == '#')) {
        // Skip comment line
        skip_check_eol(in);
        skip_blank(in, true);
    }
}

// Return true if at the end of a record (or file)
inline bool skip_check_eor(std::istream &in)
{
    skip(in);
    int c = in.peek();
    return (c == EOF || c == '.');
}

// Return true if at the end of file
inline bool skip_check_eof(std::istream &in)
{
    skip(in);
    int c = in.peek();
    return (c == EOF);
}

std::ostream &operator<<(std::ostream &out, const ConfigWord &cw)
{
    out << cw.name << " " << to_string(cw.value) << std::endl;
    return out;
}

std::istream &operator>>(std::istream &in, ConfigWord &cw)
{
    in >> cw.name;
    in >> cw.value;
    return in;
}

std::ostream &operator<<(std::ostream &out, const TileConfig &tc)
{
    for (const auto &cword : tc.cwords)
        out << cword;
    return out;
}

std::istream &operator>>(std::istream &in, TileConfig &tc)
{
    tc.cwords.clear();
    while (!skip_check_eor(in)) {
        ConfigWord w;
        in >> w;
        tc.cwords.push_back(w);
    }
    return in;
}

void TileConfig::add_word(const std::string &name, const std::vector<bool> &value) { cwords.push_back({name, value}); }

std::string TileConfig::to_string() const
{
    std::stringstream ss;
    ss << *this;
    return ss.str();
}

TileConfig TileConfig::from_string(const std::string &str)
{
    std::stringstream ss(str);
    TileConfig tc;
    ss >> tc;
    return tc;
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
                if (i % 32 == 31)
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

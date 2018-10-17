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

#include "config.h"
#include <boost/range/adaptor/reversed.hpp>
#include <iomanip>
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

struct ConfigBit
{
    int frame;
    int bit;
    bool inv;
};

static ConfigBit cbit_from_str(const std::string &s)
{
    size_t idx = 0;
    ConfigBit b;
    if (s[idx] == '!') {
        b.inv = true;
        ++idx;
    } else {
        b.inv = false;
    }
    NPNR_ASSERT(s[idx] == 'F');
    ++idx;
    size_t b_pos = s.find('B');
    NPNR_ASSERT(b_pos != std::string::npos);
    b.frame = stoi(s.substr(idx, b_pos - idx));
    b.bit = stoi(s.substr(b_pos + 1));
    return b;
}

inline std::string to_string(ConfigBit b)
{
    std::ostringstream ss;
    if (b.inv)
        ss << "!";
    ss << "F" << b.frame;
    ss << "B" << b.bit;
    return ss.str();
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

std::ostream &operator<<(std::ostream &out, const ConfigArc &arc)
{
    out << "arc: " << arc.sink << " " << arc.source << std::endl;
    return out;
}

std::istream &operator>>(std::istream &in, ConfigArc &arc)
{
    in >> arc.sink;
    in >> arc.source;
    return in;
}

std::ostream &operator<<(std::ostream &out, const ConfigWord &cw)
{
    out << "word: " << cw.name << " " << to_string(cw.value) << std::endl;
    return out;
}

std::istream &operator>>(std::istream &in, ConfigWord &cw)
{
    in >> cw.name;
    in >> cw.value;
    return in;
}

std::ostream &operator<<(std::ostream &out, const ConfigEnum &cw)
{
    out << "enum: " << cw.name << " " << cw.value << std::endl;
    return out;
}

std::istream &operator>>(std::istream &in, ConfigEnum &ce)
{
    in >> ce.name;
    in >> ce.value;
    return in;
}

std::ostream &operator<<(std::ostream &out, const ConfigUnknown &cu)
{
    out << "unknown: " << to_string(ConfigBit{cu.frame, cu.bit, false}) << std::endl;
    return out;
}

std::istream &operator>>(std::istream &in, ConfigUnknown &cu)
{
    std::string s;
    in >> s;
    ConfigBit c = cbit_from_str(s);
    cu.frame = c.frame;
    cu.bit = c.bit;
    assert(!c.inv);
    return in;
}

std::ostream &operator<<(std::ostream &out, const TileConfig &tc)
{
    for (const auto &arc : tc.carcs)
        out << arc;
    for (const auto &cword : tc.cwords)
        out << cword;
    for (const auto &cenum : tc.cenums)
        out << cenum;
    for (const auto &cunk : tc.cunknowns)
        out << cunk;
    return out;
}

std::istream &operator>>(std::istream &in, TileConfig &tc)
{
    tc.carcs.clear();
    tc.cwords.clear();
    tc.cenums.clear();
    while (!skip_check_eor(in)) {
        std::string type;
        in >> type;
        if (type == "arc:") {
            ConfigArc a;
            in >> a;
            tc.carcs.push_back(a);
        } else if (type == "word:") {
            ConfigWord w;
            in >> w;
            tc.cwords.push_back(w);
        } else if (type == "enum:") {
            ConfigEnum e;
            in >> e;
            tc.cenums.push_back(e);
        } else if (type == "unknown:") {
            ConfigUnknown u;
            in >> u;
            tc.cunknowns.push_back(u);
        } else {
            NPNR_ASSERT_FALSE_STR("unexpected token " + type + " while reading config text");
        }
    }
    return in;
}

void TileConfig::add_arc(const std::string &sink, const std::string &source) { carcs.push_back({sink, source}); }

void TileConfig::add_word(const std::string &name, const std::vector<bool> &value) { cwords.push_back({name, value}); }

void TileConfig::add_enum(const std::string &name, const std::string &value) { cenums.push_back({name, value}); }

void TileConfig::add_unknown(int frame, int bit) { cunknowns.push_back({frame, bit}); }

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

bool TileConfig::empty() const { return carcs.empty() && cwords.empty() && cenums.empty() && cunknowns.empty(); }

std::ostream &operator<<(std::ostream &out, const ChipConfig &cc)
{
    out << ".device " << cc.chip_name << std::endl << std::endl;
    for (const auto &meta : cc.metadata)
        out << ".comment " << meta << std::endl;
    out << std::endl;
    for (const auto &tile : cc.tiles) {
        if (!tile.second.empty()) {
            out << ".tile " << tile.first << std::endl;
            out << tile.second;
            out << std::endl;
        }
    }
    for (const auto &bram : cc.bram_data) {
        out << ".bram_init " << bram.first << std::endl;
        std::ios_base::fmtflags f(out.flags());
        for (size_t i = 0; i < bram.second.size(); i++) {
            out << std::setw(3) << std::setfill('0') << std::hex << bram.second.at(i);
            if (i % 8 == 7)
                out << std::endl;
            else
                out << " ";
        }
        out.flags(f);
        out << std::endl;
    }
    for (const auto &tg : cc.tilegroups) {
        out << ".tile_group";
        for (const auto &tile : tg.tiles) {
            out << " " << tile;
        }
        out << std::endl;
        out << tg.config;
        out << std::endl;
    }
    return out;
}

std::istream &operator>>(std::istream &in, ChipConfig &cc)
{
    while (!skip_check_eof(in)) {
        std::string verb;
        in >> verb;
        if (verb == ".device") {
            in >> cc.chip_name;
        } else if (verb == ".comment") {
            std::string line;
            getline(in, line);
            cc.metadata.push_back(line);
        } else if (verb == ".tile") {
            std::string tilename;
            in >> tilename;
            TileConfig tc;
            in >> tc;
            cc.tiles[tilename] = tc;
        } else if (verb == ".tile_group") {
            TileGroup tg;
            std::string line;
            getline(in, line);
            std::stringstream ss2(line);

            std::string tile;
            while (ss2) {
                ss2 >> tile;
                tg.tiles.push_back(tile);
            }
            in >> tg.config;
            cc.tilegroups.push_back(tg);
        } else if (verb == ".bram_init") {
            uint16_t bram;
            in >> bram;
            std::ios_base::fmtflags f(in.flags());
            while (!skip_check_eor(in)) {
                uint16_t value;
                in >> std::hex >> value;
                cc.bram_data[bram].push_back(value);
            }
            in.flags(f);
        } else {
            log_error("unrecognised config entry %s\n", verb.c_str());
        }
    }
    return in;
}

NEXTPNR_NAMESPACE_END

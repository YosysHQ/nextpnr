/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
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

#ifndef PROJECT_H
#define PROJECT_H

#include <boost/filesystem/convenience.hpp>
#include <boost/property_tree/ptree.hpp>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

namespace pt = boost::property_tree;

struct ProjectHandler
{
    void save(Context *ctx, std::string filename);
    std::unique_ptr<Context> load(std::string filename);
    // implemented per arch
    void saveArch(Context *ctx, pt::ptree &root, std::string path);
    std::unique_ptr<Context> createContext(pt::ptree &root);
    void loadArch(Context *ctx, pt::ptree &root, std::string path);
};

boost::filesystem::path make_relative(boost::filesystem::path child, boost::filesystem::path parent);

NEXTPNR_NAMESPACE_END

#endif // PROJECT_H

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

#include "project.h"
#include <boost/filesystem/convenience.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

void ProjectHandler::saveArch(Context *ctx, pt::ptree &root, std::string path)
{
    root.put("project.arch.package", ctx->archArgs().package);
    root.put("project.arch.speed", ctx->archArgs().speed);
}

std::unique_ptr<Context> ProjectHandler::createContext(pt::ptree &root)
{
    ArchArgs chipArgs;
    std::string arch_type = root.get<std::string>("project.arch.type");
    if (arch_type == "25k") {
        chipArgs.type = ArchArgs::LFE5U_25F;
    }
    if (arch_type == "45k") {
        chipArgs.type = ArchArgs::LFE5U_45F;
    }
    if (arch_type == "85k") {
        chipArgs.type = ArchArgs::LFE5U_85F;
    }
    chipArgs.package = root.get<std::string>("project.arch.package");
    chipArgs.speed = ArchArgs::SpeedGrade(root.get<int>("project.arch.speed"));

    return std::unique_ptr<Context>(new Context(chipArgs));
}

void ProjectHandler::loadArch(Context *ctx, pt::ptree &root, std::string path) {}

NEXTPNR_NAMESPACE_END

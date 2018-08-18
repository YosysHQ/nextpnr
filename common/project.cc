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
#include <algorithm>
#include <boost/filesystem/convenience.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include "jsonparse.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

boost::filesystem::path make_relative(boost::filesystem::path child, boost::filesystem::path parent)
{
    boost::filesystem::path::const_iterator parentIter = parent.begin();
    boost::filesystem::path::const_iterator childIter = child.begin();

    while (parentIter != parent.end() && childIter != child.end() && (*childIter) == (*parentIter)) {
        ++childIter;
        ++parentIter;
    }

    boost::filesystem::path finalPath;
    while (parentIter != parent.end()) {
        finalPath /= "..";
        ++parentIter;
    }

    while (childIter != child.end()) {
        finalPath /= *childIter;
        ++childIter;
    }

    return finalPath;
}

void ProjectHandler::save(Context *ctx, std::string filename)
{
    try {
        boost::filesystem::path proj(filename);
        std::ofstream f(filename);
        pt::ptree root;

        log_info("Saving project %s...\n", filename.c_str());
        log_break();

        root.put("project.version", 1);
        root.put("project.name", boost::filesystem::basename(filename));
        root.put("project.arch.name", ctx->archId().c_str(ctx));
        root.put("project.arch.type", ctx->archArgsToId(ctx->archArgs()).c_str(ctx));
        std::string fn = ctx->settings[ctx->id("input/json")];
        root.put("project.input.json", make_relative(fn, proj.parent_path()).string());
        root.put("project.params.freq", int(ctx->target_freq / 1e6));
        root.put("project.params.seed", ctx->rngstate);
        saveArch(ctx, root, proj.parent_path().string());
        for (auto const &item : ctx->settings) {
            std::string path = "project.settings.";
            path += item.first.c_str(ctx);
            std::replace(path.begin(), path.end(), '/', '.');
            root.put(path, item.second);
        }
        pt::write_json(f, root);
    } catch (...) {
        log_error("Error saving project file.\n");
    }
}

void addSettings(Context *ctx, std::string path, pt::ptree sub)
{
    for (pt::ptree::value_type &v : sub) {
        const std::string &key = v.first;
        const boost::property_tree::ptree &subtree = v.second;
        if (subtree.empty()) {
            ctx->settings.emplace(ctx->id(path + key), subtree.get_value<std::string>().c_str());
        } else {
            addSettings(ctx, path + key + "/", subtree);
        }
    }
}

std::unique_ptr<Context> ProjectHandler::load(std::string filename)
{
    std::unique_ptr<Context> ctx;
    try {
        pt::ptree root;
        boost::filesystem::path proj(filename);
        pt::read_json(filename, root);
        log_info("Loading project %s...\n", filename.c_str());
        log_break();

        int version = root.get<int>("project.version");
        if (version != 1)
            log_error("Wrong project format version.\n");

        ctx = createContext(root);

        std::string arch_name = root.get<std::string>("project.arch.name");
        if (arch_name != ctx->archId().c_str(ctx.get()))
            log_error("Unsuported project architecture.\n");

        auto project = root.get_child("project");
        auto input = project.get_child("input");
        std::string fn = input.get<std::string>("json");
        boost::filesystem::path json = proj.parent_path() / fn;
        std::ifstream f(json.string());
        if (!parse_json_file(f, fn, ctx.get()))
            log_error("Loading design failed.\n");

        if (project.count("params")) {
            auto params = project.get_child("params");
            if (params.count("freq"))
                ctx->target_freq = params.get<double>("freq") * 1e6;
            if (params.count("seed"))
                ctx->rngseed(params.get<uint64_t>("seed"));
        }
        if (project.count("settings")) {
            addSettings(ctx.get(), "", project.get_child("settings"));
        }

        loadArch(ctx.get(), root, proj.parent_path().string());
    } catch (...) {
        log_error("Error loading project file.\n");
    }
    return ctx;
}

NEXTPNR_NAMESPACE_END

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
#include "jsonparse.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

void ProjectHandler::save(Context *ctx, std::string filename)
{
    std::ofstream f(filename);
    pt::ptree root;
    root.put("project.version", 1);
    root.put("project.name", boost::filesystem::basename(filename));
    root.put("project.arch.name", ctx->archId().c_str(ctx));
    root.put("project.arch.type", ctx->archArgsToId(ctx->archArgs()).c_str(ctx));
    /*  root.put("project.input.json", );*/
    root.put("project.params.freq", int(ctx->target_freq / 1e6));
    root.put("project.params.seed", ctx->rngstate);
    saveArch(ctx, root);
    pt::write_json(f, root);
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
        std::string filename = input.get<std::string>("json");
        boost::filesystem::path json = proj.parent_path() / filename;
        std::ifstream f(json.string());
        if (!parse_json_file(f, filename, ctx.get()))
            log_error("Loading design failed.\n");

        if (project.count("params")) {
            auto params = project.get_child("params");
            if (params.count("freq"))
                ctx->target_freq = params.get<double>("freq") * 1e6;
            if (params.count("seed"))
                ctx->rngseed(params.get<uint64_t>("seed"));
        }
        loadArch(ctx.get(), root, proj.parent_path().string());
    } catch (...) {
        log_error("Error loading project file.\n");
    }
    return ctx;
}

NEXTPNR_NAMESPACE_END

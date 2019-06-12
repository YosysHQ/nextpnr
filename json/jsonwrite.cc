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

#include "jsonwrite.h"
#include <assert.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <log.h>
#include <map>
#include <string>
#include "nextpnr.h"
#include "version.h"

NEXTPNR_NAMESPACE_BEGIN

namespace JsonWriter {

std::string get_string(std::string str)
{
    std::string newstr = "\"";
    for (char c : str) {
        if (c == '\\')
            newstr += c;
        newstr += c;
    }
    return newstr + "\"";
}

std::string get_name(IdString name, Context *ctx)
{
    return get_string(name.c_str(ctx));
}

void write_parameters(std::ostream &f, Context *ctx, const std::unordered_map<IdString, Property> &parameters, bool for_module=false)
{
    bool first = true;
    for (auto &param : parameters) {
        f << stringf("%s\n", first ? "" : ",");
        f << stringf("        %s%s: ", for_module ? "" : "    ", get_name(param.first,ctx).c_str());
        if (param.second.isString())
            f << get_string(param.second);
        else
            f << param.second.num;
        first = false;
    }
}

void write_module(std::ostream &f, Context *ctx)
{
    auto val = ctx->attrs.find(ctx->id("module"));
    if (val != ctx->attrs.end())    
        f << stringf("    %s: {\n", get_string(val->second.str).c_str());
    else
        f << stringf("    %s: {\n", get_string("top").c_str());
    f << stringf("      \"settings\": {");
    write_parameters(f, ctx, ctx->settings, true);
    f << stringf("\n      },\n");
    f << stringf("      \"attributes\": {");
    write_parameters(f, ctx, ctx->attrs, true);
    f << stringf("\n      },\n");
    f << stringf("      \"ports\": {");
    // TODO: Top level ports
    f << stringf("\n      },\n");

    f << stringf("      \"cells\": {");
    bool first = true;
    for (auto &pair : ctx->cells) {        
        auto &c = pair.second;
        f << stringf("%s\n", first ? "" : ",");
        f << stringf("        %s: {\n", get_name(c->name, ctx).c_str());
        f << stringf("          \"hide_name\": %s,\n", c->name.c_str(ctx)[0] == '$' ? "1" : "0");
        f << stringf("          \"type\": %s,\n", get_name(c->type, ctx).c_str());
        f << stringf("          \"parameters\": {");
        write_parameters(f, ctx, c->params);
        f << stringf("\n          },\n");
        f << stringf("          \"attributes\": {");
        write_parameters(f, ctx, c->attrs);
        f << stringf("\n          },\n");
        f << stringf("          \"port_directions\": {");
        bool first2 = true;
        for (auto &conn : c->ports) {
            auto &p = conn.second;
            std::string direction = (p.type == PORT_IN) ? "input" : (p.type == PORT_OUT) ? "output" : "inout";
            f << stringf("%s\n", first2 ? "" : ",");
            f << stringf("            %s: \"%s\"", get_name(conn.first, ctx).c_str(), direction.c_str());
            first2 = false;
        }
        f << stringf("\n          },\n");
        f << stringf("          \"connections\": {");
        first2 = true;
        for (auto &conn : c->ports) {
            auto &p = conn.second;
            f << stringf("%s\n", first2 ? "" : ",");
            if (p.net)
                f << stringf("            %s: [ %d ]", get_name(conn.first,ctx).c_str(), p.net->name.index);
            else 
                f << stringf("            %s: [ ]", get_name(conn.first,ctx).c_str());

            first2 = false;
        }
        f << stringf("\n          }\n");

        f << stringf("        }");
        first = false;
    }

    f << stringf("\n      },\n");

    f << stringf("      \"netnames\": {");
    first = true;
    for (auto &pair : ctx->nets) {
        auto &w = pair.second;
        f << stringf("%s\n", first ? "" : ",");
        f << stringf("        %s: {\n", get_name(w->name, ctx).c_str());
        f << stringf("          \"hide_name\": %s,\n", w->name.c_str(ctx)[0] == '$' ? "1" : "0");
        f << stringf("          \"bits\": [ %d ] ,\n", pair.first.index);
        f << stringf("          \"attributes\": {");
        write_parameters(f, ctx, w->attrs);
        f << stringf("\n          }\n");
        f << stringf("        }");
        first = false;
    }
    
    f << stringf("\n      }\n");
    f << stringf("    }");
}

void write_context(std::ostream &f, Context *ctx)
{
    f << stringf("{\n");
    f << stringf("  \"creator\": %s,\n", get_string( "Next Generation Place and Route (git sha1 " GIT_COMMIT_HASH_STR ")").c_str());
    f << stringf("  \"modules\": {\n");
    write_module(f, ctx);
    f << stringf("\n  }");
    f << stringf("\n}\n");
}

}; // End Namespace JsonWriter

bool write_json_file(std::ostream &f, std::string &filename, Context *ctx)
{
    try {
        using namespace JsonWriter;        
        if (!f)
            log_error("failed to open JSON file.\n");
        write_context(f, ctx);
        log_break();
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END

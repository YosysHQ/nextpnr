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

bool write_parameters(std::ostream &f, Context *ctx, const std::unordered_map<IdString, Property> &parameters, bool for_module=false)
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
    return first;
}

void write_routing(std::ostream &f, Context *ctx, NetInfo *net, bool first)
{
    std::string routing;
    bool first2 = true;
    for (auto &item : net->wires) {
        routing += first2 ? "" : ";";
        routing += ctx->getWireName(item.first).c_str(ctx);
        routing += ",";
        if (item.second.pip != PipId())
            routing += ctx->getPipName(item.second.pip).c_str(ctx);
        first2 = false;
    }

    f << stringf("%s\n", first ? "" : ",");
    f << stringf("            \"NEXTPNR_ROUTING\": ");
    f << get_string(routing);
}

void write_constraints(std::ostream &f, Context *ctx, CellInfo *cell, bool first)
{
    std::string constr;
    constr += std::to_string(cell->constr_x) + ";";
    constr += std::to_string(cell->constr_y) + ";";
    constr += std::to_string(cell->constr_z) + ";";
    constr += std::to_string(cell->constr_abs_z ? 1:0) + ";";
    constr += cell->constr_parent!=nullptr ? cell->constr_parent->name.c_str(ctx) : "";
    f << stringf("%s\n", first ? "" : ",");
    f << stringf("            \"NEXTPNR_CONSTRAINT\": ");
    f << get_string(constr);

    constr = "";
    for(auto &item : cell->constr_children)
    {
        if (!constr.empty()) constr += std::string(";");
        constr += item->name.c_str(ctx);
    }
    f << stringf(",\n");
    f << stringf("            \"NEXTPNR_CONSTR_CHILDREN\": ");
    f << get_string(constr);
    if (cell->bel != BelId()) {
        f << stringf(",\n");
        f << stringf("            \"NEXTPNR_BEL\": ");
        f << get_string(ctx->getBelName(cell->bel).c_str(ctx));
    }

}

void write_module(std::ostream &f, Context *ctx)
{
    f << stringf("    %s: {\n", get_string("top").c_str());
    // TODO: check if this is better to be separate
    /*f << stringf("      \"settings\": {");
    write_parameters(f, ctx, ctx->settings, true);
    f << stringf("\n      },\n");*/
    f << stringf("      \"attributes\": {");
    // TODO: Top level attributes
    f << stringf("\n      },\n");
    f << stringf("      \"ports\": {");
    // TODO: Top level ports
    f << stringf("\n      },\n");

    auto fn = ctx->nets.hash_function();

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
        bool first3 = write_parameters(f, ctx, c->attrs);
        write_constraints(f, ctx, c.get(), first3);
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
                f << stringf("            %s: [ %d ]", get_name(conn.first,ctx).c_str(), fn(p.net->name));
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
        f << stringf("          \"bits\": [ %d ] ,\n", fn(pair.first));
        f << stringf("          \"attributes\": {");
        bool first2 = write_parameters(f, ctx, w->attrs);
        write_routing(f, ctx, w.get(), first2);
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

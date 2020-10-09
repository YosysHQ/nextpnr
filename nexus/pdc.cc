/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
 *
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

#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

struct TCLEntity
{
    enum EntityType
    {
        ENTITY_CELL,
        ENTITY_PORT,
        ENTITY_NET,
    } type;
    IdString name;

    TCLEntity(EntityType type, IdString name) : type(type), name(name) {}

    const std::string &to_string(Context *ctx) { return name.str(ctx); }

    CellInfo *get_cell(Context *ctx)
    {
        if (type != ENTITY_CELL)
            return nullptr;
        return ctx->cells.at(name).get();
    }

    PortInfo *get_port(Context *ctx)
    {
        if (type != ENTITY_PORT)
            return nullptr;
        return &ctx->ports.at(name);
    }

    NetInfo *get_net(Context *ctx)
    {
        if (type != ENTITY_NET)
            return nullptr;
        return ctx->nets.at(name).get();
    }
};

struct TCLValue
{
    TCLValue(const std::string &s) : is_string(true), str(s){};
    TCLValue(const std::vector<TCLEntity> &l) : is_string(false), list(l){};

    bool is_string;
    std::string str;             // simple string value
    std::vector<TCLEntity> list; // list of entities
};

struct PDCParser
{
    std::string buf;
    int pos = 0;
    int lineno = 1;
    Context *ctx;

    inline bool eof() const { return pos == int(buf.size()); }

    inline char peek() const { return buf.at(pos); }

    inline char get()
    {
        char c = buf.at(pos++);
        if (c == '\n')
            ++lineno;
        return c;
    }

    std::string get(int n)
    {
        std::string s = buf.substr(pos, n);
        pos += n;
        return s;
    }

    // If next char matches c, take it from the stream and return true
    bool check_get(char c)
    {
        if (peek() == c) {
            get();
            return true;
        } else {
            return false;
        }
    }

    // If next char matches any in chars, take it from the stream and return true
    bool check_get_any(const std::string &chrs)
    {
        char c = peek();
        if (chrs.find(c) != std::string::npos) {
            get();
            return true;
        } else {
            return false;
        }
    }

    inline void skip_blank(bool nl = false)
    {
        while (!eof() && check_get_any(nl ? " \t\n\r" : " \t"))
            ;
    }

    // Return true if end of line (or file)
    inline bool skip_check_eol()
    {
        skip_blank(false);
        if (eof())
            return true;
        char c = peek();
        // Comments count as end of line
        if (c == '#') {
            get();
            while (!eof() && peek() != '\n' && peek() != '\r')
                get();
            return true;
        }
        if (c == ';') {
            // Forced end of line
            get();
            return true;
        }
        return (c == '\n' || c == '\r');
    }

    inline std::string get_str()
    {
        std::string s;
        skip_blank(false);
        if (eof())
            return "";

        bool in_quotes = false, in_braces = false, escaped = false;

        char c = get();

        if (c == '"')
            in_quotes = true;
        else if (c == '{')
            in_braces = true;
        else
            s += c;

        while (true) {
            char c = peek();
            if (!in_quotes && !in_braces && !escaped && std::isblank(c)) {
                break;
            }
            get();
            if (escaped) {
                s += c;
                escaped = false;
            } else if ((in_quotes && c == '"') || (in_braces && c == '}')) {
                break;
            } else if (c == '\\') {
                escaped = true;
            } else {
                s += c;
            }
        }

        return s;
    }

    TCLValue evaluate(const std::vector<TCLValue> &arguments)
    {
        NPNR_ASSERT(!arguments.empty());
        auto &arg0 = arguments.at(0);
        NPNR_ASSERT(arg0.is_string);
        const std::string &cmd = arg0.str;
        if (cmd == "get_ports")
            return cmd_get_ports(arguments);
        else if (cmd == "ldc_set_location")
            return cmd_ldc_set_location(arguments);
        else if (cmd == "ldc_set_port")
            return cmd_ldc_set_port(arguments);
    }

    std::vector<TCLValue> get_arguments()
    {
        std::vector<TCLValue> args;
        while (!skip_check_eol()) {
            if (check_get('[')) {
                // Start of a sub-expression
                auto result = evaluate(get_arguments());
                NPNR_ASSERT(check_get(']'));
                args.push_back(result);
            } else {
                args.push_back(get_str());
            }
        }
        skip_blank(true);
        return args;
    }

    TCLValue cmd_get_ports(const std::vector<TCLValue> &arguments)
    {
        std::vector<TCLEntity> ports;
        for (int i = 1; i < int(arguments.size()); i++) {
            auto &arg = arguments.at(i);
            if (!arg.is_string)
                log_error("get_ports expected string arguments (line %d)\n", lineno);
            std::string s = arg.str;
            if (s.at(0) == '-')
                log_error("unsupported argument '%s' to get_ports (line %d)\n", s.c_str(), lineno);
            IdString id = ctx->id(s);
            if (ctx->ports.count(id))
                ports.emplace_back(TCLEntity::ENTITY_PORT, id);
        }
        return ports;
    }

    TCLValue cmd_ldc_set_location(const std::vector<TCLValue> &arguments)
    {
        std::string site;

        for (int i = 1; i < int(arguments.size()); i++) {
            auto &arg = arguments.at(i);
            if (arg.is_string) {
                std::string s = arg.str;
                if (s == "-site") {
                    i++;
                    auto &val = arguments.at(i);
                    if (!val.is_string)
                        log_error("expecting string argument to -site (line %d)\n", lineno);
                    site = val.str;
                }
            } else {
                if (site.empty())
                    log_error("expecting -site before list of objects (line %d)\n", lineno);
                for (const auto &ety : arg.list) {
                    if (ety.type == TCLEntity::ENTITY_PORT)
                        ctx->io_attr[ety.name][id_LOC] = site;
                    else if (ety.type == TCLEntity::ENTITY_CELL)
                        ctx->cells[ety.name]->attrs[id_LOC] = site;
                    else
                        log_error("ldc_set_location applies only to cells or IO ports (line %d)\n", lineno);
                }
            }
        }
        return std::string{};
    }

    TCLValue cmd_ldc_set_port(const std::vector<TCLValue> &arguments)
    {
        std::unordered_map<IdString, Property> args;
        for (int i = 1; i < int(arguments.size()); i++) {
            auto &arg = arguments.at(i);
            if (arg.is_string) {
                std::string s = arg.str;
                if (s == "-iobuf") {
                    i++;
                    auto &val = arguments.at(i);
                    if (!val.is_string)
                        log_error("expecting string argument to -iobuf (line %d)\n", lineno);
                    std::stringstream ss(val.str);
                    std::string k, v;
                    while (ss >> k >> v) {
                        args[ctx->id(k)] = v;
                    }
                } else {
                    log_error("unexpected argument '%s' to ldc_set_port (line %d)\n", s.c_str(), lineno);
                }
            } else {
                for (const auto &ety : arg.list) {
                    if (ety.type == TCLEntity::ENTITY_PORT)
                        for (const auto &kv : args)
                            ctx->io_attr[ety.name][kv.first] = kv.second;
                    else
                        log_error("ldc_set_port applies only to IO ports (line %d)\n", lineno);
                }
            }
        }
        return std::string{};
    }
};

NEXTPNR_NAMESPACE_END

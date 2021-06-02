/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021 gatecat <gatecat@ds0.me>
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
#include "util.h"

#include <iterator>

NEXTPNR_NAMESPACE_BEGIN

namespace {

struct QsfOption
{
    std::string name; // name, excluding the initial '-'
    int arg_count;    // number of arguments that follow the option
    bool required;    // error out if this option isn't passed
};

typedef dict<std::string, std::vector<std::string>> option_map_t;

struct QsfCommand
{
    std::string name;               // name of the command
    std::vector<QsfOption> options; // list of "-options"
    int pos_arg_count;              // number of positional arguments expected to follow the command, -1 for any
    std::function<void(Context *ctx, const option_map_t &options, const std::vector<std::string> &pos_args)> func;
};

void set_location_assignment_cmd(Context *ctx, const option_map_t &options, const std::vector<std::string> &pos_args)
{
    ctx->io_attr[ctx->id(options.at("to").at(0))][id_LOC] = pos_args.at(0);
}

void set_instance_assignment_cmd(Context *ctx, const option_map_t &options, const std::vector<std::string> &pos_args)
{
    ctx->io_attr[ctx->id(options.at("to").at(0))][ctx->id(options.at("name").at(0))] = pos_args.at(0);
}

void set_global_assignment_cmd(Context *ctx, const option_map_t &options, const std::vector<std::string> &pos_args)
{
    // TODO
}

static const std::vector<QsfCommand> commands = {
        {"set_location_assignment", {{"to", 1, true}}, 1, set_location_assignment_cmd},
        {"set_instance_assignment",
         {{"to", 1, true}, {"name", 1, true}, {"section_id", 1, false}},
         1,
         set_instance_assignment_cmd},
        {"set_global_assignment",
         {{"name", 1, true}, {"section_id", 1, false}, {"rise", 0, false}, {"fall", 0, false}},
         1,
         set_global_assignment_cmd},
};

struct QsfParser
{
    std::string buf;
    int pos = 0;
    int lineno = 0;
    Context *ctx;

    QsfParser(const std::string &buf, Context *ctx) : buf(buf), ctx(ctx){};

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

    // We need to distinguish between quoted and unquoted strings, the former don't count as options
    struct StringVal
    {
        std::string str;
        bool is_quoted = false;
    };

    inline StringVal get_str()
    {
        StringVal s;
        skip_blank(false);
        if (eof())
            return {"", false};

        bool in_quotes = false, in_braces = false, escaped = false;

        char c = get();

        if (c == '"') {
            in_quotes = true;
            s.is_quoted = true;
        } else if (c == '{') {
            in_braces = true;
            s.is_quoted = true;
        } else {
            s.str += c;
        }

        while (!eof()) {
            char c = peek();
            if (!in_quotes && !in_braces && !escaped && (std::isblank(c) || c == '\n' || c == '\r')) {
                break;
            }
            get();
            if (escaped) {
                s.str += c;
                escaped = false;
            } else if ((in_quotes && c == '"') || (in_braces && c == '}')) {
                break;
            } else if (c == '\\') {
                escaped = true;
            } else {
                s.str += c;
            }
        }

        return s;
    }

    std::vector<StringVal> get_arguments()
    {
        std::vector<StringVal> args;
        while (!skip_check_eol()) {
            args.push_back(get_str());
        }
        skip_blank(true);
        return args;
    }

    void evaluate(const std::vector<StringVal> &args)
    {
        if (args.empty())
            return;
        auto cmd_name = args.at(0).str;
        auto fnd_cmd =
                std::find_if(commands.begin(), commands.end(), [&](const QsfCommand &c) { return c.name == cmd_name; });
        if (fnd_cmd == commands.end()) {
            log_warning("Ignoring unknown command '%s' (line %d)\n", cmd_name.c_str(), lineno);
            return;
        }
        option_map_t opt;
        std::vector<std::string> pos_args;
        for (size_t i = 1; i < args.size(); i++) {
            auto arg = args.at(i);
            if (arg.str.at(0) == '-' && !arg.is_quoted) {
                for (auto &opt_data : fnd_cmd->options) {
                    if (arg.str.compare(1, std::string::npos, opt_data.name) != 0)
                        continue;
                    opt[opt_data.name]; // create empty entry, even if 0 arguments
                    for (int j = 0; j < opt_data.arg_count; j++) {
                        ++i;
                        if (i >= args.size())
                            log_error("Unexpected end of argument list to option '%s' (line %d)\n", arg.str.c_str(),
                                      lineno);
                        opt[opt_data.name].push_back(args.at(i).str);
                    }
                    goto done;
                }
                log_error("Unknown option '%s' to command '%s' (line %d)\n", arg.str.c_str(), cmd_name.c_str(), lineno);
            done:;
            } else {
                // positional argument
                pos_args.push_back(arg.str);
            }
        }
        // Check positional argument count
        if (int(pos_args.size()) != fnd_cmd->pos_arg_count && fnd_cmd->pos_arg_count != -1) {
            log_error("Expected %d positional arguments to command '%s', got %d (line %d)\n", fnd_cmd->pos_arg_count,
                      cmd_name.c_str(), int(pos_args.size()), lineno);
        }
        // Check required options
        for (auto &opt_data : fnd_cmd->options) {
            if (opt_data.required && !opt.count(opt_data.name))
                log_error("Missing required option '%s' to command '%s' (line %d)\n", opt_data.name.c_str(),
                          cmd_name.c_str(), lineno);
        }
        // Execute
        fnd_cmd->func(ctx, opt, pos_args);
    }

    void operator()()
    {
        while (!eof()) {
            skip_blank(true);
            auto args = get_arguments();
            evaluate(args);
        }
    }
};

}; // namespace

void Arch::read_qsf(std::istream &in)
{
    std::string buf(std::istreambuf_iterator<char>(in), {});
    QsfParser(buf, getCtx())();
}

NEXTPNR_NAMESPACE_END

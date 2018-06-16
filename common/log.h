/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2012  Clifford Wolf <clifford@clifford.at>
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

#ifndef LOG_H
#define LOG_H

#include <ostream>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "nextpnr.h"

// from libs/sha1/sha1.h

#define NXP_NORETURN
#define NXP_ATTRIBUTE(...) __attribute__((__VA_ARGS__))

NEXTPNR_NAMESPACE_BEGIN

struct log_cmd_error_exception
{
};

extern std::vector<FILE *> log_files;
extern std::vector<std::ostream *> log_streams;
extern FILE *log_errfile;

extern bool log_quiet_warnings;
extern int log_verbose_level;
extern std::string log_last_error;
extern void (*log_error_atexit)();

void logv(const char *format, va_list ap);
void logv_warning(const char *format, va_list ap);
void logv_warning_noprefix(const char *format, va_list ap);
NXP_NORETURN void logv_error(const char *format, va_list ap)
        NXP_ATTRIBUTE(noreturn);

extern std::ostream clog;
void log(const char *format, ...) NXP_ATTRIBUTE(format(printf, 1, 2));
void log_header(const char *format, ...) NXP_ATTRIBUTE(format(printf, 1, 2));
void log_info(const char *format, ...) NXP_ATTRIBUTE(format(printf, 1, 2));
void log_warning(const char *format, ...) NXP_ATTRIBUTE(format(printf, 1, 2));
void log_warning_noprefix(const char *format, ...)
        NXP_ATTRIBUTE(format(printf, 1, 2));
NXP_NORETURN void log_error(const char *format, ...)
        NXP_ATTRIBUTE(format(printf, 1, 2));
NXP_NORETURN void log_cmd_error(const char *format, ...)
        NXP_ATTRIBUTE(format(printf, 1, 2));

void log_spacer();
void log_push();
void log_pop();

void log_backtrace(const char *prefix, int levels);
void log_reset_stack();
void log_flush();

/*
const char *log_id(RTLIL::IdString id);

template<typename T> static inline const char *log_id(T *obj) {
        return log_id(obj->name);
}
*/

void log_cell(CellInfo *cell, std::string indent = "");
void log_net(NetInfo *net, std::string indent = "");

#ifndef NDEBUG
static inline void log_assert_worker(bool cond, const char *expr,
                                     const char *file, int line)
{
    if (!cond)
        log_error("Assert `%s' failed in %s:%d.\n", expr, file, line);
}
#define log_assert(_assert_expr_)                                              \
    YOSYS_NAMESPACE_PREFIX log_assert_worker(_assert_expr_, #_assert_expr_,    \
                                             __FILE__, __LINE__)
#else
#define log_assert(_assert_expr_)
#endif

#define log_abort() log_error("Abort in %s:%d.\n", __FILE__, __LINE__)
#define log_ping()                                                             \
    log("-- %s:%d %s --\n", __FILE__, __LINE__, __PRETTY_FUNCTION__)

NEXTPNR_NAMESPACE_END

#endif

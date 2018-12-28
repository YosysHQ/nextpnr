/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#include <list>
#include <map>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

NPNR_NORETURN void logv_error(const char *format, va_list ap) NPNR_ATTRIBUTE(noreturn);

std::vector<std::pair<std::ostream *, LogLevel>> log_streams;
log_write_type log_write_function = nullptr;

std::string log_last_error;
void (*log_error_atexit)() = NULL;

std::unordered_map<LogLevel, int> message_count_by_level;
static int log_newline_count = 0;
bool had_nonfatal_error = false;

std::string stringf(const char *fmt, ...)
{
    std::string string;
    va_list ap;

    va_start(ap, fmt);
    string = vstringf(fmt, ap);
    va_end(ap);

    return string;
}

std::string vstringf(const char *fmt, va_list ap)
{
    std::string string;
    char *str = NULL;

#ifdef _WIN32
    int sz = 64 + strlen(fmt), rc;
    while (1) {
        va_list apc;
        va_copy(apc, ap);
        str = (char *)realloc(str, sz);
        rc = vsnprintf(str, sz, fmt, apc);
        va_end(apc);
        if (rc >= 0 && rc < sz)
            break;
        sz *= 2;
    }
#else
    if (vasprintf(&str, fmt, ap) < 0)
        str = NULL;
#endif

    if (str != NULL) {
        string = str;
        free(str);
    }

    return string;
}

void logv(const char *format, va_list ap, LogLevel level = LogLevel::LOG_MSG)
{
    //
    // Trim newlines from the beginning
    while (format[0] == '\n' && format[1] != 0) {
        log_always("\n");
        format++;
    }

    std::string str = vstringf(format, ap);

    if (str.empty())
        return;

    size_t nnl_pos = str.find_last_not_of('\n');
    if (nnl_pos == std::string::npos)
        log_newline_count += str.size();
    else
        log_newline_count = str.size() - nnl_pos - 1;

    for (auto f : log_streams)
        if (f.second <= level)
            *f.first << str;
    if (log_write_function)
        log_write_function(str);
}

void log_with_level(LogLevel level, const char *format, ...)
{
    message_count_by_level[level]++;
    va_list ap;
    va_start(ap, format);
    logv(format, ap, level);
    va_end(ap);
}

void logv_prefixed(const char *prefix, const char *format, va_list ap, LogLevel level)
{
    std::string message = vstringf(format, ap);

    log_with_level(level, "%s%s", prefix, message.c_str());
    log_flush();
}

void log_always(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv(format, ap, LogLevel::ALWAYS_MSG);
    va_end(ap);
}

void log(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv(format, ap, LogLevel::LOG_MSG);
    va_end(ap);
}

void log_info(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv_prefixed("Info: ", format, ap, LogLevel::INFO_MSG);
    va_end(ap);
}

void log_warning(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv_prefixed("Warning: ", format, ap, LogLevel::WARNING_MSG);
    va_end(ap);
}

void log_error(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv_prefixed("ERROR: ", format, ap, LogLevel::ERROR_MSG);

    if (log_error_atexit)
        log_error_atexit();

    throw log_execution_error_exception();
}

void log_break()
{
    if (log_newline_count < 2)
        log("\n");
    if (log_newline_count < 2)
        log("\n");
}

void log_nonfatal_error(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv_prefixed("ERROR: ", format, ap, LogLevel::ERROR_MSG);
    va_end(ap);
    had_nonfatal_error = true;
}

void log_flush()
{
    for (auto f : log_streams)
        f.first->flush();
}

NEXTPNR_NAMESPACE_END

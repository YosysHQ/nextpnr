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

std::vector<FILE *> log_files;
std::vector<std::ostream *> log_streams;
FILE *log_errfile = NULL;
log_write_type log_write_function = nullptr;

bool log_error_stderr = false;
bool log_cmd_error_throw = false;
bool log_quiet_warnings = false;
std::string log_last_error;
void (*log_error_atexit)() = NULL;

// static bool next_print_log = false;
static int log_newline_count = 0;

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

void logv(const char *format, va_list ap)
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

    for (auto f : log_files)
        fputs(str.c_str(), f);

    for (auto f : log_streams)
        *f << str;
    if (log_write_function)
        log_write_function(str);
}

void logv_info(const char *format, va_list ap)
{
    std::string message = vstringf(format, ap);

    log_always("Info: %s", message.c_str());
    log_flush();
}

void logv_warning(const char *format, va_list ap)
{
    std::string message = vstringf(format, ap);

    log_always("Warning: %s", message.c_str());
    log_flush();
}

void logv_warning_noprefix(const char *format, va_list ap)
{
    std::string message = vstringf(format, ap);

    log_always("%s", message.c_str());
}

void logv_error(const char *format, va_list ap)
{
#ifdef EMSCRIPTEN
    auto backup_log_files = log_files;
#endif

    if (log_errfile != NULL)
        log_files.push_back(log_errfile);

    if (log_error_stderr)
        for (auto &f : log_files)
            if (f == stdout)
                f = stderr;

    log_last_error = vstringf(format, ap);
    log_always("ERROR: %s", log_last_error.c_str());
    log_flush();

    if (log_error_atexit)
        log_error_atexit();

#ifdef EMSCRIPTEN
    log_files = backup_log_files;
#endif
    throw log_execution_error_exception();
}

void log_always(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv(format, ap);
    va_end(ap);
}

void log(const char *format, ...)
{
    if (log_quiet_warnings)
        return;
    va_list ap;
    va_start(ap, format);
    logv(format, ap);
    va_end(ap);
}

void log_info(const char *format, ...)
{
    if (log_quiet_warnings)
        return;
    va_list ap;
    va_start(ap, format);
    logv_info(format, ap);
    va_end(ap);
}

void log_warning(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv_warning(format, ap);
    va_end(ap);
}

void log_warning_noprefix(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv_warning_noprefix(format, ap);
    va_end(ap);
}

void log_error(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    logv_error(format, ap);
}

void log_cmd_error(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);

    if (log_cmd_error_throw) {
        log_last_error = vstringf(format, ap);
        log_always("ERROR: %s", log_last_error.c_str());
        log_flush();
        throw log_cmd_error_exception();
    }

    logv_error(format, ap);
}

void log_break()
{
    if (log_quiet_warnings)
        return;
    if (log_newline_count < 2)
        log_always("\n");
    if (log_newline_count < 2)
        log_always("\n");
}

void log_flush()
{
    for (auto f : log_files)
        fflush(f);

    for (auto f : log_streams)
        f->flush();
}

NEXTPNR_NAMESPACE_END

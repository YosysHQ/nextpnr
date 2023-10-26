
/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <micko@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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

#include "application.h"
#include <QMessageBox>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QTextStream>
#include <exception>
#include "log.h"

#ifdef __linux__
#include <execinfo.h>
#endif

NEXTPNR_NAMESPACE_BEGIN

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
BOOL WINAPI WinHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT)
        qApp->quit();
    return TRUE;
}
#endif

namespace {
#ifdef __linux__
std::string get_backtrace_str()
{
    static const size_t MAX_BT_SIZE = 1024;
    std::array<void *, MAX_BT_SIZE> bt_data;
    int bt_len = backtrace(bt_data.data(), MAX_BT_SIZE);
    char **bt_symbols = backtrace_symbols(bt_data.data(), bt_len);
    if (bt_symbols == nullptr)
        return "";
    std::ostringstream ss;
    ss << "Backtrace: " << std::endl;
    for (int i = 0; i < bt_len; i++)
        ss << "  " << bt_symbols[i] << std::endl;
    free(bt_symbols);
    return ss.str();
}
#else
std::string get_backtrace_str() { return ""; }
#endif

void do_error()
{
    std::string bt = get_backtrace_str();

    std::exception_ptr eptr = std::current_exception();
    std::string err_msg = "Unknown Exception Type";

    try {
        if (eptr) {
            std::rethrow_exception(eptr);
        }
    } catch (const std::exception &e) {
        err_msg = e.what();
    } catch (...) {
    }

    QString msg;
    QTextStream out(&msg);
    out << "Internal Error: " << err_msg.c_str() << "\n";
    out << bt.c_str();
    QMessageBox::critical(0, "Error", msg);
    std::abort();
}

} // namespace

Application::Application(int &argc, char **argv, bool noantialiasing) : QApplication(argc, argv)
{
    QSurfaceFormat fmt;
    if (!noantialiasing)
        fmt.setSamples(10);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    // macOS is very picky about this version matching
    // the version of openGL  used in ImGuiRenderer
    fmt.setMajorVersion(3);
    fmt.setMinorVersion(2);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOpenGLContext glContext;
    fmt = glContext.format();
    if (fmt.majorVersion() < 3) {
        printf("Could not get OpenGL 3.0 context. Aborting.\n");
        log_abort();
    }
    if (fmt.minorVersion() < 2) {
        printf("Could not get OpenGL 3.2 context - trying anyway...\n ");
    }

#ifdef _WIN32
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)WinHandler, TRUE);
#endif

    std::set_terminate(do_error);
}

bool Application::notify(QObject *receiver, QEvent *event)
{
    try {
        return QApplication::notify(receiver, event);
    } catch (log_execution_error_exception) {
        QMessageBox::critical(0, "Error", "Pass failed, see log for details!");
        return true;
    }
}

NEXTPNR_NAMESPACE_END

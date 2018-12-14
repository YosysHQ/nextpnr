/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
 *  Copyright (C) 2018  Alex Tsui
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

#include "pyconsole.h"
#include "pyinterpreter.h"

NEXTPNR_NAMESPACE_BEGIN

const QColor PythonConsole::NORMAL_COLOR = QColor::fromRgbF(0, 0, 0);
const QColor PythonConsole::ERROR_COLOR = QColor::fromRgbF(1.0, 0, 0);
const QColor PythonConsole::OUTPUT_COLOR = QColor::fromRgbF(0, 0, 1.0);

PythonConsole::PythonConsole(QWidget *parent) : QTextEdit(parent) {}

void PythonConsole::parseEvent(const ParseMessage &message)
{
    // handle invalid user input
    if (message.errorCode) {
        setTextColor(ERROR_COLOR);
        append(message.message.c_str());

        setTextColor(NORMAL_COLOR);
        append("");
        return;
    }
    // interpret valid user input
    int errorCode = 0;
    std::string res;
    if (message.message.size())
        res = pyinterpreter_execute(message.message, &errorCode);
    if (errorCode) {
        setTextColor(ERROR_COLOR);
    } else {
        setTextColor(OUTPUT_COLOR);
    }

    if (res.size()) {
        append(res.c_str());
    }
    setTextColor(NORMAL_COLOR);
    append("");
    moveCursorToEnd();
}

void PythonConsole::displayString(QString text)
{
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextColor(NORMAL_COLOR);
    cursor.insertText(text);
    cursor.movePosition(QTextCursor::EndOfLine);
    moveCursorToEnd();
}

void PythonConsole::moveCursorToEnd()
{
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
}

void PythonConsole::execute_python(std::string filename)
{
    int errorCode = 0;
    std::string res;
    res = pyinterpreter_execute_file(filename.c_str(), &errorCode);
    if (res.size()) {
        if (errorCode) {
            setTextColor(ERROR_COLOR);
        } else {
            setTextColor(OUTPUT_COLOR);
        }
        append(res.c_str());
        setTextColor(NORMAL_COLOR);
        moveCursorToEnd();
    }
}

NEXTPNR_NAMESPACE_END

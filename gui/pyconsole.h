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

#ifndef PYCONSOLE_H
#define PYCONSOLE_H

#include <QColor>
#include <QMimeData>
#include <QTextEdit>
#include "ParseHelper.h"
#include "ParseListener.h"
#include "nextpnr.h"

class QWidget;
class QKeyEvent;

NEXTPNR_NAMESPACE_BEGIN

class PythonConsole : public QTextEdit, public ParseListener
{
    Q_OBJECT

  public:
    PythonConsole(QWidget *parent = 0);

    void displayString(QString text);
    void moveCursorToEnd();
    virtual void parseEvent(const ParseMessage &message);
    void execute_python(std::string filename);

  protected:
    static const QColor NORMAL_COLOR;
    static const QColor ERROR_COLOR;
    static const QColor OUTPUT_COLOR;
};

NEXTPNR_NAMESPACE_END

#endif // PYCONSOLE_H

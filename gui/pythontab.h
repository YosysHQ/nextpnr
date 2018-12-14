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

#ifndef PYTHONTAB_H
#define PYTHONTAB_H

#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include "ParseHelper.h"
#include "line_editor.h"
#include "nextpnr.h"
#include "pyconsole.h"

NEXTPNR_NAMESPACE_BEGIN

class PythonTab : public QWidget
{
    Q_OBJECT

  public:
    explicit PythonTab(QWidget *parent = 0);
    ~PythonTab();

  private Q_SLOTS:
    void showContextMenu(const QPoint &pt);
    void editLineReturnPressed(QString text);
  public Q_SLOTS:
    void newContext(Context *ctx);
    void info(std::string str);
    void clearBuffer();
    void execute_python(std::string filename);

  private:
    PythonConsole *console;
    LineEditor *lineEdit;
    QMenu *contextMenu;
    bool initialized;
    ParseHelper parseHelper;
    QString prompt;

    static const QString PROMPT;
    static const QString MULTILINE_PROMPT;
};

NEXTPNR_NAMESPACE_END

#endif // PYTHONTAB_H

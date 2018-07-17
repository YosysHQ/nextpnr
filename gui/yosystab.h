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

#ifndef YOSYSTAB_H
#define YOSYSTAB_H

#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include "nextpnr.h"
#include "yosys_edit.h"

NEXTPNR_NAMESPACE_BEGIN

class YosysTab : public QWidget
{
    Q_OBJECT

  public:
    explicit YosysTab(QString folder, QWidget *parent = 0);
    ~YosysTab();

  private:
    void displayString(QString text);
  private Q_SLOTS:
    void showContextMenu(const QPoint &pt);
    void editLineReturnPressed(QString text);
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
  public Q_SLOTS:
    void clearBuffer();

  private:
    QPlainTextEdit *console;
    YosysLineEditor *lineEdit;
    QMenu *contextMenu;
    QProcess *process;
};

NEXTPNR_NAMESPACE_END

#endif // YOSYSTAB_H

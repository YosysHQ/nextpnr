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

#ifndef YOSYS_EDIT_H
#define YOSYS_EDIT_H

#include <QLineEdit>
#include <QMenu>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

class YosysLineEditor : public QLineEdit
{
    Q_OBJECT

  public:
    explicit YosysLineEditor(QWidget *parent = 0);

  private Q_SLOTS:
    void textInserted();
    void showContextMenu(const QPoint &pt);
    void clearHistory();

  Q_SIGNALS:
    void textLineInserted(QString);

  protected:
    void keyPressEvent(QKeyEvent *) Q_DECL_OVERRIDE;
    bool focusNextPrevChild(bool next) Q_DECL_OVERRIDE;

  private:
    int index;
    QStringList lines;
    QMenu *contextMenu;
};

NEXTPNR_NAMESPACE_END

#endif // YOSYS_EDIT_H

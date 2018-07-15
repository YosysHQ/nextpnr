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

#ifndef LINE_EDITOR_H
#define LINE_EDITOR_H

#include <QLineEdit>
#include <QMenu>
#include "ParseHelper.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

class LineEditor : public QLineEdit
{
    Q_OBJECT

  public:
    explicit LineEditor(ParseHelper *helper, QWidget *parent = 0);

  private Q_SLOTS:
    void textInserted();
    void showContextMenu(const QPoint &pt);
    void clearHistory();

  Q_SIGNALS:
    void textLineInserted(QString);

  protected:
    void keyPressEvent(QKeyEvent *) Q_DECL_OVERRIDE;
    bool focusNextPrevChild(bool next) Q_DECL_OVERRIDE;
    void autocomplete();

  private:
    int index;
    QStringList lines;
    QMenu *contextMenu;
    ParseHelper *parseHelper;
};

NEXTPNR_NAMESPACE_END

#endif // LINE_EDITOR_H

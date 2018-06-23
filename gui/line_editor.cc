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

#include "line_editor.h"
#include <QKeyEvent>

NEXTPNR_NAMESPACE_BEGIN

LineEditor::LineEditor(QWidget *parent) : QLineEdit(parent), index(0)
{
    setContextMenuPolicy(Qt::CustomContextMenu);
    QAction *clearAction = new QAction("Clear &history", this);
    clearAction->setStatusTip("Clears line edit history");
    connect(clearAction, SIGNAL(triggered()), this, SLOT(clearHistory()));
    contextMenu = createStandardContextMenu();
    contextMenu->addSeparator();
    contextMenu->addAction(clearAction);

    connect(this, SIGNAL(returnPressed()), SLOT(textInserted()));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint)), this, SLOT(showContextMenu(const QPoint)));
}

void LineEditor::keyPressEvent(QKeyEvent *ev)
{
    if (ev->key() == Qt::Key_Up || ev->key() == Qt::Key_Down) {
        if (lines.empty())
            return;

        if (ev->key() == Qt::Key_Up)
            index--;
        if (ev->key() == Qt::Key_Down)
            index++;

        if (index < 0)
            index = 0;
        if (index >= lines.size()) {
            index = lines.size();
            clear();
            return;
        }
        setText(lines[index]);
    } else if (ev->key() == Qt::Key_Escape) {
        clear();
        return;
    }
    QLineEdit::keyPressEvent(ev);
}

void LineEditor::textInserted()
{
    if (lines.empty() || lines.back() != text())
        lines += text();
    if (lines.size() > 100)
        lines.removeFirst();
    index = lines.size();
    clear();
    Q_EMIT textLineInserted(lines.back());
}

void LineEditor::showContextMenu(const QPoint &pt) { contextMenu->exec(mapToGlobal(pt)); }

void LineEditor::clearHistory()
{
    lines.clear();
    index = 0;
    clear();
}

NEXTPNR_NAMESPACE_END
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

#include "line_editor.h"
#include <QKeyEvent>
#include <QToolTip>
#include "ColumnFormatter.h"
#include "Utils.h"
#include "pyinterpreter.h"

NEXTPNR_NAMESPACE_BEGIN

LineEditor::LineEditor(ParseHelper *helper, QWidget *parent) : QLineEdit(parent), index(0), parseHelper(helper)
{
    setContextMenuPolicy(Qt::CustomContextMenu);
    QAction *clearAction = new QAction("Clear &history", this);
    clearAction->setStatusTip("Clears line edit history");
    connect(clearAction, &QAction::triggered, this, &LineEditor::clearHistory);
    contextMenu = createStandardContextMenu();
    contextMenu->addSeparator();
    contextMenu->addAction(clearAction);

    connect(this, &LineEditor::returnPressed, this, &LineEditor::textInserted);
    connect(this, &LineEditor::customContextMenuRequested, this, &LineEditor::showContextMenu);
}

void LineEditor::keyPressEvent(QKeyEvent *ev)
{

    if (ev->key() == Qt::Key_Up || ev->key() == Qt::Key_Down) {
        QToolTip::hideText();
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
        QToolTip::hideText();
        clear();
        return;
    } else if (ev->key() == Qt::Key_Tab) {
        autocomplete();
        return;
    }
    QToolTip::hideText();

    QLineEdit::keyPressEvent(ev);
}

// This makes TAB work
bool LineEditor::focusNextPrevChild(bool next) { return false; }

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

void LineEditor::autocomplete()
{
    QString line = text();
    const std::list<std::string> &suggestions = pyinterpreter_suggest(line.toStdString());
    if (suggestions.size() == 1) {
        line = suggestions.back().c_str();
    } else {
        // try to complete to longest common prefix
        std::string prefix = LongestCommonPrefix(suggestions.begin(), suggestions.end());
        if (prefix.size() > (size_t)line.size()) {
            line = prefix.c_str();
        } else {
            ColumnFormatter fmt;
            fmt.setItems(suggestions.begin(), suggestions.end());
            fmt.format(width() / 5);
            QString out = "";
            for (auto &it : fmt.formattedOutput()) {
                if (!out.isEmpty())
                    out += "\n";
                out += it.c_str();
            }
            QToolTip::setFont(font());
            if (!out.trimmed().isEmpty())
                QToolTip::showText(mapToGlobal(QPoint(0, 0)), out);
        }
    }
    // set up the next line on the console
    setText(line);
}

NEXTPNR_NAMESPACE_END

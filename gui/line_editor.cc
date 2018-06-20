#include "line_editor.h"

#include <QKeyEvent>

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
    connect(this, SIGNAL(customContextMenuRequested(const QPoint)), this,
            SLOT(showContextMenu(const QPoint)));
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

void LineEditor::showContextMenu(const QPoint &pt)
{
    contextMenu->exec(mapToGlobal(pt));
}

void LineEditor::clearHistory()
{
    lines.clear();
    index = 0;
    clear();
}
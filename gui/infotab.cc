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

#include "infotab.h"
#include <QGridLayout>

NEXTPNR_NAMESPACE_BEGIN

InfoTab::InfoTab(QWidget *parent) : QWidget(parent)
{
    plainTextEdit = new QPlainTextEdit();
    plainTextEdit->setReadOnly(true);
    QFont f("unexistent");
    f.setStyleHint(QFont::Monospace);
    plainTextEdit->setFont(f);

    plainTextEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    QAction *clearAction = new QAction("Clear &buffer", this);
    clearAction->setStatusTip("Clears display buffer");
    connect(clearAction, SIGNAL(triggered()), this, SLOT(clearBuffer()));
    contextMenu = plainTextEdit->createStandardContextMenu();
    contextMenu->addSeparator();
    contextMenu->addAction(clearAction);
    connect(plainTextEdit, SIGNAL(customContextMenuRequested(const QPoint)),
            this, SLOT(showContextMenu(const QPoint)));

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->addWidget(plainTextEdit);
    setLayout(mainLayout);
}

void InfoTab::info(std::string str)
{
    plainTextEdit->moveCursor(QTextCursor::End);
    plainTextEdit->insertPlainText(str.c_str());
    plainTextEdit->moveCursor(QTextCursor::End);
}

void InfoTab::showContextMenu(const QPoint &pt)
{
    contextMenu->exec(mapToGlobal(pt));
}

void InfoTab::clearBuffer() { plainTextEdit->clear(); }

NEXTPNR_NAMESPACE_END

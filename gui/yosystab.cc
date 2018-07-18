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

#include "yosystab.h"
#include <QGridLayout>
#include <QMessageBox>

NEXTPNR_NAMESPACE_BEGIN

YosysTab::YosysTab(QString folder, QWidget *parent) : QWidget(parent)
{
    QFont f("unexistent");
    f.setStyleHint(QFont::Monospace);

    console = new QPlainTextEdit();
    console->setMinimumHeight(100);
    console->setReadOnly(true);
    console->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    console->setFont(f);

    console->setContextMenuPolicy(Qt::CustomContextMenu);
    QAction *clearAction = new QAction("Clear &buffer", this);
    clearAction->setStatusTip("Clears display buffer");
    connect(clearAction, SIGNAL(triggered()), this, SLOT(clearBuffer()));
    contextMenu = console->createStandardContextMenu();
    contextMenu->addSeparator();
    contextMenu->addAction(clearAction);
    connect(console, SIGNAL(customContextMenuRequested(const QPoint)), this, SLOT(showContextMenu(const QPoint)));

    lineEdit = new YosysLineEditor();
    lineEdit->setMinimumHeight(30);
    lineEdit->setMaximumHeight(30);
    lineEdit->setFont(f);
    lineEdit->setFocus();
    lineEdit->setEnabled(false);
    lineEdit->setPlaceholderText("yosys>");
    connect(lineEdit, SIGNAL(textLineInserted(QString)), this, SLOT(editLineReturnPressed(QString)));

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->addWidget(console, 0, 0);
    mainLayout->addWidget(lineEdit, 1, 0);
    setLayout(mainLayout);

    process = new QProcess();
    connect(process, SIGNAL(readyReadStandardError()), this, SLOT(onReadyReadStandardError()));
    connect(process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadyReadStandardOutput()));
    connect(process, &QProcess::started, this, [this] { lineEdit->setEnabled(true); });

#if QT_VERSION < QT_VERSION_CHECK(5, 6, 0)
    connect(process, static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::error), this, [this](QProcess::ProcessError error) {
#else
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
#endif
        if (error == QProcess::FailedToStart) {
            QMessageBox::critical(
                    this, QString::fromUtf8("Yosys cannot be started!"),
                    QString::fromUtf8("<p>Please make sure you have Yosys installed and available in path</p>"));
            Q_EMIT deleteLater();
        }
    });
    process->setWorkingDirectory(folder);
    process->start("yosys");
}

YosysTab::~YosysTab()
{
    process->terminate();
    process->waitForFinished(1000); // in ms
    process->kill();
    process->close();
}

void YosysTab::displayString(QString text)
{
    QTextCursor cursor = console->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    cursor.movePosition(QTextCursor::End);
    console->setTextCursor(cursor);
}

void YosysTab::onReadyReadStandardOutput() { displayString(process->readAllStandardOutput()); }
void YosysTab::onReadyReadStandardError() { displayString(process->readAllStandardError()); }

void YosysTab::editLineReturnPressed(QString text) { process->write(text.toLatin1() + "\n"); }

void YosysTab::showContextMenu(const QPoint &pt) { contextMenu->exec(mapToGlobal(pt)); }

void YosysTab::clearBuffer() { console->clear(); }

NEXTPNR_NAMESPACE_END

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

#include "pythontab.h"
#include <QGridLayout>
#include "pybindings.h"
#include "pyinterpreter.h"

NEXTPNR_NAMESPACE_BEGIN

const QString PythonTab::PROMPT = ">>> ";
const QString PythonTab::MULTILINE_PROMPT = "... ";

PythonTab::PythonTab(QWidget *parent) : QWidget(parent), initialized(false)
{
    QFont f("unexistent");
    f.setStyleHint(QFont::Monospace);

    // Add text area for Python output and input line
    console = new PythonConsole();
    console->setMinimumHeight(100);
    console->setReadOnly(true);
    console->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    console->setFont(f);

    console->setContextMenuPolicy(Qt::CustomContextMenu);
    QAction *clearAction = new QAction("Clear &buffer", this);
    clearAction->setStatusTip("Clears display buffer");
    connect(clearAction, &QAction::triggered, this, &PythonTab::clearBuffer);
    contextMenu = console->createStandardContextMenu();
    contextMenu->addSeparator();
    contextMenu->addAction(clearAction);
    connect(console, &PythonConsole::customContextMenuRequested, this, &PythonTab::showContextMenu);

    lineEdit = new LineEditor(&parseHelper);
    lineEdit->setMinimumHeight(30);
    lineEdit->setMaximumHeight(30);
    lineEdit->setFont(f);
    lineEdit->setPlaceholderText(PythonTab::PROMPT);
    connect(lineEdit, &LineEditor::textLineInserted, this, &PythonTab::editLineReturnPressed);

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->addWidget(console, 0, 0);
    mainLayout->addWidget(lineEdit, 1, 0);
    setLayout(mainLayout);

    parseHelper.subscribe(console);

    prompt = PythonTab::PROMPT;
}

PythonTab::~PythonTab()
{
    if (initialized) {
        pyinterpreter_finalize();
        deinit_python();
    }
}

void PythonTab::editLineReturnPressed(QString text)
{
    console->displayString(prompt + text + "\n");

    parseHelper.process(text.toStdString());

    if (parseHelper.buffered())
        prompt = PythonTab::MULTILINE_PROMPT;
    else
        prompt = PythonTab::PROMPT;

    lineEdit->setPlaceholderText(prompt);
}

void PythonTab::newContext(Context *ctx)
{
    if (initialized) {
        pyinterpreter_finalize();
        deinit_python();
    }
    console->clear();

    pyinterpreter_preinit();
    init_python("nextpnr", !initialized);
    pyinterpreter_initialize();
    pyinterpreter_aquire();
    python_export_global("ctx", ctx);
    pyinterpreter_release();

    initialized = true;

    QString version = QString("Python %1 on %2\n").arg(Py_GetVersion(), Py_GetPlatform());
    console->displayString(version);
}

void PythonTab::showContextMenu(const QPoint &pt) { contextMenu->exec(mapToGlobal(pt)); }

void PythonTab::clearBuffer() { console->clear(); }

void PythonTab::info(std::string str) { console->displayString(str.c_str()); }

void PythonTab::execute_python(std::string filename) { console->execute_python(filename); }

NEXTPNR_NAMESPACE_END

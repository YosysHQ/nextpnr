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
#ifndef NO_PYTHON

#include "pythontab.h"
#include <QGridLayout>
#include "pybindings.h"
#include "pyinterpreter.h"

NEXTPNR_NAMESPACE_BEGIN

PythonTab::PythonTab(QWidget *parent) : QWidget(parent), initialized(false)
{
    // Add text area for Python output and input line
    console = new PythonConsole();
    console->setMinimumHeight(100);
    console->setEnabled(false);

    console->setContextMenuPolicy(Qt::CustomContextMenu);
    QAction *clearAction = new QAction("Clear &buffer", this);
    clearAction->setStatusTip("Clears display buffer");
    connect(clearAction, SIGNAL(triggered()), this, SLOT(clearBuffer()));
    contextMenu = console->createStandardContextMenu();
    contextMenu->addSeparator();
    contextMenu->addAction(clearAction);
    connect(console, SIGNAL(customContextMenuRequested(const QPoint)), this, SLOT(showContextMenu(const QPoint)));

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->addWidget(console, 0, 0);
    setLayout(mainLayout);
}

PythonTab::~PythonTab()
{
    if (initialized) {
        pyinterpreter_finalize();
        deinit_python();
    }
}

void PythonTab::newContext(Context *ctx)
{
    if (initialized) {
        pyinterpreter_finalize();
        deinit_python();
    }
    console->setEnabled(true);
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
    console->displayPrompt();
}

void PythonTab::showContextMenu(const QPoint &pt) { contextMenu->exec(mapToGlobal(pt)); }

void PythonTab::clearBuffer() { console->clear(); }

NEXTPNR_NAMESPACE_END

#endif
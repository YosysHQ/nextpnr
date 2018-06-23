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

#include <QAction>
#include <QFileDialog>
#include <QGridLayout>
#include <QIcon>
#include <QSplitter>
#include "designwidget.h"
#include "fpgaviewwidget.h"
#include "jsonparse.h"
#include "log.h"
#include "mainwindow.h"

#ifndef NO_PYTHON
#include "pythontab.h"
#endif

static void initBasenameResource() { Q_INIT_RESOURCE(base); }

NEXTPNR_NAMESPACE_BEGIN

BaseMainWindow::BaseMainWindow(Context *_ctx, QWidget *parent) : QMainWindow(parent), ctx(_ctx)
{
    initBasenameResource();
    qRegisterMetaType<std::string>();

    log_files.clear();
    log_streams.clear();

    setObjectName(QStringLiteral("BaseMainWindow"));
    resize(1024, 768);

    createMenusAndBars();

    QWidget *centralWidget = new QWidget(this);

    QGridLayout *gridLayout = new QGridLayout(centralWidget);
    gridLayout->setSpacing(6);
    gridLayout->setContentsMargins(11, 11, 11, 11);

    QSplitter *splitter_h = new QSplitter(Qt::Horizontal, centralWidget);
    QSplitter *splitter_v = new QSplitter(Qt::Vertical, splitter_h);
    splitter_h->addWidget(splitter_v);

    gridLayout->addWidget(splitter_h, 0, 0, 1, 1);

    setCentralWidget(centralWidget);

    DesignWidget *designview = new DesignWidget(ctx);
    designview->setMinimumWidth(300);
    designview->setMaximumWidth(300);
    splitter_h->addWidget(designview);

    connect(designview, SIGNAL(info(std::string)), this, SLOT(writeInfo(std::string)));

    tabWidget = new QTabWidget();
#ifndef NO_PYTHON
    tabWidget->addTab(new PythonTab(), "Python");
#endif
    info = new InfoTab();
    tabWidget->addTab(info, "Info");

    centralTabWidget = new QTabWidget();
    centralTabWidget->addTab(new FPGAViewWidget(), "Graphics");

    splitter_v->addWidget(centralTabWidget);
    splitter_v->addWidget(tabWidget);
}

BaseMainWindow::~BaseMainWindow() {}

void BaseMainWindow::writeInfo(std::string text) { info->info(text); }

void BaseMainWindow::createMenusAndBars()
{
    QAction *actionOpen = new QAction("Open", this);
    QIcon icon1;
    icon1.addFile(QStringLiteral(":/icons/resources/open.png"));
    actionOpen->setIcon(icon1);
    actionOpen->setShortcuts(QKeySequence::Open);
    actionOpen->setStatusTip("Open an existing JSON file");
    connect(actionOpen, SIGNAL(triggered()), this, SLOT(open()));

    QAction *actionSave = new QAction("Save", this);
    QIcon icon2;
    icon2.addFile(QStringLiteral(":/icons/resources/save.png"));
    actionSave->setIcon(icon2);
    actionSave->setShortcuts(QKeySequence::Save);
    actionSave->setStatusTip("Save the ASC to disk");
    connect(actionSave, SIGNAL(triggered()), this, SLOT(save()));
    actionSave->setEnabled(false);

    QAction *actionExit = new QAction("Exit", this);
    QIcon icon3;
    icon3.addFile(QStringLiteral(":/icons/resources/exit.png"));
    actionExit->setIcon(icon3);
    actionExit->setShortcuts(QKeySequence::Quit);
    actionExit->setStatusTip("Exit the application");
    connect(actionExit, SIGNAL(triggered()), this, SLOT(close()));

    QAction *actionAbout = new QAction("About", this);

    menuBar = new QMenuBar();
    menuBar->setGeometry(QRect(0, 0, 1024, 27));
    QMenu *menu_File = new QMenu("&File", menuBar);
    QMenu *menu_Help = new QMenu("&Help", menuBar);
    menuBar->addAction(menu_File->menuAction());
    menuBar->addAction(menu_Help->menuAction());
    setMenuBar(menuBar);

    mainToolBar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, mainToolBar);

    statusBar = new QStatusBar();
    setStatusBar(statusBar);

    menu_File->addAction(actionOpen);
    menu_File->addAction(actionSave);
    menu_File->addSeparator();
    menu_File->addAction(actionExit);
    menu_Help->addAction(actionAbout);

    mainToolBar->addAction(actionOpen);
    mainToolBar->addAction(actionSave);
}

NEXTPNR_NAMESPACE_END

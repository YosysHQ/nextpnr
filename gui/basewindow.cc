/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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
#include <QCoreApplication>
#include <QFileDialog>
#include <QGridLayout>
#include <QIcon>
#include <QSplitter>
#include "designwidget.h"
#include "fpgaviewwidget.h"
#include "log.h"
#include "mainwindow.h"
#include "pythontab.h"

static void initBasenameResource() { Q_INIT_RESOURCE(base); }

NEXTPNR_NAMESPACE_BEGIN

BaseMainWindow::BaseMainWindow(std::unique_ptr<Context> context, QWidget *parent)
        : QMainWindow(parent), ctx(std::move(context))
{
    initBasenameResource();
    qRegisterMetaType<std::string>();

    log_files.clear();
    log_streams.clear();

    setObjectName("BaseMainWindow");
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

    designview = new DesignWidget();
    designview->setMinimumWidth(300);
    splitter_h->addWidget(designview);

    tabWidget = new QTabWidget();

    console = new PythonTab();
    tabWidget->addTab(console, "Console");
    connect(this, SIGNAL(contextChanged(Context *)), console, SLOT(newContext(Context *)));

    centralTabWidget = new QTabWidget();
    centralTabWidget->setTabsClosable(true);
    connect(centralTabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));

    fpgaView = new FPGAViewWidget();
    centralTabWidget->addTab(fpgaView, "Graphics");
    centralTabWidget->tabBar()->tabButton(0, QTabBar::RightSide)->resize(0, 0);

    connect(this, SIGNAL(contextChanged(Context *)), fpgaView, SLOT(newContext(Context *)));
    connect(designview, SIGNAL(selected(std::vector<DecalXY>, bool)), fpgaView,
            SLOT(onSelectedArchItem(std::vector<DecalXY>, bool)));
    connect(fpgaView, SIGNAL(clickedBel(BelId, bool)), designview, SLOT(onClickedBel(BelId, bool)));
    connect(fpgaView, SIGNAL(clickedWire(WireId, bool)), designview, SLOT(onClickedWire(WireId, bool)));
    connect(fpgaView, SIGNAL(clickedPip(PipId, bool)), designview, SLOT(onClickedPip(PipId, bool)));

    connect(designview, SIGNAL(highlight(std::vector<DecalXY>, int)), fpgaView,
            SLOT(onHighlightGroupChanged(std::vector<DecalXY>, int)));

    connect(this, SIGNAL(contextChanged(Context *)), designview, SLOT(newContext(Context *)));
    connect(this, SIGNAL(updateTreeView()), designview, SLOT(updateTree()));

    connect(designview, SIGNAL(info(std::string)), this, SLOT(writeInfo(std::string)));

    splitter_v->addWidget(centralTabWidget);
    splitter_v->addWidget(tabWidget);
}

BaseMainWindow::~BaseMainWindow() {}

void BaseMainWindow::closeTab(int index) { delete centralTabWidget->widget(index); }

void BaseMainWindow::writeInfo(std::string text) { console->info(text); }

void BaseMainWindow::createMenusAndBars()
{
    actionNew = new QAction("New", this);
    actionNew->setIcon(QIcon(":/icons/resources/new.png"));
    actionNew->setShortcuts(QKeySequence::New);
    actionNew->setStatusTip("New project file");
    connect(actionNew, SIGNAL(triggered()), this, SLOT(new_proj()));

    actionOpen = new QAction("Open", this);
    actionOpen->setIcon(QIcon(":/icons/resources/open.png"));
    actionOpen->setShortcuts(QKeySequence::Open);
    actionOpen->setStatusTip("Open an existing project file");
    connect(actionOpen, SIGNAL(triggered()), this, SLOT(open_proj()));

    actionSave = new QAction("Save", this);
    actionSave->setIcon(QIcon(":/icons/resources/save.png"));
    actionSave->setShortcuts(QKeySequence::Save);
    actionSave->setStatusTip("Save existing project to disk");
    actionSave->setEnabled(false);
    connect(actionSave, SIGNAL(triggered()), this, SLOT(save_proj()));

    QAction *actionExit = new QAction("Exit", this);
    actionExit->setIcon(QIcon(":/icons/resources/exit.png"));
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
    progressBar = new QProgressBar(statusBar);
    progressBar->setAlignment(Qt::AlignRight);
    progressBar->setMaximumSize(180, 19);
    statusBar->addPermanentWidget(progressBar);
    progressBar->setValue(0);
    progressBar->setEnabled(false);
    setStatusBar(statusBar);

    menu_File->addAction(actionNew);
    menu_File->addAction(actionOpen);
    menu_File->addAction(actionSave);
    menu_File->addSeparator();
    menu_File->addAction(actionExit);
    menu_Help->addAction(actionAbout);

    mainToolBar->addAction(actionNew);
    mainToolBar->addAction(actionOpen);
    mainToolBar->addAction(actionSave);
}

void BaseMainWindow::createGraphicsBar()
{
    QAction *actionZoomIn = new QAction("Zoom In", this);
    actionZoomIn->setIcon(QIcon(":/icons/resources/zoom_in.png"));
    connect(actionZoomIn, SIGNAL(triggered()), fpgaView, SLOT(zoomIn()));

    QAction *actionZoomOut = new QAction("Zoom Out", this);
    actionZoomOut->setIcon(QIcon(":/icons/resources/zoom_out.png"));
    connect(actionZoomOut, SIGNAL(triggered()), fpgaView, SLOT(zoomOut()));

    QAction *actionZoomSelected = new QAction("Zoom Selected", this);
    actionZoomSelected->setIcon(QIcon(":/icons/resources/shape_handles.png"));
    connect(actionZoomSelected, SIGNAL(triggered()), fpgaView, SLOT(zoomSelected()));

    QAction *actionZoomOutbound = new QAction("Zoom Outbound", this);
    actionZoomOutbound->setIcon(QIcon(":/icons/resources/shape_square.png"));
    connect(actionZoomOutbound, SIGNAL(triggered()), fpgaView, SLOT(zoomOutbound()));

    graphicsToolBar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, graphicsToolBar);
    graphicsToolBar->addAction(actionZoomIn);
    graphicsToolBar->addAction(actionZoomOut);
    graphicsToolBar->addAction(actionZoomSelected);
    graphicsToolBar->addAction(actionZoomOutbound);
}

NEXTPNR_NAMESPACE_END

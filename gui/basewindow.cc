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
#include <QInputDialog>
#include <QSplitter>
#include <fstream>
#include "designwidget.h"
#include "fpgaviewwidget.h"
#include "jsonparse.h"
#include "log.h"
#include "mainwindow.h"
#include "project.h"
#include "pythontab.h"

static void initBasenameResource() { Q_INIT_RESOURCE(base); }

NEXTPNR_NAMESPACE_BEGIN

BaseMainWindow::BaseMainWindow(std::unique_ptr<Context> context, ArchArgs args, QWidget *parent)
        : QMainWindow(parent), chipArgs(args), ctx(std::move(context)), timing_driven(false)
{
    initBasenameResource();
    qRegisterMetaType<std::string>();

    log_streams.clear();

    setObjectName("BaseMainWindow");
    resize(1024, 768);

    task = new TaskManager();

    // Create and deploy widgets on main screen
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

    centralTabWidget = new QTabWidget();
    centralTabWidget->setTabsClosable(true);

    fpgaView = new FPGAViewWidget();
    centralTabWidget->addTab(fpgaView, "Device");
    centralTabWidget->tabBar()->setTabButton(0, QTabBar::RightSide, 0);
    centralTabWidget->tabBar()->setTabButton(0, QTabBar::LeftSide, 0);

    splitter_v->addWidget(centralTabWidget);
    splitter_v->addWidget(tabWidget);

    // Connect Worker
    connect(task, &TaskManager::log, this, &BaseMainWindow::writeInfo);
    connect(task, &TaskManager::pack_finished, this, &BaseMainWindow::pack_finished);
    connect(task, &TaskManager::budget_finish, this, &BaseMainWindow::budget_finish);
    connect(task, &TaskManager::place_finished, this, &BaseMainWindow::place_finished);
    connect(task, &TaskManager::route_finished, this, &BaseMainWindow::route_finished);
    connect(task, &TaskManager::taskCanceled, this, &BaseMainWindow::taskCanceled);
    connect(task, &TaskManager::taskStarted, this, &BaseMainWindow::taskStarted);
    connect(task, &TaskManager::taskPaused, this, &BaseMainWindow::taskPaused);

    // Events for context change
    connect(this, &BaseMainWindow::contextChanged, task, &TaskManager::contextChanged);
    connect(this, &BaseMainWindow::contextChanged, console, &PythonTab::newContext);
    connect(this, &BaseMainWindow::contextChanged, fpgaView, &FPGAViewWidget::newContext);
    connect(this, &BaseMainWindow::contextChanged, designview, &DesignWidget::newContext);

    // Catch close tab events
    connect(centralTabWidget, &QTabWidget::tabCloseRequested, this, &BaseMainWindow::closeTab);

    // Propagate events from design view to device view
    connect(designview, &DesignWidget::selected, fpgaView, &FPGAViewWidget::onSelectedArchItem);
    connect(designview, &DesignWidget::zoomSelected, fpgaView, &FPGAViewWidget::zoomSelected);
    connect(designview, &DesignWidget::highlight, fpgaView, &FPGAViewWidget::onHighlightGroupChanged);
    connect(designview, &DesignWidget::hover, fpgaView, &FPGAViewWidget::onHoverItemChanged);

    // Click event on device view
    connect(fpgaView, &FPGAViewWidget::clickedBel, designview, &DesignWidget::onClickedBel);
    connect(fpgaView, &FPGAViewWidget::clickedWire, designview, &DesignWidget::onClickedWire);
    connect(fpgaView, &FPGAViewWidget::clickedPip, designview, &DesignWidget::onClickedPip);

    // Update tree event
    connect(this, &BaseMainWindow::updateTreeView, designview, &DesignWidget::updateTree);

    createMenusAndBars();
}

BaseMainWindow::~BaseMainWindow() { delete task; }

void BaseMainWindow::closeTab(int index) { delete centralTabWidget->widget(index); }

void BaseMainWindow::writeInfo(std::string text) { console->info(text); }

void BaseMainWindow::createMenusAndBars()
{
    // File menu / project toolbar actions
    actionNew = new QAction("New", this);
    actionNew->setIcon(QIcon(":/icons/resources/new.png"));
    actionNew->setShortcuts(QKeySequence::New);
    actionNew->setStatusTip("New project file");
    connect(actionNew, &QAction::triggered, this, &BaseMainWindow::new_proj);

    actionOpen = new QAction("Open", this);
    actionOpen->setIcon(QIcon(":/icons/resources/open.png"));
    actionOpen->setShortcuts(QKeySequence::Open);
    actionOpen->setStatusTip("Open an existing project file");
    connect(actionOpen, &QAction::triggered, this, &BaseMainWindow::open_proj);

    actionSave = new QAction("Save", this);
    actionSave->setIcon(QIcon(":/icons/resources/save.png"));
    actionSave->setShortcuts(QKeySequence::Save);
    actionSave->setStatusTip("Save existing project to disk");
    actionSave->setEnabled(false);
    connect(actionSave, &QAction::triggered, this, &BaseMainWindow::save_proj);

    QAction *actionExit = new QAction("Exit", this);
    actionExit->setIcon(QIcon(":/icons/resources/exit.png"));
    actionExit->setShortcuts(QKeySequence::Quit);
    actionExit->setStatusTip("Exit the application");
    connect(actionExit, &QAction::triggered, this, &BaseMainWindow::close);

    // Help menu actions
    QAction *actionAbout = new QAction("About", this);

    // Design menu options
    actionLoadJSON = new QAction("Open JSON", this);
    actionLoadJSON->setIcon(QIcon(":/icons/resources/open_json.png"));
    actionLoadJSON->setStatusTip("Open an existing JSON file");
    actionLoadJSON->setEnabled(true);
    connect(actionLoadJSON, &QAction::triggered, this, &BaseMainWindow::open_json);

    actionPack = new QAction("Pack", this);
    actionPack->setIcon(QIcon(":/icons/resources/pack.png"));
    actionPack->setStatusTip("Pack current design");
    actionPack->setEnabled(false);
    connect(actionPack, &QAction::triggered, task, &TaskManager::pack);

    actionAssignBudget = new QAction("Assign Budget", this);
    actionAssignBudget->setIcon(QIcon(":/icons/resources/time_add.png"));
    actionAssignBudget->setStatusTip("Assign time budget for current design");
    actionAssignBudget->setEnabled(false);
    connect(actionAssignBudget, &QAction::triggered, this, &BaseMainWindow::budget);

    actionPlace = new QAction("Place", this);
    actionPlace->setIcon(QIcon(":/icons/resources/place.png"));
    actionPlace->setStatusTip("Place current design");
    actionPlace->setEnabled(false);
    connect(actionPlace, &QAction::triggered, this, &BaseMainWindow::place);

    actionRoute = new QAction("Route", this);
    actionRoute->setIcon(QIcon(":/icons/resources/route.png"));
    actionRoute->setStatusTip("Route current design");
    actionRoute->setEnabled(false);
    connect(actionRoute, &QAction::triggered, task, &TaskManager::route);

    actionExecutePy = new QAction("Execute Python", this);
    actionExecutePy->setIcon(QIcon(":/icons/resources/py.png"));
    actionExecutePy->setStatusTip("Execute Python script");
    actionExecutePy->setEnabled(true);
    connect(actionExecutePy, &QAction::triggered, this, &BaseMainWindow::execute_python);

    // Worker control toolbar actions
    actionPlay = new QAction("Play", this);
    actionPlay->setIcon(QIcon(":/icons/resources/control_play.png"));
    actionPlay->setStatusTip("Continue running task");
    actionPlay->setEnabled(false);
    connect(actionPlay, &QAction::triggered, task, &TaskManager::continue_thread);

    actionPause = new QAction("Pause", this);
    actionPause->setIcon(QIcon(":/icons/resources/control_pause.png"));
    actionPause->setStatusTip("Pause running task");
    actionPause->setEnabled(false);
    connect(actionPause, &QAction::triggered, task, &TaskManager::pause_thread);

    actionStop = new QAction("Stop", this);
    actionStop->setIcon(QIcon(":/icons/resources/control_stop.png"));
    actionStop->setStatusTip("Stop running task");
    actionStop->setEnabled(false);
    connect(actionStop, &QAction::triggered, task, &TaskManager::terminate_thread);

    // Device view control toolbar actions
    QAction *actionZoomIn = new QAction("Zoom In", this);
    actionZoomIn->setIcon(QIcon(":/icons/resources/zoom_in.png"));
    connect(actionZoomIn, &QAction::triggered, fpgaView, &FPGAViewWidget::zoomIn);

    QAction *actionZoomOut = new QAction("Zoom Out", this);
    actionZoomOut->setIcon(QIcon(":/icons/resources/zoom_out.png"));
    connect(actionZoomOut, &QAction::triggered, fpgaView, &FPGAViewWidget::zoomOut);

    QAction *actionZoomSelected = new QAction("Zoom Selected", this);
    actionZoomSelected->setIcon(QIcon(":/icons/resources/shape_handles.png"));
    connect(actionZoomSelected, &QAction::triggered, fpgaView, &FPGAViewWidget::zoomSelected);

    QAction *actionZoomOutbound = new QAction("Zoom Outbound", this);
    actionZoomOutbound->setIcon(QIcon(":/icons/resources/shape_square.png"));
    connect(actionZoomOutbound, &QAction::triggered, fpgaView, &FPGAViewWidget::zoomOutbound);

    // Add main menu
    menuBar = new QMenuBar();
    menuBar->setGeometry(QRect(0, 0, 1024, 27));
    setMenuBar(menuBar);
    QMenu *menuFile = new QMenu("&File", menuBar);
    QMenu *menuHelp = new QMenu("&Help", menuBar);
    menuDesign = new QMenu("&Design", menuBar);
    menuBar->addAction(menuFile->menuAction());
    menuBar->addAction(menuDesign->menuAction());
    menuBar->addAction(menuHelp->menuAction());

    // Add File menu actions
    menuFile->addAction(actionNew);
    menuFile->addAction(actionOpen);
    menuFile->addAction(actionSave);
    menuFile->addSeparator();
    menuFile->addAction(actionExit);

    // Add Design menu actions
    menuDesign->addAction(actionLoadJSON);
    menuDesign->addAction(actionPack);
    menuDesign->addAction(actionAssignBudget);
    menuDesign->addAction(actionPlace);
    menuDesign->addAction(actionRoute);
    menuDesign->addSeparator();
    menuDesign->addAction(actionExecutePy);

    // Add Help menu actions
    menuHelp->addAction(actionAbout);

    // Project toolbar
    QToolBar *projectToolBar = new QToolBar("Project");
    addToolBar(Qt::TopToolBarArea, projectToolBar);
    projectToolBar->addAction(actionNew);
    projectToolBar->addAction(actionOpen);
    projectToolBar->addAction(actionSave);

    // Main action bar
    mainActionBar = new QToolBar("Main");
    addToolBar(Qt::TopToolBarArea, mainActionBar);
    mainActionBar->addAction(actionLoadJSON);
    mainActionBar->addAction(actionPack);
    mainActionBar->addAction(actionAssignBudget);
    mainActionBar->addAction(actionPlace);
    mainActionBar->addAction(actionRoute);
    mainActionBar->addAction(actionExecutePy);

    // Add worker control toolbar
    QToolBar *workerControlToolBar = new QToolBar("Worker");
    addToolBar(Qt::TopToolBarArea, workerControlToolBar);
    workerControlToolBar->addAction(actionPlay);
    workerControlToolBar->addAction(actionPause);
    workerControlToolBar->addAction(actionStop);

    // Add device view control toolbar
    QToolBar *deviceViewToolBar = new QToolBar("Device");
    addToolBar(Qt::TopToolBarArea, deviceViewToolBar);
    deviceViewToolBar->addAction(actionZoomIn);
    deviceViewToolBar->addAction(actionZoomOut);
    deviceViewToolBar->addAction(actionZoomSelected);
    deviceViewToolBar->addAction(actionZoomOutbound);

    // Add status bar with progress bar
    statusBar = new QStatusBar();
    progressBar = new QProgressBar(statusBar);
    progressBar->setAlignment(Qt::AlignRight);
    progressBar->setMaximumSize(180, 19);
    statusBar->addPermanentWidget(progressBar);
    progressBar->setValue(0);
    progressBar->setEnabled(false);
    setStatusBar(statusBar);
}

void BaseMainWindow::load_json(std::string filename)
{
    disableActions();
    std::ifstream f(filename);
    if (parse_json_file(f, filename, ctx.get())) {
        log("Loading design successful.\n");
        Q_EMIT updateTreeView();
        updateLoaded();
    } else {
        actionLoadJSON->setEnabled(true);
        log("Loading design failed.\n");
    }
}

void BaseMainWindow::open_json()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open JSON"), QString(), QString("*.json"));
    if (!fileName.isEmpty()) {
        load_json(fileName.toStdString());
    }
}

void BaseMainWindow::pack_finished(bool status)
{
    disableActions();
    if (status) {
        log("Packing design successful.\n");
        Q_EMIT updateTreeView();
        actionPlace->setEnabled(true);
        actionAssignBudget->setEnabled(true);
        onPackFinished();
    } else {
        log("Packing design failed.\n");
    }
}

void BaseMainWindow::budget_finish(bool status)
{
    disableActions();
    if (status) {
        log("Assigning timing budget successful.\n");
        actionPlace->setEnabled(true);
        onBudgetFinished();
    } else {
        log("Assigning timing budget failed.\n");
    }
}

void BaseMainWindow::place_finished(bool status)
{
    disableActions();
    if (status) {
        log("Placing design successful.\n");
        Q_EMIT updateTreeView();
        actionRoute->setEnabled(true);
        onPlaceFinished();
    } else {
        log("Placing design failed.\n");
    }
}
void BaseMainWindow::route_finished(bool status)
{
    disableActions();
    if (status) {
        log("Routing design successful.\n");
        Q_EMIT updateTreeView();
        onRouteFinished();
    } else
        log("Routing design failed.\n");
}

void BaseMainWindow::taskCanceled()
{
    log("CANCELED\n");
    disableActions();
}

void BaseMainWindow::taskStarted()
{
    disableActions();
    actionPause->setEnabled(true);
    actionStop->setEnabled(true);

    actionNew->setEnabled(false);
    actionOpen->setEnabled(false);
}

void BaseMainWindow::taskPaused()
{
    disableActions();
    actionPlay->setEnabled(true);
    actionStop->setEnabled(true);

    actionNew->setEnabled(false);
    actionOpen->setEnabled(false);
}

void BaseMainWindow::budget()
{
    bool ok;
    double freq = QInputDialog::getDouble(this, "Assign timing budget", "Frequency [MHz]:", 50, 0, 250, 2, &ok);
    if (ok) {
        freq *= 1e6;
        timing_driven = true;
        Q_EMIT task->budget(freq);
    }
}

void BaseMainWindow::place() { Q_EMIT task->place(timing_driven); }

void BaseMainWindow::disableActions()
{
    actionLoadJSON->setEnabled(false);
    actionPack->setEnabled(false);
    actionAssignBudget->setEnabled(false);
    actionPlace->setEnabled(false);
    actionRoute->setEnabled(false);
    actionExecutePy->setEnabled(true);

    actionPlay->setEnabled(false);
    actionPause->setEnabled(false);
    actionStop->setEnabled(false);

    actionNew->setEnabled(true);
    actionOpen->setEnabled(true);

    if (ctx->settings.find(ctx->id("input/json")) != ctx->settings.end())
        actionSave->setEnabled(true);
    else
        actionSave->setEnabled(false);

    onDisableActions();
}

void BaseMainWindow::updateLoaded()
{
    disableActions();
    actionPack->setEnabled(true);
    onJsonLoaded();
    onProjectLoaded();
}

void BaseMainWindow::projectLoad(std::string filename)
{
    ProjectHandler proj;
    disableActions();
    ctx = proj.load(filename);
    Q_EMIT contextChanged(ctx.get());
    log_info("Loaded project %s...\n", filename.c_str());
    updateLoaded();
}

void BaseMainWindow::open_proj()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open Project"), QString(), QString("*.proj"));
    if (!fileName.isEmpty()) {
        projectLoad(fileName.toStdString());
    }
}

void BaseMainWindow::execute_python()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Execute Python"), QString(), QString("*.py"));
    if (!fileName.isEmpty()) {
        console->execute_python(fileName.toStdString());
    }
}

void BaseMainWindow::notifyChangeContext() { Q_EMIT contextChanged(ctx.get()); }
void BaseMainWindow::save_proj()
{
    if (currentProj.empty()) {
        QString fileName = QFileDialog::getSaveFileName(this, QString("Save Project"), QString(), QString("*.proj"));
        if (fileName.isEmpty())
            return;
        currentProj = fileName.toStdString();
    }
    if (!currentProj.empty()) {
        ProjectHandler proj;
        proj.save(ctx.get(), currentProj);
    }
}

NEXTPNR_NAMESPACE_END

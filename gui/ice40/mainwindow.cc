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

#include "mainwindow.h"
#include <QAction>
#include <QFileDialog>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include "bitstream.h"
#include "design_utils.h"
#include "jsonparse.h"
#include "log.h"
#include "pack.h"
#include "pcf.h"
#include "place_sa.h"
#include "route.h"

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(QWidget *parent) : BaseMainWindow(parent), timing_driven(false)
{
    initMainResource();

    std::string title = "nextpnr-ice40 - [EMPTY]";
    setWindowTitle(title.c_str());

    task = new TaskManager();
    connect(task, SIGNAL(log(std::string)), this, SLOT(writeInfo(std::string)));

    connect(task, SIGNAL(loadfile_finished(bool)), this, SLOT(loadfile_finished(bool)));
    connect(task, SIGNAL(loadpcf_finished(bool)), this, SLOT(loadpcf_finished(bool)));
    connect(task, SIGNAL(saveasc_finished(bool)), this, SLOT(saveasc_finished(bool)));
    connect(task, SIGNAL(pack_finished(bool)), this, SLOT(pack_finished(bool)));
    connect(task, SIGNAL(budget_finish(bool)), this, SLOT(budget_finish(bool)));
    connect(task, SIGNAL(place_finished(bool)), this, SLOT(place_finished(bool)));
    connect(task, SIGNAL(route_finished(bool)), this, SLOT(route_finished(bool)));

    connect(task, SIGNAL(taskCanceled()), this, SLOT(taskCanceled()));
    connect(task, SIGNAL(taskStarted()), this, SLOT(taskStarted()));
    connect(task, SIGNAL(taskPaused()), this, SLOT(taskPaused()));

    connect(this, SIGNAL(contextChanged(Context*)), this, SLOT(newContext(Context*)));
    connect(this, SIGNAL(contextChanged(Context*)), task, SIGNAL(contextChanged(Context*)));

    createMenu();
}

MainWindow::~MainWindow() { delete task; }

void MainWindow::createMenu()
{
    QMenu *menu_Design = new QMenu("&Design", menuBar);
    menuBar->addAction(menu_Design->menuAction());

    actionLoadJSON = new QAction("Open JSON", this);
    QIcon iconLoadJSON;
    iconLoadJSON.addFile(QStringLiteral(":/icons/resources/open_json.png"));
    actionLoadJSON->setIcon(iconLoadJSON);
    actionLoadJSON->setStatusTip("Open an existing JSON file");
    connect(actionLoadJSON, SIGNAL(triggered()), this, SLOT(open_json()));
    actionLoadJSON->setEnabled(false);

    actionLoadPCF = new QAction("Open PCF", this);
    QIcon iconLoadPCF;
    iconLoadPCF.addFile(QStringLiteral(":/icons/resources/open_pcf.png"));
    actionLoadPCF->setIcon(iconLoadPCF);
    actionLoadPCF->setStatusTip("Open PCF file");
    connect(actionLoadPCF, SIGNAL(triggered()), this, SLOT(open_pcf()));
    actionLoadPCF->setEnabled(false);

    actionPack = new QAction("Pack", this);
    QIcon iconPack;
    iconPack.addFile(QStringLiteral(":/icons/resources/pack.png"));
    actionPack->setIcon(iconPack);
    actionPack->setStatusTip("Pack current design");
    connect(actionPack, SIGNAL(triggered()), task, SIGNAL(pack()));
    actionPack->setEnabled(false);

    actionAssignBudget = new QAction("Assign Budget", this);
    QIcon iconAssignBudget;
    iconAssignBudget.addFile(QStringLiteral(":/icons/resources/time_add.png"));
    actionAssignBudget->setIcon(iconAssignBudget);
    actionAssignBudget->setStatusTip("Assign time budget for current design");
    connect(actionAssignBudget, SIGNAL(triggered()), this, SLOT(budget()));
    actionAssignBudget->setEnabled(false);

    actionPlace = new QAction("Place", this);
    QIcon iconPlace;
    iconPlace.addFile(QStringLiteral(":/icons/resources/place.png"));
    actionPlace->setIcon(iconPlace);
    actionPlace->setStatusTip("Place current design");
    connect(actionPlace, SIGNAL(triggered()), this, SLOT(place()));
    actionPlace->setEnabled(false);

    actionRoute = new QAction("Route", this);
    QIcon iconRoute;
    iconRoute.addFile(QStringLiteral(":/icons/resources/route.png"));
    actionRoute->setIcon(iconRoute);
    actionRoute->setStatusTip("Route current design");
    connect(actionRoute, SIGNAL(triggered()), task, SIGNAL(route()));
    actionRoute->setEnabled(false);

    actionSaveAsc = new QAction("Save ASC", this);
    QIcon iconSaveAsc;
    iconSaveAsc.addFile(QStringLiteral(":/icons/resources/save_asc.png"));
    actionSaveAsc->setIcon(iconSaveAsc);
    actionSaveAsc->setStatusTip("Save ASC file");
    connect(actionSaveAsc, SIGNAL(triggered()), this, SLOT(save_asc()));
    actionSaveAsc->setEnabled(false);

    QToolBar *taskFPGABar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, taskFPGABar);

    taskFPGABar->addAction(actionLoadJSON);
    taskFPGABar->addAction(actionLoadPCF);
    taskFPGABar->addAction(actionPack);
    taskFPGABar->addAction(actionAssignBudget);
    taskFPGABar->addAction(actionPlace);
    taskFPGABar->addAction(actionRoute);
    taskFPGABar->addAction(actionSaveAsc);

    menu_Design->addAction(actionLoadJSON);
    menu_Design->addAction(actionLoadPCF);
    menu_Design->addAction(actionPack);
    menu_Design->addAction(actionAssignBudget);
    menu_Design->addAction(actionPlace);
    menu_Design->addAction(actionRoute);
    menu_Design->addAction(actionSaveAsc);

    actionPlay = new QAction("Play", this);
    QIcon iconPlay;
    iconPlay.addFile(QStringLiteral(":/icons/resources/control_play.png"));
    actionPlay->setIcon(iconPlay);
    actionPlay->setStatusTip("Continue running task");
    connect(actionPlay, SIGNAL(triggered()), task, SLOT(continue_thread()));
    actionPlay->setEnabled(false);

    actionPause = new QAction("Pause", this);
    QIcon iconPause;
    iconPause.addFile(QStringLiteral(":/icons/resources/control_pause.png"));
    actionPause->setIcon(iconPause);
    actionPause->setStatusTip("Pause running task");
    connect(actionPause, SIGNAL(triggered()), task, SLOT(pause_thread()));
    actionPause->setEnabled(false);

    actionStop = new QAction("Stop", this);
    QIcon iconStop;
    iconStop.addFile(QStringLiteral(":/icons/resources/control_stop.png"));
    actionStop->setIcon(iconStop);
    actionStop->setStatusTip("Stop running task");
    connect(actionStop, SIGNAL(triggered()), task, SLOT(terminate_thread()));
    actionStop->setEnabled(false);

    QToolBar *taskToolBar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, taskToolBar);

    taskToolBar->addAction(actionPlay);
    taskToolBar->addAction(actionPause);
    taskToolBar->addAction(actionStop);
}

void MainWindow::new_proj()
{
    disableActions();
    ArchArgs chipArgs;
    chipArgs.type = ArchArgs::HX1K;
    chipArgs.package = "tq144";
    if (ctx) 
        delete ctx;
    ctx = new Context(chipArgs);
    
    Q_EMIT contextChanged(ctx);

    actionLoadJSON->setEnabled(true);
}

void MainWindow::newContext(Context *ctx)
{
    std::string title = "nextpnr-ice40 - " + ctx->getChipName();
    setWindowTitle(title.c_str());
}

void MainWindow::open_proj()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open Project"), QString(), QString("*.proj"));
    if (!fileName.isEmpty()) {
        tabWidget->setCurrentWidget(info);

        std::string fn = fileName.toStdString();
        disableActions();
    }
}

void MainWindow::open_json()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open JSON"), QString(), QString("*.json"));
    if (!fileName.isEmpty()) {
        tabWidget->setCurrentWidget(info);

        std::string fn = fileName.toStdString();
        disableActions();
        timing_driven = false;
        Q_EMIT task->loadfile(fn);
    }
}

void MainWindow::open_pcf()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open PCF"), QString(), QString("*.pcf"));
    if (!fileName.isEmpty()) {
        tabWidget->setCurrentWidget(info);

        std::string fn = fileName.toStdString();
        disableActions();
        Q_EMIT task->loadpcf(fn);
    }
}

bool MainWindow::save_proj() { return false; }

void MainWindow::save_asc()
{
    QString fileName = QFileDialog::getSaveFileName(this, QString("Save ASC"), QString(), QString("*.asc"));
    if (!fileName.isEmpty()) {
        std::string fn = fileName.toStdString();
        disableActions();
        Q_EMIT task->saveasc(fn);
    }
}

void MainWindow::disableActions()
{
    actionLoadJSON->setEnabled(false);
    actionLoadPCF->setEnabled(false);
    actionPack->setEnabled(false);
    actionAssignBudget->setEnabled(false);
    actionPlace->setEnabled(false);
    actionRoute->setEnabled(false);
    actionSaveAsc->setEnabled(false);

    actionPlay->setEnabled(false);
    actionPause->setEnabled(false);
    actionStop->setEnabled(false);

    actionNew->setEnabled(true);
    actionOpen->setEnabled(true);
}

void MainWindow::loadfile_finished(bool status)
{
    disableActions();
    if (status) {
        log("Loading design successful.\n");
        actionLoadPCF->setEnabled(true);
        actionPack->setEnabled(true);
    } else {
        log("Loading design failed.\n");
    }
}

void MainWindow::loadpcf_finished(bool status)
{
    disableActions();
    if (status) {
        log("Loading PCF successful.\n");
        actionPack->setEnabled(true);
    } else {
        log("Loading PCF failed.\n");
    }
}

void MainWindow::saveasc_finished(bool status)
{
    disableActions();
    if (status) {
        log("Saving ASC successful.\n");
    } else {
        log("Saving ASC failed.\n");
    }
}

void MainWindow::pack_finished(bool status)
{
    disableActions();
    if (status) {
        log("Packing design successful.\n");
        actionPlace->setEnabled(true);
        actionAssignBudget->setEnabled(true);
    } else {
        log("Packing design failed.\n");
    }
}

void MainWindow::budget_finish(bool status)
{
    disableActions();
    if (status) {
        log("Assigning timing budget successful.\n");
        actionPlace->setEnabled(true);
    } else {
        log("Assigning timing budget failed.\n");
    }
}

void MainWindow::place_finished(bool status)
{
    disableActions();
    if (status) {
        log("Placing design successful.\n");
        actionRoute->setEnabled(true);
    } else {
        log("Placing design failed.\n");
    }
}
void MainWindow::route_finished(bool status)
{
    disableActions();
    if (status) {
        log("Routing design successful.\n");
        actionSaveAsc->setEnabled(true);
    } else
        log("Routing design failed.\n");
}

void MainWindow::taskCanceled()
{
    log("CANCELED\n");
    disableActions();
}

void MainWindow::taskStarted()
{
    disableActions();
    actionPause->setEnabled(true);
    actionStop->setEnabled(true);
    
    actionNew->setEnabled(false);
    actionOpen->setEnabled(false);
}

void MainWindow::taskPaused()
{
    disableActions();
    actionPlay->setEnabled(true);
    actionStop->setEnabled(true);
    
    actionNew->setEnabled(false);
    actionOpen->setEnabled(false);
}

void MainWindow::budget()
{
    bool ok;
    double freq = QInputDialog::getDouble(this, "Assign timing budget", "Frequency [MHz]:", 50, 0, 250, 2, &ok);
    if (ok) {
        freq *= 1e6;
        timing_driven = true;
        Q_EMIT task->budget(freq);
    }
}

void MainWindow::place() { Q_EMIT task->place(timing_driven); }

NEXTPNR_NAMESPACE_END
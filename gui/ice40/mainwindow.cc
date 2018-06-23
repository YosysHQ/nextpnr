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

MainWindow::MainWindow(Context *_ctx, QWidget *parent)
        : BaseMainWindow(_ctx, parent), timing_driven(false)
{
    initMainResource();

    std::string title = "nextpnr-ice40 - " + ctx->getChipName();
    setWindowTitle(title.c_str());

    task = new TaskManager(_ctx);
    connect(task, SIGNAL(log(std::string)), this, SLOT(writeInfo(std::string)));

    connect(task, SIGNAL(loadfile_finished(bool)), this,
            SLOT(loadfile_finished(bool)));
    connect(task, SIGNAL(pack_finished(bool)), this, SLOT(pack_finished(bool)));
    connect(task, SIGNAL(budget_finish(bool)), this, SLOT(budget_finish(bool)));
    connect(task, SIGNAL(place_finished(bool)), this,
            SLOT(place_finished(bool)));
    connect(task, SIGNAL(route_finished(bool)), this,
            SLOT(route_finished(bool)));

    connect(task, SIGNAL(taskCanceled()), this, SLOT(taskCanceled()));
    connect(task, SIGNAL(taskStarted()), this, SLOT(taskStarted()));
    connect(task, SIGNAL(taskPaused()), this, SLOT(taskPaused()));


    connect(this, SIGNAL(budget(double)), task, SIGNAL(budget(double)));
    connect(this, SIGNAL(place(bool)), task, SIGNAL(place(bool)));

    createMenu();
}

MainWindow::~MainWindow() { delete task; }

void MainWindow::createMenu()
{
    QMenu *menu_Design = new QMenu("&Design", menuBar);
    menuBar->addAction(menu_Design->menuAction());

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

    QToolBar *taskFPGABar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, taskFPGABar);

    taskFPGABar->addAction(actionPack);
    taskFPGABar->addAction(actionAssignBudget);
    taskFPGABar->addAction(actionPlace);
    taskFPGABar->addAction(actionRoute);

    menu_Design->addAction(actionPack);
    menu_Design->addAction(actionAssignBudget);
    menu_Design->addAction(actionPlace);
    menu_Design->addAction(actionRoute);

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

void MainWindow::open()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString(), QString(),
                                                    QString("*.json"));
    if (!fileName.isEmpty()) {
        tabWidget->setCurrentWidget(info);

        std::string fn = fileName.toStdString();
        disableActions();
        timing_driven = false;
        Q_EMIT task->loadfile(fn);
    }
}

bool MainWindow::save() { return false; }

void MainWindow::disableActions()
{
    actionPack->setEnabled(false);
    actionAssignBudget->setEnabled(false);
    actionPlace->setEnabled(false);
    actionRoute->setEnabled(false);

    actionPlay->setEnabled(false);
    actionPause->setEnabled(false);
    actionStop->setEnabled(false);
}

void MainWindow::loadfile_finished(bool status)
{
    disableActions();
    if (status) {
        log("Loading design successful.\n");
        actionPack->setEnabled(true);
    } else {
        log("Loading design failed.\n");
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
    if (status)
        log("Routing design successful.\n");
    else
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
}

void MainWindow::taskPaused()
{
    disableActions();
    actionPlay->setEnabled(true);
    actionStop->setEnabled(true);
}

void MainWindow::budget()
{
    bool ok;
    double freq = QInputDialog::getDouble(this, "Assign timing budget",
                                        "Frequency [MHz]:",
                                        50, 0, 250, 2, &ok);
    if (ok) {
        freq *= 1e6;
        timing_driven = true;
        Q_EMIT budget(freq);
    }

}

void MainWindow::place()
{
    Q_EMIT place(timing_driven);
}


NEXTPNR_NAMESPACE_END
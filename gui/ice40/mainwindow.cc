#include "mainwindow.h"
#include <QAction>
#include <QFileDialog>
#include <QIcon>
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
        : BaseMainWindow(_ctx, parent)
{
    initMainResource();

    std::string title = "nextpnr-ice40 - " + ctx->getChipName();
    setWindowTitle(title.c_str());

    task = new TaskManager(_ctx);
    connect(task, SIGNAL(log(std::string)), this, SLOT(writeInfo(std::string)));

    createMenu();
}

MainWindow::~MainWindow() { delete task; }

void MainWindow::createMenu()
{
    QMenu *menu_Custom = new QMenu("&ICE 40", menuBar);
    menuBar->addAction(menu_Custom->menuAction());

    QAction *actionPlay = new QAction("Play", this);
    QIcon icon1;
    icon1.addFile(QStringLiteral(":/icons/resources/control_play.png"));
    actionPlay->setIcon(icon1);
    actionPlay->setStatusTip("Continue running task");
    connect(actionPlay, SIGNAL(triggered()), task, SLOT(continue_thread()));

    QAction *actionPause = new QAction("Pause", this);
    QIcon icon2;
    icon2.addFile(QStringLiteral(":/icons/resources/control_pause.png"));
    actionPause->setIcon(icon2);
    actionPause->setStatusTip("Pause running task");
    connect(actionPause, SIGNAL(triggered()), task, SLOT(pause_thread()));

    QAction *actionStop = new QAction("Stop", this);
    QIcon icon3;
    icon3.addFile(QStringLiteral(":/icons/resources/control_stop.png"));
    actionStop->setIcon(icon3);
    actionStop->setStatusTip("Stop running task");
    connect(actionStop, SIGNAL(triggered()), task, SLOT(terminate_thread()));

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
        Q_EMIT task->parsejson(fn);
    }
}

bool MainWindow::save() { return false; }

NEXTPNR_NAMESPACE_END
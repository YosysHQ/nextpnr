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

MainWindow::MainWindow(Context *_ctx, QWidget *parent)
        : BaseMainWindow(_ctx, parent)
{
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

    QAction *actionTerminate = new QAction("Terminate", this);
    actionTerminate->setStatusTip("Terminate running task");
    connect(actionTerminate, SIGNAL(triggered()), task,
            SLOT(terminate_thread()));
    menu_Custom->addAction(actionTerminate);
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
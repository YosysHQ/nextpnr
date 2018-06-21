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

    createMenu();

    task = new TaskManager(_ctx);
    connect(task, SIGNAL(log(std::string)), this, SLOT(writeInfo(std::string)));
}

MainWindow::~MainWindow() {}

void MainWindow::createMenu()
{
    QMenu *menu_Custom = new QMenu("&ICE 40", menuBar);
    menuBar->addAction(menu_Custom->menuAction());
}

void MainWindow::open()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString(), QString(),
                                                    QString("*.json"));
    if (!fileName.isEmpty()) {
        tabWidget->setCurrentWidget(info);

        std::string fn = fileName.toStdString();
        task->parsejson(fn);
    }
}
bool MainWindow::save() { return false; }
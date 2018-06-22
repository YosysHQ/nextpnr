#include "mainwindow.h"

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(Context *_ctx, QWidget *parent)
        : BaseMainWindow(_ctx, parent)
{
    initMainResource();

    std::string title = "nextpnr-dummy - " + ctx->getChipName();
    setWindowTitle(title.c_str());

    createMenu();
}

MainWindow::~MainWindow() {}

void MainWindow::createMenu()
{
    QMenu *menu_Custom = new QMenu("&Dummy", menuBar);
    menuBar->addAction(menu_Custom->menuAction());
}

void MainWindow::open() {}

bool MainWindow::save() { return false; }

NEXTPNR_NAMESPACE_END

#include "mainwindow.h"

MainWindow::MainWindow(Context *_ctx, QWidget *parent)
        : BaseMainWindow(_ctx, parent)
{
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

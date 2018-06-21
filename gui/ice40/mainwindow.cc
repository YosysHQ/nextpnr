#include "mainwindow.h"
#include <QAction>
#include <QIcon>
#include "jsonparse.h"
#include "log.h"
#include "pack.h"
#include "pcf.h"
#include "place_sa.h"
#include "route.h"
#include "bitstream.h"
#include "design_utils.h"

MainWindow::MainWindow(Context *_ctx, QWidget *parent)
        : BaseMainWindow(_ctx, parent)
{
    std::string title = "nextpnr-ice40 - " + ctx->getChipName();
    setWindowTitle(title.c_str());

    createMenu();
}

MainWindow::~MainWindow() {}

void MainWindow::createMenu()
{
    QMenu *menu_Custom = new QMenu("&ICE 40", menuBar);
    menuBar->addAction(menu_Custom->menuAction());

}

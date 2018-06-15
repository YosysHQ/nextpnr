#include "mainwindow.h"
#include "designwidget.h"
#include "fpgaviewwidget.h"
#include "pythontab.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(Design *_design, QWidget *parent)
        : QMainWindow(parent), ui(new Ui::MainWindow), design(_design)
{
    ui->setupUi(this);
    std::string title = "nextpnr-ice40 - " + design->chip.getChipName();
    setWindowTitle(title.c_str());

    ui->splitter->addWidget(new FPGAViewWidget());

    DesignWidget *designview = new DesignWidget(design);
    designview->setMinimumWidth(300);
    designview->setMaximumWidth(300);

    connect(designview, SIGNAL(info(std::string)), this,
            SLOT(writeInfo(std::string)));

    ui->splitter_2->addWidget(designview);

    tabWidget = new QTabWidget();
    tabWidget->addTab(new PythonTab(), "Python");
    info = new InfoTab();
    tabWidget->addTab(info, "Info");
    ui->splitter->addWidget(tabWidget);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::writeInfo(std::string text) { info->info(text); }

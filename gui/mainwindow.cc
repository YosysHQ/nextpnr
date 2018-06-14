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

    ui->splitter_2->addWidget(new DesignWidget(design));
    ui->splitter_2->setMinimumWidth(300);
    ui->splitter_2->setMaximumWidth(300);

    tabWidget = new QTabWidget();
    tabWidget->addTab(new PythonTab(), "Python");
    info = new InfoTab();
    tabWidget->addTab(info, "Info");
    ui->splitter->addWidget(tabWidget);
}

MainWindow::~MainWindow() { delete ui; }

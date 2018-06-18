#include "mainwindow.h"
#include <QAction>
#include <QGridLayout>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include "designwidget.h"
#include "fpgaviewwidget.h"
#include "pythontab.h"

MainWindow::MainWindow(Context *_ctx, QWidget *parent)
        : QMainWindow(parent), ctx(_ctx)
{
    std::string title = "nextpnr-ice40 - " + ctx->getChipName();
    setWindowTitle(title.c_str());
    setObjectName(QStringLiteral("MainWindow"));
    resize(1024, 768);

    createMenusAndBars();

    QWidget *centralWidget = new QWidget(this);

    QGridLayout *gridLayout = new QGridLayout(centralWidget);
    gridLayout->setSpacing(6);
    gridLayout->setContentsMargins(11, 11, 11, 11);

    QSplitter *splitter_h = new QSplitter(Qt::Horizontal, centralWidget);
    QSplitter *splitter_v = new QSplitter(Qt::Vertical, splitter_h);
    splitter_h->addWidget(splitter_v);

    gridLayout->addWidget(splitter_h, 0, 0, 1, 1);

    setCentralWidget(centralWidget);

    DesignWidget *designview = new DesignWidget(ctx);
    designview->setMinimumWidth(300);
    designview->setMaximumWidth(300);
    splitter_h->addWidget(designview);

    connect(designview, SIGNAL(info(std::string)), this,
            SLOT(writeInfo(std::string)));

    QTabWidget *tabWidget = new QTabWidget();
    tabWidget->addTab(new PythonTab(), "Python");
    info = new InfoTab();
    tabWidget->addTab(info, "Info");
    splitter_v->addWidget(new FPGAViewWidget());
    splitter_v->addWidget(tabWidget);
}

MainWindow::~MainWindow() {}

void MainWindow::writeInfo(std::string text) { info->info(text); }

void MainWindow::createMenusAndBars()
{
    QAction *actionNew = new QAction("New", this);
    QIcon icon;
    icon.addFile(QStringLiteral(":/icons/resources/new.png"));
    actionNew->setIcon(icon);

    QAction *actionOpen = new QAction("Open", this);
    QIcon icon1;
    icon1.addFile(QStringLiteral(":/icons/resources/open.png"));
    actionOpen->setIcon(icon1);

    QAction *actionSave = new QAction("Save", this);
    QIcon icon2;
    icon2.addFile(QStringLiteral(":/icons/resources/save.png"));
    actionSave->setIcon(icon2);

    QAction *actionSave_as = new QAction("Save as ...", this);

    QAction *actionClose = new QAction("Close", this);

    QAction *actionExit = new QAction("Exit", this);

    QIcon icon3;
    icon3.addFile(QStringLiteral(":/icons/resources/exit.png"));
    actionExit->setIcon(icon3);

    QAction *actionAbout = new QAction("About", this);

    QMenuBar *menuBar = new QMenuBar();
    menuBar->setGeometry(QRect(0, 0, 1024, 27));
    QMenu *menu_File = new QMenu("&File", menuBar);
    QMenu *menu_Help = new QMenu("&Help", menuBar);
    menuBar->addAction(menu_File->menuAction());
    menuBar->addAction(menu_Help->menuAction());
    setMenuBar(menuBar);

    QToolBar *mainToolBar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, mainToolBar);

    QStatusBar *statusBar = new QStatusBar();
    setStatusBar(statusBar);

    menu_File->addAction(actionNew);
    menu_File->addAction(actionOpen);
    menu_File->addAction(actionSave);
    menu_File->addAction(actionSave_as);
    menu_File->addAction(actionClose);
    menu_File->addSeparator();
    menu_File->addAction(actionExit);
    menu_Help->addAction(actionAbout);

    mainToolBar->addAction(actionNew);
    mainToolBar->addAction(actionOpen);
    mainToolBar->addAction(actionSave);

    connect(actionExit, SIGNAL(triggered()), this, SLOT(close()));
}

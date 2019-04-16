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
#include <fstream>
#include "log.h"

#include <QFileDialog>

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(std::unique_ptr<Context> context, ArchArgs args, QWidget *parent)
        : BaseMainWindow(std::move(context), args, parent)
{
    initMainResource();

    std::string title = "nextpnr-leuctra - [EMPTY]";
    setWindowTitle(title.c_str());

    connect(this, &BaseMainWindow::contextChanged, this, &MainWindow::newContext);

    createMenu();
}

MainWindow::~MainWindow() {}

void MainWindow::newContext(Context *ctx)
{
    std::string title = "nextpnr-leuctra - " + ctx->getChipName();
    setWindowTitle(title.c_str());
}

void MainWindow::createMenu() {
    // Add arch specific actions
    actionLoadUCF = new QAction("Open UCF", this);
    actionLoadUCF->setIcon(QIcon(":/icons/resources/open_ucf.png"));
    actionLoadUCF->setStatusTip("Open UCF file");
    actionLoadUCF->setEnabled(false);
    connect(actionLoadUCF, &QAction::triggered, this, &MainWindow::open_ucf);

    // Add actions in menus
    mainActionBar->addSeparator();
    mainActionBar->addAction(actionLoadUCF);

    menuDesign->addSeparator();
    menuDesign->addAction(actionLoadUCF);
}

void MainWindow::new_proj() {}

void MainWindow::open_ucf()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open UCF"), QString(), QString("*.ucf"));
    if (!fileName.isEmpty()) {
        std::ifstream in(fileName.toStdString());
        if (ctx->applyUCF(fileName.toStdString(), in)) {
            log("Loading UCF successful.\n");
            actionPack->setEnabled(true);
            actionLoadUCF->setEnabled(false);
        } else {
            actionLoadUCF->setEnabled(true);
            log("Loading UCF failed.\n");
        }
    }
}

NEXTPNR_NAMESPACE_END

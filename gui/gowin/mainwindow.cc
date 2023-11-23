/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <micko@yosyshq.com>
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

#include <QFileDialog>
#include <QMessageBox>
#include <cstdlib>

#include "cst.h"

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(std::unique_ptr<Context> context, CommandHandler *handler, QWidget *parent)
        : BaseMainWindow(std::move(context), handler, parent)
{
    initMainResource();
    std::string title = "nextpnr-gowin - [EMPTY]";
    setWindowTitle(title.c_str());
    connect(this, &BaseMainWindow::contextChanged, this, &MainWindow::newContext);
    createMenu();
}

MainWindow::~MainWindow() {}

void MainWindow::newContext(Context *ctx)
{
    std::string title = "nextpnr-gowin - " + ctx->getChipName();
    setWindowTitle(title.c_str());
}

void MainWindow::load_cst(std::string filename)
{
    disableActions();
    std::ifstream f(filename);
    if (read_cst(ctx.get(), f)) {
        log("Loading CST successful.\n");
        actionPack->setEnabled(true);
    } else {
        actionLoadCST->setEnabled(true);
        log("Loading CST failed.\n");
    }
}

void MainWindow::createMenu()
{
    actionLoadCST = new QAction("Open CST", this);
    actionLoadCST->setIcon(QIcon(":/icons/resources/open_cst.png"));
    actionLoadCST->setStatusTip("Open CST file");
    actionLoadCST->setEnabled(false);
    connect(actionLoadCST, &QAction::triggered, this, &MainWindow::open_cst);

    // Add actions in menus
    mainActionBar->addSeparator();
    mainActionBar->addAction(actionLoadCST);

    menuDesign->addSeparator();
    menuDesign->addAction(actionLoadCST);
}

void MainWindow::new_proj() {}

void MainWindow::open_cst()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open CST"), QString(), QString("*.cst"));
    if (!fileName.isEmpty()) {
        load_cst(fileName.toStdString());
    }
}

void MainWindow::onDisableActions() { actionLoadCST->setEnabled(false); }

void MainWindow::onUpdateActions()
{
    if (ctx->settings.find(ctx->id("synth")) != ctx->settings.end()) {
        actionLoadCST->setEnabled(true);
    }
    if (ctx->settings.find(ctx->id("cst")) != ctx->settings.end()) {
        actionLoadCST->setEnabled(false);
    }
    if (ctx->settings.find(ctx->id("pack")) != ctx->settings.end()) {
        actionLoadCST->setEnabled(false);
    }
}
NEXTPNR_NAMESPACE_END

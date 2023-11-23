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
#include <fstream>
#include "bitstream.h"
#include "log.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(std::unique_ptr<Context> context, CommandHandler *handler, QWidget *parent)
        : BaseMainWindow(std::move(context), handler, parent)
{
    initMainResource();

    std::string title = "nextpnr-ecp5 - [EMPTY]";
    setWindowTitle(title.c_str());

    connect(this, &BaseMainWindow::contextChanged, this, &MainWindow::newContext);

    createMenu();
}

MainWindow::~MainWindow() {}

void MainWindow::newContext(Context *ctx)
{
    std::string title = "nextpnr-ecp5 - " + ctx->getChipName() + " ( " + ctx->archArgs().package + " )";
    setWindowTitle(title.c_str());
}

void MainWindow::createMenu()
{
    // Add arch specific actions
    actionLoadLPF = new QAction("Open LPF", this);
    actionLoadLPF->setIcon(QIcon(":/icons/resources/open_lpf.png"));
    actionLoadLPF->setStatusTip("Open LPF file");
    actionLoadLPF->setEnabled(false);
    connect(actionLoadLPF, &QAction::triggered, this, &MainWindow::open_lpf);

    actionSaveConfig = new QAction("Save Bitstream", this);
    actionSaveConfig->setIcon(QIcon(":/icons/resources/save_config.png"));
    actionSaveConfig->setStatusTip("Save Bitstream config file");
    actionSaveConfig->setEnabled(false);
    connect(actionSaveConfig, &QAction::triggered, this, &MainWindow::save_config);

    // Add actions in menus
    mainActionBar->addSeparator();
    mainActionBar->addAction(actionLoadLPF);
    mainActionBar->addAction(actionSaveConfig);

    menuDesign->addSeparator();
    menuDesign->addAction(actionLoadLPF);
    menuDesign->addAction(actionSaveConfig);
}

void MainWindow::new_proj()
{
    QMap<QString, int> arch;
    if (Arch::is_available(ArchArgs::LFE5U_25F))
        arch.insert("Lattice ECP5 LFE5U-25F", ArchArgs::LFE5U_25F);
    if (Arch::is_available(ArchArgs::LFE5U_45F))
        arch.insert("Lattice ECP5 LFE5U-45F", ArchArgs::LFE5U_45F);
    if (Arch::is_available(ArchArgs::LFE5U_85F))
        arch.insert("Lattice ECP5 LFE5U-85F", ArchArgs::LFE5U_85F);
    if (Arch::is_available(ArchArgs::LFE5UM_25F))
        arch.insert("Lattice ECP5 LFE5UM-25F", ArchArgs::LFE5UM_25F);
    if (Arch::is_available(ArchArgs::LFE5UM_45F))
        arch.insert("Lattice ECP5 LFE5UM-45F", ArchArgs::LFE5UM_45F);
    if (Arch::is_available(ArchArgs::LFE5UM_85F))
        arch.insert("Lattice ECP5 LFE5UM-85F", ArchArgs::LFE5UM_85F);
    if (Arch::is_available(ArchArgs::LFE5UM5G_25F))
        arch.insert("Lattice ECP5 LFE5UM5G-25F", ArchArgs::LFE5UM5G_25F);
    if (Arch::is_available(ArchArgs::LFE5UM5G_45F))
        arch.insert("Lattice ECP5 LFE5UM5G-45F", ArchArgs::LFE5UM5G_45F);
    if (Arch::is_available(ArchArgs::LFE5UM5G_85F))
        arch.insert("Lattice ECP5 LFE5UM5G-85F", ArchArgs::LFE5UM5G_85F);

    bool ok;
    QString item = QInputDialog::getItem(this, "Select new context", "Chip:", arch.keys(), 0, false, &ok);
    if (ok && !item.isEmpty()) {
        ArchArgs chipArgs;
        chipArgs.type = (ArchArgs::ArchArgsTypes)arch.value(item);

        QStringList packages;
        for (auto package : Arch::get_supported_packages(chipArgs.type))
            packages.append(QLatin1String(package.data(), package.size()));
        QString package = QInputDialog::getItem(this, "Select package", "Package:", packages, 0, false, &ok);

        if (ok && !item.isEmpty()) {
            handler->clear();
            currentProj = "";
            disableActions();
            chipArgs.package = package.toStdString().c_str();
            ctx = std::unique_ptr<Context>(new Context(chipArgs));
            actionLoadJSON->setEnabled(true);

            Q_EMIT contextChanged(ctx.get());
        }
    }
}

void MainWindow::open_lpf()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open LPF"), QString(), QString("*.lpf"));
    if (!fileName.isEmpty()) {
        std::ifstream in(fileName.toStdString());
        if (ctx->apply_lpf(fileName.toStdString(), in)) {
            log("Loading LPF successful.\n");
            actionPack->setEnabled(true);
            actionLoadLPF->setEnabled(false);
        } else {
            actionLoadLPF->setEnabled(true);
            log("Loading LPF failed.\n");
        }
    }
}

void MainWindow::save_config()
{
    QString fileName = QFileDialog::getSaveFileName(this, QString("Save Bitstream"), QString(), QString("*.config"));
    if (!fileName.isEmpty()) {
        std::string fn = fileName.toStdString();
        disableActions();
        write_bitstream(ctx.get(), "", fileName.toStdString());
        log("Saving Bitstream successful.\n");
    }
}

void MainWindow::onDisableActions()
{
    actionLoadLPF->setEnabled(false);
    actionSaveConfig->setEnabled(false);
}

void MainWindow::onUpdateActions()
{
    if (ctx->settings.find(ctx->id("pack")) == ctx->settings.end())
        actionLoadLPF->setEnabled(true);
    if (ctx->settings.find(ctx->id("route")) != ctx->settings.end())
        actionSaveConfig->setEnabled(true);
}

NEXTPNR_NAMESPACE_END

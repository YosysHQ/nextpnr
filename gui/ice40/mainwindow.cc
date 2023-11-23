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
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <fstream>
#include "bitstream.h"
#include "design_utils.h"
#include "log.h"
#include "pcf.h"

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(std::unique_ptr<Context> context, CommandHandler *handler, QWidget *parent)
        : BaseMainWindow(std::move(context), handler, parent)
{
    initMainResource();

    std::string title = "nextpnr-ice40 - [EMPTY]";
    setWindowTitle(title.c_str());

    connect(this, &BaseMainWindow::contextChanged, this, &MainWindow::newContext);

    createMenu();
}

MainWindow::~MainWindow() {}

void MainWindow::createMenu()
{
    // Add arch specific actions
    actionLoadPCF = new QAction("Open PCF", this);
    actionLoadPCF->setIcon(QIcon(":/icons/resources/open_pcf.png"));
    actionLoadPCF->setStatusTip("Open PCF file");
    actionLoadPCF->setEnabled(false);
    connect(actionLoadPCF, &QAction::triggered, this, &MainWindow::open_pcf);

    actionSaveAsc = new QAction("Save ASC", this);
    actionSaveAsc->setIcon(QIcon(":/icons/resources/save_asc.png"));
    actionSaveAsc->setStatusTip("Save ASC file");
    actionSaveAsc->setEnabled(false);
    connect(actionSaveAsc, &QAction::triggered, this, &MainWindow::save_asc);

    // Add actions in menus
    mainActionBar->addSeparator();
    mainActionBar->addAction(actionLoadPCF);
    mainActionBar->addAction(actionSaveAsc);

    menuDesign->addSeparator();
    menuDesign->addAction(actionLoadPCF);
    menuDesign->addAction(actionSaveAsc);
}

void MainWindow::new_proj()
{
    QMap<QString, int> arch;
    if (Arch::is_available(ArchArgs::LP384))
        arch.insert("Lattice iCE40LP384", ArchArgs::LP384);
    if (Arch::is_available(ArchArgs::LP1K))
        arch.insert("Lattice iCE40LP1K", ArchArgs::LP1K);
    if (Arch::is_available(ArchArgs::HX1K))
        arch.insert("Lattice iCE40HX1K", ArchArgs::HX1K);
    if (Arch::is_available(ArchArgs::U1K))
        arch.insert("Lattice iCE5LP1K", ArchArgs::U1K);
    if (Arch::is_available(ArchArgs::U2K))
        arch.insert("Lattice iCE5LP2K", ArchArgs::U2K);
    if (Arch::is_available(ArchArgs::U4K))
        arch.insert("Lattice iCE5LP4K", ArchArgs::U4K);
    if (Arch::is_available(ArchArgs::UP3K))
        arch.insert("Lattice iCE40UP3K", ArchArgs::UP3K);
    if (Arch::is_available(ArchArgs::UP5K))
        arch.insert("Lattice iCE40UP5K", ArchArgs::UP5K);
    if (Arch::is_available(ArchArgs::LP4K))
        arch.insert("Lattice iCE40LP4K", ArchArgs::LP4K);
    if (Arch::is_available(ArchArgs::LP8K))
        arch.insert("Lattice iCE40LP8K", ArchArgs::LP8K);
    if (Arch::is_available(ArchArgs::HX4K))
        arch.insert("Lattice iCE40HX4K", ArchArgs::HX4K);
    if (Arch::is_available(ArchArgs::HX8K))
        arch.insert("Lattice iCE40HX8K", ArchArgs::HX8K);

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

void MainWindow::load_pcf(std::string filename)
{
    disableActions();
    std::ifstream f(filename);
    if (apply_pcf(ctx.get(), filename, f)) {
        log("Loading PCF successful.\n");
        actionPack->setEnabled(true);
    } else {
        actionLoadPCF->setEnabled(true);
        log("Loading PCF failed.\n");
    }
}

void MainWindow::newContext(Context *ctx)
{
    std::string title = "nextpnr-ice40 - " + ctx->getChipName() + " ( " + ctx->archArgs().package + " )";
    setWindowTitle(title.c_str());
}

void MainWindow::open_pcf()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open PCF"), QString(), QString("*.pcf"));
    if (!fileName.isEmpty()) {
        load_pcf(fileName.toStdString());
    }
}

void MainWindow::save_asc()
{
    QString fileName = QFileDialog::getSaveFileName(this, QString("Save ASC"), QString(), QString("*.asc"));
    if (!fileName.isEmpty()) {
        std::string fn = fileName.toStdString();
        disableActions();
        std::ofstream f(fn);
        write_asc(ctx.get(), f);
        log("Saving ASC successful.\n");
    }
}

void MainWindow::onDisableActions()
{
    actionLoadPCF->setEnabled(false);
    actionSaveAsc->setEnabled(false);
}

void MainWindow::onUpdateActions()
{
    if (ctx->settings.find(ctx->id("pack")) == ctx->settings.end())
        actionLoadPCF->setEnabled(true);
    if (ctx->settings.find(ctx->id("route")) != ctx->settings.end())
        actionSaveAsc->setEnabled(true);
}

NEXTPNR_NAMESPACE_END

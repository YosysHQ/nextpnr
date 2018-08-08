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
#include "bitstream.h"
#include "log.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(std::unique_ptr<Context> context, ArchArgs args, QWidget *parent)
        : BaseMainWindow(std::move(context), args, parent)
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
    std::string title = "nextpnr-generic - " + ctx->getChipName() + " ( " + chipArgs.package + " )";
    setWindowTitle(title.c_str());
}

void MainWindow::createMenu()
{
    // Add arch specific actions
    actionLoadBase = new QAction("Open Base Config", this);
    actionLoadBase->setIcon(QIcon(":/icons/resources/open_base.png"));
    actionLoadBase->setStatusTip("Open Base Config file");
    actionLoadBase->setEnabled(false);
    connect(actionLoadBase, &QAction::triggered, this, &MainWindow::open_base);

    actionSaveConfig = new QAction("Save Bitstream", this);
    actionSaveConfig->setIcon(QIcon(":/icons/resources/save_config.png"));
    actionSaveConfig->setStatusTip("Save Bitstream config file");
    actionSaveConfig->setEnabled(false);
    connect(actionSaveConfig, &QAction::triggered, this, &MainWindow::save_config);

    // Add actions in menus
    mainActionBar->addSeparator();
    mainActionBar->addAction(actionLoadBase);
    mainActionBar->addAction(actionSaveConfig);

    menuDesign->addSeparator();
    menuDesign->addAction(actionLoadBase);
    menuDesign->addAction(actionSaveConfig);
}

static const ChipInfoPOD *get_chip_info(const RelPtr<ChipInfoPOD> *ptr) { return ptr->get(); }

static QStringList getSupportedPackages(ArchArgs::ArchArgsTypes chip)
{
    QStringList packages;
    const ChipInfoPOD *chip_info;
    if (chip == ArchArgs::LFE5U_25F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_25k));
    } else if (chip == ArchArgs::LFE5U_45F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_45k));
    } else if (chip == ArchArgs::LFE5U_85F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_85k));
    } else {
        log_error("Unsupported ECP5 chip type.\n");
    }

    for (int i = 0; i < chip_info->num_packages; i++) {
        packages << chip_info->package_info[i].name.get();
    }
    return packages;
}

void MainWindow::new_proj()
{
    QMap<QString, int> arch;
    arch.insert("Lattice ECP5 25K", ArchArgs::LFE5U_25F);
    arch.insert("Lattice ECP5 45K", ArchArgs::LFE5U_45F);
    arch.insert("Lattice ECP5 85K", ArchArgs::LFE5U_85F);
    bool ok;
    QString item = QInputDialog::getItem(this, "Select new context", "Chip:", arch.keys(), 0, false, &ok);
    if (ok && !item.isEmpty()) {

        chipArgs.type = (ArchArgs::ArchArgsTypes)arch.value(item);

        QString package = QInputDialog::getItem(this, "Select package", "Package:", getSupportedPackages(chipArgs.type),
                                                0, false, &ok);

        if (ok && !item.isEmpty()) {
            currentProj = "";
            disableActions();
            chipArgs.package = package.toStdString().c_str();
            ctx = std::unique_ptr<Context>(new Context(chipArgs));
            actionLoadJSON->setEnabled(true);

            Q_EMIT contextChanged(ctx.get());
        }
    }
}

void MainWindow::load_base_config(std::string filename)
{
    disableActions();
    currentBaseConfig = filename;
    actionSaveConfig->setEnabled(true);
}

void MainWindow::open_base()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open Base Config"), QString(), QString("*.config"));
    if (!fileName.isEmpty()) {
        load_base_config(fileName.toStdString());
    }
}

void MainWindow::save_config()
{
    QString fileName = QFileDialog::getSaveFileName(this, QString("Save Bitstream"), QString(), QString("*.config"));
    if (!fileName.isEmpty()) {
        std::string fn = fileName.toStdString();
        disableActions();
        write_bitstream(ctx.get(), currentBaseConfig, fileName.toStdString());
        log("Saving Bitstream successful.\n");
    }
}

void MainWindow::onDisableActions()
{
    actionLoadBase->setEnabled(false);
    actionSaveConfig->setEnabled(false);
}

void MainWindow::onRouteFinished() { actionLoadBase->setEnabled(true); }

NEXTPNR_NAMESPACE_END

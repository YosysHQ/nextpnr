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
#include <QSet>
#include <fstream>
#include "design_utils.h"
#include "log.h"

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(std::unique_ptr<Context> context, CommandHandler *handler, QWidget *parent)
        : BaseMainWindow(std::move(context), handler, parent)
{
    initMainResource();

    std::string title = "nextpnr-xo2 - [EMPTY]";
    setWindowTitle(title.c_str());

    connect(this, &BaseMainWindow::contextChanged, this, &MainWindow::newContext);

    createMenu();
}

MainWindow::~MainWindow() {}

void MainWindow::createMenu()
{
    // Add arch specific actions

    // Add actions in menus
}

void MainWindow::new_proj()
{
    QMap<QString, int> arch;
    if (Arch::is_available(ArchArgs::LCMXO2_256HC))
        arch.insert("LCMXO2-256HC", ArchArgs::LCMXO2_256HC);
    if (Arch::is_available(ArchArgs::LCMXO2_640HC))
        arch.insert("LCMXO2-640HC", ArchArgs::LCMXO2_640HC);
    if (Arch::is_available(ArchArgs::LCMXO2_1200HC))
        arch.insert("LCMXO2-1200HC", ArchArgs::LCMXO2_1200HC);
    if (Arch::is_available(ArchArgs::LCMXO2_2000HC))
        arch.insert("LCMXO2-2000HC", ArchArgs::LCMXO2_2000HC);
    if (Arch::is_available(ArchArgs::LCMXO2_4000HC))
        arch.insert("LCMXO2-4000HC", ArchArgs::LCMXO2_4000HC);
    if (Arch::is_available(ArchArgs::LCMXO2_7000HC))
        arch.insert("LCMXO2-7000HC", ArchArgs::LCMXO2_7000HC);
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

void MainWindow::newContext(Context *ctx)
{
    std::string title = "nextpnr-xo2 - " + ctx->getChipName();
    setWindowTitle(title.c_str());
}

void MainWindow::onDisableActions() {}

void MainWindow::onUpdateActions() {}

NEXTPNR_NAMESPACE_END

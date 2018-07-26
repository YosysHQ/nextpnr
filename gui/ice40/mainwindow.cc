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
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include "bitstream.h"
#include "design_utils.h"
#include "jsonparse.h"
#include "log.h"
#include "pcf.h"

static void initMainResource() { Q_INIT_RESOURCE(nextpnr); }

NEXTPNR_NAMESPACE_BEGIN

MainWindow::MainWindow(std::unique_ptr<Context> context, ArchArgs args, QWidget *parent)
        : BaseMainWindow(std::move(context), parent), timing_driven(false), chipArgs(args)
{
    initMainResource();

    std::string title = "nextpnr-ice40 - [EMPTY]";
    setWindowTitle(title.c_str());

    task = new TaskManager();
    connect(task, SIGNAL(log(std::string)), this, SLOT(writeInfo(std::string)));

    connect(task, SIGNAL(loadfile_finished(bool)), this, SLOT(loadfile_finished(bool)));
    connect(task, SIGNAL(loadpcf_finished(bool)), this, SLOT(loadpcf_finished(bool)));
    connect(task, SIGNAL(saveasc_finished(bool)), this, SLOT(saveasc_finished(bool)));
    connect(task, SIGNAL(pack_finished(bool)), this, SLOT(pack_finished(bool)));
    connect(task, SIGNAL(budget_finish(bool)), this, SLOT(budget_finish(bool)));
    connect(task, SIGNAL(place_finished(bool)), this, SLOT(place_finished(bool)));
    connect(task, SIGNAL(route_finished(bool)), this, SLOT(route_finished(bool)));

    connect(task, SIGNAL(taskCanceled()), this, SLOT(taskCanceled()));
    connect(task, SIGNAL(taskStarted()), this, SLOT(taskStarted()));
    connect(task, SIGNAL(taskPaused()), this, SLOT(taskPaused()));

    connect(this, SIGNAL(contextChanged(Context *)), this, SLOT(newContext(Context *)));
    connect(this, SIGNAL(contextChanged(Context *)), task, SIGNAL(contextChanged(Context *)));

    createMenu();

    Q_EMIT contextChanged(ctx.get());
}

MainWindow::~MainWindow() { delete task; }

void MainWindow::createMenu()
{
    QMenu *menu_Design = new QMenu("&Design", menuBar);
    menuBar->addAction(menu_Design->menuAction());

    actionLoadJSON = new QAction("Open JSON", this);
    actionLoadJSON->setIcon(QIcon(":/icons/resources/open_json.png"));
    actionLoadJSON->setStatusTip("Open an existing JSON file");
    actionLoadJSON->setEnabled(true);
    connect(actionLoadJSON, SIGNAL(triggered()), this, SLOT(open_json()));

    actionLoadPCF = new QAction("Open PCF", this);
    actionLoadPCF->setIcon(QIcon(":/icons/resources/open_pcf.png"));
    actionLoadPCF->setStatusTip("Open PCF file");
    actionLoadPCF->setEnabled(false);
    connect(actionLoadPCF, SIGNAL(triggered()), this, SLOT(open_pcf()));

    actionPack = new QAction("Pack", this);
    actionPack->setIcon(QIcon(":/icons/resources/pack.png"));
    actionPack->setStatusTip("Pack current design");
    actionPack->setEnabled(false);
    connect(actionPack, SIGNAL(triggered()), task, SIGNAL(pack()));

    actionAssignBudget = new QAction("Assign Budget", this);
    actionAssignBudget->setIcon(QIcon(":/icons/resources/time_add.png"));
    actionAssignBudget->setStatusTip("Assign time budget for current design");
    actionAssignBudget->setEnabled(false);
    connect(actionAssignBudget, SIGNAL(triggered()), this, SLOT(budget()));

    actionPlace = new QAction("Place", this);
    actionPlace->setIcon(QIcon(":/icons/resources/place.png"));
    actionPlace->setStatusTip("Place current design");
    actionPlace->setEnabled(false);
    connect(actionPlace, SIGNAL(triggered()), this, SLOT(place()));

    actionRoute = new QAction("Route", this);
    actionRoute->setIcon(QIcon(":/icons/resources/route.png"));
    actionRoute->setStatusTip("Route current design");
    actionRoute->setEnabled(false);
    connect(actionRoute, SIGNAL(triggered()), task, SIGNAL(route()));

    actionSaveAsc = new QAction("Save ASC", this);
    actionSaveAsc->setIcon(QIcon(":/icons/resources/save_asc.png"));
    actionSaveAsc->setStatusTip("Save ASC file");
    actionSaveAsc->setEnabled(false);
    connect(actionSaveAsc, SIGNAL(triggered()), this, SLOT(save_asc()));

    QToolBar *taskFPGABar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, taskFPGABar);

    taskFPGABar->addAction(actionLoadJSON);
    taskFPGABar->addAction(actionLoadPCF);
    taskFPGABar->addAction(actionPack);
    taskFPGABar->addAction(actionAssignBudget);
    taskFPGABar->addAction(actionPlace);
    taskFPGABar->addAction(actionRoute);
    taskFPGABar->addAction(actionSaveAsc);

    menu_Design->addAction(actionLoadJSON);
    menu_Design->addAction(actionLoadPCF);
    menu_Design->addAction(actionPack);
    menu_Design->addAction(actionAssignBudget);
    menu_Design->addAction(actionPlace);
    menu_Design->addAction(actionRoute);
    menu_Design->addAction(actionSaveAsc);

    actionPlay = new QAction("Play", this);
    actionPlay->setIcon(QIcon(":/icons/resources/control_play.png"));
    actionPlay->setStatusTip("Continue running task");
    actionPlay->setEnabled(false);
    connect(actionPlay, SIGNAL(triggered()), task, SLOT(continue_thread()));

    actionPause = new QAction("Pause", this);
    actionPause->setIcon(QIcon(":/icons/resources/control_pause.png"));
    actionPause->setStatusTip("Pause running task");
    actionPause->setEnabled(false);
    connect(actionPause, SIGNAL(triggered()), task, SLOT(pause_thread()));

    actionStop = new QAction("Stop", this);
    actionStop->setIcon(QIcon(":/icons/resources/control_stop.png"));
    actionStop->setStatusTip("Stop running task");
    actionStop->setEnabled(false);
    connect(actionStop, SIGNAL(triggered()), task, SLOT(terminate_thread()));

    QToolBar *taskToolBar = new QToolBar();
    addToolBar(Qt::TopToolBarArea, taskToolBar);

    taskToolBar->addAction(actionPlay);
    taskToolBar->addAction(actionPause);
    taskToolBar->addAction(actionStop);

    createGraphicsBar();
}

#if defined(_MSC_VER)
void load_chipdb();
#endif

static const ChipInfoPOD *get_chip_info(const RelPtr<ChipInfoPOD> *ptr) { return ptr->get(); }

QStringList getSupportedPackages(ArchArgs::ArchArgsTypes chip)
{
    QStringList packages;
#if defined(_MSC_VER)
    load_chipdb();
#endif
    const ChipInfoPOD *chip_info;
#ifdef ICE40_HX1K_ONLY
    if (chip == ArchArgs::HX1K) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_1k));
    } else {
        log_error("Unsupported iCE40 chip type.\n");
    }
#else
    if (chip == ArchArgs::LP384) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_384));
    } else if (chip == ArchArgs::LP1K || chip == ArchArgs::HX1K) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_1k));
    } else if (chip == ArchArgs::UP5K) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_5k));
    } else if (chip == ArchArgs::LP8K || chip == ArchArgs::HX8K) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_8k));
    } else {
        log_error("Unsupported iCE40 chip type.\n");
    }
#endif

    for (int i = 0; i < chip_info->num_packages; i++) {
        packages << chip_info->packages_data[i].name.get();
    }
    return packages;
}

void MainWindow::new_proj()
{
    QMap<QString, int> arch;
#ifdef ICE40_HX1K_ONLY
    arch.insert("Lattice HX1K", ArchArgs::HX1K);
#else
    arch.insert("Lattice LP384", ArchArgs::LP384);
    arch.insert("Lattice LP1K", ArchArgs::LP1K);
    arch.insert("Lattice HX1K", ArchArgs::HX1K);
    arch.insert("Lattice UP5K", ArchArgs::UP5K);
    arch.insert("Lattice LP8K", ArchArgs::LP8K);
    arch.insert("Lattice HX8K", ArchArgs::HX8K);
#endif
    bool ok;
    QString item = QInputDialog::getItem(this, "Select new context", "Chip:", arch.keys(), 0, false, &ok);
    if (ok && !item.isEmpty()) {

        chipArgs.type = (ArchArgs::ArchArgsTypes)arch.value(item);

        QString package = QInputDialog::getItem(this, "Select package", "Package:", getSupportedPackages(chipArgs.type),
                                                0, false, &ok);

        if (ok && !item.isEmpty()) {
            currentProj = "";
            currentJson = "";
            currentPCF = "";
            disableActions();
            chipArgs.package = package.toStdString().c_str();
            ctx = std::unique_ptr<Context>(new Context(chipArgs));
            actionLoadJSON->setEnabled(true);

            Q_EMIT contextChanged(ctx.get());
        }
    }
}

void MainWindow::load_json(std::string filename, std::string pcf)
{
    disableActions();
    currentJson = filename;
    currentPCF = pcf;
    Q_EMIT task->loadfile(filename);
}

void MainWindow::load_pcf(std::string filename)
{
    disableActions();
    currentPCF = filename;
    Q_EMIT task->loadpcf(filename);
}

void MainWindow::newContext(Context *ctx)
{
    std::string title = "nextpnr-ice40 - " + ctx->getChipName() + " ( " + chipArgs.package + " )";
    setWindowTitle(title.c_str());
}

void MainWindow::open_proj()
{
    QMap<std::string, int> arch;
#ifdef ICE40_HX1K_ONLY
    arch.insert("hx1k", ArchArgs::HX1K);
#else
    arch.insert("lp384", ArchArgs::LP384);
    arch.insert("lp1k", ArchArgs::LP1K);
    arch.insert("hx1k", ArchArgs::HX1K);
    arch.insert("up5k", ArchArgs::UP5K);
    arch.insert("lp8k", ArchArgs::LP8K);
    arch.insert("hx8k", ArchArgs::HX8K);
#endif

    QString fileName = QFileDialog::getOpenFileName(this, QString("Open Project"), QString(), QString("*.proj"));
    if (!fileName.isEmpty()) {
        try {
            namespace pt = boost::property_tree;

            std::string fn = fileName.toStdString();
            currentProj = fn;
            disableActions();

            pt::ptree root;
            std::string filename = fileName.toStdString();
            pt::read_json(filename, root);
            log_info("Loading project %s...\n", filename.c_str());
            log_break();

            int version = root.get<int>("project.version");
            if (version != 1)
                log_error("Wrong project format version.\n");

            std::string arch_name = root.get<std::string>("project.arch.name");
            if (arch_name != "ice40")
                log_error("Unsuported project architecture.\n");

            std::string arch_type = root.get<std::string>("project.arch.type");
            std::string arch_package = root.get<std::string>("project.arch.package");

            chipArgs.type = (ArchArgs::ArchArgsTypes)arch.value(arch_type);
            chipArgs.package = arch_package;
            ctx = std::unique_ptr<Context>(new Context(chipArgs));
            Q_EMIT contextChanged(ctx.get());

            QFileInfo fi(fileName);
            QDir::setCurrent(fi.absoluteDir().absolutePath());
            log_info("Setting current dir to %s...\n", fi.absoluteDir().absolutePath().toStdString().c_str());
            log_info("Loading project %s...\n", filename.c_str());
            log_info("Context changed to %s (%s)\n", arch_type.c_str(), arch_package.c_str());

            auto project = root.get_child("project");
            std::string json;
            std::string pcf;
            if (project.count("input")) {
                auto input = project.get_child("input");
                if (input.count("json"))
                    json = input.get<std::string>("json");
                if (input.count("pcf"))
                    pcf = input.get<std::string>("pcf");
            }

            if (!(QFileInfo::exists(json.c_str()) && QFileInfo(json.c_str()).isFile())) {
                log_error("Json file does not exist.\n");
            }
            if (!pcf.empty()) {
                if (!(QFileInfo::exists(pcf.c_str()) && QFileInfo(pcf.c_str()).isFile())) {
                    log_error("PCF file does not exist.\n");
                }
            }

            log_info("Loading json: %s...\n", json.c_str());
            load_json(json, pcf);
        } catch (log_execution_error_exception) {
        }
    }
}

void MainWindow::open_json()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open JSON"), QString(), QString("*.json"));
    if (!fileName.isEmpty()) {
        load_json(fileName.toStdString(), "");
    }
}

void MainWindow::open_pcf()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString("Open PCF"), QString(), QString("*.pcf"));
    if (!fileName.isEmpty()) {
        load_pcf(fileName.toStdString());
    }
}

bool MainWindow::save_proj()
{
    if (currentProj.empty()) {
        QString fileName = QFileDialog::getSaveFileName(this, QString("Save Project"), QString(), QString("*.proj"));
        if (fileName.isEmpty())
            return false;
        currentProj = fileName.toStdString();
    }
    if (!currentProj.empty()) {
        namespace pt = boost::property_tree;
        QFileInfo fi(currentProj.c_str());
        QDir dir(fi.absoluteDir().absolutePath());
        std::ofstream f(currentProj);
        pt::ptree root;
        root.put("project.version", 1);
        root.put("project.name", fi.baseName().toStdString());
        root.put("project.arch.name", ctx->archId().c_str(ctx.get()));
        root.put("project.arch.type", ctx->archArgsToId(chipArgs).c_str(ctx.get()));
        root.put("project.arch.package", chipArgs.package);
        if (!currentJson.empty())
            root.put("project.input.json", dir.relativeFilePath(currentJson.c_str()).toStdString());
        if (!currentPCF.empty())
            root.put("project.input.pcf", dir.relativeFilePath(currentPCF.c_str()).toStdString());
        pt::write_json(f, root);
        log_info("Project %s saved...\n", fi.baseName().toStdString().c_str());
        return true;
    }
    return false;
}

void MainWindow::save_asc()
{
    QString fileName = QFileDialog::getSaveFileName(this, QString("Save ASC"), QString(), QString("*.asc"));
    if (!fileName.isEmpty()) {
        std::string fn = fileName.toStdString();
        disableActions();
        Q_EMIT task->saveasc(fn);
    }
}

void MainWindow::disableActions()
{
    actionLoadJSON->setEnabled(false);
    actionLoadPCF->setEnabled(false);
    actionPack->setEnabled(false);
    actionAssignBudget->setEnabled(false);
    actionPlace->setEnabled(false);
    actionRoute->setEnabled(false);
    actionSaveAsc->setEnabled(false);

    actionPlay->setEnabled(false);
    actionPause->setEnabled(false);
    actionStop->setEnabled(false);

    actionNew->setEnabled(true);
    actionOpen->setEnabled(true);
    actionSave->setEnabled(!currentJson.empty());
}

void MainWindow::loadfile_finished(bool status)
{
    disableActions();
    if (status) {
        log("Loading design successful.\n");
        actionLoadPCF->setEnabled(true);
        actionPack->setEnabled(true);
        if (!currentPCF.empty())
            load_pcf(currentPCF);
        Q_EMIT updateTreeView();
    } else {
        log("Loading design failed.\n");
        currentPCF = "";
    }
}

void MainWindow::loadpcf_finished(bool status)
{
    disableActions();
    if (status) {
        log("Loading PCF successful.\n");
        actionPack->setEnabled(true);
    } else {
        log("Loading PCF failed.\n");
    }
}

void MainWindow::saveasc_finished(bool status)
{
    disableActions();
    if (status) {
        log("Saving ASC successful.\n");
    } else {
        log("Saving ASC failed.\n");
    }
}

void MainWindow::pack_finished(bool status)
{
    disableActions();
    if (status) {
        log("Packing design successful.\n");
        Q_EMIT updateTreeView();
        actionPlace->setEnabled(true);
        actionAssignBudget->setEnabled(true);
    } else {
        log("Packing design failed.\n");
    }
}

void MainWindow::budget_finish(bool status)
{
    disableActions();
    if (status) {
        log("Assigning timing budget successful.\n");
        actionPlace->setEnabled(true);
    } else {
        log("Assigning timing budget failed.\n");
    }
}

void MainWindow::place_finished(bool status)
{
    disableActions();
    if (status) {
        log("Placing design successful.\n");
        Q_EMIT updateTreeView();
        actionRoute->setEnabled(true);
    } else {
        log("Placing design failed.\n");
    }
}
void MainWindow::route_finished(bool status)
{
    disableActions();
    if (status) {
        log("Routing design successful.\n");
        Q_EMIT updateTreeView();
        actionSaveAsc->setEnabled(true);
    } else
        log("Routing design failed.\n");
}

void MainWindow::taskCanceled()
{
    log("CANCELED\n");
    disableActions();
}

void MainWindow::taskStarted()
{
    disableActions();
    actionPause->setEnabled(true);
    actionStop->setEnabled(true);

    actionNew->setEnabled(false);
    actionOpen->setEnabled(false);
}

void MainWindow::taskPaused()
{
    disableActions();
    actionPlay->setEnabled(true);
    actionStop->setEnabled(true);

    actionNew->setEnabled(false);
    actionOpen->setEnabled(false);
}

void MainWindow::budget()
{
    bool ok;
    double freq = QInputDialog::getDouble(this, "Assign timing budget", "Frequency [MHz]:", 50, 0, 250, 2, &ok);
    if (ok) {
        freq *= 1e6;
        timing_driven = true;
        Q_EMIT task->budget(freq);
    }
}

void MainWindow::place() { Q_EMIT task->place(timing_driven); }

NEXTPNR_NAMESPACE_END

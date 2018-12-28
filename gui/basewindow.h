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

#ifndef BASEMAINWINDOW_H
#define BASEMAINWINDOW_H

#include "nextpnr.h"
#include "worker.h"

#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

Q_DECLARE_METATYPE(std::string)
Q_DECLARE_METATYPE(NEXTPNR_NAMESPACE_PREFIX DecalXY)

NEXTPNR_NAMESPACE_BEGIN

class PythonTab;
class DesignWidget;
class FPGAViewWidget;

class BaseMainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit BaseMainWindow(std::unique_ptr<Context> context, ArchArgs args, QWidget *parent = 0);
    virtual ~BaseMainWindow();
    Context *getContext() { return ctx.get(); }
    void updateLoaded();
    void projectLoad(std::string filename);
    void notifyChangeContext();

  protected:
    void createMenusAndBars();
    void disableActions();
    void load_json(std::string filename);

    virtual void onDisableActions(){};
    virtual void onJsonLoaded(){};
    virtual void onProjectLoaded(){};
    virtual void onPackFinished(){};
    virtual void onBudgetFinished(){};
    virtual void onPlaceFinished(){};
    virtual void onRouteFinished(){};

  protected Q_SLOTS:
    void writeInfo(std::string text);
    void closeTab(int index);

    virtual void new_proj() = 0;

    void open_proj();
    void save_proj();

    void open_json();
    void budget();
    void place();

    void execute_python();

    void pack_finished(bool status);
    void budget_finish(bool status);
    void place_finished(bool status);
    void route_finished(bool status);

    void taskCanceled();
    void taskStarted();
    void taskPaused();

  Q_SIGNALS:
    void contextChanged(Context *ctx);
    void updateTreeView();

  protected:
    // state variables
    ArchArgs chipArgs;
    std::unique_ptr<Context> ctx;
    TaskManager *task;
    bool timing_driven;
    std::string currentProj;

    // main widgets
    QTabWidget *tabWidget;
    QTabWidget *centralTabWidget;
    PythonTab *console;
    DesignWidget *designview;
    FPGAViewWidget *fpgaView;

    // Menus, bars and actions
    QMenuBar *menuBar;
    QMenu *menuDesign;
    QStatusBar *statusBar;
    QToolBar *mainActionBar;
    QProgressBar *progressBar;

    QAction *actionNew;
    QAction *actionOpen;
    QAction *actionSave;

    QAction *actionLoadJSON;
    QAction *actionPack;
    QAction *actionAssignBudget;
    QAction *actionPlace;
    QAction *actionRoute;

    QAction *actionExecutePy;

    QAction *actionPlay;
    QAction *actionPause;
    QAction *actionStop;
};

NEXTPNR_NAMESPACE_END

#endif // BASEMAINWINDOW_H

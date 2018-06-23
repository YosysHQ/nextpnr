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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "../basewindow.h"
#include "worker.h"

NEXTPNR_NAMESPACE_BEGIN

class MainWindow : public BaseMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(Context *ctx, QWidget *parent = 0);
    virtual ~MainWindow();

  public:
    void createMenu();

  Q_SIGNALS:
    void budget(double freq);
    void place(bool timing_driven);

  protected Q_SLOTS:
    virtual void open();
    virtual bool save();

    void budget();
    void place();

    void loadfile_finished(bool status);
    void pack_finished(bool status);
    void budget_finish(bool status);
    void place_finished(bool status);
    void route_finished(bool status);

    void taskCanceled();
    void taskStarted();
    void taskPaused();

  private:
    void disableActions();

    TaskManager *task;
    QAction *actionPack;
    QAction *actionAssignBudget;
    QAction *actionPlace;
    QAction *actionRoute;
    QAction *actionPlay;
    QAction *actionPause;
    QAction *actionStop;

    bool timing_driven;
};

NEXTPNR_NAMESPACE_END

#endif // MAINWINDOW_H

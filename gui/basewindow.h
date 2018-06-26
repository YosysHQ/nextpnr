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

#include "infotab.h"
#include "nextpnr.h"

#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

Q_DECLARE_METATYPE(std::string)

NEXTPNR_NAMESPACE_BEGIN

class BaseMainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit BaseMainWindow(QWidget *parent = 0);
    virtual ~BaseMainWindow();
    Context *getContext() { return ctx; }

  protected:
    void createMenusAndBars();

  protected Q_SLOTS:
    void writeInfo(std::string text);

    virtual void new_proj() = 0;
    virtual void open_proj() = 0;
    virtual bool save_proj() = 0;

  Q_SIGNALS:
    void contextChanged(Context *ctx);

  protected:
    Context *ctx;
    QTabWidget *tabWidget;
    QTabWidget *centralTabWidget;
    InfoTab *info;

    QMenuBar *menuBar;
    QToolBar *mainToolBar;
    QStatusBar *statusBar;
};

NEXTPNR_NAMESPACE_END

#endif // BASEMAINWINDOW_H

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

// FIXME
USING_NEXTPNR_NAMESPACE

Q_DECLARE_METATYPE(std::string)

class BaseMainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit BaseMainWindow(Context *ctx, QWidget *parent = 0);
    virtual ~BaseMainWindow();
    Context *getContext() { return ctx; }

  protected:
    void createMenusAndBars();

  protected Q_SLOTS:
    void writeInfo(std::string text);

    virtual void open() = 0;
    virtual bool save() = 0;

  protected:
    Context *ctx;
    QTabWidget *tabWidget;
    QTabWidget *centralTabWidget;
    InfoTab *info;

    QMenuBar *menuBar;
    QToolBar *mainToolBar;
    QStatusBar *statusBar;
};

#endif // BASEMAINWINDOW_H

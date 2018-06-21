#ifndef BASEMAINWINDOW_H
#define BASEMAINWINDOW_H

#include "infotab.h"
#include "nextpnr.h"

#include <QMainWindow>
#include <QTabWidget>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>


// FIXME
USING_NEXTPNR_NAMESPACE

class BaseMainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit BaseMainWindow(Context *ctx, QWidget *parent = 0);
    ~BaseMainWindow();
    Context *getContext() { return ctx; }

  protected:
    void createMenusAndBars();

  protected Q_SLOTS:
    void writeInfo(std::string text);
    void open();
    bool save();

  protected:
    Context *ctx;
    QTabWidget *tabWidget;
    InfoTab *info;
    
    QMenuBar *menuBar;
    QToolBar *mainToolBar;
    QStatusBar *statusBar;
};

#endif // BASEMAINWINDOW_H

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

  protected Q_SLOTS:
    virtual void open();
    virtual bool save();
    void loadfile_finished(bool status);
    void pack_finished(bool status);
    void place_finished(bool status);
    void route_finished(bool status);
    
    void taskCanceled();
    void taskStarted();
    void taskPaused();

  private:
    void disableActions();

    TaskManager *task;
    QAction *actionPack;
    QAction *actionPlace;
    QAction *actionRoute;
    QAction *actionPlay;
    QAction *actionPause;
    QAction *actionStop;    
};

NEXTPNR_NAMESPACE_END

#endif // MAINWINDOW_H

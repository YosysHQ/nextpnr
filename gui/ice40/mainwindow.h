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

  private:
    TaskManager *task;
};

NEXTPNR_NAMESPACE_END

#endif // MAINWINDOW_H

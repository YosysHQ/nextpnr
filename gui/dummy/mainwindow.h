#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "../basewindow.h"

// FIXME
USING_NEXTPNR_NAMESPACE

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
};

#endif // MAINWINDOW_H

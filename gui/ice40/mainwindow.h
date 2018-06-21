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
    ~MainWindow();

  public:
    void createMenu();
};

#endif // MAINWINDOW_H

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "infotab.h"
#include "nextpnr.h"

#include <QMainWindow>
#include <QTabWidget>

// FIXME
USING_NEXTPNR_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(Context *ctx, QWidget *parent = 0);
    ~MainWindow();
    Context *getContext() { return ctx; }

  private:
    void createMenusAndBars();

  private Q_SLOTS:
    void writeInfo(std::string text);
    void open();
    bool save();

  private:
    Context *ctx;
    QTabWidget *tabWidget;
    InfoTab *info;
};

#endif // MAINWINDOW_H

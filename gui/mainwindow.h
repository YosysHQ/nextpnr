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
    explicit MainWindow(Design *design, QWidget *parent = 0);
    ~MainWindow();
    Design *getDesign() { return design; }

  private:
    void createMenusAndBars();

  private Q_SLOTS:
    void writeInfo(std::string text);

  private:
    Design *design;
    InfoTab *info;
};

#endif // MAINWINDOW_H

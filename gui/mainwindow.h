#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "emb.h"
#include "nextpnr.h"

#include <QMainWindow>

// FIXME
USING_NEXTPNR_NAMESPACE

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(Design *design, QWidget *parent = 0);
    ~MainWindow();
    Design *getDesign() { return design; }

  private:
    int executePython(std::string command);

  private Q_SLOTS:
    void on_lineEdit_returnPressed();

  private:
    Ui::MainWindow *ui;
    emb::stdout_write_type write;
    Design *design;
};

#endif // MAINWINDOW_H

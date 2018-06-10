#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include "design.h"
#include "emb.h"

#include <QMainWindow>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(Design *design, QWidget *parent = 0);
    ~MainWindow();

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

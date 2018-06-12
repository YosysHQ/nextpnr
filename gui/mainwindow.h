#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "emb.h"
#include "nextpnr.h"
#include "qtpropertymanager.h"
#include "qttreepropertybrowser.h"
#include "qtvariantproperty.h"

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
    void prepareMenu(const QPoint &pos);
    void selectObject(QTreeWidgetItem *item);

  private:
    Ui::MainWindow *ui;
    emb::stdout_write_type write;
    Design *design;
    QtVariantPropertyManager *variantManager;
    QtVariantEditorFactory *variantFactory;
    QtTreePropertyBrowser *variantEditor;
};

#endif // MAINWINDOW_H

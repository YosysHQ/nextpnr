#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "emb.h"
#include "nextpnr.h"
#include "qtpropertymanager.h"
#include "qttreepropertybrowser.h"
#include "qtvariantproperty.h"

#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>

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
    void addProperty(QtVariantProperty *property, const QString &id);

  private Q_SLOTS:
    void editLineReturnPressed();
    void prepareMenu(const QPoint &pos);
    void selectObject();
    void onItemClicked(QTreeWidgetItem *item, int);

  private:
    Ui::MainWindow *ui;
    emb::stdout_write_type write;
    Design *design;
    QtVariantPropertyManager *variantManager;
    QtVariantEditorFactory *variantFactory;
    QtTreePropertyBrowser *propertyEditor;
    QTreeWidgetItem *itemContextMenu;

    QMap<QtProperty *, QString> propertyToId;
    QMap<QString, QtVariantProperty *> idToProperty;

    QPlainTextEdit *plainTextEdit;
    QLineEdit *lineEdit;
};

#endif // MAINWINDOW_H

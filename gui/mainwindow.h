#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "emb.h"
#include "nextpnr.h"
#include "qtpropertymanager.h"
#include "qttreepropertybrowser.h"
#include "qtvariantproperty.h"
#include "pythontab.h"
#include "infotab.h"

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QTabWidget>

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
    void addProperty(QtVariantProperty *property, const QString &id);

  private Q_SLOTS:
    void prepareMenu(const QPoint &pos);
    void selectObject();
    void onItemClicked(QTreeWidgetItem *item, int);

  private:
    Ui::MainWindow *ui;
    Design *design;
    QtVariantPropertyManager *variantManager;
    QtVariantEditorFactory *variantFactory;
    QtTreePropertyBrowser *propertyEditor;
    QTreeWidgetItem *itemContextMenu;

    QMap<QtProperty *, QString> propertyToId;
    QMap<QString, QtVariantProperty *> idToProperty;

    QTabWidget *tabWidget;
    InfoTab *info;
};

#endif // MAINWINDOW_H

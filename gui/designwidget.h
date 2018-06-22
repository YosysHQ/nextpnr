#ifndef DESIGNWIDGET_H
#define DESIGNWIDGET_H

#include <QTreeWidget>
#include "nextpnr.h"
#include "qtpropertymanager.h"
#include "qttreepropertybrowser.h"
#include "qtvariantproperty.h"

NEXTPNR_NAMESPACE_BEGIN

class DesignWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit DesignWidget(Context *ctx, QWidget *parent = 0);
    ~DesignWidget();
    Context *getContext() { return ctx; }

  private:
    void addProperty(QtVariantProperty *property, const QString &id);
    void clearProperties();

  Q_SIGNALS:
    void info(std::string text);

  private Q_SLOTS:
    void prepareMenu(const QPoint &pos);
    void onItemClicked(QTreeWidgetItem *item, int);
    void selectObject();

  private:
    Context *ctx;

    QTreeWidget *treeWidget;

    QtVariantPropertyManager *variantManager;
    QtVariantEditorFactory *variantFactory;
    QtTreePropertyBrowser *propertyEditor;
    QTreeWidgetItem *itemContextMenu;

    QMap<QtProperty *, QString> propertyToId;
    QMap<QString, QtVariantProperty *> idToProperty;
};

NEXTPNR_NAMESPACE_END

#endif // DESIGNWIDGET_H

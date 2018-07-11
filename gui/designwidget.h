/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifndef DESIGNWIDGET_H
#define DESIGNWIDGET_H

#include <QTreeWidget>
#include "nextpnr.h"
#include "qtgroupboxpropertybrowser.h"
#include "qtpropertymanager.h"
#include "qttreepropertybrowser.h"
#include "qtvariantproperty.h"

NEXTPNR_NAMESPACE_BEGIN

class DesignWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit DesignWidget(QWidget *parent = 0);
    ~DesignWidget();

  private:
    void addProperty(QtProperty *property, const QString &id);
    void clearProperties();

  Q_SIGNALS:
    void info(std::string text);

  private Q_SLOTS:
    void prepareMenu(const QPoint &pos);
    void onItemClicked(QTreeWidgetItem *item, int);
    void selectObject();
  public Q_SLOTS:
    void newContext(Context *ctx);
    void updateTree();

  private:
    Context *ctx;

    QTreeWidget *treeWidget;

    QtVariantPropertyManager *variantManager;
    QtVariantPropertyManager *readOnlyManager;
    QtGroupPropertyManager *groupManager;
    QtVariantEditorFactory *variantFactory;
    QtTreePropertyBrowser *propertyEditor;
    QTreeWidgetItem *itemContextMenu;

    QMap<QtProperty *, QString> propertyToId;
    QMap<QString, QtProperty *> idToProperty;
    QTreeWidgetItem *nets_root;
    QTreeWidgetItem *cells_root;

    QAction *actionFirst;
    QAction *actionPrev;
    QAction *actionNext;
    QAction *actionLast;
};

NEXTPNR_NAMESPACE_END

#endif // DESIGNWIDGET_H

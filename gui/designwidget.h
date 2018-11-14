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

#include <QMouseEvent>
#include <QTabWidget>
#include <QTreeView>
#include <QVariant>
#include "nextpnr.h"
#include "qtgroupboxpropertybrowser.h"
#include "qtpropertymanager.h"
#include "qttreepropertybrowser.h"
#include "qtvariantproperty.h"
#include "treemodel.h"

NEXTPNR_NAMESPACE_BEGIN

class TreeView : public QTreeView
{
    Q_OBJECT

  public:
    explicit TreeView(QWidget *parent = 0);
    ~TreeView();
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

  Q_SIGNALS:
    void hoverIndexChanged(QModelIndex index);

  private:
    QModelIndex current;
};

class DesignWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit DesignWidget(QWidget *parent = 0);
    ~DesignWidget();

  private:
    void clearProperties();
    QtProperty *addTopLevelProperty(const QString &id);
    QtProperty *addSubGroup(QtProperty *topItem, const QString &name);
    void addProperty(QtProperty *topItem, int propertyType, const QString &name, QVariant value,
                     const ElementType &type = ElementType::NONE);
    QString getElementTypeName(ElementType type);
    ElementType getElementTypeByName(QString type);
    TreeModel::Model *getTreeByElementType(ElementType type);
    int getIndexByElementType(ElementType type);
    int getElementIndex(ElementType type);
    void updateButtons();
    void addToHistory(int tab, QModelIndex item);
    std::vector<DecalXY> getDecals(ElementType type, IdString value);
    void updateHighlightGroup(QList<TreeModel::Item *> item, int group);
    void clearAllSelectionModels();
  Q_SIGNALS:
    void selected(std::vector<DecalXY> decal, bool keep);
    void highlight(std::vector<DecalXY> decal, int group);
    void hover(DecalXY decal);
    void zoomSelected();

  private Q_SLOTS:
    void prepareMenuProperty(const QPoint &pos);
    void prepareMenuTree(int num, const QPoint &pos);
    void onSelectionChanged(int num, const QItemSelection &selected, const QItemSelection &deselected);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onDoubleClicked(const QModelIndex &index);
    void onSearchInserted();
    void onHoverIndexChanged(int num, QModelIndex index);
    void onHoverPropertyChanged(QtBrowserItem *item);
  public Q_SLOTS:
    void newContext(Context *ctx);
    void updateTree();
    void onClickedBel(BelId bel, bool keep);
    void onClickedWire(WireId wire, bool keep);
    void onClickedPip(PipId pip, bool keep);

  private:
    Context *ctx;

    QTabWidget *tabWidget;

    TreeView *treeView[6];
    QItemSelectionModel *selectionModel[6];
    TreeModel::Model *treeModel[6];
    QLineEdit *searchEdit;
    QtVariantPropertyManager *variantManager;
    QtVariantPropertyManager *readOnlyManager;
    QtGroupPropertyManager *groupManager;
    QtVariantEditorFactory *variantFactory;
    QtTreePropertyBrowser *propertyEditor;

    QMap<QtProperty *, QString> propertyToId;
    QMap<QString, QtProperty *> idToProperty;

    std::vector<std::pair<int, QModelIndex>> history;
    int history_index;
    bool history_ignore;

    QAction *actionFirst;
    QAction *actionPrev;
    QAction *actionNext;
    QAction *actionLast;
    QAction *actionClear;

    QColor highlightColors[8];
    QMap<TreeModel::Item *, int> highlightSelected;

    QString currentSearch;
    QList<QModelIndex> currentSearchIndexes;
    int currentIndex;
    int currentIndexTab;
};

NEXTPNR_NAMESPACE_END

#endif // DESIGNWIDGET_H

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

#ifndef TREEMODEL_H
#define TREEMODEL_H

#include <QAbstractItemModel>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

enum class ElementType
{
    NONE,
    BEL,
    WIRE,
    PIP,
    NET,
    CELL,
    GROUP
};

class ContextTreeItem
{
  public:
    ContextTreeItem();
    ContextTreeItem(QString name);
    ContextTreeItem(IdString id, ElementType type, QString name);
    ~ContextTreeItem();

    void addChild(ContextTreeItem *item);
    int indexOf(ContextTreeItem *n) const { return children.indexOf(n); }
    ContextTreeItem *at(int idx) const { return children.at(idx); }
    int count() const { return children.count(); }
    ContextTreeItem *parent() const { return parentNode; }
    IdString id() const { return itemId; }
    ElementType type() const { return itemType; }
    QString name() const { return itemName; }
    void sort();
  private:
    ContextTreeItem *parentNode;
    QList<ContextTreeItem *> children;
    IdString itemId;
    ElementType itemType;
    QString itemName;
};

class ContextTreeModel : public QAbstractItemModel
{
  public:
    ContextTreeModel(QObject *parent = nullptr);
    ~ContextTreeModel();

    void loadData(Context *ctx);
    void updateData(Context *ctx);
    ContextTreeItem *nodeFromIndex(const QModelIndex &idx) const;
    QModelIndex indexFromNode(ContextTreeItem *node);
    ContextTreeItem *nodeForIdType(const ElementType type, const QString name) const;
    QList<QModelIndex> search(QString text);
    // Override QAbstractItemModel methods
    int rowCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
    int columnCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
    QModelIndex parent(const QModelIndex &child) const Q_DECL_OVERRIDE;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const Q_DECL_OVERRIDE;
    Qt::ItemFlags flags(const QModelIndex &index) const Q_DECL_OVERRIDE;

  private:
    ContextTreeItem *root;
    QMap<QString, ContextTreeItem *> nameToItem[6];
    ContextTreeItem *nets_root;
    ContextTreeItem *cells_root;
};

NEXTPNR_NAMESPACE_END

#endif // TREEMODEL_H

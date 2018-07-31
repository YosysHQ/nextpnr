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

#include "treemodel.h"

NEXTPNR_NAMESPACE_BEGIN

ContextTreeItem::ContextTreeItem() { parentNode = nullptr; }

ContextTreeItem::ContextTreeItem(QString name)
        : parentNode(nullptr), itemId(IdString()), itemType(ElementType::NONE), itemName(name)
{
}

ContextTreeItem::ContextTreeItem(IdString id, ElementType type, QString name)
        : parentNode(nullptr), itemId(id), itemType(type), itemName(name)
{
}

ContextTreeItem::~ContextTreeItem()
{
    if (parentNode)
        parentNode->children.removeOne(this);
    qDeleteAll(children);
}

//void ContextTreeItem::addChild(ContextTreeItem *item)
//{
//    item->parentNode = this;
//    children.append(item);
//}

void ContextTreeItem::sort()
{
    for (auto item : children)
        if (item->count()>1) item->sort();
    qSort(children.begin(), children.end(), [&](const ContextTreeItem *a, const ContextTreeItem *b){
        QString name_a = a->name();
        QString name_b = b->name();
        // Try to extract a common prefix from both strings.
        QString common;
        for (int i = 0; i < std::min(name_a.size(), name_b.size()); i++) {
            const QChar c_a = name_a[i];
            const QChar c_b = name_b[i];
            if (c_a == c_b) {
                common.push_back(c_a);
            } else {
                break;
            }
        }
        // No common part? lexical sort.
        if (common.size() == 0) {
            return a->name() < b->name();
        }

        // Get the non-common parts.
        name_a.remove(0, common.size());
        name_b.remove(0, common.size());
        // And see if they're strings.
        bool ok = true;
        int num_a = name_a.toInt(&ok);
        if (!ok) {
            return a->name() < b->name();
        }
        int num_b = name_b.toInt(&ok);
        if (!ok) {
            return a->name() < b->name();
        }
        return num_a < num_b;
    });
}

ContextTreeModel::ContextTreeModel(QObject *parent) :
        QAbstractItemModel(parent),
        root_(new StaticTreeItem("Elements", nullptr)) {}

ContextTreeModel::~ContextTreeModel() {}

void ContextTreeModel::loadData(Context *ctx)
{
    if (!ctx)
        return;

    beginResetModel();

    {
        printf("generating bel map...\n");
        std::map<std::pair<int, int>, std::vector<BelId>> belMap;
        for (auto bel : ctx->getBels()) {
            auto loc = ctx->getBelLocation(bel);
            belMap[std::pair<int, int>(loc.x, loc.y)].push_back(bel);
        }
        printf("generating bel static tree...\n");
        auto belGetter = [](Context *ctx, BelId id) { return ctx->getBelName(id); };
        bel_root_ = std::unique_ptr<BelXYRoot>(new BelXYRoot(ctx, "Bels", root_.get(), belMap, belGetter));

        printf("generating wire map...\n");
        std::map<std::pair<int, int>, std::vector<WireId>> wireMap;
        //TODO(q3k): change this once we have an API to get wire categories/locations/labels
        for (int i = 0; i < ctx->chip_info->num_wires; i++) {
            const auto wire = &ctx->chip_info->wire_data[i];
            WireId wireid;
            wireid.index = i;
            wireMap[std::pair<int, int>(wire->x, wire->y)].push_back(wireid);
        }
        printf("generating wire static tree...\n");
        auto wireGetter = [](Context *ctx, WireId id) { return ctx->getWireName(id); };
        wire_root_ = std::unique_ptr<WireXYRoot>(new WireXYRoot(ctx, "Wires", root_.get(), wireMap, wireGetter));

        printf("generating pip map...\n");
        std::map<std::pair<int, int>, std::vector<PipId>> pipMap;
        //TODO(q3k): change this once we have an API to get wire categories/locations/labels
        for (int i = 0; i < ctx->chip_info->num_pips; i++) {
            const auto pip = &ctx->chip_info->pip_data[i];
            PipId pipid;
            pipid.index = i;
            pipMap[std::pair<int, int>(pip->x, pip->y)].push_back(pipid);
        }
        printf("generating pip static tree...\n");
        auto pipGetter = [](Context *ctx, PipId id) { return ctx->getPipName(id); };
        pip_root_ = std::unique_ptr<PipXYRoot>(new PipXYRoot(ctx, "Pips", root_.get(), pipMap, pipGetter));
    }

    //nets_root = new ContextTreeItem("Nets");
    //root->addChild(nets_root);

    //cells_root = new ContextTreeItem("Cells");
    //root->addChild(cells_root);

    endResetModel();
}

void ContextTreeModel::updateData(Context *ctx)
{
    if (!ctx)
        return;

    beginResetModel();

    //QModelIndex nets_index = indexFromNode(nets_root);
    // Remove nets not existing any more
    //QMap<QString, ContextTreeItem *>::iterator i = nameToItem[3].begin();
    //while (i != nameToItem[3].end()) {
    //    QMap<QString, ContextTreeItem *>::iterator prev = i;
    //    ++i;
    //    if (ctx->nets.find(ctx->id(prev.key().toStdString())) == ctx->nets.end()) {
    //        //int pos = prev.value()->parent()->indexOf(prev.value());
    //        //beginRemoveRows(nets_index, pos, pos);
    //        delete prev.value();
    //        nameToItem[3].erase(prev);
    //        //endRemoveRows();
    //    }
    //}
    //// Add nets to tree
    //for (auto &item : ctx->nets) {
    //    auto id = item.first;
    //    QString name = QString(id.c_str(ctx));
    //    if (!nameToItem[3].contains(name)) {
    //        //beginInsertRows(nets_index, nets_root->count() + 1, nets_root->count() + 1);
    //        ContextTreeItem *newItem = new ContextTreeItem(id, ElementType::NET, name);
    //        nets_root->addChild(newItem);
    //        nameToItem[3].insert(name, newItem);
    //        //endInsertRows();
    //    }
    //}

    //nets_root->sort();

    //QModelIndex cell_index = indexFromNode(cells_root);
    // Remove cells not existing any more
    //i = nameToItem[4].begin();
    //while (i != nameToItem[4].end()) {
    //    QMap<QString, ContextTreeItem *>::iterator prev = i;
    //    ++i;
    //    if (ctx->cells.find(ctx->id(prev.key().toStdString())) == ctx->cells.end()) {
    //        //int pos = prev.value()->parent()->indexOf(prev.value());
    //        //beginRemoveRows(cell_index, pos, pos);
    //        delete prev.value();
    //        nameToItem[4].erase(prev);
    //        //endRemoveRows();
    //    }
    //}
    //// Add cells to tree
    //for (auto &item : ctx->cells) {
    //    auto id = item.first;
    //    QString name = QString(id.c_str(ctx));
    //    if (!nameToItem[4].contains(name)) {
    //        //beginInsertRows(cell_index, cells_root->count() + 1, cells_root->count() + 1);
    //        ContextTreeItem *newItem = new ContextTreeItem(id, ElementType::CELL, name);
    //        cells_root->addChild(newItem);
    //        nameToItem[4].insert(name, newItem);
    //        //endInsertRows();
    //    }
    //}

    //cells_root->sort();

    endResetModel();
}

int ContextTreeModel::rowCount(const QModelIndex &parent) const { return nodeFromIndex(parent)->count(); }

int ContextTreeModel::columnCount(const QModelIndex &parent) const { return 1; }

QModelIndex ContextTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    LazyTreeItem *node = nodeFromIndex(parent);
    if (row >= node->count())
        return QModelIndex();

    return createIndex(row, column, node->child(row));
}

QModelIndex ContextTreeModel::parent(const QModelIndex &child) const
{
    LazyTreeItem *parent = nodeFromIndex(child)->parent();
    if (parent == root_.get())
        return QModelIndex();
    LazyTreeItem *node = parent->parent();
    return createIndex(node->indexOf(parent), 0, parent);
}

QVariant ContextTreeModel::data(const QModelIndex &index, int role) const
{
    if (index.column() != 0)
        return QVariant();
    if (role != Qt::DisplayRole)
        return QVariant();
    LazyTreeItem *node = nodeFromIndex(index);
    return node->name();
}

QVariant ContextTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(section);
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return QString("Items");

    return QVariant();
}

LazyTreeItem *ContextTreeModel::nodeFromIndex(const QModelIndex &idx) const
{
    if (idx.isValid())
        return (LazyTreeItem *)idx.internalPointer();
    return root_.get();
}

static int getElementIndex(ElementType type)
{
    if (type == ElementType::BEL)
        return 0;
    if (type == ElementType::WIRE)
        return 1;
    if (type == ElementType::PIP)
        return 2;
    if (type == ElementType::NET)
        return 3;
    if (type == ElementType::CELL)
        return 4;
    return -1;
}

//ContextTreeItem *ContextTreeModel::nodeForIdType(const ElementType type, const QString name) const
//{
//    int index = getElementIndex(type);
//    if (type != ElementType::NONE && nameToItem[index].contains(name))
//        return nameToItem[index].value(name);
//    return nullptr;
//}

//QModelIndex ContextTreeModel::indexFromNode(ContextTreeItem *node)
//{
//    ContextTreeItem *parent = node->parent();
//    if (parent == root)
//        return QModelIndex();
//    return createIndex(parent->indexOf(node), 0, node);
//}

Qt::ItemFlags ContextTreeModel::flags(const QModelIndex &index) const
{
    LazyTreeItem *node = nodeFromIndex(index);
    return Qt::ItemIsEnabled | (node->type() != ElementType::NONE ? Qt::ItemIsSelectable : Qt::NoItemFlags);
}


void ContextTreeModel::fetchMore(const QModelIndex &parent)
{
    nodeFromIndex(parent)->fetchMore();
}

bool ContextTreeModel::canFetchMore(const QModelIndex &parent) const
{
    return nodeFromIndex(parent)->canFetchMore();
}

NEXTPNR_NAMESPACE_END

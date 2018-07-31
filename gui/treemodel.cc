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
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

ContextTreeModel::ContextTreeModel(QObject *parent) :
        QAbstractItemModel(parent),
        root_(new StaticTreeItem("Elements", nullptr)) {}

ContextTreeModel::~ContextTreeModel() {}

void ContextTreeModel::loadContext(Context *ctx)
{
    if (!ctx)
        return;

    beginResetModel();

    // Currently we lack an API to get a proper hierarchy of bels/pip/wires
    // cross-arch. So we only do this for ICE40 by querying the ChipDB
    // directly.
    // TODO(q3k): once AnyId and the tree API land in Arch, move this over.
#ifdef ARCH_ICE40
    {
        std::map<std::pair<int, int>, std::vector<BelId>> belMap;
        for (auto bel : ctx->getBels()) {
            auto loc = ctx->getBelLocation(bel);
            belMap[std::pair<int, int>(loc.x, loc.y)].push_back(bel);
        }
        auto belGetter = [](Context *ctx, BelId id) { return ctx->getBelName(id); };
        bel_root_ = std::unique_ptr<BelXYRoot>(new BelXYRoot(ctx, "Bels", root_.get(), belMap, belGetter));

        std::map<std::pair<int, int>, std::vector<WireId>> wireMap;
        for (int i = 0; i < ctx->chip_info->num_wires; i++) {
            const auto wire = &ctx->chip_info->wire_data[i];
            WireId wireid;
            wireid.index = i;
            wireMap[std::pair<int, int>(wire->x, wire->y)].push_back(wireid);
        }
        auto wireGetter = [](Context *ctx, WireId id) { return ctx->getWireName(id); };
        wire_root_ = std::unique_ptr<WireXYRoot>(new WireXYRoot(ctx, "Wires", root_.get(), wireMap, wireGetter));

        std::map<std::pair<int, int>, std::vector<PipId>> pipMap;
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
#endif

    cell_root_ = std::unique_ptr<IdStringList>(new IdStringList(QString("Cells"), root_.get()));
    net_root_ = std::unique_ptr<IdStringList>(new IdStringList(QString("Nets"), root_.get()));

    endResetModel();

    updateCellsNets(ctx);
}

void ContextTreeModel::updateCellsNets(Context *ctx)
{
    if (!ctx)
        return;

    beginResetModel();

    std::vector<IdString> cells;
    for (auto &pair : ctx->cells) {
        cells.push_back(pair.first);
    }
    cell_root_->updateElements(ctx, cells);

    std::vector<IdString> nets;
    for (auto &pair : ctx->nets) {
        nets.push_back(pair.first);
    }
    net_root_->updateElements(ctx, nets);

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

QList<QModelIndex> ContextTreeModel::search(QString text)
{
    QList<QModelIndex> list;
    //for (int i = 0; i < 6; i++) {
    //    for (auto key : nameToItem[i].keys()) {
    //        if (key.contains(text, Qt::CaseInsensitive)) {
    //            list.append(indexFromNode(nameToItem[i].value(key)));
    //            if (list.count() > 500)
    //                break; // limit to 500 results
    //        }
    //    }
    //}
    return list;
}

NEXTPNR_NAMESPACE_END

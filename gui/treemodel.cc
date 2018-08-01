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

namespace TreeModel {

Model::Model(QObject *parent) :
        QAbstractItemModel(parent),
        root_(new Item("Elements", nullptr, ElementType::NONE)) {}

Model::~Model() {}

void Model::loadContext(Context *ctx)
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
        bel_root_ = std::unique_ptr<BelXYRoot>(new BelXYRoot(ctx, "Bels", root_.get(), belMap, belGetter, ElementType::BEL));

        std::map<std::pair<int, int>, std::vector<WireId>> wireMap;
        for (int i = 0; i < ctx->chip_info->num_wires; i++) {
            const auto wire = &ctx->chip_info->wire_data[i];
            WireId wireid;
            wireid.index = i;
            wireMap[std::pair<int, int>(wire->x, wire->y)].push_back(wireid);
        }
        auto wireGetter = [](Context *ctx, WireId id) { return ctx->getWireName(id); };
        wire_root_ = std::unique_ptr<WireXYRoot>(new WireXYRoot(ctx, "Wires", root_.get(), wireMap, wireGetter, ElementType::WIRE));

        std::map<std::pair<int, int>, std::vector<PipId>> pipMap;
        for (int i = 0; i < ctx->chip_info->num_pips; i++) {
            const auto pip = &ctx->chip_info->pip_data[i];
            PipId pipid;
            pipid.index = i;
            pipMap[std::pair<int, int>(pip->x, pip->y)].push_back(pipid);
        }
        printf("generating pip static tree...\n");
        auto pipGetter = [](Context *ctx, PipId id) { return ctx->getPipName(id); };
        pip_root_ = std::unique_ptr<PipXYRoot>(new PipXYRoot(ctx, "Pips", root_.get(), pipMap, pipGetter, ElementType::PIP));
    }
#endif

    cell_root_ = std::unique_ptr<IdStringList>(new IdStringList(QString("Cells"), root_.get(), ElementType::CELL));
    net_root_ = std::unique_ptr<IdStringList>(new IdStringList(QString("Nets"), root_.get(), ElementType::NET));

    endResetModel();

    updateCellsNets(ctx);
}

void Model::updateCellsNets(Context *ctx)
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

int Model::rowCount(const QModelIndex &parent) const { return nodeFromIndex(parent)->count(); }

int Model::columnCount(const QModelIndex &parent) const { return 1; }

QModelIndex Model::index(int row, int column, const QModelIndex &parent) const
{
    Item *node = nodeFromIndex(parent);
    if (row >= node->count())
        return QModelIndex();

    return createIndex(row, column, node->child(row));
}

QModelIndex Model::parent(const QModelIndex &child) const
{
    Item *parent = nodeFromIndex(child)->parent();
    if (parent == root_.get())
        return QModelIndex();
    Item *node = parent->parent();
    return createIndex(node->indexOf(parent), 0, parent);
}

QVariant Model::data(const QModelIndex &index, int role) const
{
    if (index.column() != 0)
        return QVariant();
    if (role != Qt::DisplayRole)
        return QVariant();
    Item *node = nodeFromIndex(index);
    return node->name();
}

QVariant Model::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(section);
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return QString("Items");

    return QVariant();
}

Item *Model::nodeFromIndex(const QModelIndex &idx) const
{
    if (idx.isValid())
        return (Item *)idx.internalPointer();
    return root_.get();
}

Qt::ItemFlags Model::flags(const QModelIndex &index) const
{
    Item *node = nodeFromIndex(index);
    return Qt::ItemIsEnabled | (node->type() != ElementType::NONE ? Qt::ItemIsSelectable : Qt::NoItemFlags);
}


void Model::fetchMore(const QModelIndex &parent)
{
    nodeFromIndex(parent)->fetchMore();
}

bool Model::canFetchMore(const QModelIndex &parent) const
{
    return nodeFromIndex(parent)->canFetchMore();
}

QList<QModelIndex> Model::search(QString text)
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

}; // namespace TreeModel

NEXTPNR_NAMESPACE_END

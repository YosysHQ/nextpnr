/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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

// converts 'aa123bb432' -> ['aa', '123', 'bb', '432']
std::vector<QString> IdStringList::alphaNumSplit(const QString &str)
{
    std::vector<QString> res;
    QString current_part;

    bool number = true;
    for (const auto c : str) {
        if (current_part.size() == 0 && res.size() == 0) {
            current_part.push_back(c);
            number = c.isNumber();
            continue;
        }

        if (number != c.isNumber()) {
            number = c.isNumber();
            res.push_back(current_part);
            current_part.clear();
        }

        current_part.push_back(c);
    }

    res.push_back(current_part);

    return res;
}

void IdStringList::updateElements(Context *ctx, std::vector<IdString> elements)
{
    bool changed = false;

    // For any elements that are not yet in managed_, created them.
    std::unordered_set<IdString> element_set;
    for (auto elem : elements) {
        element_set.insert(elem);
        auto existing = managed_.find(elem);
        if (existing == managed_.end()) {
            auto item = new IdStringItem(ctx, elem, this, child_type_);
            managed_.emplace(elem, std::unique_ptr<IdStringItem>(item));
            changed = true;
        }
    }

    // For any elements that are in managed_ but not in new, delete them.
    auto it = managed_.begin();
    while (it != managed_.end()) {
        if (element_set.count(it->first) != 0) {
            ++it;
        } else {
            it = managed_.erase(it);
            changed = true;
        }
    }

    // Return early if there are no changes.
    if (!changed)
        return;

    // Rebuild children list.
    children_.clear();
    for (auto &pair : managed_) {
        if (element_set.count(pair.first) != 0) {
            children_.push_back(pair.second.get());
        }
    }

    // Sort new children
    qSort(children_.begin(), children_.end(), [&](const Item *a, const Item *b) {
        auto parts_a = alphaNumSplit(a->name());
        auto parts_b = alphaNumSplit(b->name());

        // Short-circuit for different part count.
        if (parts_a.size() != parts_b.size()) {
            return parts_a.size() < parts_b.size();
        }

        for (size_t i = 0; i < parts_a.size(); i++) {
            auto &part_a = parts_a.at(i);
            auto &part_b = parts_b.at(i);

            bool a_is_number, b_is_number;
            int a_number = part_a.toInt(&a_is_number);
            int b_number = part_b.toInt(&b_is_number);

            // If both parts are numbers, compare numerically.
            // If they're equal, continue to next part.
            if (a_is_number && b_is_number) {
                if (a_number != b_number) {
                    return a_number < b_number;
                } else {
                    continue;
                }
            }

            // For different alpha/nonalpha types, make numeric parts appear
            // first.
            if (a_is_number != b_is_number) {
                return a_is_number;
            }

            // If both parts are numbers, compare lexically.
            // If they're equal, continue to next part.
            if (part_a == part_b) {
                continue;
            }
            return part_a < part_b;
        }

        // Same string.
        return true;
    });
}

void IdStringList::search(QList<Item *> &results, QString text, int limit)
{
    for (const auto &child : children_) {
        if (limit != -1 && results.size() > limit)
            return;

        if (child->name().contains(text))
            results.push_back(child);
    }
}

Model::Model(QObject *parent) : QAbstractItemModel(parent), root_(new Item("Elements", nullptr)) {}

Model::~Model() {}

void Model::loadContext(Context *ctx)
{
    if (!ctx)
        return;
    ctx_ = ctx;

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
        bel_root_ = std::unique_ptr<BelXYRoot>(
                new BelXYRoot(ctx, "Bels", root_.get(), belMap, belGetter, ElementType::BEL));

        std::map<std::pair<int, int>, std::vector<WireId>> wireMap;
        for (int i = 0; i < ctx->chip_info->num_wires; i++) {
            const auto wire = &ctx->chip_info->wire_data[i];
            WireId wireid;
            wireid.index = i;
            wireMap[std::pair<int, int>(wire->x, wire->y)].push_back(wireid);
        }
        auto wireGetter = [](Context *ctx, WireId id) { return ctx->getWireName(id); };
        wire_root_ = std::unique_ptr<WireXYRoot>(
                new WireXYRoot(ctx, "Wires", root_.get(), wireMap, wireGetter, ElementType::WIRE));

        std::map<std::pair<int, int>, std::vector<PipId>> pipMap;
        for (int i = 0; i < ctx->chip_info->num_pips; i++) {
            const auto pip = &ctx->chip_info->pip_data[i];
            PipId pipid;
            pipid.index = i;
            pipMap[std::pair<int, int>(pip->x, pip->y)].push_back(pipid);
        }
        auto pipGetter = [](Context *ctx, PipId id) { return ctx->getPipName(id); };
        pip_root_ = std::unique_ptr<PipXYRoot>(
                new PipXYRoot(ctx, "Pips", root_.get(), pipMap, pipGetter, ElementType::PIP));
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
    if (ctx_ == nullptr)
        return;

    std::lock_guard<std::mutex> lock_ui(ctx_->ui_mutex);
    std::lock_guard<std::mutex> lock(ctx_->mutex);

    nodeFromIndex(parent)->fetchMore();
}

bool Model::canFetchMore(const QModelIndex &parent) const { return nodeFromIndex(parent)->canFetchMore(); }

QList<QModelIndex> Model::search(QString text)
{
    const int limit = 500;
    QList<Item *> list;
    cell_root_->search(list, text, limit);
    net_root_->search(list, text, limit);
    bel_root_->search(list, text, limit);
    wire_root_->search(list, text, limit);
    pip_root_->search(list, text, limit);

    QList<QModelIndex> res;
    for (auto i : list) {
        res.push_back(indexFromNode(i));
    }
    return res;
}

}; // namespace TreeModel

NEXTPNR_NAMESPACE_END

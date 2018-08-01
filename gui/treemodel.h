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
#include <boost/optional.hpp>

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

namespace TreeModel {

class Item
{
  protected:
    QString name_;
    Item *parent_;
    QList<Item *> children_;
    ElementType type_;

    void addChild(Item *child)
    {
        children_.append(child);
    }

  public:
    Item(QString name, Item *parent, ElementType type) :
            name_(name), parent_(parent), type_(type)
    {
        // Register in parent if exists.
        if (parent_ != nullptr) {
            parent_->addChild(this);
        }
    };

    int count() const
    {
        return children_.count();
    }

    QString name() const
    {
        return name_;
    }

    Item *child(int index)
    {
        return children_.at(index);
    }

    int indexOf(const Item *child) const
    { 
        // Dropping the const for indexOf to work.
        return children_.indexOf((Item *)child, 0);
    }

    int indexOf(Item *child)
    { 
        return children_.indexOf(child, 0);
    }

    const Item *parent() const
    {
        return parent_;
    }

    Item *parent()
    {
        return parent_;
    }

    ElementType type() const
    {
        return type_;
    }

    virtual bool canFetchMore() const = 0;
    virtual void fetchMore() = 0;
    virtual IdString id() const = 0;

    virtual ~Item() {}
};

class StaticTreeItem : public Item
{
  public:
    using Item::Item;

    virtual bool canFetchMore() const override
    {
        return false;
    }

    virtual void fetchMore() override
    {
    }

    virtual ~StaticTreeItem() {}

    virtual IdString id() const override
    {
        return IdString();
    }
};

class IdStringItem : public StaticTreeItem
{
  private:
    IdString id_;

  public:
    IdStringItem(Context *ctx, IdString str, Item *parent, ElementType type) :
            StaticTreeItem(QString(str.c_str(ctx)), parent, type), id_(str) {}

    virtual IdString id() const override
    {
        return id_;
    }
};

template <typename ElementT>
class ElementList : public Item
{
  public:
    using ElementMap = std::map<std::pair<int, int>, std::vector<ElementT>>;
    using ElementGetter = std::function<IdString(Context *, ElementT)>;

  private:
    Context *ctx_;
    const ElementMap *map_;
    int x_, y_;
    ElementGetter getter_;
    std::unordered_map<IdString, std::unique_ptr<StaticTreeItem>> managed_;
    ElementType child_type_;

    // scope valid until map gets mutated...
    const std::vector<ElementT> *elements() const
    {
        return &map_->at(std::pair<int, int>(x_, y_));
    }

  public:
    ElementList(Context *ctx, QString name, Item *parent, ElementMap *map, int x, int y, ElementGetter getter, ElementType type) :
            Item(name, parent, ElementType::NONE), ctx_(ctx), map_(map), x_(x), y_(y), getter_(getter), child_type_(type)
    {
    }

    virtual bool canFetchMore() const override
    {
        return (size_t)children_.size() < elements()->size();
    }

    void fetchMore(int count)
    {
        size_t start = children_.size();
        size_t end = std::min(start + count, elements()->size());
        for (size_t i = start; i < end; i++) {
            auto idstring = getter_(ctx_, elements()->at(i));
            QString name(idstring.c_str(ctx_));

            // Remove X.../Y.../ prefix
            QString prefix = QString("X%1/Y%2/").arg(x_).arg(y_);
            if (name.startsWith(prefix))
                name.remove(0, prefix.size());

            auto item = new IdStringItem(ctx_, idstring, this, child_type_);
            managed_[idstring] = std::move(std::unique_ptr<StaticTreeItem>(item));
        }
    }

    virtual void fetchMore() override
    {
         fetchMore(100);
    }

    virtual IdString id() const override
    {
        return IdString();
    }

    boost::optional<Item*> getById(IdString id)
    {
        // Search requires us to load all our elements...
        while (canFetchMore()) fetchMore();

        auto res = managed_.find(id);
        if (res != managed_.end()) {
            return res->second.get();
        }
        return boost::none;
    }
};

class IdStringList : public StaticTreeItem
{
  private:
    std::unordered_map<IdString, std::unique_ptr<IdStringItem>> managed_;
    ElementType child_type_;
  public:
    IdStringList(QString name, Item *parent, ElementType type) :
            StaticTreeItem(name, parent, ElementType::NONE), child_type_(type) {}
    using StaticTreeItem::StaticTreeItem;

    static std::vector<QString> alphaNumSplit(const QString &str)
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

    IdStringItem *getById(IdString id) const
    {
        return managed_.at(id).get();
    }

    void updateElements(Context *ctx, std::vector<IdString> elements)
    {
        // for any elements that are not yet in managed_, created them.
        std::unordered_set<IdString> element_set;
        for (auto elem : elements) {
            element_set.insert(elem);
            auto existing = managed_.find(elem);
            if (existing == managed_.end()) {
                auto item = new IdStringItem(ctx, elem, this, child_type_);
                managed_.emplace(elem, std::unique_ptr<IdStringItem>(item));
            }
        }
        
        children_.clear();
        // for any elements that are in managed_ but not in new, delete them.
        for (auto &pair : managed_) {
            if (element_set.count(pair.first) != 0) {
                children_.push_back(pair.second.get());
                continue;
            }
            managed_.erase(pair.first);
        }

        // sort new children
        qSort(children_.begin(), children_.end(), [&](const Item *a, const Item *b){
            auto parts_a = alphaNumSplit(a->name());
            auto parts_b = alphaNumSplit(b->name());

            if (parts_a.size() != parts_b.size()) {
                return parts_a.size() < parts_b.size();
            }

            for (size_t i = 0; i < parts_a.size(); i++) {
                auto &part_a = parts_a.at(i);
                auto &part_b = parts_b.at(i);

                
                bool a_is_number, b_is_number;
                int a_number = part_a.toInt(&a_is_number);
                int b_number = part_b.toInt(&b_is_number);

                if (a_is_number && b_is_number) {
                    if (a_number != b_number) {
                        return a_number < b_number;
                    } else {
                        continue;
                    }
                }

                if (a_is_number != b_is_number) {
                    return a_is_number;
                }

                // both strings

                if (part_a == part_b) {
                    continue;
                }
                
                return part_a < part_b;
            }

            // both equal
            return true;
        });
    }
};

template <typename ElementT>
class ElementXYRoot : public StaticTreeItem
{
  public:
    using ElementMap = std::map<std::pair<int, int>, std::vector<ElementT>>;
    using ElementGetter = std::function<IdString(Context *, ElementT)>;


  private:
    Context *ctx_;
    std::vector<std::unique_ptr<StaticTreeItem>> managed_labels_;
    std::vector<std::unique_ptr<ElementList<ElementT>>> managed_lists_;
    ElementMap map_;
    ElementGetter getter_;
    ElementType child_type_;

  public:
    ElementXYRoot(Context *ctx, QString name, Item *parent, ElementMap map, ElementGetter getter, ElementType type) :
            StaticTreeItem(name, parent, ElementType::NONE), ctx_(ctx), map_(map), getter_(getter), child_type_(type)
    {
        std::vector<int> y_present;

        for (int i = 0; i < ctx->getGridDimX(); i++) {
            y_present.clear();
            // first find all the elements in all Y coordinates in this X
            for (int j = 0; j < ctx->getGridDimY(); j++) {
                if (map_.count(std::pair<int, int>(i, j)) == 0)
                    continue;
                y_present.push_back(j);
            }
            // no bels in any X coordinate? do not add X tree item.
            if (y_present.size() == 0)
                continue;

            // create X item for tree
            auto item = new StaticTreeItem(QString("X%1").arg(i), this, child_type_);
            managed_labels_.push_back(std::move(std::unique_ptr<StaticTreeItem>(item)));
            for (auto j : y_present) {
                auto item2 = new ElementList<ElementT>(ctx_, QString("Y%1").arg(j), item, &map_, i, j, getter_, child_type_);
                item2->fetchMore(1);
                managed_lists_.push_back(std::move(std::unique_ptr<ElementList<ElementT>>(item2)));
            }
        }
    }

    boost::optional<Item*> getById(IdString id)
    {
        // For now, scan linearly all ElementLists.
        // TODO(q3k) fix this once we have tree API from arch
        for (auto &l : managed_lists_) {
            auto res = l->getById(id);
            if (res) {
                return res;
            }
        }
        return boost::none;
    }
};

class Model : public QAbstractItemModel
{
  public:
    using BelXYRoot = ElementXYRoot<BelId>;
    using WireXYRoot = ElementXYRoot<WireId>;
    using PipXYRoot = ElementXYRoot<PipId>;

    Model(QObject *parent = nullptr);
    ~Model();

    void loadContext(Context *ctx);
    void updateCellsNets(Context *ctx);
    Item *nodeFromIndex(const QModelIndex &idx) const;
    QModelIndex indexFromNode(Item *node)
    {
        const Item *parent = node->parent();
        if (parent == nullptr)
            return QModelIndex();
        
        return createIndex(parent->indexOf(node), 0, node);
    }

    QList<QModelIndex> search(QString text);
    boost::optional<Item*> nodeForIdType(ElementType type, IdString id) const
    {
        switch (type) {
        case ElementType::BEL:
            return bel_root_->getById(id);
        case ElementType::WIRE:
            return wire_root_->getById(id);
        case ElementType::PIP:
            return pip_root_->getById(id);
        case ElementType::CELL:
            return cell_root_->getById(id);
        case ElementType::NET:
            return net_root_->getById(id);
        default:
            return boost::none;
        }
    }
 
    // Override QAbstractItemModel methods
    int rowCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
    int columnCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
    QModelIndex parent(const QModelIndex &child) const Q_DECL_OVERRIDE;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const Q_DECL_OVERRIDE;
    Qt::ItemFlags flags(const QModelIndex &index) const Q_DECL_OVERRIDE;
    void fetchMore(const QModelIndex &parent) Q_DECL_OVERRIDE;
    bool canFetchMore(const QModelIndex &parent) const Q_DECL_OVERRIDE;

  private:
    std::unique_ptr<Item> root_;
    std::unique_ptr<BelXYRoot> bel_root_;
    std::unique_ptr<WireXYRoot> wire_root_;
    std::unique_ptr<PipXYRoot> pip_root_;
    std::unique_ptr<IdStringList> cell_root_;
    std::unique_ptr<IdStringList> net_root_;
};

}; // namespace TreeModel

NEXTPNR_NAMESPACE_END

#endif // TREEMODEL_H

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

// Item is a leaf or non-leaf item in the TreeModel hierarchy. It does not
// manage any memory.
// It has a list of children, and when created it registers itself as a child
// of its parent.
// It has some PNR-specific members, like type (if any), idstring (if ay).
// They should be overwritten by deriving classes to make them relate to an
// object somewhere in the arch universe.
// It also has provisions for lazy loading of data, via the canFetchMore and
// fetchMore methods.
class Item
{
  protected:
    // Human-friendly name of this item.
    QString name_;
    // Parent or nullptr if root.
    Item *parent_;
    // Children that are loaded into memory.
    QList<Item *> children_;

    void addChild(Item *child) { children_.append(child); }

    void deleteChild(Item *child) { children_.removeAll(child); }

  public:
    Item(QString name, Item *parent) : name_(name), parent_(parent)
    {
        // Register in parent if exists.
        if (parent_ != nullptr) {
            parent_->addChild(this);
        }
    };

    // Number of children.
    int count() const { return children_.count(); }

    // Name getter.
    QString name() const { return name_; }

    // Child getter.
    Item *child(int index) { return children_.at(index); }

    // Parent getter.
    const Item *parent() const { return parent_; }
    Item *parent() { return parent_; }

    // indexOf gets index of child in children array.
    int indexOf(const Item *child) const
    {
        // Dropping the const for indexOf to work.
        return children_.indexOf((Item *)child, 0);
    }
    int indexOf(Item *child) { return children_.indexOf(child, 0); }

    // Arch id and type that correspond to this element.
    virtual IdString id() const { return IdString(); }
    virtual ElementType type() const { return ElementType::NONE; }

    // Lazy loading methods.
    virtual bool canFetchMore() const { return false; }
    virtual void fetchMore() {}

    virtual boost::optional<Item *> getById(IdString id) { return boost::none; }
    virtual void search(QList<Item *> &results, QString text, int limit) {}
    virtual void updateElements(Context *ctx, std::vector<IdString> elements) {}

    virtual ~Item()
    {
        if (parent_ != nullptr) {
            parent_->deleteChild(this);
        }
    }
};

// IdString is an Item that corresponds to a real element in Arch.
class IdStringItem : public Item
{
  private:
    IdString id_;
    ElementType type_;

  public:
    IdStringItem(Context *ctx, IdString str, Item *parent, ElementType type)
            : Item(QString(str.c_str(ctx)), parent), id_(str), type_(type)
    {
    }

    virtual IdString id() const override { return id_; }

    virtual ElementType type() const override { return type_; }
};

// IdString list is a static list of IdStrings which can be set/updates from
// a vector of IdStrings. It will render each IdStrings as a child, with the
// list sorted in a smart way.
class IdStringList : public Item
{
  private:
    // Children that we manage the memory for, stored for quick lookup from
    // IdString to child.
    std::unordered_map<IdString, std::unique_ptr<IdStringItem>> managed_;
    // Type of children that the list creates.
    ElementType child_type_;

  public:
    // Create an IdStringList at given partent that will contain elements of
    // the given type.
    IdStringList(ElementType type) : Item("root", nullptr), child_type_(type) {}

    // Split a name into alpha/non-alpha parts, which is then used for sorting
    // of children.
    static std::vector<QString> alphaNumSplit(const QString &str);

    // getById finds a child for the given IdString.
    virtual boost::optional<Item *> getById(IdString id) override { return managed_.at(id).get(); }

    // (Re-)create children from a list of IdStrings.
    virtual void updateElements(Context *ctx, std::vector<IdString> elements) override;

    // Find children that contain the given text.
    virtual void search(QList<Item *> &results, QString text, int limit) override;
};

// ElementList is a dynamic list of ElementT (BelId,WireId,...) that are
// automatically generated based on an overall map of elements.
// ElementList is emitted from ElementXYRoot, and contains the actual
// Bels/Wires/Pips underneath it.
template <typename ElementT> class ElementList : public Item
{
  public:
    // A map from tile (X,Y) to list of ElementTs in that tile.
    using ElementMap = std::map<std::pair<int, int>, std::vector<ElementT>>;
    // A method that converts an ElementT to an IdString.
    using ElementGetter = std::function<IdString(Context *, ElementT)>;

  private:
    Context *ctx_;
    // ElementMap given to use by our constructor.
    const ElementMap *map_;
    // The X, Y that this list handles.
    int x_, y_;
    ElementGetter getter_;
    // Children that we manage the memory for, stored for quick lookup from
    // IdString to child.
    std::unordered_map<IdString, std::unique_ptr<Item>> managed_;
    // Type of children that he list creates.
    ElementType child_type_;

    // Gets elements that this list should create from the map. This pointer is
    // short-lived (as it will change when the map mutates.
    const std::vector<ElementT> *elements() const { return &map_->at(std::make_pair(x_, y_)); }

  public:
    ElementList(Context *ctx, QString name, Item *parent, ElementMap *map, int x, int y, ElementGetter getter,
                ElementType type)
            : Item(name, parent), ctx_(ctx), map_(map), x_(x), y_(y), getter_(getter), child_type_(type)
    {
    }

    // Lazy loading of elements.

    virtual bool canFetchMore() const override { return (size_t)children_.size() < elements()->size(); }

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
            managed_[idstring] = std::move(std::unique_ptr<Item>(item));
        }
    }

    virtual void fetchMore() override { fetchMore(100); }

    // getById finds a child for the given IdString.
    virtual boost::optional<Item *> getById(IdString id) override
    {
        // Search requires us to load all our elements...
        while (canFetchMore())
            fetchMore();

        auto res = managed_.find(id);
        if (res != managed_.end()) {
            return res->second.get();
        }
        return boost::none;
    }

    // Find children that contain the given text.
    virtual void search(QList<Item *> &results, QString text, int limit) override
    {
        // Last chance to bail out from loading entire tree into memory.
        if (limit != -1 && results.size() > limit)
            return;

        // Search requires us to load all our elements...
        while (canFetchMore())
            fetchMore();

        for (const auto &child : children_) {
            if (limit != -1 && results.size() > limit)
                return;
            if (child->name().contains(text))
                results.push_back(child);
        }
    }
};

// ElementXYRoot is the root of an ElementT multi-level lazy loading list.
// It can take any of {BelId,WireId,PipId} and create a tree that
// hierarchizes them by X and Y tile positions, when given a map from X,Y to
// list of ElementTs in that tile.
template <typename ElementT> class ElementXYRoot : public Item
{
  public:
    // A map from tile (X,Y) to list of ElementTs in that tile.
    using ElementMap = std::map<std::pair<int, int>, std::vector<ElementT>>;
    // A method that converts an ElementT to an IdString.
    using ElementGetter = std::function<IdString(Context *, ElementT)>;

  private:
    Context *ctx_;
    // X-index children that we manage the memory for.
    std::vector<std::unique_ptr<Item>> managed_labels_;
    // Y-index children (ElementLists) that we manage the memory for.
    std::vector<std::unique_ptr<ElementList<ElementT>>> managed_lists_;
    // Source of truth for elements to display.
    ElementMap map_;
    ElementGetter getter_;
    // Type of children that he list creates in X->Y->...
    ElementType child_type_;

  public:
    ElementXYRoot(Context *ctx, ElementMap map, ElementGetter getter, ElementType type)
            : Item("root", nullptr), ctx_(ctx), map_(map), getter_(getter), child_type_(type)
    {
        // Create all X and Y label Items/ElementLists.

        // Y coordinates at which an element exists for a given X - taken out
        // of loop to limit heap allocation/deallocation.
        std::vector<int> y_present;

        for (int i = 0; i < ctx->getGridDimX(); i++) {
            y_present.clear();
            // First find all the elements in all Y coordinates in this X.
            for (int j = 0; j < ctx->getGridDimY(); j++) {
                if (map_.count(std::make_pair(i, j)) == 0)
                    continue;
                y_present.push_back(j);
            }
            // No elements in any X coordinate? Do not add X tree item.
            if (y_present.size() == 0)
                continue;

            // Create X list Item.
            auto item = new Item(QString("X%1").arg(i), this);
            managed_labels_.push_back(std::move(std::unique_ptr<Item>(item)));

            for (auto j : y_present) {
                // Create Y list ElementList.
                auto item2 =
                        new ElementList<ElementT>(ctx_, QString("Y%1").arg(j), item, &map_, i, j, getter_, child_type_);
                // Pre-populate list with one element, other Qt will never ask for more.
                item2->fetchMore(1);
                managed_lists_.push_back(std::move(std::unique_ptr<ElementList<ElementT>>(item2)));
            }
        }
    }

    // getById finds a child for the given IdString.
    virtual boost::optional<Item *> getById(IdString id) override
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

    // Find children that contain the given text.
    virtual void search(QList<Item *> &results, QString text, int limit) override
    {
        for (auto &l : managed_lists_) {
            if (limit != -1 && results.size() > limit)
                return;
            l->search(results, text, limit);
        }
    }
};

class Model : public QAbstractItemModel
{
  private:
    Context *ctx_ = nullptr;

  public:
    Model(QObject *parent = nullptr);
    ~Model();

    void loadData(Context *ctx, std::unique_ptr<Item> data);
    void updateElements(std::vector<IdString> elements);
    Item *nodeFromIndex(const QModelIndex &idx) const;
    QModelIndex indexFromNode(Item *node)
    {
        const Item *parent = node->parent();
        if (parent == nullptr)
            return QModelIndex();

        return createIndex(parent->indexOf(node), 0, node);
    }

    QList<QModelIndex> search(QString text);

    boost::optional<Item *> nodeForId(IdString id) const { return root_->getById(id); }

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
    // Tree elements that we manage the memory for.
    std::unique_ptr<Item> root_;
};

}; // namespace TreeModel

NEXTPNR_NAMESPACE_END

#endif // TREEMODEL_H

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

class LazyTreeItem
{
  protected:
    QString name_;
    LazyTreeItem *parent_;
    QList<LazyTreeItem *> children_;

    void addChild(LazyTreeItem *child)
    {
        children_.append(child);
    }

  public:
    LazyTreeItem(QString name, LazyTreeItem *parent) :
            name_(name), parent_(parent)
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

    LazyTreeItem *child(int index)
    {
        return children_.at(index);
    }

    int indexOf(LazyTreeItem *child) const
    { 
        return children_.indexOf(child, 0);
    }

    LazyTreeItem *parent()
    {
        return parent_;
    }

    virtual bool canFetchMore() const = 0;
    virtual void fetchMore() = 0;
    virtual ElementType type() const = 0;
    virtual IdString id() const = 0;

    virtual ~LazyTreeItem() {}
};

class StaticTreeItem : public LazyTreeItem
{
  public:
    using LazyTreeItem::LazyTreeItem;

    virtual bool canFetchMore() const override
    {
        return false;
    }

    virtual void fetchMore() override
    {
    }

    virtual ~StaticTreeItem() {}

    virtual ElementType type() const override
    {
        return ElementType::NONE;
    }
    
    virtual IdString id() const override
    {
        return IdString();
    }
};

template <typename ElementT>
class ElementList : public LazyTreeItem
{
  public:
    using ElementMap = std::map<std::pair<int, int>, std::vector<ElementT>>;
    using ElementGetter = std::function<IdString(Context *, ElementT)>;

  private:
    Context *ctx_;
    const ElementMap *map_;
    int x_, y_;
    ElementGetter getter_;
    std::vector<std::unique_ptr<StaticTreeItem>> managed_;

    // scope valid until map gets mutated...
    const std::vector<ElementT> *elements() const
    {
        return &map_->at(std::pair<int, int>(x_, y_));
    }

  public:
    ElementList(Context *ctx, QString name, LazyTreeItem *parent, ElementMap *map, int x, int y, ElementGetter getter) :
            LazyTreeItem(name, parent), ctx_(ctx), map_(map), x_(x), y_(y), getter_(getter)
    {
    }

    virtual bool canFetchMore() const override
    {
        return children_.size() < elements()->size();
    }

    void fetchMore(int count)
    {
        int start = children_.size();
        size_t end = std::min(start + count, (int)elements()->size());
        for (int i = start; i < end; i++) {
            QString name(getter_(ctx_, elements()->at(i)).c_str(ctx_));

            // Remove X.../Y.../ prefix
            QString prefix = QString("X%1/Y%2/").arg(x_).arg(y_);
            if (name.startsWith(prefix))
                name.remove(0, prefix.size());

            auto item = new StaticTreeItem(name, this);
            managed_.push_back(std::move(std::unique_ptr<StaticTreeItem>(item)));
        }
    }

    virtual void fetchMore() override
    {
         fetchMore(100);
    }

    virtual ElementType type() const override
    {
        return ElementType::NONE;
    }
    
    virtual IdString id() const override
    {
        return IdString();
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
    std::vector<std::unique_ptr<LazyTreeItem>> bels_;
    ElementMap map_;
    ElementGetter getter_;

  public:
    ElementXYRoot(Context *ctx, QString name, LazyTreeItem *parent, ElementMap map, ElementGetter getter) :
            StaticTreeItem(name, parent), ctx_(ctx), map_(map), getter_(getter)
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
            auto item = new StaticTreeItem(QString("X%1").arg(i), this);
            bels_.push_back(std::move(std::unique_ptr<LazyTreeItem>(item)));
            for (auto j : y_present) {
                auto item2 = new ElementList<ElementT>(ctx_, QString("Y%1").arg(j), item, &map_, i, j, getter_);
                item2->fetchMore(1);
                bels_.push_back(std::move(std::unique_ptr<LazyTreeItem>(item2)));
            }
        }
    }
};

class ContextTreeModel : public QAbstractItemModel
{
  public:
    using BelXYRoot = ElementXYRoot<BelId>;
    using WireXYRoot = ElementXYRoot<WireId>;
    using PipXYRoot = ElementXYRoot<PipId>;

    ContextTreeModel(QObject *parent = nullptr);
    ~ContextTreeModel();

    void loadData(Context *ctx);
    void updateData(Context *ctx);
    LazyTreeItem *nodeFromIndex(const QModelIndex &idx) const;
    //QModelIndex indexFromNode(ContextTreeItem *node);
    //ContextTreeItem *nodeForIdType(const ElementType type, const QString name) const;
 
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
    std::unique_ptr<LazyTreeItem> root_;
    std::unique_ptr<BelXYRoot> bel_root_;
    std::unique_ptr<WireXYRoot> wire_root_;
    std::unique_ptr<PipXYRoot> pip_root_;
    //std::unique_ptr<ElementXYRoot> wires_root_;
    //std::unique_ptr<ElementXYRoot> pips_root_;
    //QMap<QString, ContextTreeItem *> nameToItem[6];
    //ContextTreeItem *nets_root;
    //ContextTreeItem *cells_root;
};

NEXTPNR_NAMESPACE_END

#endif // TREEMODEL_H

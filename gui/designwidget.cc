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

#include "designwidget.h"
#include <QAction>
#include <QGridLayout>
#include <QMenu>
#include <QSplitter>
#include <QTreeWidgetItem>
#include "fpgaviewwidget.h"

NEXTPNR_NAMESPACE_BEGIN

enum class ElementType
{
    NONE,
    BEL,
    WIRE,
    PIP
};

class ElementTreeItem : public QTreeWidgetItem
{
  public:
    ElementTreeItem(ElementType t, QString str, QTreeWidgetItem *parent) : QTreeWidgetItem(parent, QStringList(str)), type(t) {}
    virtual ~ElementTreeItem(){};

    ElementType getType() { return type; };

  private:
    ElementType type;
};

class BelTreeItem : public ElementTreeItem
{
  public:
    BelTreeItem(IdString d, QString str, QTreeWidgetItem *parent) : ElementTreeItem(ElementType::BEL, str, parent) { this->data = d; }
    virtual ~BelTreeItem(){};

    IdString getData() { return this->data; };

  private:
    IdString data;
};

class WireTreeItem : public ElementTreeItem
{
  public:
    WireTreeItem(IdString d, QString str, QTreeWidgetItem *parent) : ElementTreeItem(ElementType::WIRE, str, parent) { this->data = d; }
    virtual ~WireTreeItem(){};

    IdString getData() { return this->data; };

  private:
    IdString data;
};

class PipTreeItem : public ElementTreeItem
{
  public:
    PipTreeItem(IdString d, QString str, QTreeWidgetItem *parent) : ElementTreeItem(ElementType::PIP, str, parent) { this->data = d; }
    virtual ~PipTreeItem(){};

    IdString getData() { return this->data; };

  private:
    IdString data;
};

DesignWidget::DesignWidget(QWidget *parent) : QWidget(parent), ctx(nullptr), nets_root(nullptr), cells_root(nullptr)
{

    treeWidget = new QTreeWidget();

    // Add tree view
    treeWidget->setColumnCount(1);
    treeWidget->setHeaderLabel(QString("Items"));
    treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    // Add property view
    variantManager = new QtVariantPropertyManager();
    variantFactory = new QtVariantEditorFactory();
    propertyEditor = new QtTreePropertyBrowser();
    propertyEditor->setFactoryForManager(variantManager, variantFactory);
    propertyEditor->setPropertiesWithoutValueMarked(true);
    propertyEditor->setRootIsDecorated(false);

    propertyEditor->show();

    QSplitter *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(treeWidget);
    splitter->addWidget(propertyEditor);

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(splitter);
    setLayout(mainLayout);

    // Connection
    connect(treeWidget, &QTreeWidget::customContextMenuRequested, this, &DesignWidget::prepareMenu);

    connect(treeWidget, SIGNAL(itemClicked(QTreeWidgetItem *, int)), SLOT(onItemClicked(QTreeWidgetItem *, int)));
}

DesignWidget::~DesignWidget()
{
    delete variantManager;
    delete variantFactory;
    delete propertyEditor;
}

void DesignWidget::newContext(Context *ctx)
{
    treeWidget->clear();
    this->ctx = ctx;

    // Add bels to tree
    QTreeWidgetItem *bel_root = new QTreeWidgetItem(treeWidget);
    QMap<QString, QTreeWidgetItem *> bel_items;
    bel_root->setText(0, QString("Bels"));
    treeWidget->insertTopLevelItem(0, bel_root);    
    if (ctx) {
        for (auto bel : ctx->getBels()) {
            auto id = ctx->getBelName(bel);
            QStringList items = QString(id.c_str(ctx)).split("/");
            QString name;
            QTreeWidgetItem *parent = nullptr;
            for(int i=0;i<items.size();i++)
            {
                if (!name.isEmpty()) name += "/";
                name += items.at(i);
                if (!bel_items.contains(name)) {
                    if (i==items.size()-1)
                        bel_items.insert(name,new BelTreeItem(id, items.at(i),parent));
                    else
                        bel_items.insert(name,new ElementTreeItem(ElementType::NONE, items.at(i),parent));
                } 
                parent = bel_items[name];
            }
        }
    }
    for (auto bel : bel_items.toStdMap()) {        
        bel_root->addChild(bel.second);
    }

    // Add wires to tree
    QTreeWidgetItem *wire_root = new QTreeWidgetItem(treeWidget);
    QMap<QString, QTreeWidgetItem *> wire_items;
    wire_root->setText(0, QString("Wires"));
    treeWidget->insertTopLevelItem(0, wire_root);    
    if (ctx) {
        for (auto wire : ctx->getWires()) {
            auto id = ctx->getWireName(wire);
            QStringList items = QString(id.c_str(ctx)).split("/");
            QString name;
            QTreeWidgetItem *parent = nullptr;
            for(int i=0;i<items.size();i++)
            {
                if (!name.isEmpty()) name += "/";
                name += items.at(i);
                if (!wire_items.contains(name)) {
                    if (i==items.size()-1)
                        wire_items.insert(name,new WireTreeItem(id, items.at(i),parent));
                    else
                        wire_items.insert(name,new ElementTreeItem(ElementType::NONE, items.at(i),parent));
                } 
                parent = wire_items[name];
            }
        }
    }
    for (auto wire : wire_items.toStdMap()) {        
        wire_root->addChild(wire.second);
    }

    // Add pips to tree
    QTreeWidgetItem *pip_root = new QTreeWidgetItem(treeWidget);
    QMap<QString, QTreeWidgetItem *> pip_items;
    pip_root->setText(0, QString("Pips"));
    treeWidget->insertTopLevelItem(0, pip_root);
    if (ctx) {
        for (auto pip : ctx->getPips()) {
            auto id = ctx->getPipName(pip);
            QStringList items = QString(id.c_str(ctx)).split("/");
            QString name;
            QTreeWidgetItem *parent = nullptr;
            for(int i=0;i<items.size();i++)
            {
                if (!name.isEmpty()) name += "/";
                name += items.at(i);
                if (!pip_items.contains(name)) {
                    if (i==items.size()-1)
                        pip_items.insert(name,new PipTreeItem(id, items.at(i),parent));
                    else
                        pip_items.insert(name,new ElementTreeItem(ElementType::NONE, items.at(i),parent));
                } 
                parent = pip_items[name];
            }
        }
    }
    for (auto pip : pip_items.toStdMap()) {        
        pip_root->addChild(pip.second);
    }

    // Add nets to tree
    nets_root = new QTreeWidgetItem(treeWidget);
    nets_root->setText(0, QString("Nets"));
    treeWidget->insertTopLevelItem(0, nets_root);    

    // Add cells to tree
    cells_root = new QTreeWidgetItem(treeWidget);
    cells_root->setText(0, QString("Cells"));
    treeWidget->insertTopLevelItem(0, cells_root);    

}

void DesignWidget::updateTree()
{
    delete nets_root;
    delete cells_root;

    // Add nets to tree
    nets_root = new QTreeWidgetItem(treeWidget);
    QMap<QString, QTreeWidgetItem *> nets_items;
    nets_root->setText(0, QString("Nets"));
    treeWidget->insertTopLevelItem(0, nets_root);   
    if (ctx) {
        for (auto& item : ctx->nets) {
            auto id = item.first;
            QString name = QString(id.c_str(ctx));
            nets_items.insert(name,new ElementTreeItem(ElementType::NONE, name, nullptr));
        }
    }
    for (auto item : nets_items.toStdMap()) {        
        nets_root->addChild(item.second);
    }    

    // Add cells to tree
    cells_root = new QTreeWidgetItem(treeWidget);
    QMap<QString, QTreeWidgetItem *> cells_items;
    cells_root->setText(0, QString("Cells"));
    treeWidget->insertTopLevelItem(0, cells_root);   
    if (ctx) {
        for (auto& item : ctx->cells) {
            auto id = item.first;
            QString name = QString(id.c_str(ctx));
            cells_items.insert(name,new ElementTreeItem(ElementType::NONE, name, nullptr));
        }
    }
    for (auto item : cells_items.toStdMap()) {        
        cells_root->addChild(item.second);
    }    
 
}

void DesignWidget::addProperty(QtVariantProperty *property, const QString &id)
{
    propertyToId[property] = id;
    idToProperty[id] = property;
    propertyEditor->addProperty(property);
}

void DesignWidget::clearProperties()
{
    QMap<QtProperty *, QString>::ConstIterator itProp = propertyToId.constBegin();
    while (itProp != propertyToId.constEnd()) {
        delete itProp.key();
        itProp++;
    }
    propertyToId.clear();
    idToProperty.clear();
}

void DesignWidget::onItemClicked(QTreeWidgetItem *item, int pos)
{
    if (!item->parent())
        return;


    ElementType type = static_cast<ElementTreeItem *>(item)->getType();
    if (type == ElementType::NONE) {
        return;
    }
    
    clearProperties();
    if (type == ElementType::BEL) {
        IdString c = static_cast<BelTreeItem *>(item)->getData();

        BelType type = ctx->getBelType(ctx->getBelByName(c));
        QtVariantProperty *topItem = variantManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(c.c_str(ctx)));
        addProperty(topItem, QString("Name"));

        QtVariantProperty *typeItem = variantManager->addProperty(QVariant::String, QString("Type"));
        typeItem->setValue(QString(ctx->belTypeToId(type).c_str(ctx)));
        addProperty(typeItem, QString("Type"));

    } else if (type == ElementType::WIRE) {
        IdString c = static_cast<WireTreeItem *>(item)->getData();

        QtVariantProperty *topItem = variantManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(c.c_str(ctx)));
        addProperty(topItem, QString("Name"));

    } else if (type == ElementType::PIP) {
        IdString c = static_cast<PipTreeItem *>(item)->getData();

        QtVariantProperty *topItem = variantManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(c.c_str(ctx)));
        addProperty(topItem, QString("Name"));
    }
}

void DesignWidget::prepareMenu(const QPoint &pos)
{
    QTreeWidget *tree = treeWidget;

    itemContextMenu = tree->itemAt(pos);

    QAction *selectAction = new QAction("&Select", this);
    selectAction->setStatusTip("Select item on view");

    connect(selectAction, SIGNAL(triggered()), this, SLOT(selectObject()));

    QMenu menu(this);
    menu.addAction(selectAction);

    menu.exec(tree->mapToGlobal(pos));
}

void DesignWidget::selectObject() { Q_EMIT info("selected " + itemContextMenu->text(0).toStdString() + "\n"); }

NEXTPNR_NAMESPACE_END

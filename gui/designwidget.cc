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
    PIP,
    NET,
    CELL
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

class IdStringTreeItem : public ElementTreeItem
{
  public:
    IdStringTreeItem(IdString d, ElementType t, QString str, QTreeWidgetItem *parent) : ElementTreeItem(t, str, parent) { this->data = d; }
    virtual ~IdStringTreeItem(){};

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
    readOnlyManager = new QtVariantPropertyManager(this);
    groupManager = new QtGroupPropertyManager(this);
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
    delete readOnlyManager;
    delete groupManager;
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
                        bel_items.insert(name,new IdStringTreeItem(id, ElementType::BEL, items.at(i),parent));
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
                        wire_items.insert(name,new IdStringTreeItem(id, ElementType::WIRE, items.at(i),parent));
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
                        pip_items.insert(name,new IdStringTreeItem(id, ElementType::PIP, items.at(i),parent));
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
    clearProperties();
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
            nets_items.insert(name, new IdStringTreeItem(id, ElementType::NET, name, nullptr));
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
            cells_items.insert(name,new IdStringTreeItem(id, ElementType::CELL,name, nullptr));
        }
    }
    for (auto item : cells_items.toStdMap()) {        
        cells_root->addChild(item.second);
    }    
 
}

void DesignWidget::addProperty(QtProperty *property, const QString &id)
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
        IdString c = static_cast<IdStringTreeItem *>(item)->getData();

        BelType type = ctx->getBelType(ctx->getBelByName(c));
        QtVariantProperty *topItem = readOnlyManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(c.c_str(ctx)));
        addProperty(topItem, QString("Name"));

        QtVariantProperty *typeItem = readOnlyManager->addProperty(QVariant::String, QString("Type"));
        typeItem->setValue(QString(ctx->belTypeToId(type).c_str(ctx)));
        addProperty(typeItem, QString("Type"));

    } else if (type == ElementType::WIRE) {
        IdString c = static_cast<IdStringTreeItem *>(item)->getData();

        QtVariantProperty *topItem = readOnlyManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(c.c_str(ctx)));
        addProperty(topItem, QString("Name"));

    } else if (type == ElementType::PIP) {
        IdString c = static_cast<IdStringTreeItem *>(item)->getData();

        QtVariantProperty *topItem = readOnlyManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(c.c_str(ctx)));
        addProperty(topItem, QString("Name"));
    } else if (type == ElementType::NET) {
        IdString c = static_cast<IdStringTreeItem *>(item)->getData();

        NetInfo *net = ctx->nets.at(c).get();

        QtVariantProperty *topItem = readOnlyManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(net->name.c_str(ctx)));
        addProperty(topItem, QString("Name"));

        QtVariantProperty *portItem = readOnlyManager->addProperty(QVariant::String, QString("Port"));
        portItem->setValue(QString(net->driver.port.c_str(ctx)));
        addProperty(portItem, QString("Port"));
        
        QtVariantProperty *budgetItem = readOnlyManager->addProperty(QVariant::Double, QString("Budget"));
        budgetItem->setValue(net->driver.budget);
        addProperty(budgetItem, QString("Budget"));

        if (net->driver.cell) {
            CellInfo *cell = net->driver.cell;
            QtProperty *cellItem = groupManager->addProperty(QString("Cell"));
            addProperty(cellItem, QString("Cell"));

            QtVariantProperty *cellNameItem = readOnlyManager->addProperty(QVariant::String, QString("Name"));
            cellNameItem->setValue(QString(cell->name.c_str(ctx)));
            cellItem->addSubProperty(cellNameItem);

            QtVariantProperty *cellTypeItem = readOnlyManager->addProperty(QVariant::String, QString("Type"));
            cellTypeItem->setValue(QString(cell->type.c_str(ctx)));
            cellItem->addSubProperty(cellTypeItem);

            QtProperty *cellPortsItem = groupManager->addProperty(QString("Ports"));
            cellItem->addSubProperty(cellPortsItem);
            for(auto &item : cell->ports)
            {
                PortInfo p = item.second;
                
                QtProperty *portInfoItem = groupManager->addProperty(QString(p.name.c_str(ctx)));

                QtVariantProperty *portInfoNameItem = readOnlyManager->addProperty(QVariant::String, QString("Name"));
                portInfoNameItem->setValue(QString(p.name.c_str(ctx)));
                portInfoItem->addSubProperty(portInfoNameItem);

                QtVariantProperty *portInfoTypeItem = readOnlyManager->addProperty(QVariant::Int, QString("Type"));
                portInfoTypeItem->setValue(int(p.type));
                portInfoItem->addSubProperty(portInfoTypeItem);

                cellPortsItem->addSubProperty(portInfoItem);
            }

            QtProperty *cellAttrItem = groupManager->addProperty(QString("Attributes"));
            cellItem->addSubProperty(cellAttrItem);
            for(auto &item : cell->attrs)
            {
                QtVariantProperty *attrItem = readOnlyManager->addProperty(QVariant::String, QString(item.first.c_str(ctx)));
                attrItem->setValue(QString(item.second.c_str()));
                cellAttrItem->addSubProperty(attrItem);
            }

            QtProperty *cellParamsItem = groupManager->addProperty(QString("Parameters"));
            cellItem->addSubProperty(cellParamsItem);
            for(auto &item : cell->params)
            {
                QtVariantProperty *paramItem = readOnlyManager->addProperty(QVariant::String, QString(item.first.c_str(ctx)));
                paramItem->setValue(QString(item.second.c_str()));
                cellParamsItem->addSubProperty(paramItem);
            }
        }

    } else if (type == ElementType::CELL) {
        IdString c = static_cast<IdStringTreeItem *>(item)->getData();

        QtVariantProperty *topItem = readOnlyManager->addProperty(QVariant::String, QString("Name"));
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

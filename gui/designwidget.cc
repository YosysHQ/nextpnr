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
    ElementTreeItem(ElementType t, QString str, QTreeWidgetItem *parent)
            : QTreeWidgetItem(parent, QStringList(str)), type(t)
    {
    }
    virtual ~ElementTreeItem(){};

    ElementType getType() { return type; };

  private:
    ElementType type;
};

class IdStringTreeItem : public ElementTreeItem
{
  public:
    IdStringTreeItem(IdString d, ElementType t, QString str, QTreeWidgetItem *parent) : ElementTreeItem(t, str, parent)
    {
        this->data = d;
    }
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
    treeWidget->setHeaderLabel("Items");
    treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    // Add property view
    variantManager = new QtVariantPropertyManager(this);
    readOnlyManager = new QtVariantPropertyManager(this);
    groupManager = new QtGroupPropertyManager(this);
    variantFactory = new QtVariantEditorFactory(this);
    propertyEditor = new QtTreePropertyBrowser(this);
    propertyEditor->setFactoryForManager(variantManager, variantFactory);
    propertyEditor->setPropertiesWithoutValueMarked(true);

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

DesignWidget::~DesignWidget() {}

void DesignWidget::newContext(Context *ctx)
{
    treeWidget->clear();
    this->ctx = ctx;

    // Add bels to tree
    QTreeWidgetItem *bel_root = new QTreeWidgetItem(treeWidget);
    QMap<QString, QTreeWidgetItem *> bel_items;
    bel_root->setText(0, "Bels");
    treeWidget->insertTopLevelItem(0, bel_root);
    if (ctx) {
        for (auto bel : ctx->getBels()) {
            auto id = ctx->getBelName(bel);
            QStringList items = QString(id.c_str(ctx)).split("/");
            QString name;
            QTreeWidgetItem *parent = nullptr;
            for (int i = 0; i < items.size(); i++) {
                if (!name.isEmpty())
                    name += "/";
                name += items.at(i);
                if (!bel_items.contains(name)) {
                    if (i == items.size() - 1)
                        bel_items.insert(name, new IdStringTreeItem(id, ElementType::BEL, items.at(i), parent));
                    else
                        bel_items.insert(name, new ElementTreeItem(ElementType::NONE, items.at(i), parent));
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
    wire_root->setText(0, "Wires");
    treeWidget->insertTopLevelItem(0, wire_root);
    if (ctx) {
        for (auto wire : ctx->getWires()) {
            auto id = ctx->getWireName(wire);
            QStringList items = QString(id.c_str(ctx)).split("/");
            QString name;
            QTreeWidgetItem *parent = nullptr;
            for (int i = 0; i < items.size(); i++) {
                if (!name.isEmpty())
                    name += "/";
                name += items.at(i);
                if (!wire_items.contains(name)) {
                    if (i == items.size() - 1)
                        wire_items.insert(name, new IdStringTreeItem(id, ElementType::WIRE, items.at(i), parent));
                    else
                        wire_items.insert(name, new ElementTreeItem(ElementType::NONE, items.at(i), parent));
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
    pip_root->setText(0, "Pips");
    treeWidget->insertTopLevelItem(0, pip_root);
    if (ctx) {
        for (auto pip : ctx->getPips()) {
            auto id = ctx->getPipName(pip);
            QStringList items = QString(id.c_str(ctx)).split("/");
            QString name;
            QTreeWidgetItem *parent = nullptr;
            for (int i = 0; i < items.size(); i++) {
                if (!name.isEmpty())
                    name += "/";
                name += items.at(i);
                if (!pip_items.contains(name)) {
                    if (i == items.size() - 1)
                        pip_items.insert(name, new IdStringTreeItem(id, ElementType::PIP, items.at(i), parent));
                    else
                        pip_items.insert(name, new ElementTreeItem(ElementType::NONE, items.at(i), parent));
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
    nets_root->setText(0, "Nets");
    treeWidget->insertTopLevelItem(0, nets_root);

    // Add cells to tree
    cells_root = new QTreeWidgetItem(treeWidget);
    cells_root->setText(0, "Cells");
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
    nets_root->setText(0, "Nets");
    treeWidget->insertTopLevelItem(0, nets_root);
    if (ctx) {
        for (auto &item : ctx->nets) {
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
    cells_root->setText(0, "Cells");
    treeWidget->insertTopLevelItem(0, cells_root);
    if (ctx) {
        for (auto &item : ctx->cells) {
            auto id = item.first;
            QString name = QString(id.c_str(ctx));
            cells_items.insert(name, new IdStringTreeItem(id, ElementType::CELL, name, nullptr));
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

        QtProperty *topItem = groupManager->addProperty("Bel");
        addProperty(topItem, "Bel");

        QtVariantProperty *nameItem = readOnlyManager->addProperty(QVariant::String, "Name");
        nameItem->setValue(c.c_str(ctx));
        topItem->addSubProperty(nameItem);

        QtVariantProperty *typeItem = readOnlyManager->addProperty(QVariant::String, "Type");
        typeItem->setValue(ctx->belTypeToId(type).c_str(ctx));
        topItem->addSubProperty(typeItem);

    } else if (type == ElementType::WIRE) {
        IdString c = static_cast<IdStringTreeItem *>(item)->getData();

        QtProperty *topItem = groupManager->addProperty("Wire");
        addProperty(topItem, "Wire");

        QtVariantProperty *nameItem = readOnlyManager->addProperty(QVariant::String, "Name");
        nameItem->setValue(c.c_str(ctx));
        topItem->addSubProperty(nameItem);

    } else if (type == ElementType::PIP) {
        IdString c = static_cast<IdStringTreeItem *>(item)->getData();

        QtProperty *topItem = groupManager->addProperty("Pip");
        addProperty(topItem, "Pip");

        QtVariantProperty *nameItem = readOnlyManager->addProperty(QVariant::String, "Name");
        nameItem->setValue(c.c_str(ctx));
        topItem->addSubProperty(nameItem);

    } else if (type == ElementType::NET) {
        IdString c = static_cast<IdStringTreeItem *>(item)->getData();
        NetInfo *net = ctx->nets.at(c).get();

        QtProperty *topItem = groupManager->addProperty("Net");
        addProperty(topItem, "Net");

        QtVariantProperty *nameItem = readOnlyManager->addProperty(QVariant::String, "Name");
        nameItem->setValue(net->name.c_str(ctx));
        topItem->addSubProperty(nameItem);

        QtProperty *driverItem = groupManager->addProperty("Driver");
        topItem->addSubProperty(driverItem);

        QtVariantProperty *portItem = readOnlyManager->addProperty(QVariant::String, "Port");
        portItem->setValue(net->driver.port.c_str(ctx));
        driverItem->addSubProperty(portItem);

        QtVariantProperty *budgetItem = readOnlyManager->addProperty(QVariant::Double, "Budget");
        budgetItem->setValue(net->driver.budget);
        driverItem->addSubProperty(budgetItem);

        QtVariantProperty *cellNameItem = readOnlyManager->addProperty(QVariant::String, "Cell");
        if (net->driver.cell)
            cellNameItem->setValue(net->driver.cell->name.c_str(ctx));
        else
            cellNameItem->setValue("");
        driverItem->addSubProperty(cellNameItem);

        QtProperty *usersItem = groupManager->addProperty("Users");
        topItem->addSubProperty(usersItem);
        for (auto &item : net->users) {
            QtProperty *portItem = groupManager->addProperty(item.port.c_str(ctx));
            usersItem->addSubProperty(portItem);

            QtVariantProperty *nameItem = readOnlyManager->addProperty(QVariant::String, "Port");
            nameItem->setValue(item.port.c_str(ctx));
            portItem->addSubProperty(nameItem);

            QtVariantProperty *budgetItem = readOnlyManager->addProperty(QVariant::Double, "Budget");
            budgetItem->setValue(item.budget);
            portItem->addSubProperty(budgetItem);

            QtVariantProperty *userItem = readOnlyManager->addProperty(QVariant::String, "Cell");
            if (item.cell)
                userItem->setValue(item.cell->name.c_str(ctx));
            else
                userItem->setValue("");
            portItem->addSubProperty(userItem);
        }

        QtProperty *attrsItem = groupManager->addProperty("Attributes");
        topItem->addSubProperty(attrsItem);
        for (auto &item : net->attrs) {
            QtVariantProperty *attrItem = readOnlyManager->addProperty(QVariant::String, item.first.c_str(ctx));
            attrItem->setValue(item.second.c_str());
            attrsItem->addSubProperty(attrItem);
        }

        QtProperty *wiresItem = groupManager->addProperty("Wires");
        topItem->addSubProperty(wiresItem);
        for (auto &item : net->wires) {
            auto name = ctx->getWireName(item.first).c_str(ctx);

            QtProperty *wireItem = groupManager->addProperty(name);

            QtVariantProperty *nameItem = readOnlyManager->addProperty(QVariant::String, "Name");
            nameItem->setValue(name);
            wireItem->addSubProperty(nameItem);

            QtVariantProperty *pipItem = readOnlyManager->addProperty(QVariant::String, "Pip");

            if (item.second.pip != PipId())
                pipItem->setValue(ctx->getPipName(item.second.pip).c_str(ctx));
            else
                pipItem->setValue("");
            wireItem->addSubProperty(pipItem);

            QtVariantProperty *strengthItem = readOnlyManager->addProperty(QVariant::Int, "Strength");
            strengthItem->setValue((int)item.second.strength);
            wireItem->addSubProperty(strengthItem);

            wiresItem->addSubProperty(wireItem);
        }

    } else if (type == ElementType::CELL) {
        IdString c = static_cast<IdStringTreeItem *>(item)->getData();
        CellInfo *cell = ctx->cells.at(c).get();

        QtProperty *topItem = groupManager->addProperty("Cell");
        addProperty(topItem, "Cell");

        QtVariantProperty *cellNameItem = readOnlyManager->addProperty(QVariant::String, "Name");
        cellNameItem->setValue(cell->name.c_str(ctx));
        topItem->addSubProperty(cellNameItem);

        QtVariantProperty *cellTypeItem = readOnlyManager->addProperty(QVariant::String, "Type");
        cellTypeItem->setValue(cell->type.c_str(ctx));
        topItem->addSubProperty(cellTypeItem);

        QtVariantProperty *cellBelItem = readOnlyManager->addProperty(QVariant::String, "Bel");
        if (cell->bel != BelId())
            cellBelItem->setValue(ctx->getBelName(cell->bel).c_str(ctx));
        else
            cellBelItem->setValue("");
        topItem->addSubProperty(cellBelItem);

        QtVariantProperty *cellBelStrItem = readOnlyManager->addProperty(QVariant::Int, "Bel strength");
        cellBelStrItem->setValue(int(cell->belStrength));
        topItem->addSubProperty(cellBelStrItem);

        QtProperty *cellPortsItem = groupManager->addProperty("Ports");
        topItem->addSubProperty(cellPortsItem);
        for (auto &item : cell->ports) {
            PortInfo p = item.second;

            QtProperty *portInfoItem = groupManager->addProperty(p.name.c_str(ctx));

            QtVariantProperty *portInfoNameItem = readOnlyManager->addProperty(QVariant::String, "Name");
            portInfoNameItem->setValue(p.name.c_str(ctx));
            portInfoItem->addSubProperty(portInfoNameItem);

            QtVariantProperty *portInfoTypeItem = readOnlyManager->addProperty(QVariant::Int, "Type");
            portInfoTypeItem->setValue(int(p.type));
            portInfoItem->addSubProperty(portInfoTypeItem);

            QtVariantProperty *portInfoNetItem = readOnlyManager->addProperty(QVariant::String, "Net");
            if (p.net)
                portInfoNetItem->setValue(p.net->name.c_str(ctx));
            else
                portInfoNetItem->setValue("");
            portInfoItem->addSubProperty(portInfoNetItem);

            cellPortsItem->addSubProperty(portInfoItem);
        }

        QtProperty *cellAttrItem = groupManager->addProperty("Attributes");
        topItem->addSubProperty(cellAttrItem);
        for (auto &item : cell->attrs) {
            QtVariantProperty *attrItem = readOnlyManager->addProperty(QVariant::String, item.first.c_str(ctx));
            attrItem->setValue(item.second.c_str());
            cellAttrItem->addSubProperty(attrItem);
        }

        QtProperty *cellParamsItem = groupManager->addProperty("Parameters");
        topItem->addSubProperty(cellParamsItem);
        for (auto &item : cell->params) {
            QtVariantProperty *paramItem = readOnlyManager->addProperty(QVariant::String, item.first.c_str(ctx));
            paramItem->setValue(item.second.c_str());
            cellParamsItem->addSubProperty(paramItem);
        }

        QtProperty *cellPinsItem = groupManager->addProperty("Pins");
        topItem->addSubProperty(cellPinsItem);
        for (auto &item : cell->pins) {
            std::string cell_port = item.first.c_str(ctx);
            std::string bel_pin = item.second.c_str(ctx);

            QtProperty *pinGroupItem = groupManager->addProperty((cell_port + " -> " + bel_pin).c_str());

            QtVariantProperty *cellItem = readOnlyManager->addProperty(QVariant::String, "Cell");
            cellItem->setValue(cell_port.c_str());
            pinGroupItem->addSubProperty(cellItem);

            QtVariantProperty *belItem = readOnlyManager->addProperty(QVariant::String, "Bel");
            belItem->setValue(bel_pin.c_str());
            pinGroupItem->addSubProperty(belItem);

            cellPinsItem->addSubProperty(pinGroupItem);
        }
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

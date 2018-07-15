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
#include <QLineEdit>
#include <QMenu>
#include <QSplitter>
#include <QToolBar>
#include <QTreeWidgetItem>
#include "fpgaviewwidget.h"

NEXTPNR_NAMESPACE_BEGIN

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
    propertyEditor->treeWidget()->setContextMenuPolicy(Qt::CustomContextMenu);

    QLineEdit *lineEdit = new QLineEdit();
    lineEdit->setClearButtonEnabled(true);
    lineEdit->addAction(QIcon(":/icons/resources/zoom.png"), QLineEdit::LeadingPosition);
    lineEdit->setPlaceholderText("Search...");

    actionFirst = new QAction("", this);
    actionFirst->setIcon(QIcon(":/icons/resources/resultset_first.png"));
    actionFirst->setEnabled(false);

    actionPrev = new QAction("", this);
    actionPrev->setIcon(QIcon(":/icons/resources/resultset_previous.png"));
    actionPrev->setEnabled(false);

    actionNext = new QAction("", this);
    actionNext->setIcon(QIcon(":/icons/resources/resultset_next.png"));
    actionNext->setEnabled(false);

    actionLast = new QAction("", this);
    actionLast->setIcon(QIcon(":/icons/resources/resultset_last.png"));
    actionLast->setEnabled(false);

    QToolBar *toolbar = new QToolBar();
    toolbar->addAction(actionFirst);
    toolbar->addAction(actionPrev);
    toolbar->addAction(actionNext);
    toolbar->addAction(actionLast);

    QWidget *topWidget = new QWidget();
    QVBoxLayout *vbox1 = new QVBoxLayout();
    topWidget->setLayout(vbox1);
    vbox1->setSpacing(5);
    vbox1->setContentsMargins(0, 0, 0, 0);
    vbox1->addWidget(lineEdit);
    vbox1->addWidget(treeWidget);

    QWidget *toolbarWidget = new QWidget();
    QHBoxLayout *hbox = new QHBoxLayout;
    hbox->setAlignment(Qt::AlignCenter);
    toolbarWidget->setLayout(hbox);
    hbox->setSpacing(0);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->addWidget(toolbar);

    QWidget *btmWidget = new QWidget();

    QVBoxLayout *vbox2 = new QVBoxLayout();
    btmWidget->setLayout(vbox2);
    vbox2->setSpacing(0);
    vbox2->setContentsMargins(0, 0, 0, 0);
    vbox2->addWidget(toolbarWidget);
    vbox2->addWidget(propertyEditor);

    QSplitter *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(topWidget);
    splitter->addWidget(btmWidget);

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(splitter);
    setLayout(mainLayout);

    // Connection
    connect(propertyEditor->treeWidget(), &QTreeWidget::customContextMenuRequested, this,
            &DesignWidget::prepareMenuProperty);
    connect(propertyEditor->treeWidget(), &QTreeWidget::itemDoubleClicked, this, &DesignWidget::onItemDoubleClicked);

    connect(treeWidget, SIGNAL(itemSelectionChanged()), SLOT(onItemSelectionChanged()));
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
QtProperty *DesignWidget::addTopLevelProperty(const QString &id)
{
    QtProperty *topItem = groupManager->addProperty(id);
    propertyToId[topItem] = id;
    idToProperty[id] = topItem;
    propertyEditor->addProperty(topItem);
    return topItem;
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

void DesignWidget::onCurrentPropertySelected(QtBrowserItem *_item)
{
    if (_item) {
        QtProperty *selectedProperty = _item->property();
        ElementType type = getElementTypeByName(selectedProperty->propertyId());
        IdString value = ctx->id(selectedProperty->valueText().toStdString());
        std::vector<DecalXY> decals;
        switch (type) {
        case ElementType::BEL: {
            BelId bel = ctx->getBelByName(value);
            if (bel != BelId()) {
                decals.push_back(ctx->getBelDecal(bel));
                Q_EMIT selected(decals);
            }
        } break;
        case ElementType::WIRE: {
            WireId wire = ctx->getWireByName(value);
            if (wire != WireId()) {
                decals.push_back(ctx->getWireDecal(wire));
                Q_EMIT selected(decals);
            }
        } break;
        case ElementType::PIP: {
            PipId pip = ctx->getPipByName(value);
            if (pip != PipId()) {
                decals.push_back(ctx->getPipDecal(pip));
                Q_EMIT selected(decals);
            }
        } break;
        case ElementType::NET: {
        } break;
        case ElementType::CELL: {
        } break;
        default:
            break;
        }
    }
}

QString DesignWidget::getElementTypeName(ElementType type)
{
    if (type == ElementType::NONE)
        return "";
    if (type == ElementType::BEL)
        return "BEL";
    if (type == ElementType::WIRE)
        return "WIRE";
    if (type == ElementType::PIP)
        return "PIP";
    if (type == ElementType::NET)
        return "NET";
    if (type == ElementType::CELL)
        return "CELL";
    return "";
}

ElementType DesignWidget::getElementTypeByName(QString type)
{
    if (type == "BEL")
        return ElementType::BEL;
    if (type == "WIRE")
        return ElementType::WIRE;
    if (type == "PIP")
        return ElementType::PIP;
    if (type == "NET")
        return ElementType::NET;
    if (type == "CELL")
        return ElementType::CELL;
    return ElementType::NONE;
}

void DesignWidget::addProperty(QtProperty *topItem, int propertyType, const QString &name, QVariant value,
                               const ElementType &type)
{
    QtVariantProperty *item = readOnlyManager->addProperty(propertyType, name);
    item->setValue(value);
    item->setPropertyId(getElementTypeName(type));
    topItem->addSubProperty(item);
}

QtProperty *DesignWidget::addSubGroup(QtProperty *topItem, const QString &name)
{
    QtProperty *item = groupManager->addProperty(name);
    topItem->addSubProperty(item);
    return item;
}

void DesignWidget::onItemSelectionChanged()
{
    if (treeWidget->selectedItems().size() == 0)
        return;

    QTreeWidgetItem *clickItem = treeWidget->selectedItems().at(0);

    if (!clickItem->parent())
        return;

    ElementType type = static_cast<ElementTreeItem *>(clickItem)->getType();
    if (type == ElementType::NONE) {
        return;
    }

    std::vector<DecalXY> decals;

    clearProperties();
    if (type == ElementType::BEL) {
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
        BelId bel = ctx->getBelByName(c);

        decals.push_back(ctx->getBelDecal(bel));
        Q_EMIT selected(decals);

        QtProperty *topItem = addTopLevelProperty("Bel");

        addProperty(topItem, QVariant::String, "Name", c.c_str(ctx));
        addProperty(topItem, QVariant::String, "Type", ctx->belTypeToId(ctx->getBelType(bel)).c_str(ctx));
        addProperty(topItem, QVariant::Bool, "Available", ctx->checkBelAvail(bel));
        addProperty(topItem, QVariant::String, "Bound Cell", ctx->getBoundBelCell(bel).c_str(ctx), ElementType::CELL);
        addProperty(topItem, QVariant::String, "Conflicting Cell", ctx->getConflictingBelCell(bel).c_str(ctx),
                    ElementType::CELL);

    } else if (type == ElementType::WIRE) {
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
        WireId wire = ctx->getWireByName(c);

        decals.push_back(ctx->getWireDecal(wire));
        Q_EMIT selected(decals);

        QtProperty *topItem = addTopLevelProperty("Wire");

        addProperty(topItem, QVariant::String, "Name", c.c_str(ctx));
        addProperty(topItem, QVariant::Bool, "Available", ctx->checkWireAvail(wire));
        addProperty(topItem, QVariant::String, "Bound Net", ctx->getBoundWireNet(wire).c_str(ctx), ElementType::NET);
        addProperty(topItem, QVariant::String, "Conflicting Net", ctx->getConflictingWireNet(wire).c_str(ctx),
                    ElementType::NET);

        QtProperty *belpinItem = addSubGroup(topItem, "BelPin Uphill");
        BelPin uphill = ctx->getBelPinUphill(wire);
        if (uphill.bel != BelId())
            addProperty(belpinItem, QVariant::String, "Bel", ctx->getBelName(uphill.bel).c_str(ctx), ElementType::BEL);
        else
            addProperty(belpinItem, QVariant::String, "Bel", "", ElementType::BEL);

        addProperty(belpinItem, QVariant::String, "PortPin", ctx->portPinToId(uphill.pin).c_str(ctx), ElementType::BEL);

        QtProperty *downhillItem = addSubGroup(topItem, "BelPin Downhill");
        for (const auto &item : ctx->getBelPinsDownhill(wire)) {
            QString belname = "";
            if (item.bel != BelId())
                belname = ctx->getBelName(item.bel).c_str(ctx);
            QString pinname = ctx->portPinToId(item.pin).c_str(ctx);

            QtProperty *dhItem = addSubGroup(downhillItem, belname + "-" + pinname);
            addProperty(dhItem, QVariant::String, "Bel", belname, ElementType::BEL);
            addProperty(dhItem, QVariant::String, "PortPin", pinname);
        }
        /*
                QtProperty *pipsDownItem = addSubGroup(downhillItem, "Pips Downhill");
                for (const auto &item : ctx->getPipsDownhill(wire)) {
                    addProperty(pipsDownItem, QVariant::String, "", ctx->getPipName(item).c_str(ctx), ElementType::PIP);
                }

                QtProperty *pipsUpItem = addSubGroup(downhillItem, "Pips Uphill");
                for (const auto &item : ctx->getPipsUphill(wire)) {
                    addProperty(pipsUpItem, QVariant::String, "", ctx->getPipName(item).c_str(ctx), ElementType::PIP);
                }
        */
    } else if (type == ElementType::PIP) {
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
        PipId pip = ctx->getPipByName(c);

        decals.push_back(ctx->getPipDecal(pip));
        Q_EMIT selected(decals);

        QtProperty *topItem = addTopLevelProperty("Pip");

        addProperty(topItem, QVariant::String, "Name", c.c_str(ctx));
        addProperty(topItem, QVariant::Bool, "Available", ctx->checkPipAvail(pip));
        addProperty(topItem, QVariant::String, "Bound Net", ctx->getBoundPipNet(pip).c_str(ctx), ElementType::NET);
        addProperty(topItem, QVariant::String, "Conflicting Net", ctx->getConflictingPipNet(pip).c_str(ctx),
                    ElementType::NET);
        addProperty(topItem, QVariant::String, "Src Wire", ctx->getWireName(ctx->getPipSrcWire(pip)).c_str(ctx),
                    ElementType::WIRE);
        addProperty(topItem, QVariant::String, "Dest Wire", ctx->getWireName(ctx->getPipDstWire(pip)).c_str(ctx),
                    ElementType::WIRE);

        DelayInfo delay = ctx->getPipDelay(pip);

        QtProperty *delayItem = addSubGroup(topItem, "Delay");
        addProperty(delayItem, QVariant::Double, "Raise", delay.raiseDelay());
        addProperty(delayItem, QVariant::Double, "Fall", delay.fallDelay());
        addProperty(delayItem, QVariant::Double, "Average", delay.avgDelay());
    } else if (type == ElementType::NET) {
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
        NetInfo *net = ctx->nets.at(c).get();

        QtProperty *topItem = addTopLevelProperty("Net");

        addProperty(topItem, QVariant::String, "Name", net->name.c_str(ctx));

        QtProperty *driverItem = addSubGroup(topItem, "Driver");
        addProperty(driverItem, QVariant::String, "Port", net->driver.port.c_str(ctx));
        addProperty(driverItem, QVariant::Double, "Budget", net->driver.budget);
        if (net->driver.cell)
            addProperty(driverItem, QVariant::String, "Cell", net->driver.cell->name.c_str(ctx), ElementType::CELL);
        else
            addProperty(driverItem, QVariant::String, "Cell", "", ElementType::CELL);

        QtProperty *usersItem = addSubGroup(topItem, "Users");
        for (auto &item : net->users) {
            QtProperty *portItem = addSubGroup(usersItem, item.port.c_str(ctx));

            addProperty(portItem, QVariant::String, "Port", item.port.c_str(ctx));
            addProperty(portItem, QVariant::Double, "Budget", item.budget);
            if (item.cell)
                addProperty(portItem, QVariant::String, "Cell", item.cell->name.c_str(ctx), ElementType::CELL);
            else
                addProperty(portItem, QVariant::String, "Cell", "", ElementType::CELL);
        }

        QtProperty *attrsItem = addSubGroup(topItem, "Attributes");
        for (auto &item : net->attrs) {
            addProperty(attrsItem, QVariant::String, item.first.c_str(ctx), item.second.c_str());
        }

        QtProperty *wiresItem = addSubGroup(topItem, "Wires");
        for (auto &item : net->wires) {
            auto name = ctx->getWireName(item.first).c_str(ctx);

            QtProperty *wireItem = addSubGroup(wiresItem, name);
            addProperty(wireItem, QVariant::String, "Name", name);

            if (item.second.pip != PipId())
                addProperty(wireItem, QVariant::String, "Pip", ctx->getPipName(item.second.pip).c_str(ctx),
                            ElementType::PIP);
            else
                addProperty(wireItem, QVariant::String, "Pip", "", ElementType::PIP);

            addProperty(wireItem, QVariant::Int, "Strength", (int)item.second.strength);
        }

    } else if (type == ElementType::CELL) {
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
        CellInfo *cell = ctx->cells.at(c).get();

        QtProperty *topItem = addTopLevelProperty("Cell");

        addProperty(topItem, QVariant::String, "Name", cell->name.c_str(ctx));
        addProperty(topItem, QVariant::String, "Type", cell->type.c_str(ctx));
        if (cell->bel != BelId())
            addProperty(topItem, QVariant::String, "Bel", ctx->getBelName(cell->bel).c_str(ctx), ElementType::BEL);
        else
            addProperty(topItem, QVariant::String, "Bel", "", ElementType::BEL);
        addProperty(topItem, QVariant::Int, "Bel strength", int(cell->belStrength));

        QtProperty *cellPortsItem = addSubGroup(topItem, "Ports");
        for (auto &item : cell->ports) {
            PortInfo p = item.second;

            QtProperty *portInfoItem = addSubGroup(cellPortsItem, p.name.c_str(ctx));
            addProperty(portInfoItem, QVariant::String, "Name", p.name.c_str(ctx));
            addProperty(portInfoItem, QVariant::Int, "Type", int(p.type));
            if (p.net)
                addProperty(portInfoItem, QVariant::String, "Net", p.net->name.c_str(ctx), ElementType::NET);
            else
                addProperty(portInfoItem, QVariant::String, "Net", "", ElementType::NET);
        }

        QtProperty *cellAttrItem = addSubGroup(topItem, "Attributes");
        for (auto &item : cell->attrs) {
            addProperty(cellAttrItem, QVariant::String, item.first.c_str(ctx), item.second.c_str());
        }

        QtProperty *cellParamsItem = addSubGroup(topItem, "Parameters");
        for (auto &item : cell->params) {
            addProperty(cellParamsItem, QVariant::String, item.first.c_str(ctx), item.second.c_str());
        }

        QtProperty *cellPinsItem = groupManager->addProperty("Pins");
        topItem->addSubProperty(cellPinsItem);
        for (auto &item : cell->pins) {
            std::string cell_port = item.first.c_str(ctx);
            std::string bel_pin = item.second.c_str(ctx);

            QtProperty *pinGroupItem = addSubGroup(cellPortsItem, (cell_port + " -> " + bel_pin).c_str());

            addProperty(pinGroupItem, QVariant::String, "Cell", cell_port.c_str(), ElementType::CELL);
            addProperty(pinGroupItem, QVariant::String, "Bel", bel_pin.c_str(), ElementType::BEL);
        }
    }
}

void DesignWidget::prepareMenuProperty(const QPoint &pos)
{
    QTreeWidget *tree = propertyEditor->treeWidget();

    itemContextMenu = tree->itemAt(pos);
    if (itemContextMenu->parent() == nullptr)
        return;

    QtBrowserItem *browserItem = propertyEditor->itemToBrowserItem(itemContextMenu);

    // if (((ElementTreeItem*)itemContextMenu)->getType() == ElementType::NONE) return;
    QMenu menu(this);
    QAction *selectAction = new QAction("&Select", this);
    connect(selectAction, &QAction::triggered, this, [this, browserItem] { onCurrentPropertySelected(browserItem); });

    menu.addAction(selectAction);

    menu.exec(tree->mapToGlobal(pos));
}

void DesignWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    QtProperty *selectedProperty = propertyEditor->itemToBrowserItem(item)->property();
    ElementType type = getElementTypeByName(selectedProperty->propertyId());
    IdString value = ctx->id(selectedProperty->valueText().toStdString());
    switch (type) {
    case ElementType::NONE:
        return;
    default:
        Q_EMIT info("double clicked " + std::string(value.c_str(ctx)) + "\n");
        break;
    }
}

NEXTPNR_NAMESPACE_END

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
    connect(treeWidget, &QTreeWidget::customContextMenuRequested, this, &DesignWidget::prepareMenu);

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

void DesignWidget::onItemSelectionChanged()
{
    if (treeWidget->selectedItems().size()== 0) return;
    
    QTreeWidgetItem *clickItem = treeWidget->selectedItems().at(0);

    if (!clickItem->parent())
        return;

    ElementType type = static_cast<ElementTreeItem *>(clickItem)->getType();
    if (type == ElementType::NONE) {
        return;
    }

    auto &&proxy = ctx->rproxy();

    clearProperties();
    if (type == ElementType::BEL) {
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
        BelId bel = proxy.getBelByName(c);

        QtProperty *topItem = groupManager->addProperty("Bel");
        addProperty(topItem, "Bel");

        QtVariantProperty *nameItem = readOnlyManager->addProperty(QVariant::String, "Name");
        nameItem->setValue(c.c_str(ctx));
        topItem->addSubProperty(nameItem);

        QtVariantProperty *typeItem = readOnlyManager->addProperty(QVariant::String, "Type");
        typeItem->setValue(ctx->belTypeToId(ctx->getBelType(bel)).c_str(ctx));
        topItem->addSubProperty(typeItem);

        QtVariantProperty *availItem = readOnlyManager->addProperty(QVariant::Bool, "Available");
        availItem->setValue(proxy.checkBelAvail(bel));
        topItem->addSubProperty(availItem);

        QtVariantProperty *cellItem = readOnlyManager->addProperty(QVariant::String, "Bound Cell");
        cellItem->setValue(proxy.getBoundBelCell(bel).c_str(ctx));
        topItem->addSubProperty(cellItem);

        QtVariantProperty *conflictItem = readOnlyManager->addProperty(QVariant::String, "Conflicting Cell");
        conflictItem->setValue(proxy.getConflictingBelCell(bel).c_str(ctx));
        topItem->addSubProperty(conflictItem);

    } else if (type == ElementType::WIRE) {
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
        WireId wire = proxy.getWireByName(c);

        QtProperty *topItem = groupManager->addProperty("Wire");
        addProperty(topItem, "Wire");

        QtVariantProperty *nameItem = readOnlyManager->addProperty(QVariant::String, "Name");
        nameItem->setValue(c.c_str(ctx));
        topItem->addSubProperty(nameItem);

        QtVariantProperty *availItem = readOnlyManager->addProperty(QVariant::Bool, "Available");
        availItem->setValue(proxy.checkWireAvail(wire));
        topItem->addSubProperty(availItem);

        QtVariantProperty *cellItem = readOnlyManager->addProperty(QVariant::String, "Bound Net");
        cellItem->setValue(proxy.getBoundWireNet(wire).c_str(ctx));
        topItem->addSubProperty(cellItem);

        QtVariantProperty *conflictItem = readOnlyManager->addProperty(QVariant::String, "Conflicting Net");
        conflictItem->setValue(proxy.getConflictingWireNet(wire).c_str(ctx));
        topItem->addSubProperty(conflictItem);

        BelPin uphill = ctx->getBelPinUphill(wire);
        QtProperty *belpinItem = groupManager->addProperty("BelPin Uphill");
        topItem->addSubProperty(belpinItem);

        QtVariantProperty *belUphillItem = readOnlyManager->addProperty(QVariant::String, "Bel");
        if (uphill.bel != BelId())
            belUphillItem->setValue(ctx->getBelName(uphill.bel).c_str(ctx));
        else
            belUphillItem->setValue("");
        belpinItem->addSubProperty(belUphillItem);

        QtVariantProperty *portUphillItem = readOnlyManager->addProperty(QVariant::String, "PortPin");
        portUphillItem->setValue(ctx->portPinToId(uphill.pin).c_str(ctx));
        belpinItem->addSubProperty(portUphillItem);

        QtProperty *downhillItem = groupManager->addProperty("BelPins Downhill");
        topItem->addSubProperty(downhillItem);
        for (const auto &item : ctx->getBelPinsDownhill(wire)) {
            QString belname = "";
            if (item.bel != BelId())
                belname = ctx->getBelName(item.bel).c_str(ctx);
            QString pinname = ctx->portPinToId(item.pin).c_str(ctx);

            QtProperty *dhItem = groupManager->addProperty(belname + "-" + pinname);
            downhillItem->addSubProperty(dhItem);

            QtVariantProperty *belItem = readOnlyManager->addProperty(QVariant::String, "Bel");
            belItem->setValue(belname);
            dhItem->addSubProperty(belItem);

            QtVariantProperty *portItem = readOnlyManager->addProperty(QVariant::String, "PortPin");
            portItem->setValue(pinname);
            dhItem->addSubProperty(portItem);
        }

        QtProperty *pipsDownItem = groupManager->addProperty("Pips Downhill");
        topItem->addSubProperty(pipsDownItem);
        for (const auto &item : ctx->getPipsDownhill(wire)) {
            QtVariantProperty *pipItem = readOnlyManager->addProperty(QVariant::String, "");
            pipItem->setValue(ctx->getPipName(item).c_str(ctx));
            pipsDownItem->addSubProperty(pipItem);
        }

        QtProperty *pipsUpItem = groupManager->addProperty("Pips Uphill");
        topItem->addSubProperty(pipsUpItem);
        for (const auto &item : ctx->getPipsUphill(wire)) {
            QtVariantProperty *pipItem = readOnlyManager->addProperty(QVariant::String, "");
            pipItem->setValue(ctx->getPipName(item).c_str(ctx));
            pipsUpItem->addSubProperty(pipItem);
        }

    } else if (type == ElementType::PIP) {
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
        PipId pip = proxy.getPipByName(c);

        QtProperty *topItem = groupManager->addProperty("Pip");
        addProperty(topItem, "Pip");

        QtVariantProperty *nameItem = readOnlyManager->addProperty(QVariant::String, "Name");
        nameItem->setValue(c.c_str(ctx));
        topItem->addSubProperty(nameItem);

        QtVariantProperty *availItem = readOnlyManager->addProperty(QVariant::Bool, "Available");
        availItem->setValue(proxy.checkPipAvail(pip));
        topItem->addSubProperty(availItem);

        QtVariantProperty *cellItem = readOnlyManager->addProperty(QVariant::String, "Bound Net");
        cellItem->setValue(proxy.getBoundPipNet(pip).c_str(ctx));
        topItem->addSubProperty(cellItem);

        QtVariantProperty *conflictItem = readOnlyManager->addProperty(QVariant::String, "Conflicting Net");
        conflictItem->setValue(proxy.getConflictingPipNet(pip).c_str(ctx));
        topItem->addSubProperty(conflictItem);

        QtVariantProperty *srcWireItem = readOnlyManager->addProperty(QVariant::String, "Src Wire");
        srcWireItem->setValue(ctx->getWireName(ctx->getPipSrcWire(pip)).c_str(ctx));
        topItem->addSubProperty(srcWireItem);

        QtVariantProperty *destWireItem = readOnlyManager->addProperty(QVariant::String, "Dest Wire");
        destWireItem->setValue(ctx->getWireName(ctx->getPipDstWire(pip)).c_str(ctx));
        topItem->addSubProperty(destWireItem);

        DelayInfo delay = ctx->getPipDelay(pip);
        QtProperty *delayItem = groupManager->addProperty("Delay");
        topItem->addSubProperty(delayItem);

        QtVariantProperty *raiseDelayItem = readOnlyManager->addProperty(QVariant::Double, "Raise");
        raiseDelayItem->setValue(delay.raiseDelay());
        delayItem->addSubProperty(raiseDelayItem);

        QtVariantProperty *fallDelayItem = readOnlyManager->addProperty(QVariant::Double, "Fall");
        fallDelayItem->setValue(delay.fallDelay());
        delayItem->addSubProperty(fallDelayItem);

        QtVariantProperty *avgDelayItem = readOnlyManager->addProperty(QVariant::Double, "Average");
        avgDelayItem->setValue(delay.avgDelay());
        delayItem->addSubProperty(avgDelayItem);

    } else if (type == ElementType::NET) {
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
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
        IdString c = static_cast<IdStringTreeItem *>(clickItem)->getData();
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

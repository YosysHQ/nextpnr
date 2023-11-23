/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <micko@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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
#include <QApplication>
#include <QGridLayout>
#include <QLineEdit>
#include <QMenu>
#include <QSplitter>
#include <QToolBar>
#include <QTreeWidgetItem>
#include "fpgaviewwidget.h"

NEXTPNR_NAMESPACE_BEGIN

TreeView::TreeView(QWidget *parent) : QTreeView(parent) {}

TreeView::~TreeView() {}

void TreeView::mouseMoveEvent(QMouseEvent *event)
{
    QModelIndex index = indexAt(event->pos());
    if (index != current) {
        current = index;
        Q_EMIT hoverIndexChanged(index);
    }
    QTreeView::mouseMoveEvent(event);
}

void TreeView::leaveEvent(QEvent *event) { Q_EMIT hoverIndexChanged(QModelIndex()); }

DesignWidget::DesignWidget(QWidget *parent) : QWidget(parent), ctx(nullptr)
{
    tabWidget = new QTabWidget();

    // Add tree view
    for (int i = 0; i < 6; i++) {
        treeView[i] = new TreeView();
        treeModel[i] = new TreeModel::Model();
        treeView[i]->setModel(treeModel[i]);
        treeView[i]->setContextMenuPolicy(Qt::CustomContextMenu);
        treeView[i]->setSelectionMode(QAbstractItemView::ExtendedSelection);
        treeView[i]->viewport()->setMouseTracking(true);
        selectionModel[i] = nullptr;
    }

    tabWidget->addTab(treeView[0], "Bels");
    tabWidget->addTab(treeView[1], "Wires");
    tabWidget->addTab(treeView[2], "Pips");
    tabWidget->addTab(treeView[3], "Cells");
    tabWidget->addTab(treeView[4], "Nets");
    tabWidget->addTab(treeView[5], "Groups");

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
    propertyEditor->treeWidget()->setSelectionMode(QAbstractItemView::ExtendedSelection);
    propertyEditor->treeWidget()->viewport()->setMouseTracking(true);

    searchEdit = new QLineEdit();
    searchEdit->setClearButtonEnabled(true);
    searchEdit->addAction(QIcon(":/icons/resources/zoom.png"), QLineEdit::LeadingPosition);
    searchEdit->setPlaceholderText("Search...");
    connect(searchEdit, &QLineEdit::returnPressed, this, &DesignWidget::onSearchInserted);

    actionFirst = new QAction("", this);
    actionFirst->setIcon(QIcon(":/icons/resources/resultset_first.png"));
    actionFirst->setEnabled(false);
    connect(actionFirst, &QAction::triggered, this, [this] {
        history_ignore = true;
        history_index = 0;
        auto h = history.at(history_index);
        if (tabWidget->currentIndex() != h.first) {
            selectionModel[tabWidget->currentIndex()]->clearSelection();
            tabWidget->setCurrentIndex(h.first);
            selectionModel[h.first]->setCurrentIndex(h.second, QItemSelectionModel::Select);
        } else {
            selectionModel[h.first]->setCurrentIndex(h.second, QItemSelectionModel::ClearAndSelect);
        }
        updateButtons();
    });

    actionPrev = new QAction("", this);
    actionPrev->setIcon(QIcon(":/icons/resources/resultset_previous.png"));
    actionPrev->setEnabled(false);
    connect(actionPrev, &QAction::triggered, this, [this] {
        history_ignore = true;
        history_index--;
        auto h = history.at(history_index);
        if (tabWidget->currentIndex() != h.first) {
            selectionModel[tabWidget->currentIndex()]->clearSelection();
            tabWidget->setCurrentIndex(h.first);
            selectionModel[h.first]->setCurrentIndex(h.second, QItemSelectionModel::Select);
        } else {
            selectionModel[h.first]->setCurrentIndex(h.second, QItemSelectionModel::ClearAndSelect);
        }
        updateButtons();
    });

    actionNext = new QAction("", this);
    actionNext->setIcon(QIcon(":/icons/resources/resultset_next.png"));
    actionNext->setEnabled(false);
    connect(actionNext, &QAction::triggered, this, [this] {
        history_ignore = true;
        history_index++;
        auto h = history.at(history_index);
        if (tabWidget->currentIndex() != h.first) {
            selectionModel[tabWidget->currentIndex()]->clearSelection();
            tabWidget->setCurrentIndex(h.first);
            selectionModel[h.first]->setCurrentIndex(h.second, QItemSelectionModel::Select);
        } else {
            selectionModel[h.first]->setCurrentIndex(h.second, QItemSelectionModel::ClearAndSelect);
        }
        updateButtons();
    });

    actionLast = new QAction("", this);
    actionLast->setIcon(QIcon(":/icons/resources/resultset_last.png"));
    actionLast->setEnabled(false);
    connect(actionLast, &QAction::triggered, this, [this] {
        history_ignore = true;
        history_index = int(history.size() - 1);
        auto h = history.at(history_index);
        if (tabWidget->currentIndex() != h.first) {
            selectionModel[tabWidget->currentIndex()]->clearSelection();
            tabWidget->setCurrentIndex(h.first);
            selectionModel[h.first]->setCurrentIndex(h.second, QItemSelectionModel::Select);
        } else {
            selectionModel[h.first]->setCurrentIndex(h.second, QItemSelectionModel::ClearAndSelect);
        }
        updateButtons();
    });

    actionClear = new QAction("", this);
    actionClear->setIcon(QIcon(":/icons/resources/cross.png"));
    actionClear->setEnabled(true);
    connect(actionClear, &QAction::triggered, this, [this] {
        history_index = -1;
        history.clear();
        int num = tabWidget->currentIndex();
        if (selectionModel[num]->selectedIndexes().size() > 0) {
            QModelIndex index = selectionModel[num]->selectedIndexes().at(0);
            if (index.isValid()) {
                ElementType type = treeModel[num]->nodeFromIndex(index)->type();
                if (type != ElementType::NONE)
                    addToHistory(num, index);
            }
        }
        updateButtons();
    });

    QToolBar *toolbar = new QToolBar();
    toolbar->addAction(actionFirst);
    toolbar->addAction(actionPrev);
    toolbar->addAction(actionNext);
    toolbar->addAction(actionLast);
    toolbar->addAction(actionClear);

    QWidget *topWidget = new QWidget();
    QVBoxLayout *vbox1 = new QVBoxLayout();
    topWidget->setLayout(vbox1);
    vbox1->setSpacing(5);
    vbox1->setContentsMargins(0, 0, 0, 0);
    vbox1->addWidget(searchEdit);
    vbox1->addWidget(tabWidget);

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
    connect(propertyEditor, &QtTreePropertyBrowser::hoverPropertyChanged, this, &DesignWidget::onHoverPropertyChanged);

    for (int num = 0; num < 6; num++) {
        connect(treeView[num], &TreeView::customContextMenuRequested,
                [this, num](const QPoint &pos) { prepareMenuTree(num, pos); });
        connect(treeView[num], &TreeView::doubleClicked, [this](const QModelIndex &index) { onDoubleClicked(index); });
        connect(treeView[num], &TreeView::hoverIndexChanged,
                [this, num](QModelIndex index) { onHoverIndexChanged(num, index); });
        selectionModel[num] = treeView[num]->selectionModel();
        connect(selectionModel[num], &QItemSelectionModel::selectionChanged,
                [this, num](const QItemSelection &selected, const QItemSelection &deselected) {
                    onSelectionChanged(num, selected, deselected);
                });
    }

    history_index = -1;
    history_ignore = false;

    highlightColors[0] = QColor("#6495ed");
    highlightColors[1] = QColor("#7fffd4");
    highlightColors[2] = QColor("#98fb98");
    highlightColors[3] = QColor("#ffd700");
    highlightColors[4] = QColor("#cd5c5c");
    highlightColors[5] = QColor("#fa8072");
    highlightColors[6] = QColor("#ff69b4");
    highlightColors[7] = QColor("#da70d6");
}

DesignWidget::~DesignWidget() {}

void DesignWidget::updateButtons()
{
    int count = int(history.size());
    actionFirst->setEnabled(history_index > 0);
    actionPrev->setEnabled(history_index > 0);
    actionNext->setEnabled(history_index < (count - 1));
    actionLast->setEnabled(history_index < (count - 1));
}

void DesignWidget::addToHistory(int tab, QModelIndex item)
{
    if (!history_ignore) {
        int count = int(history.size());
        for (int i = count - 1; i > history_index; i--)
            history.pop_back();
        history.push_back(std::make_pair(tab, item));
        history_index++;
    }
    history_ignore = false;
    updateButtons();
}

void DesignWidget::newContext(Context *ctx)
{
    if (!ctx)
        return;

    highlightSelected.clear();
    history_ignore = false;
    history_index = -1;
    history.clear();
    updateButtons();

    highlightSelected.clear();
    this->ctx = ctx;
    {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        {
            TreeModel::ElementXYRoot<BelId>::ElementMap belMap;
            for (const auto &bel : ctx->getBels()) {
                auto loc = ctx->getBelLocation(bel);
                belMap[std::pair<int, int>(loc.x, loc.y)].push_back(bel);
            }
            auto belGetter = [](Context *ctx, BelId id) { return ctx->getBelName(id); };

            getTreeByElementType(ElementType::BEL)
                    ->loadData(ctx,
                               std::unique_ptr<TreeModel::ElementXYRoot<BelId>>(
                                       new TreeModel::ElementXYRoot<BelId>(ctx, belMap, belGetter, ElementType::BEL)));
        }

        {
            TreeModel::ElementXYRoot<WireId>::ElementMap wireMap;
#ifdef ARCH_ICE40
            for (int i = 0; i < int(ctx->chip_info->wire_data.size()); i++) {
                const auto wire = &ctx->chip_info->wire_data[i];
                WireId wireid;
                wireid.index = i;
                wireMap[std::pair<int, int>(wire->x, wire->y)].push_back(wireid);
            }
#endif
#ifdef ARCH_ECP5
            for (const auto &wire : ctx->getWires()) {
                wireMap[std::pair<int, int>(wire.location.x, wire.location.y)].push_back(wire);
            }
#endif
#ifdef ARCH_MACHXO2
            for (const auto &wire : ctx->getWires()) {
                wireMap[std::pair<int, int>(wire.location.x, wire.location.y)].push_back(wire);
            }
#endif
#ifdef ARCH_GOWIN
            for (const auto &wire : ctx->getWires()) {
                WireInfo wi = ctx->wire_info(wire);
                wireMap[std::pair<int, int>(wi.x, wi.y)].push_back(wire);
            }
#endif
#ifdef ARCH_HIMBAECHEL
            for (const auto &wire : ctx->getWires()) {
                Loc loc;
                tile_xy(ctx->chip_info, wire.tile, loc.x, loc.y);
                wireMap[std::pair<int, int>(loc.x, loc.y)].push_back(wire);
            }
#endif
            auto wireGetter = [](Context *ctx, WireId id) { return ctx->getWireName(id); };
            getTreeByElementType(ElementType::WIRE)
                    ->loadData(ctx,
                               std::unique_ptr<TreeModel::ElementXYRoot<WireId>>(new TreeModel::ElementXYRoot<WireId>(
                                       ctx, wireMap, wireGetter, ElementType::WIRE)));
        }

        {
            TreeModel::ElementXYRoot<PipId>::ElementMap pipMap;
            for (const auto &pip : ctx->getPips()) {
                auto loc = ctx->getPipLocation(pip);
                pipMap[std::pair<int, int>(loc.x, loc.y)].push_back(pip);
            }
            auto pipGetter = [](Context *ctx, PipId id) { return ctx->getPipName(id); };

            getTreeByElementType(ElementType::PIP)
                    ->loadData(ctx,
                               std::unique_ptr<TreeModel::ElementXYRoot<PipId>>(
                                       new TreeModel::ElementXYRoot<PipId>(ctx, pipMap, pipGetter, ElementType::PIP)));
        }

        getTreeByElementType(ElementType::CELL)
                ->loadData(ctx, std::unique_ptr<TreeModel::IdList>(new TreeModel::IdList(ElementType::CELL)));
        getTreeByElementType(ElementType::NET)
                ->loadData(ctx, std::unique_ptr<TreeModel::IdList>(new TreeModel::IdList(ElementType::NET)));
    }
    updateTree();
}

void DesignWidget::updateTree()
{
    clearProperties();

    QMap<TreeModel::Item *, int>::iterator i = highlightSelected.begin();
    while (i != highlightSelected.end()) {
        QMap<TreeModel::Item *, int>::iterator prev = i;
        ++i;
        if (prev.key()->type() == ElementType::NET && ctx->nets.find(prev.key()->id()[0]) == ctx->nets.end()) {
            highlightSelected.erase(prev);
        }
        if (prev.key()->type() == ElementType::CELL && ctx->cells.find(prev.key()->id()[0]) == ctx->cells.end()) {
            highlightSelected.erase(prev);
        }
    }

    {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        std::vector<IdStringList> cells;
        for (auto &pair : ctx->cells) {
            cells.push_back(IdStringList(pair.first));
        }
        std::vector<IdStringList> nets;
        for (auto &pair : ctx->nets) {
            nets.push_back(IdStringList(pair.first));
        }

        getTreeByElementType(ElementType::CELL)->updateElements(cells);
        getTreeByElementType(ElementType::NET)->updateElements(nets);
    }
}
QtProperty *DesignWidget::addTopLevelProperty(const QString &id)
{
    QtProperty *topItem = groupManager->addProperty(id);
    propertyToId[topItem] = id;
    idToProperty[id] = topItem;
    topItem->setSelectable(false);
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

TreeModel::Model *DesignWidget::getTreeByElementType(ElementType type)
{
    if (type == ElementType::NONE)
        return nullptr;
    if (type == ElementType::BEL)
        return treeModel[0];
    if (type == ElementType::WIRE)
        return treeModel[1];
    if (type == ElementType::PIP)
        return treeModel[2];
    if (type == ElementType::CELL)
        return treeModel[3];
    if (type == ElementType::NET)
        return treeModel[4];
    return nullptr;
}
int DesignWidget::getIndexByElementType(ElementType type)
{
    if (type == ElementType::NONE)
        return -1;
    if (type == ElementType::BEL)
        return 0;
    if (type == ElementType::WIRE)
        return 1;
    if (type == ElementType::PIP)
        return 2;
    if (type == ElementType::CELL)
        return 3;
    if (type == ElementType::NET)
        return 4;
    if (type == ElementType::GROUP)
        return 5;
    return -1;
}
void DesignWidget::addProperty(QtProperty *topItem, int propertyType, const QString &name, QVariant value,
                               const ElementType &type)
{
    QtVariantProperty *item = readOnlyManager->addProperty(propertyType, name);
    item->setValue(value);
    item->setPropertyId(getElementTypeName(type));
    item->setSelectable(type != ElementType::NONE);
    topItem->addSubProperty(item);
}

QtProperty *DesignWidget::addSubGroup(QtProperty *topItem, const QString &name)
{
    QtProperty *item = groupManager->addProperty(name);
    item->setSelectable(false);
    topItem->addSubProperty(item);
    return item;
}

void DesignWidget::clearAllSelectionModels()
{
    for (int i = 0; i <= getIndexByElementType(ElementType::GROUP); i++)
        selectionModel[i]->clearSelection();
}

void DesignWidget::onClickedBel(BelId bel, bool keep)
{
    boost::optional<TreeModel::Item *> item;
    {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        item = getTreeByElementType(ElementType::BEL)->nodeForId(ctx->getBelName(bel));
        if (!item)
            return;

        Q_EMIT selected(getDecals(ElementType::BEL, ctx->getBelName(bel)), keep);
    }
    int index = getIndexByElementType(ElementType::BEL);
    if (!keep)
        clearAllSelectionModels();
    if (tabWidget->currentIndex() != index) {
        tabWidget->setCurrentIndex(index);
    }
    selectionModel[index]->setCurrentIndex(getTreeByElementType(ElementType::BEL)->indexFromNode(*item),
                                           keep ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect);
}

void DesignWidget::onClickedWire(WireId wire, bool keep)
{
    boost::optional<TreeModel::Item *> item;
    {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        item = getTreeByElementType(ElementType::WIRE)->nodeForId(ctx->getWireName(wire));
        if (!item)
            return;

        Q_EMIT selected(getDecals(ElementType::WIRE, ctx->getWireName(wire)), keep);
    }
    int index = getIndexByElementType(ElementType::WIRE);
    if (!keep)
        clearAllSelectionModels();
    if (tabWidget->currentIndex() != index)
        tabWidget->setCurrentIndex(index);
    selectionModel[index]->setCurrentIndex(getTreeByElementType(ElementType::WIRE)->indexFromNode(*item),
                                           keep ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect);
}

void DesignWidget::onClickedPip(PipId pip, bool keep)
{
    boost::optional<TreeModel::Item *> item;
    {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        item = getTreeByElementType(ElementType::PIP)->nodeForId(ctx->getPipName(pip));
        if (!item)
            return;

        Q_EMIT selected(getDecals(ElementType::PIP, ctx->getPipName(pip)), keep);
    }

    int index = getIndexByElementType(ElementType::PIP);
    if (!keep)
        clearAllSelectionModels();
    if (tabWidget->currentIndex() != index)
        tabWidget->setCurrentIndex(index);
    selectionModel[index]->setCurrentIndex(getTreeByElementType(ElementType::PIP)->indexFromNode(*item),
                                           keep ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect);
}

void DesignWidget::onSelectionChanged(int num, const QItemSelection &, const QItemSelection &)
{
    int num_selected = 0;
    std::vector<DecalXY> decals;
    for (int i = 0; i <= getIndexByElementType(ElementType::GROUP); i++) {
        num_selected += selectionModel[i]->selectedIndexes().size();
        for (auto index : selectionModel[i]->selectedIndexes()) {
            TreeModel::Item *item = treeModel[i]->nodeFromIndex(index);
            std::vector<DecalXY> d = getDecals(item->type(), item->id());
            std::move(d.begin(), d.end(), std::back_inserter(decals));
        }
    }

    // Keep other tree seleciton only if Control is pressed
    if (num_selected > 1 && QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) == true) {
        Q_EMIT selected(decals, false);
        return;
    }

    // For deselect and multiple select just send all
    if (selectionModel[num]->selectedIndexes().size() != 1) {
        Q_EMIT selected(decals, false);
        return;
    }

    QModelIndex index = selectionModel[num]->selectedIndexes().at(0);
    if (!index.isValid())
        return;
    TreeModel::Item *clickItem = treeModel[num]->nodeFromIndex(index);

    ElementType type = clickItem->type();
    if (type == ElementType::NONE)
        return;

    // Clear other tab selections
    for (int i = 0; i <= getIndexByElementType(ElementType::GROUP); i++)
        if (i != num)
            selectionModel[i]->clearSelection();

    addToHistory(num, index);

    clearProperties();

    IdStringList c = clickItem->id();
    Q_EMIT selected(getDecals(type, c), false);

    if (type == ElementType::BEL) {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        BelId bel = ctx->getBelByName(c);
        QtProperty *topItem = addTopLevelProperty("Bel");

        addProperty(topItem, QVariant::String, "Name", ctx->nameOfBel(bel));
        addProperty(topItem, QVariant::String, "Type", ctx->getBelType(bel).c_str(ctx));
        addProperty(topItem, QVariant::Bool, "Available", ctx->checkBelAvail(bel));
        addProperty(topItem, QVariant::String, "Bound Cell", ctx->nameOf(ctx->getBoundBelCell(bel)), ElementType::CELL);
        addProperty(topItem, QVariant::String, "Conflicting Cell", ctx->nameOf(ctx->getConflictingBelCell(bel)),
                    ElementType::CELL);

        QtProperty *attrsItem = addSubGroup(topItem, "Attributes");
        for (auto &item : ctx->getBelAttrs(bel)) {
            addProperty(attrsItem, QVariant::String, item.first.c_str(ctx), item.second.c_str());
        }

        QtProperty *belpinsItem = addSubGroup(topItem, "Ports");
        for (const auto &item : ctx->getBelPins(bel)) {
            QtProperty *portInfoItem = addSubGroup(belpinsItem, item.c_str(ctx));
            addProperty(portInfoItem, QVariant::String, "Name", item.c_str(ctx));
            addProperty(portInfoItem, QVariant::Int, "Type", int(ctx->getBelPinType(bel, item)));
            WireId wire = ctx->getBelPinWire(bel, item);
            addProperty(portInfoItem, QVariant::String, "Wire", ctx->nameOfWire(wire), ElementType::WIRE);
        }
    } else if (type == ElementType::WIRE) {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        WireId wire = ctx->getWireByName(c);
        QtProperty *topItem = addTopLevelProperty("Wire");

        addProperty(topItem, QVariant::String, "Name", ctx->nameOfWire(wire));
        addProperty(topItem, QVariant::String, "Type", ctx->getWireType(wire).c_str(ctx));
        addProperty(topItem, QVariant::Bool, "Available", ctx->checkWireAvail(wire));
        addProperty(topItem, QVariant::String, "Bound Net", ctx->nameOf(ctx->getBoundWireNet(wire)), ElementType::NET);
        addProperty(topItem, QVariant::String, "Conflicting Wire", ctx->nameOfWire(ctx->getConflictingWireWire(wire)),
                    ElementType::WIRE);
        addProperty(topItem, QVariant::String, "Conflicting Net", ctx->nameOf(ctx->getConflictingWireNet(wire)),
                    ElementType::NET);

        QtProperty *attrsItem = addSubGroup(topItem, "Attributes");
        for (auto &item : ctx->getWireAttrs(wire)) {
            addProperty(attrsItem, QVariant::String, item.first.c_str(ctx), item.second.c_str());
        }

        DelayQuad delay = ctx->getWireDelay(wire);

        QtProperty *delayItem = addSubGroup(topItem, "Delay");
        addProperty(delayItem, QVariant::Double, "Min Rise", delay.minRiseDelay());
        addProperty(delayItem, QVariant::Double, "Max Rise", delay.maxRiseDelay());
        addProperty(delayItem, QVariant::Double, "Min Fall", delay.minFallDelay());
        addProperty(delayItem, QVariant::Double, "Max Fall", delay.maxFallDelay());

        QtProperty *belpinsItem = addSubGroup(topItem, "BelPins");
        for (const auto &item : ctx->getWireBelPins(wire)) {
            QString belname = "";
            if (item.bel != BelId())
                belname = ctx->nameOfBel(item.bel);
            QString pinname = item.pin.c_str(ctx);

            QtProperty *dhItem = addSubGroup(belpinsItem, belname + "-" + pinname);
            addProperty(dhItem, QVariant::String, "Bel", belname, ElementType::BEL);
            addProperty(dhItem, QVariant::String, "PortPin", pinname);
        }

        int counter = 0;
        QtProperty *pipsDownItem = addSubGroup(topItem, "Pips Downhill");
        for (const auto &item : ctx->getPipsDownhill(wire)) {
            addProperty(pipsDownItem, QVariant::String, "", ctx->nameOfPip(item), ElementType::PIP);
            counter++;
            if (counter == 50) {
                addProperty(pipsDownItem, QVariant::String, "Warning", "Too many items...", ElementType::NONE);
                break;
            }
        }

        counter = 0;
        QtProperty *pipsUpItem = addSubGroup(topItem, "Pips Uphill");
        for (const auto &item : ctx->getPipsUphill(wire)) {
            addProperty(pipsUpItem, QVariant::String, "", ctx->nameOfPip(item), ElementType::PIP);
            counter++;
            if (counter == 50) {
                addProperty(pipsUpItem, QVariant::String, "Warning", "Too many items...", ElementType::NONE);
                break;
            }
        }
    } else if (type == ElementType::PIP) {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        PipId pip = ctx->getPipByName(c);
        QtProperty *topItem = addTopLevelProperty("Pip");

        addProperty(topItem, QVariant::String, "Name", ctx->nameOfPip(pip));
        addProperty(topItem, QVariant::String, "Type", ctx->getPipType(pip).c_str(ctx));
        addProperty(topItem, QVariant::Bool, "Available", ctx->checkPipAvail(pip));
        addProperty(topItem, QVariant::String, "Bound Net", ctx->nameOf(ctx->getBoundPipNet(pip)), ElementType::NET);
        if (ctx->getConflictingPipWire(pip) != WireId()) {
            addProperty(topItem, QVariant::String, "Conflicting Wire", ctx->nameOfWire(ctx->getConflictingPipWire(pip)),
                        ElementType::WIRE);
        } else {
            addProperty(topItem, QVariant::String, "Conflicting Wire", "", ElementType::NONE);
        }
        addProperty(topItem, QVariant::String, "Conflicting Net", ctx->nameOf(ctx->getConflictingPipNet(pip)),
                    ElementType::NET);
        addProperty(topItem, QVariant::String, "Src Wire", ctx->nameOfWire(ctx->getPipSrcWire(pip)), ElementType::WIRE);
        addProperty(topItem, QVariant::String, "Dest Wire", ctx->nameOfWire(ctx->getPipDstWire(pip)),
                    ElementType::WIRE);

        QtProperty *attrsItem = addSubGroup(topItem, "Attributes");
        for (auto &item : ctx->getPipAttrs(pip)) {
            addProperty(attrsItem, QVariant::String, item.first.c_str(ctx), item.second.c_str());
        }

        DelayQuad delay = ctx->getPipDelay(pip);

        QtProperty *delayItem = addSubGroup(topItem, "Delay");
        addProperty(delayItem, QVariant::Double, "Min Rise", delay.minRiseDelay());
        addProperty(delayItem, QVariant::Double, "Max Rise", delay.maxRiseDelay());
        addProperty(delayItem, QVariant::Double, "Min Fall", delay.minFallDelay());
        addProperty(delayItem, QVariant::Double, "Max Fall", delay.maxFallDelay());
    } else if (type == ElementType::NET) {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        NetInfo *net = ctx->nets.at(c[0]).get();

        QtProperty *topItem = addTopLevelProperty("Net");

        addProperty(topItem, QVariant::String, "Name", net->name.c_str(ctx));

        QtProperty *driverItem = addSubGroup(topItem, "Driver");
        addProperty(driverItem, QVariant::String, "Port", net->driver.port.c_str(ctx));
        if (net->driver.cell)
            addProperty(driverItem, QVariant::String, "Cell", net->driver.cell->name.c_str(ctx), ElementType::CELL);
        else
            addProperty(driverItem, QVariant::String, "Cell", "", ElementType::CELL);

        QtProperty *usersItem = addSubGroup(topItem, "Users");
        for (auto &item : net->users) {
            QtProperty *portItem = addSubGroup(usersItem, item.port.c_str(ctx));

            addProperty(portItem, QVariant::String, "Port", item.port.c_str(ctx));
            if (item.cell)
                addProperty(portItem, QVariant::String, "Cell", item.cell->name.c_str(ctx), ElementType::CELL);
            else
                addProperty(portItem, QVariant::String, "Cell", "", ElementType::CELL);
        }

        QtProperty *attrsItem = addSubGroup(topItem, "Attributes");
        for (auto &item : net->attrs) {
            addProperty(attrsItem, QVariant::String, item.first.c_str(ctx),
                        item.second.is_string ? item.second.as_string().c_str() : item.second.to_string().c_str());
        }

        QtProperty *wiresItem = addSubGroup(topItem, "Wires");
        for (auto &item : net->wires) {
            auto name = ctx->nameOfWire(item.first);

            QtProperty *wireItem = addSubGroup(wiresItem, name);
            addProperty(wireItem, QVariant::String, "Wire", name, ElementType::WIRE);

            if (item.second.pip != PipId())
                addProperty(wireItem, QVariant::String, "Pip", ctx->nameOfPip(item.second.pip), ElementType::PIP);
            else
                addProperty(wireItem, QVariant::String, "Pip", "", ElementType::PIP);

            addProperty(wireItem, QVariant::Int, "Strength", (int)item.second.strength);
        }

    } else if (type == ElementType::CELL) {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        CellInfo *cell = ctx->cells.at(c[0]).get();

        QtProperty *topItem = addTopLevelProperty("Cell");

        addProperty(topItem, QVariant::String, "Name", cell->name.c_str(ctx));
        addProperty(topItem, QVariant::String, "Type", cell->type.c_str(ctx));
        if (cell->bel != BelId())
            addProperty(topItem, QVariant::String, "Bel", ctx->nameOfBel(cell->bel), ElementType::BEL);
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
            addProperty(cellAttrItem, QVariant::String, item.first.c_str(ctx),
                        item.second.is_string ? item.second.as_string().c_str() : item.second.to_string().c_str());
        }

        QtProperty *cellParamsItem = addSubGroup(topItem, "Parameters");
        for (auto &item : cell->params) {
            addProperty(cellParamsItem, QVariant::String, item.first.c_str(ctx),
                        item.second.is_string ? item.second.as_string().c_str() : item.second.to_string().c_str());
        }
    }
}

std::vector<DecalXY> DesignWidget::getDecals(ElementType type, IdStringList value)
{
    std::vector<DecalXY> decals;
    switch (type) {
    case ElementType::BEL: {
        BelId bel = ctx->getBelByName(value);
        if (bel != BelId()) {
            decals.push_back(ctx->getBelDecal(bel));
        }
    } break;
    case ElementType::WIRE: {
        WireId wire = ctx->getWireByName(value);
        if (wire != WireId()) {
            decals.push_back(ctx->getWireDecal(wire));
        }
    } break;
    case ElementType::PIP: {
        PipId pip = ctx->getPipByName(value);
        if (pip != PipId()) {
            decals.push_back(ctx->getPipDecal(pip));
        }
    } break;
    case ElementType::NET: {
        NetInfo *net = ctx->nets.at(value[0]).get();
        for (auto &item : net->wires) {
            decals.push_back(ctx->getWireDecal(item.first));
            if (item.second.pip != PipId()) {
                decals.push_back(ctx->getPipDecal(item.second.pip));
            }
        }
    } break;
    case ElementType::CELL: {
        CellInfo *cell = ctx->cells.at(value[0]).get();
        if (cell->bel != BelId()) {
            decals.push_back(ctx->getBelDecal(cell->bel));
        }
    } break;
    default:
        break;
    }
    return decals;
}

void DesignWidget::updateHighlightGroup(QList<TreeModel::Item *> items, int group)
{
    const bool shouldClear = items.size() == 1;
    for (auto item : items) {
        if (highlightSelected.contains(item)) {
            if (shouldClear && highlightSelected[item] == group) {
                highlightSelected.remove(item);
            } else
                highlightSelected[item] = group;
        } else
            highlightSelected.insert(item, group);
    }
    std::vector<DecalXY> decals[8];

    for (auto it : highlightSelected.toStdMap()) {
        std::vector<DecalXY> d = getDecals(it.first->type(), it.first->id());
        std::move(d.begin(), d.end(), std::back_inserter(decals[it.second]));
    }
    for (int i = 0; i < 8; i++)
        Q_EMIT highlight(decals[i], i);
}

void DesignWidget::prepareMenuProperty(const QPoint &pos)
{
    QTreeWidget *tree = propertyEditor->treeWidget();
    QList<TreeModel::Item *> items;
    for (auto itemContextMenu : tree->selectedItems()) {
        QtBrowserItem *browserItem = propertyEditor->itemToBrowserItem(itemContextMenu);
        if (!browserItem)
            continue;
        QtProperty *selectedProperty = browserItem->property();
        ElementType type = getElementTypeByName(selectedProperty->propertyId());
        if (type == ElementType::NONE)
            continue;
        IdStringList value = IdStringList::parse(ctx, selectedProperty->valueText().toStdString());
        auto node = getTreeByElementType(type)->nodeForId(value);
        if (!node)
            continue;
        items.append(*node);
    }
    int selectedIndex = -1;
    if (items.size() == 1) {
        TreeModel::Item *item = items.at(0);
        if (highlightSelected.contains(item))
            selectedIndex = highlightSelected[item];
    }

    QMenu menu(this);
    QAction *selectAction = new QAction("&Select", this);
    connect(selectAction, &QAction::triggered, this, [this, items] {
        std::vector<DecalXY> decals;
        for (auto clickItem : items) {
            std::vector<DecalXY> d = getDecals(clickItem->type(), clickItem->id());
            std::move(d.begin(), d.end(), std::back_inserter(decals));
        }
        Q_EMIT selected(decals, false);
    });
    menu.addAction(selectAction);

    QMenu *subMenu = menu.addMenu("Highlight");
    QActionGroup *group = new QActionGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < 8; i++) {
        QPixmap pixmap(32, 32);
        pixmap.fill(QColor(highlightColors[i]));
        QAction *action = new QAction(QIcon(pixmap), ("Group " + std::to_string(i)).c_str(), this);
        action->setCheckable(true);
        subMenu->addAction(action);
        group->addAction(action);
        if (selectedIndex == i)
            action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, i, items] { updateHighlightGroup(items, i); });
    }
    menu.exec(tree->mapToGlobal(pos));
}

void DesignWidget::prepareMenuTree(int num, const QPoint &pos)
{
    int selectedIndex = -1;

    if (selectionModel[num]->selectedIndexes().size() == 0)
        return;

    QList<TreeModel::Item *> items;
    for (int i = 0; i <= getIndexByElementType(ElementType::GROUP); i++) {
        for (auto index : selectionModel[i]->selectedIndexes()) {
            TreeModel::Item *item = treeModel[i]->nodeFromIndex(index);
            items.append(item);
        }
    }
    if (items.size() == 1) {
        TreeModel::Item *item = items.at(0);
        if (highlightSelected.contains(item))
            selectedIndex = highlightSelected[item];
    }
    QMenu menu(this);
    QMenu *subMenu = menu.addMenu("Highlight");
    QActionGroup *group = new QActionGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < 8; i++) {
        QPixmap pixmap(32, 32);
        pixmap.fill(QColor(highlightColors[i]));
        QAction *action = new QAction(QIcon(pixmap), ("Group " + std::to_string(i)).c_str(), this);
        action->setCheckable(true);
        subMenu->addAction(action);
        group->addAction(action);
        if (selectedIndex == i)
            action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, i, items] { updateHighlightGroup(items, i); });
    }
    menu.exec(treeView[num]->mapToGlobal(pos));
}

void DesignWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    QtProperty *selectedProperty = propertyEditor->itemToBrowserItem(item)->property();
    ElementType type = getElementTypeByName(selectedProperty->propertyId());
    if (type == ElementType::NONE)
        return;

    IdStringList value = IdStringList::parse(ctx, selectedProperty->valueText().toStdString());
    auto it = getTreeByElementType(type)->nodeForId(value);
    if (it) {
        int num = getIndexByElementType(type);
        clearAllSelectionModels();
        if (tabWidget->currentIndex() != num)
            tabWidget->setCurrentIndex(num);
        selectionModel[num]->setCurrentIndex(getTreeByElementType(type)->indexFromNode(*it),
                                             QItemSelectionModel::ClearAndSelect);
    }
}

void DesignWidget::onDoubleClicked(const QModelIndex &index) { Q_EMIT zoomSelected(); }

void DesignWidget::onSearchInserted()
{
    if (currentSearch == searchEdit->text() && currentIndexTab == tabWidget->currentIndex()) {
        currentIndex++;
        if (currentIndex >= currentSearchIndexes.size())
            currentIndex = 0;
    } else {
        std::lock_guard<std::mutex> lock_ui(ctx->ui_mutex);
        std::lock_guard<std::mutex> lock(ctx->mutex);

        currentSearch = searchEdit->text();
        currentSearchIndexes = treeModel[tabWidget->currentIndex()]->search(searchEdit->text());
        currentIndex = 0;
        currentIndexTab = tabWidget->currentIndex();
    }
    if (currentSearchIndexes.size() > 0 && currentIndex < currentSearchIndexes.size())
        selectionModel[tabWidget->currentIndex()]->setCurrentIndex(currentSearchIndexes.at(currentIndex),
                                                                   QItemSelectionModel::ClearAndSelect);
}

void DesignWidget::onHoverIndexChanged(int num, QModelIndex index)
{
    if (index.isValid()) {
        TreeModel::Item *item = treeModel[num]->nodeFromIndex(index);
        if (item->type() != ElementType::NONE) {
            std::vector<DecalXY> decals = getDecals(item->type(), item->id());
            if (decals.size() > 0)
                Q_EMIT hover(decals.at(0));
            return;
        }
    }
    Q_EMIT hover(DecalXY());
}

void DesignWidget::onHoverPropertyChanged(QtBrowserItem *item)
{
    if (item != nullptr) {
        QtProperty *selectedProperty = item->property();
        ElementType type = getElementTypeByName(selectedProperty->propertyId());
        if (type != ElementType::NONE) {
            IdStringList value = IdStringList::parse(ctx, selectedProperty->valueText().toStdString());
            if (value != IdStringList()) {
                auto node = getTreeByElementType(type)->nodeForId(value);
                if (node) {
                    std::vector<DecalXY> decals = getDecals((*node)->type(), (*node)->id());
                    if (decals.size() > 0)
                        Q_EMIT hover(decals.at(0));
                    return;
                }
            }
        }
    }
    Q_EMIT hover(DecalXY());
}
NEXTPNR_NAMESPACE_END

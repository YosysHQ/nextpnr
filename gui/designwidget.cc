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
    BEL,
    WIRE,
    PIP
};

class ElementTreeItem : public QTreeWidgetItem
{
  public:
    ElementTreeItem(ElementType t, QString str) : QTreeWidgetItem((QTreeWidget *)nullptr, QStringList(str)), type(t) {}
    virtual ~ElementTreeItem(){};

    ElementType getType() { return type; };

  private:
    ElementType type;
};

class BelTreeItem : public ElementTreeItem
{
  public:
    BelTreeItem(IdString d, ElementType type, QString str) : ElementTreeItem(type, str) { this->data = d; }
    virtual ~BelTreeItem(){};

    IdString getData() { return this->data; };

  private:
    IdString data;
};

class WireTreeItem : public ElementTreeItem
{
  public:
    WireTreeItem(IdString d, ElementType type, QString str) : ElementTreeItem(type, str) { this->data = d; }
    virtual ~WireTreeItem(){};

    IdString getData() { return this->data; };

  private:
    IdString data;
};

class PipTreeItem : public ElementTreeItem
{
  public:
    PipTreeItem(IdString d, ElementType type, QString str) : ElementTreeItem(type, str) { this->data = d; }
    virtual ~PipTreeItem(){};

    IdString getData() { return this->data; };

  private:
    IdString data;
};

DesignWidget::DesignWidget(QWidget *parent) : QWidget(parent), ctx(nullptr)
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
    bel_root->setText(0, QString("Bels"));
    treeWidget->insertTopLevelItem(0, bel_root);
    QList<QTreeWidgetItem *> bel_items;
    if (ctx)
    {
        for (auto bel : ctx->getBels()) {
            auto name = ctx->getBelName(bel);
            bel_items.append(new BelTreeItem(name, ElementType::BEL, QString(name.c_str(ctx))));
        }
    }
    bel_root->addChildren(bel_items);

    // Add wires to tree
    QTreeWidgetItem *wire_root = new QTreeWidgetItem(treeWidget);
    QList<QTreeWidgetItem *> wire_items;
    wire_root->setText(0, QString("Wires"));
    treeWidget->insertTopLevelItem(0, wire_root);
    if (ctx)
    {
        for (auto wire : ctx->getWires()) {
            auto name = ctx->getWireName(wire);
            wire_items.append(new WireTreeItem(name, ElementType::WIRE, QString(name.c_str(ctx))));
        }
    }
    wire_root->addChildren(wire_items);

    // Add pips to tree
    QTreeWidgetItem *pip_root = new QTreeWidgetItem(treeWidget);
    QList<QTreeWidgetItem *> pip_items;
    pip_root->setText(0, QString("Pips"));
    treeWidget->insertTopLevelItem(0, pip_root);
    if (ctx)
    {
        for (auto pip : ctx->getPips()) {
            auto name = ctx->getPipName(pip);
            pip_items.append(new PipTreeItem(name, ElementType::PIP, QString(name.c_str(ctx))));
        }
    }
    pip_root->addChildren(pip_items);
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

    clearProperties();

    ElementType type = static_cast<ElementTreeItem *>(item)->getType();

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

#include "designwidget.h"
#include <QAction>
#include <QGridLayout>
#include <QMenu>
#include <QSplitter>
#include <QTreeWidgetItem>
#include "fpgaviewwidget.h"
#include "pybindings.h"

enum class ElementType
{
    BEL,
    WIRE,
    PIP
};

class ElementTreeItem : public QTreeWidgetItem
{
  public:
    ElementTreeItem(ElementType t, QString str)
            : QTreeWidgetItem((QTreeWidget *)nullptr, QStringList(str)), type(t)
    {
    }
    virtual ~ElementTreeItem(){};

    ElementType getType() { return type; };

  private:
    ElementType type;
};

class BelTreeItem : public ElementTreeItem
{
  public:
    BelTreeItem(IdString d, ElementType type, QString str)
            : ElementTreeItem(type, str)
    {
        this->data = d;
    }
    virtual ~BelTreeItem(){};

    IdString getData() { return this->data; };

  private:
    IdString data;
};

class WireTreeItem : public ElementTreeItem
{
  public:
    WireTreeItem(IdString d, ElementType type, QString str)
            : ElementTreeItem(type, str)
    {
        this->data = d;
    }
    virtual ~WireTreeItem(){};

    IdString getData() { return this->data; };

  private:
    IdString data;
};

class PipTreeItem : public ElementTreeItem
{
  public:
    PipTreeItem(IdString d, ElementType type, QString str)
            : ElementTreeItem(type, str)
    {
        this->data = d;
    }
    virtual ~PipTreeItem(){};

    IdString getData() { return this->data; };

  private:
    IdString data;
};

DesignWidget::DesignWidget(Design *_design, QWidget *parent)
        : QWidget(parent), design(_design)
{

    treeWidget = new QTreeWidget();

    // Add tree view
    treeWidget->setColumnCount(1);
    treeWidget->setHeaderLabel(QString("Items"));
    treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    // Add bels to tree
    QTreeWidgetItem *bel_root = new QTreeWidgetItem(treeWidget);
    bel_root->setText(0, QString("Bels"));
    treeWidget->insertTopLevelItem(0, bel_root);
    QList<QTreeWidgetItem *> bel_items;
    for (auto bel : design->chip.getBels()) {
        auto name = design->chip.getBelName(bel);
        bel_items.append(
                new BelTreeItem(name, ElementType::BEL, QString(name.c_str())));
    }
    bel_root->addChildren(bel_items);

    // Add wires to tree
    QTreeWidgetItem *wire_root = new QTreeWidgetItem(treeWidget);
    QList<QTreeWidgetItem *> wire_items;
    wire_root->setText(0, QString("Wires"));
    treeWidget->insertTopLevelItem(0, wire_root);
    for (auto wire : design->chip.getWires()) {
        auto name = design->chip.getWireName(wire);
        wire_items.append(new WireTreeItem(name, ElementType::WIRE,
                                           QString(name.c_str())));
    }
    wire_root->addChildren(wire_items);

    // Add pips to tree
    QTreeWidgetItem *pip_root = new QTreeWidgetItem(treeWidget);
    QList<QTreeWidgetItem *> pip_items;
    pip_root->setText(0, QString("Pips"));
    treeWidget->insertTopLevelItem(0, pip_root);
    for (auto pip : design->chip.getPips()) {
        auto name = design->chip.getPipName(pip);
        pip_items.append(
                new PipTreeItem(name, ElementType::PIP, QString(name.c_str())));
    }
    pip_root->addChildren(pip_items);

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
    connect(treeWidget, &QTreeWidget::customContextMenuRequested, this,
            &DesignWidget::prepareMenu);

    connect(treeWidget, SIGNAL(itemClicked(QTreeWidgetItem *, int)),
            SLOT(onItemClicked(QTreeWidgetItem *, int)));
}

DesignWidget::~DesignWidget()
{
    delete variantManager;
    delete variantFactory;
    delete propertyEditor;
}

void DesignWidget::addProperty(QtVariantProperty *property, const QString &id)
{
    propertyToId[property] = id;
    idToProperty[id] = property;
    QtBrowserItem *item = propertyEditor->addProperty(property);
}

void DesignWidget::clearProperties()
{
    QMap<QtProperty *, QString>::ConstIterator itProp =
            propertyToId.constBegin();
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

        QtVariantProperty *topItem =
                variantManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(c.c_str()));
        addProperty(topItem, QString("Name"));
    } else if (type == ElementType::WIRE) {
        IdString c = static_cast<WireTreeItem *>(item)->getData();

        QtVariantProperty *topItem =
                variantManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(c.c_str()));
        addProperty(topItem, QString("Name"));

    } else if (type == ElementType::PIP) {
        IdString c = static_cast<PipTreeItem *>(item)->getData();

        QtVariantProperty *topItem =
                variantManager->addProperty(QVariant::String, QString("Name"));
        topItem->setValue(QString(c.c_str()));
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

    QPoint pt(pos);
    menu.exec(tree->mapToGlobal(pos));
}

void DesignWidget::selectObject()
{
    // info->info("selected " + itemContextMenu->text(0).toStdString() + "\n");
}

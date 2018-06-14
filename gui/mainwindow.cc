#include "mainwindow.h"
#include <functional>
#include <iostream>
#include <string>
#include "emb.h"
#include "pybindings.h"
#include "ui_mainwindow.h"

#include <QDate>
#include <QLocale>

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

MainWindow::MainWindow(Design *_design, QWidget *parent)
        : QMainWindow(parent), ui(new Ui::MainWindow), design(_design)
{
    ui->setupUi(this);
    std::string title = "nextpnr-ice40 - " + design->chip.getChipName();
    setWindowTitle(title.c_str());

    // Add tree view
    ui->treeWidget->setColumnCount(1);
    ui->treeWidget->setHeaderLabel(QString("Items"));
    ui->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeWidget, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::prepareMenu);

    // Add bels to tree
    QTreeWidgetItem *bel_root = new QTreeWidgetItem(ui->treeWidget);
    bel_root->setText(0, QString("Bels"));
    ui->treeWidget->insertTopLevelItem(0, bel_root);
    QList<QTreeWidgetItem *> bel_items;
    for (auto bel : design->chip.getBels()) {
        auto name = design->chip.getBelName(bel);
        bel_items.append(
                new BelTreeItem(name, ElementType::BEL, QString(name.c_str())));
    }
    bel_root->addChildren(bel_items);

    // Add wires to tree
    QTreeWidgetItem *wire_root = new QTreeWidgetItem(ui->treeWidget);
    QList<QTreeWidgetItem *> wire_items;
    wire_root->setText(0, QString("Wires"));
    ui->treeWidget->insertTopLevelItem(0, wire_root);
    for (auto wire : design->chip.getWires()) {
        auto name = design->chip.getWireName(wire);
        wire_items.append(new WireTreeItem(name, ElementType::WIRE,
                                           QString(name.c_str())));
    }
    wire_root->addChildren(wire_items);

    // Add pips to tree
    QTreeWidgetItem *pip_root = new QTreeWidgetItem(ui->treeWidget);
    QList<QTreeWidgetItem *> pip_items;
    pip_root->setText(0, QString("Pips"));
    ui->treeWidget->insertTopLevelItem(0, pip_root);
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
    ui->splitter_2->addWidget(propertyEditor);

    connect(ui->treeWidget, SIGNAL(itemClicked(QTreeWidgetItem *, int)),
            SLOT(onItemClicked(QTreeWidgetItem *, int)));

    tabWidget = new QTabWidget();
    tabWidget->addTab(new PythonTab(), "Python");
    info = new InfoTab();
    tabWidget->addTab(info, "Info");
    ui->splitter->addWidget(tabWidget);
}

MainWindow::~MainWindow()
{
    delete variantManager;
    delete variantFactory;
    delete propertyEditor;
    delete ui;
}

void MainWindow::addProperty(QtVariantProperty *property, const QString &id)
{
    propertyToId[property] = id;
    idToProperty[id] = property;
    QtBrowserItem *item = propertyEditor->addProperty(property);
}

void MainWindow::onItemClicked(QTreeWidgetItem *item, int pos)
{
    if (!item->parent())
        return;
    ElementType type = static_cast<ElementTreeItem *>(item)->getType();
    QMap<QtProperty *, QString>::ConstIterator itProp =
            propertyToId.constBegin();
    while (itProp != propertyToId.constEnd()) {
        delete itProp.key();
        itProp++;
    }
    propertyToId.clear();
    idToProperty.clear();

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

void MainWindow::prepareMenu(const QPoint &pos)
{
    QTreeWidget *tree = ui->treeWidget;

    itemContextMenu = tree->itemAt(pos);

    QAction *selectAction = new QAction("&Select", this);
    selectAction->setStatusTip("Select item on view");
    connect(selectAction, SIGNAL(triggered()), this, SLOT(selectObject()));

    QMenu menu(this);
    menu.addAction(selectAction);

    QPoint pt(pos);
    menu.exec(tree->mapToGlobal(pos));
}

void MainWindow::selectObject()
{
    info->info("selected " + itemContextMenu->text(0).toStdString() + "\n");
}

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
    PyImport_ImportModule("emb");

    write = [this](std::string s) {
        plainTextEdit->moveCursor(QTextCursor::End);
        plainTextEdit->insertPlainText(s.c_str());
        plainTextEdit->moveCursor(QTextCursor::End);
    };
    emb::set_stdout(write);
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

    // Add text area for Python output and input line
    plainTextEdit = new QPlainTextEdit();
    plainTextEdit->setReadOnly(true);
    plainTextEdit->setMinimumHeight(50);
    plainTextEdit->setMaximumHeight(200);
    ui->splitter->addWidget(plainTextEdit);
    lineEdit = new QLineEdit();
    lineEdit->setMinimumHeight(30);
    lineEdit->setMaximumHeight(30);
    ui->splitter->addWidget(lineEdit);
    connect(lineEdit, SIGNAL(returnPressed()), this,
            SLOT(editLineReturnPressed()));
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
    plainTextEdit->moveCursor(QTextCursor::End);
    plainTextEdit->insertPlainText(
            std::string("selected " + itemContextMenu->text(0).toStdString() +
                        "\n")
                    .c_str());
    plainTextEdit->moveCursor(QTextCursor::End);
}

void handle_system_exit() { exit(-1); }

int MainWindow::executePython(std::string command)
{
    PyObject *m, *d, *v;
    m = PyImport_AddModule("__main__");
    if (m == NULL)
        return -1;
    d = PyModule_GetDict(m);
    v = PyRun_StringFlags(command.c_str(),
                          (command.empty() ? Py_file_input : Py_single_input),
                          d, d, NULL);
    if (v == NULL) {
        PyObject *exception, *v, *tb;

        if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
            handle_system_exit();
        }
        PyErr_Fetch(&exception, &v, &tb);
        if (exception == NULL)
            return 0;
        PyErr_NormalizeException(&exception, &v, &tb);
        if (tb == NULL) {
            tb = Py_None;
            Py_INCREF(tb);
        }
        PyException_SetTraceback(v, tb);
        if (exception == NULL)
            return 0;
        PyErr_Clear();

        PyObject *objectsRepresentation = PyObject_Str(v);
        std::string errorStr =
                PyUnicode_AsUTF8(objectsRepresentation) + std::string("\n");
        plainTextEdit->moveCursor(QTextCursor::End);
        plainTextEdit->insertPlainText(errorStr.c_str());
        plainTextEdit->moveCursor(QTextCursor::End);
        Py_DECREF(objectsRepresentation);
        Py_XDECREF(exception);
        Py_XDECREF(v);
        Py_XDECREF(tb);
        return -1;
    }
    Py_DECREF(v);
    return 0;
}

void MainWindow::editLineReturnPressed()
{
    std::string input = lineEdit->text().toStdString();
    plainTextEdit->moveCursor(QTextCursor::End);
    plainTextEdit->insertPlainText(std::string(">>> " + input + "\n").c_str());
    plainTextEdit->moveCursor(QTextCursor::End);
    plainTextEdit->update();
    lineEdit->clear();
    int error = executePython(input);
}

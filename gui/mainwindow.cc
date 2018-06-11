#include "mainwindow.h"
#include <functional>
#include <iostream>
#include <string>
#include "emb.h"
#include "pybindings.h"
#include "qtpropertymanager.h"
#include "qttreepropertybrowser.h"
#include "qtvariantproperty.h"
#include "ui_mainwindow.h"

#include <QDate>
#include <QLocale>

MainWindow::MainWindow(Design *_design, QWidget *parent)
        : QMainWindow(parent), ui(new Ui::MainWindow), design(_design)
{
    ui->setupUi(this);
    PyImport_ImportModule("emb");

    write = [this](std::string s) {
        ui->plainTextEdit->moveCursor(QTextCursor::End);
        ui->plainTextEdit->insertPlainText(s.c_str());
        ui->plainTextEdit->moveCursor(QTextCursor::End);
    };
    emb::set_stdout(write);
    std::string title = "nextpnr-ice40 - " + design->chip.getChipName();
    setWindowTitle(title.c_str());
    QtVariantPropertyManager *variantManager = new QtVariantPropertyManager();

    int i = 0;
    QtProperty *topItem = variantManager->addProperty(
            QtVariantPropertyManager::groupTypeId(),
            QString::number(i++) + QLatin1String(" Group Property"));

    QtVariantProperty *item = variantManager->addProperty(
            QVariant::Bool,
            QString::number(i++) + QLatin1String(" Bool Property"));
    item->setValue(true);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Int,
                                       QString::number(i++) +
                                               QLatin1String(" Int Property"));
    item->setValue(20);
    item->setAttribute(QLatin1String("minimum"), 0);
    item->setAttribute(QLatin1String("maximum"), 100);
    item->setAttribute(QLatin1String("singleStep"), 10);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::Double,
            QString::number(i++) + QLatin1String(" Double Property"));
    item->setValue(1.2345);
    item->setAttribute(QLatin1String("singleStep"), 0.1);
    item->setAttribute(QLatin1String("decimals"), 3);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::String,
            QString::number(i++) + QLatin1String(" String Property"));
    item->setValue("Value");
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Date,
                                       QString::number(i++) +
                                               QLatin1String(" Date Property"));
    item->setValue(QDate::currentDate().addDays(2));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Time,
                                       QString::number(i++) +
                                               QLatin1String(" Time Property"));
    item->setValue(QTime::currentTime());
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::DateTime,
            QString::number(i++) + QLatin1String(" DateTime Property"));
    item->setValue(QDateTime::currentDateTime());
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::KeySequence,
            QString::number(i++) + QLatin1String(" KeySequence Property"));
    item->setValue(QKeySequence(Qt::ControlModifier | Qt::Key_Q));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Char,
                                       QString::number(i++) +
                                               QLatin1String(" Char Property"));
    item->setValue(QChar(386));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::Locale,
            QString::number(i++) + QLatin1String(" Locale Property"));
    item->setValue(QLocale(QLocale::Polish, QLocale::Poland));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::Point,
            QString::number(i++) + QLatin1String(" Point Property"));
    item->setValue(QPoint(10, 10));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::PointF,
            QString::number(i++) + QLatin1String(" PointF Property"));
    item->setValue(QPointF(1.2345, -1.23451));
    item->setAttribute(QLatin1String("decimals"), 3);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Size,
                                       QString::number(i++) +
                                               QLatin1String(" Size Property"));
    item->setValue(QSize(20, 20));
    item->setAttribute(QLatin1String("minimum"), QSize(10, 10));
    item->setAttribute(QLatin1String("maximum"), QSize(30, 30));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::SizeF,
            QString::number(i++) + QLatin1String(" SizeF Property"));
    item->setValue(QSizeF(1.2345, 1.2345));
    item->setAttribute(QLatin1String("decimals"), 3);
    item->setAttribute(QLatin1String("minimum"), QSizeF(0.12, 0.34));
    item->setAttribute(QLatin1String("maximum"), QSizeF(20.56, 20.78));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Rect,
                                       QString::number(i++) +
                                               QLatin1String(" Rect Property"));
    item->setValue(QRect(10, 10, 20, 20));
    topItem->addSubProperty(item);
    item->setAttribute(QLatin1String("constraint"), QRect(0, 0, 50, 50));

    item = variantManager->addProperty(
            QVariant::RectF,
            QString::number(i++) + QLatin1String(" RectF Property"));
    item->setValue(QRectF(1.2345, 1.2345, 1.2345, 1.2345));
    topItem->addSubProperty(item);
    item->setAttribute(QLatin1String("constraint"), QRectF(0, 0, 50, 50));
    item->setAttribute(QLatin1String("decimals"), 3);

    item = variantManager->addProperty(QtVariantPropertyManager::enumTypeId(),
                                       QString::number(i++) +
                                               QLatin1String(" Enum Property"));
    QStringList enumNames;
    enumNames << "Enum0"
              << "Enum1"
              << "Enum2";
    item->setAttribute(QLatin1String("enumNames"), enumNames);
    item->setValue(1);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QtVariantPropertyManager::flagTypeId(),
                                       QString::number(i++) +
                                               QLatin1String(" Flag Property"));
    QStringList flagNames;
    flagNames << "Flag0"
              << "Flag1"
              << "Flag2";
    item->setAttribute(QLatin1String("flagNames"), flagNames);
    item->setValue(5);
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::SizePolicy,
            QString::number(i++) + QLatin1String(" SizePolicy Property"));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(QVariant::Font,
                                       QString::number(i++) +
                                               QLatin1String(" Font Property"));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::Cursor,
            QString::number(i++) + QLatin1String(" Cursor Property"));
    topItem->addSubProperty(item);

    item = variantManager->addProperty(
            QVariant::Color,
            QString::number(i++) + QLatin1String(" Color Property"));
    topItem->addSubProperty(item);

    QtVariantEditorFactory *variantFactory = new QtVariantEditorFactory();

    QtTreePropertyBrowser *variantEditor = new QtTreePropertyBrowser();
    variantEditor->setFactoryForManager(variantManager, variantFactory);
    variantEditor->addProperty(topItem);
    variantEditor->setPropertiesWithoutValueMarked(true);
    variantEditor->setRootIsDecorated(false);
    variantEditor->show();
    ui->splitter_2->addWidget(variantEditor);
}

MainWindow::~MainWindow()
{

    // delete variantManager;
    // delete variantFactory;
    // delete variantEditor;
    delete ui;
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
        ui->plainTextEdit->moveCursor(QTextCursor::End);
        ui->plainTextEdit->insertPlainText(errorStr.c_str());
        ui->plainTextEdit->moveCursor(QTextCursor::End);
        Py_DECREF(objectsRepresentation);
        Py_XDECREF(exception);
        Py_XDECREF(v);
        Py_XDECREF(tb);
        return -1;
    }
    Py_DECREF(v);
    return 0;
}

void MainWindow::on_lineEdit_returnPressed()
{
    std::string input = ui->lineEdit->text().toStdString();
    ui->plainTextEdit->moveCursor(QTextCursor::End);
    ui->plainTextEdit->insertPlainText(
            std::string(">>> " + input + "\n").c_str());
    ui->plainTextEdit->moveCursor(QTextCursor::End);
    ui->plainTextEdit->update();
    ui->lineEdit->clear();
    int error = executePython(input);
}

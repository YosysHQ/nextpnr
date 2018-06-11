#include "mainwindow.h"
#include <functional>
#include <iostream>
#include <string>
#include "emb.h"
#include "pybindings.h"
#include "ui_mainwindow.h"

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
}

MainWindow::~MainWindow() { delete ui; }

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

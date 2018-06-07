#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <functional>
#include <iostream>
#include <string>
#include "pybindings.h"
#include "emb.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    PyImport_ImportModule("emb");
    
    write = [this] (std::string s) {
        //ui->plainTextEdit->moveCursor(QTextCursor::End);
        //ui->plainTextEdit->insertPlainText(s.c_str()); 
        //ui->plainTextEdit->moveCursor(QTextCursor::End); 
        ui->plainTextEdit->appendPlainText(s.c_str());
    };
    emb::set_stdout(write);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void handle_system_exit()
{
    exit(-1);
}

int MainWindow::executePython(std::string command)
{
    PyObject *m, *d, *v;
    m = PyImport_AddModule("__main__");
    if (m == NULL)
        return -1;
    d = PyModule_GetDict(m);
    v = PyRun_StringFlags(command.c_str(), (command.empty() ? Py_file_input : Py_single_input), d, d, NULL);
    if (v == NULL) 
    {
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

        PyObject* objectsRepresentation = PyObject_Str(v);
        const char* errorStr = PyUnicode_AsUTF8(objectsRepresentation);
        ui->plainTextEdit->appendPlainText(errorStr);
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
    ui->plainTextEdit->appendPlainText(std::string(">>> " + input).c_str());
    ui->plainTextEdit->update();
    ui->lineEdit->clear();
    int error = executePython(input);
}

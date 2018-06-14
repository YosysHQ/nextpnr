#include "pythontab.h"
#include <QGridLayout>
#include "emb.h"
#include "pybindings.h"

PythonTab::PythonTab(QWidget *parent) : QWidget(parent)
{
    PyImport_ImportModule("emb");

    // Add text area for Python output and input line
    plainTextEdit = new QPlainTextEdit();
    plainTextEdit->setReadOnly(true);
    plainTextEdit->setMinimumHeight(100);
    lineEdit = new QLineEdit();
    lineEdit->setMinimumHeight(30);
    lineEdit->setMaximumHeight(30);

    QGridLayout *mainLayout = new QGridLayout();
    mainLayout->addWidget(plainTextEdit, 0, 0);
    mainLayout->addWidget(lineEdit, 1, 0);
    setLayout(mainLayout);

    connect(lineEdit, SIGNAL(returnPressed()), this,
            SLOT(editLineReturnPressed()));

    write = [this](std::string s) {
        plainTextEdit->moveCursor(QTextCursor::End);
        plainTextEdit->insertPlainText(s.c_str());
        plainTextEdit->moveCursor(QTextCursor::End);
    };
    emb::set_stdout(write);
}

void handle_system_exit() { exit(-1); }

int PythonTab::executePython(std::string command)
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

void PythonTab::editLineReturnPressed()
{
    std::string input = lineEdit->text().toStdString();
    plainTextEdit->moveCursor(QTextCursor::End);
    plainTextEdit->insertPlainText(std::string(">>> " + input + "\n").c_str());
    plainTextEdit->moveCursor(QTextCursor::End);
    plainTextEdit->update();
    lineEdit->clear();
    int error = executePython(input);
}
